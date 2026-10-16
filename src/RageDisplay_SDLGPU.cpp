#include "global.h"

/*
 * RageDisplay_SDLGPU: drawing with SDL's GPU API, which is Metal on macOS and
 * Vulkan elsewhere.
 *
 * RageDisplay_GLCore talks to OpenGL, which works out what the GPU has to do
 * from scratch at each draw: it checks the state that changed, picks or builds
 * a shader variant for it, and on macOS translates all of it into Metal.  That
 * is most of what a frame costs, and it's paid for every one of the dozens of
 * draws a frame has.  The newer APIs let a program say up front what it will
 * do, so this draws the same pictures, with the same shader (see
 * RageDisplay_GLCore for what it reproduces of the fixed function pipeline),
 * like this:
 *
 * - A frame is recorded, not sent as it's drawn.  Draw calls add their vertices
 *   to one array for the whole frame, and runs of draws that share their state
 *   become one command.  EndFrame sends all the vertices in one upload, then
 *   plays the commands back in one render pass.
 * - All of the state of a draw that the GPU compiles (blending, the depth test,
 *   culling) is in a pipeline object, made once and then just bound.
 * - The render pass starts by clearing the frame, and the depth buffer is
 *   thrown away at the end instead of written back to memory, which tile based
 *   GPUs (Apple's) can do without touching memory at all.  ClearZBuffer in the
 *   middle of a frame doesn't end the pass: the depth range is split into
 *   slices, and each clear moves on to a nearer slice, where everything drawn
 *   wins against what's in the farther ones, as if they were cleared.
 * - Textures are uploaded once, as RGBA8, and nothing about them is checked or
 *   converted at draw time.
 */

#include "RageDisplay.h"
#include "RageDisplay_SDLGPU.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "RageException.h"
#include "RageTexture.h"
#include "RageTextureManager.h"
#include "RageMath.h"
#include "RageTimer.h"
#include "RageTypes.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "GameConstantsAndTypes.h"
#include "FrameStats.h"
#include "arch/arch.h"
#include "arch/LowLevelWindow/LowLevelWindow.h"

#include <SDL3/SDL.h>
#include <map>
#include <stddef.h>

#include "RageDisplay_SDLGPU_Shaders.h"

class RageCompiledGeometrySDLGPU;

namespace
{
	LowLevelWindow *g_pWind = NULL;
	SDL_GPUDevice *g_pDevice = NULL;
	SDL_Window *g_pWindow = NULL;
	SDL_GPUTextureFormat g_SwapchainFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
	SDL_GPUTextureFormat g_DepthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
	SDL_GPUShader *g_pSpriteVS = NULL, *g_pModelVS = NULL, *g_pFS = NULL;

	/* The depth buffer, as big as the last swapchain texture. */
	SDL_GPUTexture *g_pDepth = NULL;
	Uint32 g_iDepthWidth = 0, g_iDepthHeight = 0;

	/* How many times the depth buffer can be "cleared" in a frame without ending
	 * the render pass; see NextDepthSlice. */
	int g_iDepthSlices = 1;

	enum { TEXMODE_OFF, TEXMODE_MODULATE, TEXMODE_ADD, TEXMODE_GLOW };

	/* What we send for each vertex; the same as RageDisplay_GLCore. */
	struct Vertex
	{
		float p[3];
		float t[2];
		Uint8 c[4];	// RGBA
		Uint16 n[3];	// half floats
		Uint8 mode[2];	// unit 0's texture mode, then padding
	};
	typedef char VertexSizeCheck[sizeof(Vertex) == 32? 1: -1];

	/* The shaders' uniform blocks, laid out the way std140 has them. */
	struct TransformBlock
	{
		float Projection[16];
		float ModelView[16];
		float TexMatrix0[16];
		float TexMatrix1[16];
		float NormalMatrix[16];
		Sint32 Flags[4];	// sphere map unit 0, sphere map unit 1, lighting, light 0
		float ConstColor[4];
		Sint32 ConstMode[4];
	};
	struct LightBlock
	{
		float MatEmissive[4], MatAmbient[4], MatDiffuse[4], MatSpecular[4];
		float LightAmbient[4], LightDiffuse[4], LightSpecular[4];
		float LightDir[4];	// w: shininess
	};
	struct FragmentBlock
	{
		float ColorScale[4];
		Sint32 Flags[4];	// unit 1's texture mode, alpha test
	};

	/*
	 * Textures, by handle.  Handle 0 is no texture.  The wrap mode is the
	 * texture's, like in GL: SetTextureWrapping changes it for the texture on the
	 * active unit, and it stays that way.
	 */
	struct Texture
	{
		SDL_GPUTexture *pTex;
		bool bWrap;
	};
	vector<Texture> g_Textures;
	vector<unsigned> g_FreeHandles;
	SDL_GPUTexture *g_pWhite = NULL;	// for units that aren't used
	SDL_GPUSampler *g_pSampler[2] = { NULL, NULL };	// clamped, repeating

	/* Textures and models deleted while a recorded frame may still use them;
	 * see ReleaseDeleted. */
	vector<SDL_GPUTexture *> g_DeletedTextures[2];
	vector<RageCompiledGeometry *> g_DeletedGeometry[2];

	/* The vertices of a frame go into this buffer, through the transfer buffer. */
	SDL_GPUBuffer *g_pVertexBuffer = NULL;
	Uint32 g_iVertexBufferBytes = 0;
	SDL_GPUTransferBuffer *g_pUploadBuffer = NULL;
	Uint32 g_iUploadBufferBytes = 0;

	/* Quads are drawn as indexed triangles; quad q is vertices 4q..4q+3, split
	 * along the 1-3 diagonal like GL_QUADS was.  Indices are 16 bits, so a quad
	 * command has at most this many vertices. */
	enum { MAX_QUAD_VERTICES = 65536 };
	SDL_GPUBuffer *g_pQuadIndices = NULL;

	/*
	 * A recorded frame.
	 */
	enum CommandKind { CMD_QUADS, CMD_TRIANGLES, CMD_MODEL, CMD_CLEAR_DEPTH };
	struct Command
	{
		Uint8 iKind;
		SDL_GPUGraphicsPipeline *pPipeline;
		SDL_GPUTexture *pTexture[2];	// NULL: the unit isn't used
		SDL_GPUSampler *pSampler[2];
		int iTransform, iLight, iFragment;	// indices into the frame's blocks
		float fMinDepth, fMaxDepth;
		int iViewportX, iViewportY;
		Uint32 iFirst, iCount;	// vertices; indices for CMD_MODEL
		const RageCompiledGeometrySDLGPU *pGeometry;
	};
	struct Frame
	{
		vector<Vertex> vVertices;
		vector<Command> vCommands;
		vector<TransformBlock> vTransform;
		vector<LightBlock> vLight;
		vector<FragmentBlock> vFragment;
		vector<const RageCompiledGeometrySDLGPU *> vUploadGeometry;

		/* Trade contents without copying them. */
		void Swap( Frame &other )
		{
			vVertices.swap( other.vVertices );
			vCommands.swap( other.vCommands );
			vTransform.swap( other.vTransform );
			vLight.swap( other.vLight );
			vFragment.swap( other.vFragment );
			vUploadGeometry.swap( other.vUploadGeometry );
		}

		void Clear()
		{
			vVertices.clear();
			vCommands.clear();
			vTransform.clear();
			vLight.clear();
			vFragment.clear();
			vUploadGeometry.clear();
		}
	};
	Frame g_Frame;
	/* The last frame sent, for screenshots, and how big its target was. */
	Frame g_LastFrame;
	Uint32 g_iLastWidth = 0, g_iLastHeight = 0;
	bool g_bInFrame = false;

	/* The fences of the frames the GPU may still be drawing, in a ring of
	 * g_iFramesInFlight, so waiting for the GPU can be told apart from waiting
	 * for the window. */
	SDL_GPUFence *g_pFrameFence[3] = { NULL, NULL, NULL };
	int g_iFenceSlot = 0;
	int g_iFramesInFlight = 2;

	/* With SM_SDLGPU_PRESENT_HZ, frames that come sooner than this after the
	 * last one shown aren't drawn at all (BeginFrame says no), in performance
	 * counter ticks. */
	Uint64 g_iPresentInterval = 0;
	Uint64 g_iLastPresent = 0;

	/* Whether to wait for the window's buffer at the top of the frame, in
	 * WaitForFrame, rather than after drawing; and the next frame's command
	 * buffer and the window's buffer, once we have them. */
	bool g_bWaitFirst = false;
	SDL_GPUCommandBuffer *g_pPendingCmd = NULL;
	SDL_GPUTexture *g_pPendingTarget = NULL;
	Uint32 g_iPendingWidth = 0, g_iPendingHeight = 0;
	Uint64 g_iLastEndFrame = 0;

	/* The uniform blocks as they are now, and whether they differ from the last
	 * ones recorded. */
	TransformBlock g_Transform;
	LightBlock g_Light;
	FragmentBlock g_Fragment;
	bool g_bTransformDirty = true, g_bLightDirty = true, g_bFragmentDirty = true;

	/* The depth slice in use, or -1 if the frame hasn't used depth yet. */
	int g_iDepthSlice = -1;

	/* The viewport offset from SetViewport, in framebuffer pixels. */
	int g_iViewportX = 0, g_iViewportY = 0;

	/* What we've been asked for. */
	struct WantedState
	{
		bool bBlend;
		SDL_GPUBlendFactor BlendSrc, BlendDst;
		int iDepthRange;
		bool bZWrite, bZTest;
		SDL_GPUCompareOp DepthFunc;
		bool bClearDepthPending;
		SDL_GPUCullMode CullMode;
		SDL_GPUFillMode FillMode;

		int iActiveUnit;
		struct TexUnit
		{
			unsigned uTexture;
			bool bEnabled;
			int iMode;
			bool bSphere;
			RageMatrix TexMatrix;	// what the texture stack held when this unit was last active at a draw
			unsigned iTexMatrixGen;
		} Unit[MAX_TEXTURE_UNITS];

		bool bAlphaTest;
		bool bLighting, bLight0;
		RageColor MatEmissive, MatAmbient, MatDiffuse, MatSpecular;
		float fMatShininess;
		RageColor LightAmbient, LightDiffuse, LightSpecular;
		RageVector3 LightDir;	// in eye space
		RageColor ConstColor;	// glColor, for compiled geometry without lighting
	};
	WantedState g_Wanted;

	/* Set up by PrepareDraw for the draw being recorded. */
	struct DrawContext
	{
		bool bOnCPU;	// move vertices by the world and texture matrices here
		const RageMatrix *pWorld;
		const RageMatrix *pTexMatrix0;
		Uint8 iMode0;
		bool bModel;	// PrepareDraw is for compiled geometry

		Command Cmd;	// the state part
	};
	DrawContext g_Draw;

	/* The modelview matrix of the last draw; see SetLightDirectional. */
	RageMatrix g_LastWorld, g_LastCenteringView;

	/* Pipelines, by PipelineKey. */
	std::map<Uint32, SDL_GPUGraphicsPipeline *> g_Pipelines;
	Uint32 g_iLastPipelineKey = 0xFFFFFFFF;
	SDL_GPUGraphicsPipeline *g_pLastPipeline = NULL;
}

/* See SetColorScale. */
static float g_fColorScale[3] = { 1, 1, 1 };
static bool g_bDrawnThisFrame = false;	// what's drawn can't be scaled after the fact
static bool g_bColorScaleInexact = false;	// drew something that doesn't scale linearly

static CString GPUError( const char *szWhat )
{
	return ssprintf( "%s failed: %s", szWhat, SDL_GetError() );
}

/*
 * Pixel formats: the same as RageDisplay_GLCore.  Whatever comes in is made
 * RGBA8 when it's uploaded.
 */
static RageDisplay::PixelFormatDesc PIXEL_FORMAT_DESC[RageDisplay::NUM_PIX_FORMATS] = {
	{ /* R8G8B8A8 */ 32, { 0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF } },
	{ /* R4G4B4A4 */ 16, { 0xF000, 0x0F00, 0x00F0, 0x000F } },
	{ /* R5G5B5A1 */ 16, { 0xF800, 0x07C0, 0x003E, 0x0001 } },
	{ /* R5G5B5 */ 16, { 0xF800, 0x07C0, 0x003E, 0x0000 } },
	{ /* R8G8B8 */ 24, { 0xFF0000, 0x00FF00, 0x0000FF, 0x000000 } },
	{ /* Paletted */ 8, { 0,0,0,0 } },
	{ /* B8G8R8 */ 24, { 0x0000FF, 0x00FF00, 0xFF0000, 0x000000 } },
	{ /* A1B5G5R5 */ 16, { 0x7C00, 0x03E0, 0x001F, 0x8000 } },
};

static void FixLilEndian()
{
#if defined(ENDIAN_LITTLE)
	static bool bInitialized = false;
	if( bInitialized )
		return;
	bInitialized = true;

	/* The byte formats are in memory order; flip their masks so RageSurface,
	 * which reads pixels as numbers, sees them that way. */
	const int iByteFormats[] = { RageDisplay::FMT_RGBA8, RageDisplay::FMT_RGB8, RageDisplay::FMT_BGR8 };
	for( unsigned i = 0; i < ARRAYSIZE(iByteFormats); ++i )
	{
		RageDisplay::PixelFormatDesc &pf = PIXEL_FORMAT_DESC[iByteFormats[i]];
		for( int mask = 0; mask < 4; ++mask )
		{
			int m = pf.masks[mask];
			switch( pf.bpp )
			{
			case 24: m = Swap24(m); break;
			case 32: m = Swap32(m); break;
			default: ASSERT(0);
			}
			pf.masks[mask] = m;
		}
	}
#endif
}

/* Write a w x h part of img, from its top left, as RGBA8 to pDest. */
static void ConvertToRGBA8( const RageSurface *img, int iWidth, int iHeight, Uint8 *pDest )
{
	const RageSurfaceFormat &fmt = *img->format;
	const int iBpp = fmt.BytesPerPixel;

	/* The common case: already RGBA8 in memory order. */
	if( iBpp == 4 && fmt.palette == NULL &&
		fmt.Mask[0] == PIXEL_FORMAT_DESC[RageDisplay::FMT_RGBA8].masks[0] &&
		fmt.Mask[1] == PIXEL_FORMAT_DESC[RageDisplay::FMT_RGBA8].masks[1] &&
		fmt.Mask[2] == PIXEL_FORMAT_DESC[RageDisplay::FMT_RGBA8].masks[2] &&
		fmt.Mask[3] == PIXEL_FORMAT_DESC[RageDisplay::FMT_RGBA8].masks[3] )
	{
		for( int y = 0; y < iHeight; ++y )
			memcpy( pDest + y*iWidth*4, (const Uint8 *) img->pixels + y*img->pitch, iWidth*4 );
		return;
	}

	/* Each channel is scaled to 8 bits the way GL does it, rounding v/max*255;
	 * no alpha mask means opaque. */
	Uint32 iMax[4];
	for( int c = 0; c < 4; ++c )
		iMax[c] = fmt.Mask[c] >> fmt.Shift[c];

	for( int y = 0; y < iHeight; ++y )
	{
		const Uint8 *pRow = (const Uint8 *) img->pixels + y*img->pitch;
		Uint8 *pOut = pDest + y*iWidth*4;
		for( int x = 0; x < iWidth; ++x, pOut += 4 )
		{
			const Uint8 *p = pRow + x*iBpp;
			Uint32 iVal;
			switch( iBpp )
			{
			case 1: iVal = p[0]; break;
			case 2: iVal = p[0] | (p[1] << 8); break;
			case 3: iVal = p[0] | (p[1] << 8) | (p[2] << 16); break;
			default: iVal = p[0] | (p[1] << 8) | (p[2] << 16) | (Uint32(p[3]) << 24); break;
			}
#if !defined(ENDIAN_LITTLE)
			/* RageSurface reads pixels as native numbers. */
			switch( iBpp )
			{
			case 2: iVal = (p[0] << 8) | p[1]; break;
			case 3: iVal = (p[0] << 16) | (p[1] << 8) | p[2]; break;
			case 4: iVal = (Uint32(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; break;
			}
#endif

			if( fmt.palette )
			{
				const RageSurfaceColor &c = fmt.palette->colors[iVal & 0xFF];
				pOut[0] = c.r; pOut[1] = c.g; pOut[2] = c.b; pOut[3] = c.a;
				continue;
			}

			for( int c = 0; c < 4; ++c )
			{
				if( iMax[c] == 0 )
					pOut[c] = c == 3? 255: 0;
				else
				{
					const Uint32 v = (iVal & fmt.Mask[c]) >> fmt.Shift[c];
					pOut[c] = Uint8( (v*255 + iMax[c]/2) / iMax[c] );
				}
			}
		}
	}
}

/*
 * Pipelines.  The key packs everything a pipeline is made from.
 */
static Uint32 PipelineKey( bool bModel, bool bBlend, SDL_GPUBlendFactor src, SDL_GPUBlendFactor dst,
	bool bDepthTest, bool bDepthWrite, SDL_GPUCompareOp DepthOp, SDL_GPUCullMode cull, SDL_GPUFillMode fill )
{
	return Uint32(bModel) | (Uint32(bBlend) << 1) | (Uint32(src) << 2) | (Uint32(dst) << 7) |
		(Uint32(bDepthTest) << 12) | (Uint32(bDepthWrite) << 13) | (Uint32(DepthOp) << 14) |
		(Uint32(cull) << 18) | (Uint32(fill) << 20);
}

static SDL_GPUGraphicsPipeline *CreatePipeline( Uint32 iKey )
{
	const bool bModel = (iKey & 1) != 0;
	const bool bBlend = ((iKey >> 1) & 1) != 0;
	const SDL_GPUBlendFactor src = SDL_GPUBlendFactor( (iKey >> 2) & 31 );
	const SDL_GPUBlendFactor dst = SDL_GPUBlendFactor( (iKey >> 7) & 31 );
	const bool bDepthTest = ((iKey >> 12) & 1) != 0;
	const bool bDepthWrite = ((iKey >> 13) & 1) != 0;
	const SDL_GPUCompareOp DepthOp = SDL_GPUCompareOp( (iKey >> 14) & 15 );
	const SDL_GPUCullMode cull = SDL_GPUCullMode( (iKey >> 18) & 3 );
	const SDL_GPUFillMode fill = SDL_GPUFillMode( (iKey >> 20) & 1 );

	SDL_GPUColorTargetDescription target;
	SDL_zero( target );
	target.format = g_SwapchainFormat;
	target.blend_state.enable_blend = bBlend;
	target.blend_state.src_color_blendfactor = src;
	target.blend_state.dst_color_blendfactor = dst;
	target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
	/* glBlendFunc does alpha the same way. */
	target.blend_state.src_alpha_blendfactor = src;
	target.blend_state.dst_alpha_blendfactor = dst;
	target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

	SDL_GPUVertexBufferDescription buffers[3];
	SDL_GPUVertexAttribute attributes[6];
	SDL_zero( buffers );
	SDL_zero( attributes );

	SDL_GPUGraphicsPipelineCreateInfo info;
	SDL_zero( info );
	if( bModel )
	{
		/* Positions, normals and texture coordinates, each in its own buffer. */
		const int iPitch[3] = { 12, 12, 8 };
		const SDL_GPUVertexElementFormat format[3] = {
			SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2 };
		for( int i = 0; i < 3; ++i )
		{
			buffers[i].slot = i;
			buffers[i].pitch = iPitch[i];
			buffers[i].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
			attributes[i].location = i;
			attributes[i].buffer_slot = i;
			attributes[i].format = format[i];
			attributes[i].offset = 0;
		}
		info.vertex_input_state.num_vertex_buffers = 3;
		info.vertex_input_state.num_vertex_attributes = 3;
		info.vertex_shader = g_pModelVS;
	}
	else
	{
		buffers[0].slot = 0;
		buffers[0].pitch = sizeof(Vertex);
		buffers[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
		const struct { SDL_GPUVertexElementFormat format; Uint32 offset; } attr[6] = {
			{ SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, p) },
			{ SDL_GPU_VERTEXELEMENTFORMAT_HALF2, offsetof(Vertex, n) },
			{ SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, t) },
			{ SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(Vertex, c) },
			{ SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, offsetof(Vertex, n) + 4 },	// z, mode, padding
			{ SDL_GPU_VERTEXELEMENTFORMAT_HALF2, offsetof(Vertex, n) + 4 },
		};
		for( int i = 0; i < 6; ++i )
		{
			attributes[i].location = i;
			attributes[i].buffer_slot = 0;
			attributes[i].format = attr[i].format;
			attributes[i].offset = attr[i].offset;
		}
		info.vertex_input_state.num_vertex_buffers = 1;
		info.vertex_input_state.num_vertex_attributes = 6;
		info.vertex_shader = g_pSpriteVS;
	}
	info.vertex_input_state.vertex_buffer_descriptions = buffers;
	info.vertex_input_state.vertex_attributes = attributes;
	info.fragment_shader = g_pFS;
	info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	info.rasterizer_state.fill_mode = fill;
	info.rasterizer_state.cull_mode = cull;
	info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
	info.rasterizer_state.enable_depth_clip = true;
	info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
	info.depth_stencil_state.enable_depth_test = bDepthTest;
	info.depth_stencil_state.enable_depth_write = bDepthWrite;
	info.depth_stencil_state.compare_op = DepthOp;
	info.target_info.color_target_descriptions = &target;
	info.target_info.num_color_targets = 1;
	info.target_info.depth_stencil_format = g_DepthFormat;
	info.target_info.has_depth_stencil_target = true;

	SDL_GPUGraphicsPipeline *pPipeline = SDL_CreateGPUGraphicsPipeline( g_pDevice, &info );
	if( pPipeline == NULL )
		RageException::Throw( "%s", GPUError("SDL_CreateGPUGraphicsPipeline").c_str() );
	return pPipeline;
}

static SDL_GPUGraphicsPipeline *GetPipeline( Uint32 iKey )
{
	if( iKey == g_iLastPipelineKey )
		return g_pLastPipeline;
	std::map<Uint32, SDL_GPUGraphicsPipeline *>::iterator it = g_Pipelines.find( iKey );
	SDL_GPUGraphicsPipeline *p;
	if( it != g_Pipelines.end() )
		p = it->second;
	else
		p = g_Pipelines[iKey] = CreatePipeline( iKey );
	g_iLastPipelineKey = iKey;
	g_pLastPipeline = p;
	return p;
}

static void ReleasePipelines()
{
	for( std::map<Uint32, SDL_GPUGraphicsPipeline *>::iterator it = g_Pipelines.begin(); it != g_Pipelines.end(); ++it )
		SDL_ReleaseGPUGraphicsPipeline( g_pDevice, it->second );
	g_Pipelines.clear();
	g_iLastPipelineKey = 0xFFFFFFFF;
	g_pLastPipeline = NULL;
}

/*
 * Buffers.
 */
static void ReserveBuffer( SDL_GPUBuffer *&pBuffer, Uint32 &iSize, Uint32 iNeeded, SDL_GPUBufferUsageFlags usage )
{
	if( pBuffer && iSize >= iNeeded )
		return;
	/* The old one goes when the GPU is done with it. */
	if( pBuffer )
		SDL_ReleaseGPUBuffer( g_pDevice, pBuffer );
	iSize = max( iNeeded, 1024u );
	iSize = power_of_two( iSize );
	SDL_GPUBufferCreateInfo info;
	SDL_zero( info );
	info.usage = usage;
	info.size = iSize;
	pBuffer = SDL_CreateGPUBuffer( g_pDevice, &info );
	if( pBuffer == NULL )
		RageException::Throw( "%s", GPUError("SDL_CreateGPUBuffer").c_str() );
}

static void ReserveUploadBuffer( Uint32 iNeeded )
{
	if( g_pUploadBuffer && g_iUploadBufferBytes >= iNeeded )
		return;
	if( g_pUploadBuffer )
		SDL_ReleaseGPUTransferBuffer( g_pDevice, g_pUploadBuffer );
	g_iUploadBufferBytes = power_of_two( max(iNeeded, 65536u) );
	SDL_GPUTransferBufferCreateInfo info;
	SDL_zero( info );
	info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	info.size = g_iUploadBufferBytes;
	g_pUploadBuffer = SDL_CreateGPUTransferBuffer( g_pDevice, &info );
	if( g_pUploadBuffer == NULL )
		RageException::Throw( "%s", GPUError("SDL_CreateGPUTransferBuffer").c_str() );
}

/*
 * Compiled geometry (models).  The data is kept here too; it goes to the GPU
 * with the frame that first draws it after it changed.
 */
class RageCompiledGeometrySDLGPU: public RageCompiledGeometry
{
public:
	RageCompiledGeometrySDLGPU()
	{
		for( int i = 0; i < 4; ++i )
		{
			m_pBuffers[i] = NULL;
			m_iBufferBytes[i] = 0;
		}
		m_bDirty = true;
		m_bQueued = false;
	}

	~RageCompiledGeometrySDLGPU()
	{
		for( int i = 0; i < 4; ++i )
			if( m_pBuffers[i] )
				SDL_ReleaseGPUBuffer( g_pDevice, m_pBuffers[i] );
	}

	void Allocate( const vector<msMesh> &vMeshes )
	{
		m_vPosition.resize( GetTotalVertices()*3 );
		m_vTexture.resize( GetTotalVertices()*2 );
		m_vNormal.resize( GetTotalVertices()*3 );
		m_vTriangles.resize( GetTotalTriangles()*3 );
		m_bDirty = true;
	}

	void Change( const vector<msMesh> &vMeshes )
	{
		for( unsigned i=0; i<vMeshes.size(); i++ )
		{
			const MeshInfo& meshInfo = m_vMeshInfo[i];
			const msMesh& mesh = vMeshes[i];
			const vector<RageModelVertex> &Vertices = mesh.Vertices;
			const vector<msTriangle> &Triangles = mesh.Triangles;

			for( unsigned j=0; j<Vertices.size(); j++ )
			{
				const int iVert = meshInfo.iVertexStart+j;
				memcpy( &m_vPosition[iVert*3], &Vertices[j].p, sizeof(float)*3 );
				memcpy( &m_vTexture[iVert*2], &Vertices[j].t, sizeof(float)*2 );
				memcpy( &m_vNormal[iVert*3], &Vertices[j].n, sizeof(float)*3 );
			}

			for( unsigned j=0; j<Triangles.size(); j++ )
				for( unsigned k=0; k<3; k++ )
				{
					int iVertexIndexInVBO = meshInfo.iVertexStart + Triangles[j].nVertexIndices[k];
					m_vTriangles[(meshInfo.iTriangleStart+j)*3+k] = (uint16_t) iVertexIndexInVBO;
				}
		}
		m_bDirty = true;
	}

	/* RageCompiledGeometry wants this; we draw in DrawCompiledGeometryInternal. */
	void Draw( int iMeshIndex ) const { }

	/* Record a draw of a mesh, with the state PrepareDraw set up. */
	void Record( int iMeshIndex ) const;

	/* The bytes Upload needs, and sending them. */
	Uint32 GetUploadBytes() const
	{
		return Uint32( (m_vPosition.size() + m_vTexture.size() + m_vNormal.size())*sizeof(float) +
			((m_vTriangles.size()*sizeof(uint16_t) + 3) & ~3) );
	}
	void Upload( SDL_GPUCopyPass *pPass, Uint8 *pMapped, Uint32 &iOffset ) const
	{
		const void *pData[4] = {
			m_vPosition.empty()? NULL: &m_vPosition[0],
			m_vNormal.empty()? NULL: &m_vNormal[0],
			m_vTexture.empty()? NULL: &m_vTexture[0],
			m_vTriangles.empty()? NULL: &m_vTriangles[0] };
		const Uint32 iBytes[4] = {
			Uint32( m_vPosition.size()*sizeof(float) ),
			Uint32( m_vNormal.size()*sizeof(float) ),
			Uint32( m_vTexture.size()*sizeof(float) ),
			Uint32( m_vTriangles.size()*sizeof(uint16_t) ) };
		for( int i = 0; i < 4; ++i )
		{
			if( iBytes[i] == 0 )
				continue;
			ReserveBuffer( m_pBuffers[i], m_iBufferBytes[i], iBytes[i],
				i == 3? SDL_GPU_BUFFERUSAGE_INDEX: SDL_GPU_BUFFERUSAGE_VERTEX );
			memcpy( pMapped + iOffset, pData[i], iBytes[i] );

			SDL_GPUTransferBufferLocation src;
			src.transfer_buffer = g_pUploadBuffer;
			src.offset = iOffset;
			SDL_GPUBufferRegion dst;
			dst.buffer = m_pBuffers[i];
			dst.offset = 0;
			dst.size = iBytes[i];
			SDL_UploadToGPUBuffer( pPass, &src, &dst, true );
			iOffset += (iBytes[i] + 3) & ~3;
		}
		m_bDirty = false;
		m_bQueued = false;
	}

	void Bind( SDL_GPURenderPass *pPass ) const
	{
		SDL_GPUBufferBinding bind[3];
		for( int i = 0; i < 3; ++i )
		{
			bind[i].buffer = m_pBuffers[i];
			bind[i].offset = 0;
		}
		SDL_BindGPUVertexBuffers( pPass, 0, bind, 3 );
		SDL_GPUBufferBinding index;
		index.buffer = m_pBuffers[3];
		index.offset = 0;
		SDL_BindGPUIndexBuffer( pPass, &index, SDL_GPU_INDEXELEMENTSIZE_16BIT );
	}

	bool IsUploaded() const { return m_pBuffers[0] && m_pBuffers[3]; }

	/* The context is gone; so are the buffers. */
	void Invalidate()
	{
		for( int i = 0; i < 4; ++i )
		{
			m_pBuffers[i] = NULL;
			m_iBufferBytes[i] = 0;
		}
		m_bDirty = true;
		m_bQueued = false;
	}

	mutable bool m_bDirty, m_bQueued;

private:
	vector<float> m_vPosition, m_vTexture, m_vNormal;
	vector<uint16_t> m_vTriangles;

	mutable SDL_GPUBuffer *m_pBuffers[4];	// positions, normals, texture coordinates, triangles
	mutable Uint32 m_iBufferBytes[4];
};

/*
 * Recording.
 */

/* Record the uniform blocks that changed, and point the draw at them. */
static void RecordBlocks()
{
	Frame &f = g_Frame;
	if( g_bTransformDirty || f.vTransform.empty() )
	{
		f.vTransform.push_back( g_Transform );
		g_bTransformDirty = false;
	}
	if( g_bLightDirty || f.vLight.empty() )
	{
		f.vLight.push_back( g_Light );
		g_bLightDirty = false;
	}
	if( g_bFragmentDirty || f.vFragment.empty() )
	{
		f.vFragment.push_back( g_Fragment );
		g_bFragmentDirty = false;
	}
	g_Draw.Cmd.iTransform = int(f.vTransform.size()) - 1;
	g_Draw.Cmd.iLight = int(f.vLight.size()) - 1;
	g_Draw.Cmd.iFragment = int(f.vFragment.size()) - 1;
}

static bool SameState( const Command &a, const Command &b )
{
	return a.pPipeline == b.pPipeline &&
		(a.pTexture[0] == b.pTexture[0] || a.pTexture[0] == NULL || b.pTexture[0] == NULL) &&
		(a.pSampler[0] == b.pSampler[0] || a.pTexture[0] == NULL || b.pTexture[0] == NULL) &&
		a.pTexture[1] == b.pTexture[1] && a.pSampler[1] == b.pSampler[1] &&
		a.iTransform == b.iTransform && a.iLight == b.iLight && a.iFragment == b.iFragment &&
		a.fMinDepth == b.fMinDepth && a.fMaxDepth == b.fMaxDepth &&
		a.iViewportX == b.iViewportX && a.iViewportY == b.iViewportY;
}

/* Make room for iCount more vertices of the given kind, with the state from
 * PrepareDraw, and return where they go.  A quad command can't have more than
 * MAX_QUAD_VERTICES, so ask for at most that many quads' worth at a time. */
static Vertex *RecordVertices( Uint8 iKind, int iCount )
{
	ASSERT( iKind != CMD_QUADS || iCount <= MAX_QUAD_VERTICES );
	Frame &f = g_Frame;
	const Uint32 iFirst = Uint32( f.vVertices.size() );

	Command *pLast = f.vCommands.empty()? NULL: &f.vCommands.back();
	if( pLast != NULL && pLast->iKind == iKind && pLast->iFirst + pLast->iCount == iFirst &&
		(iKind != CMD_QUADS || pLast->iCount + iCount <= MAX_QUAD_VERTICES) &&
		SameState(*pLast, g_Draw.Cmd) )
	{
		/* Draws that don't texture unit 0 go with any texture. */
		if( pLast->pTexture[0] == NULL )
		{
			pLast->pTexture[0] = g_Draw.Cmd.pTexture[0];
			pLast->pSampler[0] = g_Draw.Cmd.pSampler[0];
		}
		pLast->iCount += iCount;
	}
	else
	{
		f.vCommands.push_back( g_Draw.Cmd );
		Command &cmd = f.vCommands.back();
		cmd.iKind = iKind;
		cmd.iFirst = iFirst;
		cmd.iCount = iCount;
		cmd.pGeometry = NULL;
	}

	if( f.vVertices.capacity() < iFirst + iCount )
		f.vVertices.reserve( max<size_t>(f.vVertices.capacity()*2, iFirst + iCount + 4096) );
	f.vVertices.resize( iFirst + iCount );
	return &f.vVertices[iFirst];
}

void RageCompiledGeometrySDLGPU::Record( int iMeshIndex ) const
{
	const MeshInfo& meshInfo = m_vMeshInfo[iMeshIndex];
	if( !meshInfo.iVertexCount || !meshInfo.iTriangleCount )
		return;

	if( m_bDirty && !m_bQueued )
	{
		g_Frame.vUploadGeometry.push_back( this );
		m_bQueued = true;
	}

	g_Frame.vCommands.push_back( g_Draw.Cmd );
	Command &cmd = g_Frame.vCommands.back();
	cmd.iKind = CMD_MODEL;
	cmd.iFirst = meshInfo.iTriangleStart*3;
	cmd.iCount = meshInfo.iTriangleCount*3;
	cmd.pGeometry = this;
}

/*
 * Depth slices.  Slice k of n covers depths [1-(k+1)/n, 1-k/n]: each one is
 * nearer than the last, so a draw in it passes a LEQUAL test, and fails a
 * GREATER one, against anything drawn in an earlier slice, just as it would
 * against a cleared buffer.
 */
static void NextDepthSlice()
{
	if( g_iDepthSlice < 0 )
		g_iDepthSlice = 0;	// cleared when the render pass starts
	else if( g_iDepthSlice+1 < g_iDepthSlices )
		++g_iDepthSlice;
	else
	{
		/* Out of slices: really clear it, which ends the render pass. */
		Command cmd;
		memset( &cmd, 0, sizeof(cmd) );
		cmd.iKind = CMD_CLEAR_DEPTH;
		g_Frame.vCommands.push_back( cmd );
		g_iDepthSlice = 0;
	}
}

/* The depth range of a draw: like RageDisplay_GLCore's glDepthRange, within the
 * current slice. */
static void GetDepthRange( int iDepthRange, float &fMin, float &fMax )
{
	const int iSlice = max( g_iDepthSlice, 0 );
	const float fWidth = 1.0f / g_iDepthSlices;
	const float fLow = 1 - (iSlice+1)*fWidth;
	const float fHigh = iSlice == 0? 1.0f: 1 - iSlice*fWidth;
	if( iDepthRange )
	{
		fMin = fLow;
		fMax = fLow + 0.95f*(fHigh - fLow);
	}
	else
	{
		fMin = fLow + 0.05f*(fHigh - fLow);
		fMax = fHigh;
	}
}

/*
 * Playing a frame back.
 */
static void BeginPass( SDL_GPUCommandBuffer *pCmd, SDL_GPUTexture *pTarget, bool bClearColor, SDL_GPURenderPass *&pPass )
{
	SDL_GPUColorTargetInfo color;
	SDL_zero( color );
	color.texture = pTarget;
	color.clear_color.r = color.clear_color.g = color.clear_color.b = 0;
	color.clear_color.a = 1;
	color.load_op = bClearColor? SDL_GPU_LOADOP_CLEAR: SDL_GPU_LOADOP_LOAD;
	color.store_op = SDL_GPU_STOREOP_STORE;

	SDL_GPUDepthStencilTargetInfo depth;
	SDL_zero( depth );
	depth.texture = g_pDepth;
	depth.clear_depth = 1;
	depth.load_op = SDL_GPU_LOADOP_CLEAR;
	depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
	depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
	depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
	depth.cycle = true;

	pPass = SDL_BeginGPURenderPass( pCmd, &color, 1, &depth );
}

static void EnsureDepthBuffer( Uint32 iWidth, Uint32 iHeight )
{
	if( g_pDepth && g_iDepthWidth == iWidth && g_iDepthHeight == iHeight )
		return;
	if( g_pDepth )
		SDL_ReleaseGPUTexture( g_pDevice, g_pDepth );
	SDL_GPUTextureCreateInfo info;
	SDL_zero( info );
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = g_DepthFormat;
	info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
	info.width = iWidth;
	info.height = iHeight;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	g_pDepth = SDL_CreateGPUTexture( g_pDevice, &info );
	if( g_pDepth == NULL )
		RageException::Throw( "%s", GPUError("Creating the depth buffer").c_str() );
	g_iDepthWidth = iWidth;
	g_iDepthHeight = iHeight;
}

/* Send the frame's vertices, and the models it draws that changed. */
static void UploadFrame( SDL_GPUCommandBuffer *pCmd, Frame &f )
{
	const Uint32 iVertexBytes = Uint32( f.vVertices.size()*sizeof(Vertex) );
	Uint32 iBytes = iVertexBytes;
	for( unsigned i = 0; i < f.vUploadGeometry.size(); ++i )
		iBytes += f.vUploadGeometry[i]->GetUploadBytes();
	if( iBytes == 0 )
		return;

	ReserveUploadBuffer( iBytes );
	if( iVertexBytes )
		ReserveBuffer( g_pVertexBuffer, g_iVertexBufferBytes, iVertexBytes, SDL_GPU_BUFFERUSAGE_VERTEX );

	/* Cycling gives us a fresh buffer if the GPU still has the last one. */
	Uint8 *pMapped = (Uint8 *) SDL_MapGPUTransferBuffer( g_pDevice, g_pUploadBuffer, true );
	if( pMapped == NULL )
		RageException::Throw( "%s", GPUError("SDL_MapGPUTransferBuffer").c_str() );

	SDL_GPUCopyPass *pPass = SDL_BeginGPUCopyPass( pCmd );
	if( iVertexBytes )
	{
		memcpy( pMapped, &f.vVertices[0], iVertexBytes );
		SDL_GPUTransferBufferLocation src;
		src.transfer_buffer = g_pUploadBuffer;
		src.offset = 0;
		SDL_GPUBufferRegion dst;
		dst.buffer = g_pVertexBuffer;
		dst.offset = 0;
		dst.size = iVertexBytes;
		SDL_UploadToGPUBuffer( pPass, &src, &dst, true );
	}

	Uint32 iOffset = iVertexBytes;
	for( unsigned i = 0; i < f.vUploadGeometry.size(); ++i )
		f.vUploadGeometry[i]->Upload( pPass, pMapped, iOffset );
	SDL_UnmapGPUTransferBuffer( g_pDevice, g_pUploadBuffer );
	SDL_EndGPUCopyPass( pPass );
}

/* With SM_RENDER_STATS set, what frames send, averaged over a second. */
namespace
{
	struct Stats
	{
		int iFrames, iCommands, iVertices, iPipelines, iSamplers, iUniforms;

		/* Where the time goes, in performance counter ticks: the game between
		 * frames, recording the GPU's work, waiting for the GPU to finish an
		 * older frame, waiting for the window to give us a buffer to draw to,
		 * and submitting. */
		Uint64 iGame, iRecord, iGPUWait, iAcquire, iAcquireMax, iSubmit;
		int iSkipped;
	};
	Stats g_Stats;
	bool g_bStats = false;
	RageTimer g_StatsTimer;
}

static double TicksToMs( Uint64 iTicks )
{
	return iTicks * 1000.0 / SDL_GetPerformanceFrequency();
}

static void LogStats()
{
	if( !g_bStats )
		return;
	++g_Stats.iFrames;
	if( g_StatsTimer.PeekDeltaTime() < 1.0f )
		return;
	g_StatsTimer.GetDeltaTime();
	const Stats &s = g_Stats;
	const float f = float( max(s.iFrames, 1) );
	LOG->Info( "SDL GPU per frame: %.1f commands, %.0f vertices, %.1f pipeline binds, %.1f texture binds, %.1f uniform pushes",
		s.iCommands/f, s.iVertices/f, s.iPipelines/f, s.iSamplers/f, s.iUniforms/f );
	LOG->Info( "SDL GPU ms per frame: game %.3f, recording %.3f, GPU wait %.3f, window buffer wait %.3f (longest %.3f), submit %.3f; %d frames drawn, %d not",
		TicksToMs(s.iGame)/f, TicksToMs(s.iRecord)/f, TicksToMs(s.iGPUWait)/f, TicksToMs(s.iAcquire)/f, TicksToMs(s.iAcquireMax),
		TicksToMs(s.iSubmit)/f, s.iFrames, s.iSkipped );
	memset( &g_Stats, 0, sizeof(g_Stats) );
}

/* Draw a recorded frame into pTarget, whose vertices are in g_pVertexBuffer. */
static void PlayFrame( SDL_GPUCommandBuffer *pCmd, const Frame &f, SDL_GPUTexture *pTarget, Uint32 iWidth, Uint32 iHeight )
{
	EnsureDepthBuffer( iWidth, iHeight );

	SDL_GPURenderPass *pPass;
	BeginPass( pCmd, pTarget, true, pPass );
	if( pPass == NULL )
		return;

	/* What's bound; reset whenever a pass begins. */
	SDL_GPUGraphicsPipeline *pPipeline = NULL;
	SDL_GPUTexture *pTexture[2] = { NULL, NULL };
	SDL_GPUSampler *pSampler[2] = { NULL, NULL };
	int iTransform = -1, iLight = -1, iFragment = -1;
	float fMinDepth = -1, fMaxDepth = -1;
	int iViewportX = 0, iViewportY = 0;
	bool bSpriteBuffers = false;

	for( unsigned c = 0; c < f.vCommands.size(); ++c )
	{
		const Command &cmd = f.vCommands[c];
		if( cmd.iKind == CMD_CLEAR_DEPTH )
		{
			SDL_EndGPURenderPass( pPass );
			BeginPass( pCmd, pTarget, false, pPass );
			if( pPass == NULL )
				return;
			pPipeline = NULL;
			pTexture[0] = pTexture[1] = NULL;
			pSampler[0] = pSampler[1] = NULL;
			iTransform = iLight = iFragment = -1;
			fMinDepth = fMaxDepth = -1;
			bSpriteBuffers = false;
			continue;
		}
		if( cmd.iKind == CMD_MODEL && !cmd.pGeometry->IsUploaded() )
			continue;

		if( cmd.pPipeline != pPipeline )
		{
			SDL_BindGPUGraphicsPipeline( pPass, cmd.pPipeline );
			pPipeline = cmd.pPipeline;
			++g_Stats.iPipelines;
		}
		if( cmd.fMinDepth != fMinDepth || cmd.fMaxDepth != fMaxDepth || cmd.iViewportX != iViewportX || cmd.iViewportY != iViewportY )
		{
			SDL_GPUViewport vp;
			vp.x = float(cmd.iViewportX);
			vp.y = float(cmd.iViewportY);
			vp.w = float(iWidth);
			vp.h = float(iHeight);
			vp.min_depth = cmd.fMinDepth;
			vp.max_depth = cmd.fMaxDepth;
			SDL_SetGPUViewport( pPass, &vp );
			fMinDepth = cmd.fMinDepth;
			fMaxDepth = cmd.fMaxDepth;
			iViewportX = cmd.iViewportX;
			iViewportY = cmd.iViewportY;
		}

		SDL_GPUTexture *pTex0 = cmd.pTexture[0]? cmd.pTexture[0]: g_pWhite;
		SDL_GPUTexture *pTex1 = cmd.pTexture[1]? cmd.pTexture[1]: g_pWhite;
		SDL_GPUSampler *pSamp0 = cmd.pTexture[0]? cmd.pSampler[0]: g_pSampler[0];
		SDL_GPUSampler *pSamp1 = cmd.pTexture[1]? cmd.pSampler[1]: g_pSampler[0];
		if( pTex0 != pTexture[0] || pTex1 != pTexture[1] || pSamp0 != pSampler[0] || pSamp1 != pSampler[1] )
		{
			SDL_GPUTextureSamplerBinding bind[2];
			bind[0].texture = pTex0;
			bind[0].sampler = pSamp0;
			bind[1].texture = pTex1;
			bind[1].sampler = pSamp1;
			SDL_BindGPUFragmentSamplers( pPass, 0, bind, 2 );
			++g_Stats.iSamplers;
			pTexture[0] = pTex0;
			pTexture[1] = pTex1;
			pSampler[0] = pSamp0;
			pSampler[1] = pSamp1;
		}

		if( cmd.iTransform != iTransform )
		{
			SDL_PushGPUVertexUniformData( pCmd, 0, &f.vTransform[cmd.iTransform], sizeof(TransformBlock) );
			iTransform = cmd.iTransform;
			++g_Stats.iUniforms;
		}
		if( cmd.iLight != iLight )
		{
			SDL_PushGPUVertexUniformData( pCmd, 1, &f.vLight[cmd.iLight], sizeof(LightBlock) );
			iLight = cmd.iLight;
			++g_Stats.iUniforms;
		}
		if( cmd.iFragment != iFragment )
		{
			SDL_PushGPUFragmentUniformData( pCmd, 0, &f.vFragment[cmd.iFragment], sizeof(FragmentBlock) );
			iFragment = cmd.iFragment;
			++g_Stats.iUniforms;
		}

		switch( cmd.iKind )
		{
		case CMD_QUADS:
		case CMD_TRIANGLES:
			if( !bSpriteBuffers )
			{
				SDL_GPUBufferBinding vb;
				vb.buffer = g_pVertexBuffer;
				vb.offset = 0;
				SDL_BindGPUVertexBuffers( pPass, 0, &vb, 1 );
				SDL_GPUBufferBinding ib;
				ib.buffer = g_pQuadIndices;
				ib.offset = 0;
				SDL_BindGPUIndexBuffer( pPass, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT );
				bSpriteBuffers = true;
			}
			if( cmd.iKind == CMD_QUADS )
				SDL_DrawGPUIndexedPrimitives( pPass, cmd.iCount/4*6, 1, 0, cmd.iFirst, 0 );
			else
				SDL_DrawGPUPrimitives( pPass, cmd.iCount, 1, cmd.iFirst, 0 );
			break;
		case CMD_MODEL:
			cmd.pGeometry->Bind( pPass );
			bSpriteBuffers = false;
			SDL_DrawGPUIndexedPrimitives( pPass, cmd.iCount, 1, cmd.iFirst, 0, 0 );
			break;
		}
	}

	SDL_EndGPURenderPass( pPass );
}

/* Textures and models deleted during frame N may be in frame N's recording,
 * and in the screenshot copy of it until frame N+1 is sent. */
static void ReleaseDeleted()
{
	for( unsigned i = 0; i < g_DeletedTextures[1].size(); ++i )
		SDL_ReleaseGPUTexture( g_pDevice, g_DeletedTextures[1][i] );
	for( unsigned i = 0; i < g_DeletedGeometry[1].size(); ++i )
		delete g_DeletedGeometry[1][i];
	g_DeletedTextures[1].swap( g_DeletedTextures[0] );
	g_DeletedTextures[0].clear();
	g_DeletedGeometry[1].swap( g_DeletedGeometry[0] );
	g_DeletedGeometry[0].clear();
}

/*
 * Setting up the device.
 */
/* A shader in each language a device might take, from shaders/build.sh. */
struct ShaderCode
{
	const unsigned char *pSPIRV, *pDXIL, *pMSL;
	size_t iSPIRVSize, iDXILSize, iMSLSize;
};
#define SHADER_CODE(name) { g_##name##SPIRV, g_##name##DXIL, g_##name##MSL, \
	sizeof(g_##name##SPIRV), sizeof(g_##name##DXIL), sizeof(g_##name##MSL) }

static SDL_GPUShader *CreateShader( SDL_GPUShaderStage stage, const ShaderCode &code,
	int iSamplers, int iUniformBuffers )
{
	SDL_GPUShaderCreateInfo info;
	SDL_zero( info );
	const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats( g_pDevice );
	if( formats & SDL_GPU_SHADERFORMAT_SPIRV )	// Vulkan
	{
		info.code = code.pSPIRV;
		info.code_size = code.iSPIRVSize;
		info.entrypoint = "main";
		info.format = SDL_GPU_SHADERFORMAT_SPIRV;
	}
	else if( formats & SDL_GPU_SHADERFORMAT_DXIL )	// Direct3D 12
	{
		info.code = code.pDXIL;
		info.code_size = code.iDXILSize;
		info.entrypoint = "main";
		info.format = SDL_GPU_SHADERFORMAT_DXIL;
	}
	else	// Metal
	{
		info.code = code.pMSL;
		info.code_size = code.iMSLSize - 1;	// without the terminating null
		info.entrypoint = "main0";
		info.format = SDL_GPU_SHADERFORMAT_MSL;
	}
	info.stage = stage;
	info.num_samplers = iSamplers;
	info.num_uniform_buffers = iUniformBuffers;
	SDL_GPUShader *pShader = SDL_CreateGPUShader( g_pDevice, &info );
	if( pShader == NULL )
		RageException::ThrowNonfatal( "%s", GPUError("SDL_CreateGPUShader").c_str() );
	return pShader;
}

static SDL_GPUSampler *CreateSampler( bool bWrap, const RageDisplay::VideoModeParams &p )
{
	SDL_GPUSamplerCreateInfo info;
	SDL_zero( info );
	info.min_filter = SDL_GPU_FILTER_LINEAR;
	info.mag_filter = SDL_GPU_FILTER_LINEAR;
	info.mipmap_mode = p.bTrilinearFiltering? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR: SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	const SDL_GPUSamplerAddressMode mode = bWrap? SDL_GPU_SAMPLERADDRESSMODE_REPEAT: SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	info.address_mode_u = info.address_mode_v = info.address_mode_w = mode;
	info.min_lod = 0;
	info.max_lod = 1000;
	if( p.bAnisotropicFiltering )
	{
		info.enable_anisotropy = true;
		info.max_anisotropy = 16;
	}
	SDL_GPUSampler *pSampler = SDL_CreateGPUSampler( g_pDevice, &info );
	if( pSampler == NULL )
		RageException::Throw( "%s", GPUError("SDL_CreateGPUSampler").c_str() );
	return pSampler;
}

/* Upload the w x h top left of img to (x, y) of a texture, and make its mip
 * maps if it has them. */
static void UploadTexture( SDL_GPUTexture *pTex, const RageSurface *img, int x, int y, int iWidth, int iHeight, bool bMipMaps )
{
	SDL_GPUTransferBufferCreateInfo info;
	SDL_zero( info );
	info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	info.size = iWidth*iHeight*4;
	SDL_GPUTransferBuffer *pTransfer = SDL_CreateGPUTransferBuffer( g_pDevice, &info );
	if( pTransfer == NULL )
		RageException::Throw( "%s", GPUError("SDL_CreateGPUTransferBuffer").c_str() );

	Uint8 *pMapped = (Uint8 *) SDL_MapGPUTransferBuffer( g_pDevice, pTransfer, false );
	if( pMapped == NULL )
		RageException::Throw( "%s", GPUError("SDL_MapGPUTransferBuffer").c_str() );
	ConvertToRGBA8( img, iWidth, iHeight, pMapped );
	SDL_UnmapGPUTransferBuffer( g_pDevice, pTransfer );

	SDL_GPUCommandBuffer *pCmd = SDL_AcquireGPUCommandBuffer( g_pDevice );
	if( pCmd == NULL )
		RageException::Throw( "%s", GPUError("SDL_AcquireGPUCommandBuffer").c_str() );
	SDL_GPUCopyPass *pPass = SDL_BeginGPUCopyPass( pCmd );
	SDL_GPUTextureTransferInfo src;
	SDL_zero( src );
	src.transfer_buffer = pTransfer;
	src.offset = 0;
	src.pixels_per_row = iWidth;
	src.rows_per_layer = iHeight;
	SDL_GPUTextureRegion dst;
	SDL_zero( dst );
	dst.texture = pTex;
	dst.x = x;
	dst.y = y;
	dst.w = iWidth;
	dst.h = iHeight;
	dst.d = 1;
	SDL_UploadToGPUTexture( pPass, &src, &dst, false );
	SDL_EndGPUCopyPass( pPass );
	if( bMipMaps )
		SDL_GenerateMipmapsForGPUTexture( pCmd, pTex );
	SDL_SubmitGPUCommandBuffer( pCmd );
	SDL_ReleaseGPUTransferBuffer( g_pDevice, pTransfer );
}

static SDL_GPUTexture *CreateGPUTexture( int iWidth, int iHeight, int iLevels, SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage )
{
	SDL_GPUTextureCreateInfo info;
	SDL_zero( info );
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = format;
	info.usage = usage;
	info.width = iWidth;
	info.height = iHeight;
	info.layer_count_or_depth = 1;
	info.num_levels = iLevels;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	return SDL_CreateGPUTexture( g_pDevice, &info );
}

static void InitDevice()
{
	const ShaderCode SpriteVS = SHADER_CODE( SpriteVert ), ModelVS = SHADER_CODE( ModelVert ), FS = SHADER_CODE( Frag );
	g_pSpriteVS = CreateShader( SDL_GPU_SHADERSTAGE_VERTEX, SpriteVS, 0, 2 );
	g_pModelVS = CreateShader( SDL_GPU_SHADERSTAGE_VERTEX, ModelVS, 0, 2 );
	g_pFS = CreateShader( SDL_GPU_SHADERSTAGE_FRAGMENT, FS, 2, 1 );

	/* 32 bit float depth has room for many slices; 24 bit for a few. */
	const SDL_GPUTextureFormat Depth[] = { SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D24_UNORM, SDL_GPU_TEXTUREFORMAT_D16_UNORM };
	const int iSlices[] = { 16, 4, 1 };
	g_DepthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
	for( unsigned i = 0; i < ARRAYSIZE(Depth); ++i )
	{
		if( SDL_GPUTextureSupportsFormat(g_pDevice, Depth[i], SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET) )
		{
			g_DepthFormat = Depth[i];
			g_iDepthSlices = iSlices[i];
			break;
		}
	}
	if( g_DepthFormat == SDL_GPU_TEXTUREFORMAT_INVALID )
		RageException::ThrowNonfatal( "No depth buffer format" );

	/* A white texel, for texture units that aren't used. */
	g_pWhite = CreateGPUTexture( 1, 1, 1, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER );
	{
		RageSurface *img = CreateSurface( 1, 1, 32,
			PIXEL_FORMAT_DESC[RageDisplay::FMT_RGBA8].masks[0], PIXEL_FORMAT_DESC[RageDisplay::FMT_RGBA8].masks[1],
			PIXEL_FORMAT_DESC[RageDisplay::FMT_RGBA8].masks[2], PIXEL_FORMAT_DESC[RageDisplay::FMT_RGBA8].masks[3] );
		memset( img->pixels, 0xFF, 4 );
		UploadTexture( g_pWhite, img, 0, 0, 1, 1, false );
		delete img;
	}

	/* The quad indices. */
	{
		const Uint32 iBytes = MAX_QUAD_VERTICES/4*6*sizeof(Uint16);
		Uint32 iSize = 0;
		ReserveBuffer( g_pQuadIndices, iSize, iBytes, SDL_GPU_BUFFERUSAGE_INDEX );
		ReserveUploadBuffer( iBytes );
		Uint16 *p = (Uint16 *) SDL_MapGPUTransferBuffer( g_pDevice, g_pUploadBuffer, true );
		for( int q = 0; q < MAX_QUAD_VERTICES/4; ++q )
		{
			const Uint16 i = Uint16( q*4 );
			p[q*6+0] = i+0; p[q*6+1] = i+1; p[q*6+2] = i+3;
			p[q*6+3] = i+1; p[q*6+4] = i+2; p[q*6+5] = i+3;
		}
		SDL_UnmapGPUTransferBuffer( g_pDevice, g_pUploadBuffer );
		SDL_GPUCommandBuffer *pCmd = SDL_AcquireGPUCommandBuffer( g_pDevice );
		SDL_GPUCopyPass *pPass = SDL_BeginGPUCopyPass( pCmd );
		SDL_GPUTransferBufferLocation src;
		src.transfer_buffer = g_pUploadBuffer;
		src.offset = 0;
		SDL_GPUBufferRegion dst;
		dst.buffer = g_pQuadIndices;
		dst.offset = 0;
		dst.size = iBytes;
		SDL_UploadToGPUBuffer( pPass, &src, &dst, false );
		SDL_EndGPUCopyPass( pPass );
		SDL_SubmitGPUCommandBuffer( pCmd );
	}
}

static void ReleaseFrameFences()
{
	for( unsigned i = 0; i < ARRAYSIZE(g_pFrameFence); ++i )
	{
		if( g_pFrameFence[i] )
			SDL_ReleaseGPUFence( g_pDevice, g_pFrameFence[i] );
		g_pFrameFence[i] = NULL;
	}
	g_iFenceSlot = 0;
}

static void FlushPendingFrame();

static void ShutdownDevice()
{
	if( g_pDevice == NULL )
		return;
	FlushPendingFrame();
	SDL_WaitForGPUIdle( g_pDevice );
	ReleaseFrameFences();

	g_Frame.Clear();
	g_LastFrame.Clear();
	ReleaseDeleted();
	ReleaseDeleted();
	ReleasePipelines();

	for( unsigned i = 0; i < g_Textures.size(); ++i )
		if( g_Textures[i].pTex )
			SDL_ReleaseGPUTexture( g_pDevice, g_Textures[i].pTex );
	g_Textures.clear();
	g_FreeHandles.clear();

	SDL_GPUTexture *pTextures[] = { g_pWhite, g_pDepth };
	for( unsigned i = 0; i < ARRAYSIZE(pTextures); ++i )
		if( pTextures[i] )
			SDL_ReleaseGPUTexture( g_pDevice, pTextures[i] );
	g_pWhite = g_pDepth = NULL;
	g_iDepthWidth = g_iDepthHeight = 0;
	for( int i = 0; i < 2; ++i )
	{
		if( g_pSampler[i] )
			SDL_ReleaseGPUSampler( g_pDevice, g_pSampler[i] );
		g_pSampler[i] = NULL;
	}
	SDL_GPUBuffer *pBuffers[] = { g_pVertexBuffer, g_pQuadIndices };
	for( unsigned i = 0; i < ARRAYSIZE(pBuffers); ++i )
		if( pBuffers[i] )
			SDL_ReleaseGPUBuffer( g_pDevice, pBuffers[i] );
	g_pVertexBuffer = g_pQuadIndices = NULL;
	g_iVertexBufferBytes = 0;
	if( g_pUploadBuffer )
		SDL_ReleaseGPUTransferBuffer( g_pDevice, g_pUploadBuffer );
	g_pUploadBuffer = NULL;
	g_iUploadBufferBytes = 0;
	SDL_GPUShader *pShaders[] = { g_pSpriteVS, g_pModelVS, g_pFS };
	for( unsigned i = 0; i < ARRAYSIZE(pShaders); ++i )
		if( pShaders[i] )
			SDL_ReleaseGPUShader( g_pDevice, pShaders[i] );
	g_pSpriteVS = g_pModelVS = g_pFS = NULL;

	if( g_pWindow )
		SDL_ReleaseWindowFromGPUDevice( g_pDevice, g_pWindow );
	g_pWindow = NULL;
	SDL_DestroyGPUDevice( g_pDevice );
	g_pDevice = NULL;
}

static void InitWantedState()
{
	WantedState &w = g_Wanted;
	w.bBlend = true;
	w.BlendSrc = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
	w.BlendDst = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	w.iDepthRange = 0;
	w.bZWrite = w.bZTest = false;
	w.DepthFunc = SDL_GPU_COMPAREOP_ALWAYS;
	w.bClearDepthPending = true;
	w.CullMode = SDL_GPU_CULLMODE_NONE;
	w.FillMode = SDL_GPU_FILLMODE_FILL;
	w.iActiveUnit = 0;
	for( int i = 0; i < MAX_TEXTURE_UNITS; ++i )
	{
		w.Unit[i].uTexture = 0;
		w.Unit[i].bEnabled = false;
		w.Unit[i].iMode = TEXMODE_MODULATE;
		w.Unit[i].bSphere = false;
		RageMatrixIdentity( &w.Unit[i].TexMatrix );
		w.Unit[i].iTexMatrixGen = unsigned(-1);
	}
	w.bAlphaTest = false;
	w.bLighting = w.bLight0 = false;

	/* GL's defaults. */
	w.MatEmissive = RageColor( 0, 0, 0, 1 );
	w.MatAmbient = RageColor( 0.2f, 0.2f, 0.2f, 1 );
	w.MatDiffuse = RageColor( 0.8f, 0.8f, 0.8f, 1 );
	w.MatSpecular = RageColor( 0, 0, 0, 1 );
	w.fMatShininess = 0;
	w.LightAmbient = RageColor( 0, 0, 0, 1 );
	w.LightDiffuse = RageColor( 1, 1, 1, 1 );
	w.LightSpecular = RageColor( 1, 1, 1, 1 );
	w.LightDir = RageVector3( 0, 0, 1 );
	w.ConstColor = RageColor( 1, 1, 1, 1 );

	RageMatrixIdentity( &g_LastWorld );
	RageMatrixIdentity( &g_LastCenteringView );

	memset( &g_Transform, 0, sizeof(g_Transform) );
	memset( &g_Light, 0, sizeof(g_Light) );
	memset( &g_Fragment, 0, sizeof(g_Fragment) );
	g_bTransformDirty = g_bLightDirty = g_bFragmentDirty = true;
}

/*
 * RageDisplay_SDLGPU
 */
RageDisplay_SDLGPU::RageDisplay_SDLGPU( VideoModeParams p )
{
	LOG->Trace( "RageDisplay_SDLGPU::RageDisplay_SDLGPU()" );
	LOG->MapLog( "renderer", "Current renderer: SDL GPU" );

	FixLilEndian();
	InitWantedState();

	g_pWind = MakeLowLevelWindow();
	g_pWind->SetNoGL( true );
	g_bStats = SDL_getenv( "SM_RENDER_STATS" ) != NULL;

	try {
		SetVideoMode( p );
	} catch(...) {
		/* SetVideoMode can throw. */
		ShutdownDevice();
		delete g_pWind;
		g_pWind = NULL;
		throw;
	}
}

RageDisplay_SDLGPU::~RageDisplay_SDLGPU()
{
	ShutdownDevice();
	delete g_pWind;
	g_pWind = NULL;
}

void RageDisplay_SDLGPU::Update( float fDeltaTime )
{
	g_pWind->Update( fDeltaTime );
}

static void SetSwapchain( const RageDisplay::VideoModeParams &p )
{
	FlushPendingFrame();
	/* Without vsync, show frames as soon as they're done. */
	SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
	if( !p.vsync )
	{
		if( SDL_WindowSupportsGPUPresentMode(g_pDevice, g_pWindow, SDL_GPU_PRESENTMODE_IMMEDIATE) )
			mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
		else if( SDL_WindowSupportsGPUPresentMode(g_pDevice, g_pWindow, SDL_GPU_PRESENTMODE_MAILBOX) )
			mode = SDL_GPU_PRESENTMODE_MAILBOX;
	}
	if( !SDL_SetGPUSwapchainParameters(g_pDevice, g_pWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode) )
		LOG->Warn( "%s", GPUError("SDL_SetGPUSwapchainParameters").c_str() );

	/* How many frames the GPU may be behind; more gives it work to do while we
	 * wait for the display, fewer shows what we draw sooner. */
	const char *szFrames = SDL_getenv( "SM_SDLGPU_FRAMES" );
	if( szFrames )
	{
		SDL_WaitForGPUIdle( g_pDevice );
		ReleaseFrameFences();
		g_iFramesInFlight = clamp( atoi(szFrames), 1, 3 );
		SDL_SetGPUAllowedFramesInFlight( g_pDevice, g_iFramesInFlight );
	}

	/* An experiment: draw only as many frames a second as this, and let the
	 * game run between them without drawing. */
	const char *szPresentHz = SDL_getenv( "SM_SDLGPU_PRESENT_HZ" );
	const int iPresentHz = szPresentHz? atoi( szPresentHz ): 0;
	g_iPresentInterval = iPresentHz > 0? SDL_GetPerformanceFrequency() / iPresentHz: 0;

	/* An experiment: a macOS window gives back the buffers we draw to only so
	 * fast (about 650 a second on an M5 at 120 Hz), and asking for one before
	 * then waits for it.  With SM_SDLGPU_WAIT_FIRST=1, wait at the top of the
	 * frame, before the game updates, rather than after drawing.  On an M5 it
	 * drew fewer frames (350-630 a second instead of 600-660), so it's off. */
	const char *szWaitFirst = SDL_getenv( "SM_SDLGPU_WAIT_FIRST" );
	g_bWaitFirst = iPresentHz <= 0 && mode == SDL_GPU_PRESENTMODE_IMMEDIATE &&
		szWaitFirst != NULL && atoi(szWaitFirst) != 0;

	const SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat( g_pDevice, g_pWindow );
	if( format != g_SwapchainFormat )
	{
		/* Pipelines are made for the format they draw to. */
		ReleasePipelines();
		g_SwapchainFormat = format;
	}

	for( int i = 0; i < 2; ++i )
	{
		if( g_pSampler[i] )
			SDL_ReleaseGPUSampler( g_pDevice, g_pSampler[i] );
		g_pSampler[i] = CreateSampler( i == 1, p );
	}

	/* Make the pipelines nearly every screen uses now, rather than at their
	 * first draw, which would be a hitch: 2D with each blend mode, and the
	 * depth tested and masking ones the note field and 3D backgrounds use. */
	const SDL_GPUBlendFactor Blend[][2] = {
		{ SDL_GPU_BLENDFACTOR_SRC_ALPHA, SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA },
		{ SDL_GPU_BLENDFACTOR_SRC_ALPHA, SDL_GPU_BLENDFACTOR_ONE },
		{ SDL_GPU_BLENDFACTOR_ZERO, SDL_GPU_BLENDFACTOR_ONE },
	};
	for( unsigned b = 0; b < ARRAYSIZE(Blend); ++b )
	{
		GetPipeline( PipelineKey(false, true, Blend[b][0], Blend[b][1], false, false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_CULLMODE_NONE, SDL_GPU_FILLMODE_FILL) );
		GetPipeline( PipelineKey(false, true, Blend[b][0], Blend[b][1], true, true, SDL_GPU_COMPAREOP_LESS_OR_EQUAL, SDL_GPU_CULLMODE_NONE, SDL_GPU_FILLMODE_FILL) );
		GetPipeline( PipelineKey(false, true, Blend[b][0], Blend[b][1], true, false, SDL_GPU_COMPAREOP_LESS_OR_EQUAL, SDL_GPU_CULLMODE_NONE, SDL_GPU_FILLMODE_FILL) );
		GetPipeline( PipelineKey(false, true, Blend[b][0], Blend[b][1], true, true, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_CULLMODE_NONE, SDL_GPU_FILLMODE_FILL) );
	}

	const char *szModes[] = { "vsync", "immediate", "mailbox" };
	LOG->Info( "SDL GPU present mode: %s%s", szModes[mode], g_bWaitFirst? ", waiting for the window before each frame": "" );
}

/* Make the device: Direct3D 12 on Windows, where it presents with less work
 * than Vulkan or GL in a window (DXGI's flip model, tearing when vsync is off),
 * and whatever SDL picks elsewhere (Metal on macOS, Vulkan otherwise).
 * SM_SDLGPU_DRIVER (vulkan, direct3d12, metal) asks for one. */
static SDL_GPUDevice *CreateDevice()
{
	const char *szDriver = SDL_getenv( "SM_SDLGPU_DRIVER" );
	const bool bAsked = szDriver != NULL && szDriver[0] != '\0';
#if defined(_WIN32)
	if( !bAsked )
		szDriver = "direct3d12";
#else
	if( !bAsked )
		szDriver = NULL;
#endif

	SDL_GPUDevice *pDevice = NULL;
	for( int iTry = 0; iTry < 2 && pDevice == NULL; ++iTry )
	{
		/* If the one we prefer isn't there, let SDL pick, unless it was asked for. */
		if( iTry == 1 )
		{
			if( szDriver == NULL || bAsked )
				break;
			LOG->Info( "SDL GPU: no %s device (%s); letting SDL choose", szDriver, SDL_GetError() );
			szDriver = NULL;
		}

		SDL_PropertiesID props = SDL_CreateProperties();
		SDL_SetBooleanProperty( props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true );
		SDL_SetBooleanProperty( props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true );
		SDL_SetBooleanProperty( props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true );
		SDL_SetBooleanProperty( props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, SDL_getenv("SM_SDLGPU_DEBUG") != NULL );
		/* We use no storage buffers or textures, so older Direct3D 12 hardware
		 * (resource binding tier 1) is enough. */
		SDL_SetBooleanProperty( props, "SDL.gpu.device.create.d3d12.allowtier1resourcebinding", true );	// SDL_PROP_GPU_DEVICE_CREATE_D3D12_ALLOW_FEWER_RESOURCE_SLOTS_BOOLEAN, SDL 3.4
		if( szDriver != NULL )
			SDL_SetStringProperty( props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, szDriver );
		pDevice = SDL_CreateGPUDeviceWithProperties( props );
		SDL_DestroyProperties( props );
	}
	return pDevice;
}

CString RageDisplay_SDLGPU::TryVideoMode( VideoModeParams p, bool &bNewDeviceOut )
{
	FlushPendingFrame();
	CString err = g_pWind->TryVideoMode( p, bNewDeviceOut );
	if( err != "" )
		return err;

	SDL_Window *pWindow = (SDL_Window *) g_pWind->GetSDLWindow();
	if( pWindow != g_pWindow || g_pDevice == NULL )
	{
		/* A new window.  (We don't ask for them; only the first is.) */
		ShutdownDevice();
		bNewDeviceOut = true;

		g_pDevice = CreateDevice();
		if( g_pDevice == NULL )
			return GPUError( "SDL_CreateGPUDevice" );
		if( !SDL_ClaimWindowForGPUDevice(g_pDevice, pWindow) )
		{
			err = GPUError( "SDL_ClaimWindowForGPUDevice" );
			SDL_DestroyGPUDevice( g_pDevice );
			g_pDevice = NULL;
			return err;
		}
		g_pWindow = pWindow;
		LOG->Info( "SDL GPU driver: %s", SDL_GetGPUDeviceDriver(g_pDevice) );

		/* Throws if the device can't do what we need, so the next renderer
		 * gets a chance. */
		g_SwapchainFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
		InitDevice();
		g_Frame.Clear();
		g_LastFrame.Clear();

		/* Our textures belonged to the old device. */
		if( TEXTUREMAN )
			TEXTUREMAN->InvalidateTextures();
	}

	SetSwapchain( p );
	this->SetDefaultRenderStates();
	ResolutionChanged();

	return "";
}

void RageDisplay_SDLGPU::ResolutionChanged()
{
	SetViewport( 0, 0 );

	/* Clear any junk that's in the framebuffer. */
	if( BeginFrame() )
		EndFrame();
}

void RageDisplay_SDLGPU::SetViewport( int shift_left, int shift_down )
{
	/* left and down are on a 0..SCREEN_WIDTH, 0..SCREEN_HEIGHT scale.
	 * Scale them to the actual viewport range, in framebuffer pixels. */
	int iWidth, iHeight;
	g_pWind->GetFramebufferSize( iWidth, iHeight );
	g_iViewportX = int( shift_left * float(iWidth) / SCREEN_WIDTH );
	g_iViewportY = int( shift_down * float(iHeight) / SCREEN_HEIGHT );
}

int RageDisplay_SDLGPU::GetMaxTextureSize() const
{
	/* Every device SDL's GPU API runs on has at least this. */
	return 8192;
}

/* Whether to let this frame go without drawing it, for SM_SDLGPU_PRESENT_HZ.
 * This is decided before the frame is drawn, so the screens don't spend time
 * drawing it either. */
static bool SkipFrame()
{
	if( g_iPresentInterval == 0 || DISPLAY->IsPacing() )
		return false;
	return SDL_GetPerformanceCounter() - g_iLastPresent < g_iPresentInterval;
}

bool RageDisplay_SDLGPU::BeginFrame()
{
	if( SkipFrame() )
	{
		++g_Stats.iSkipped;
		return false;
	}

	g_Frame.Clear();
	g_bInFrame = true;
	g_fColorScale[0] = g_fColorScale[1] = g_fColorScale[2] = 1;
	g_bDrawnThisFrame = false;
	g_bColorScaleInexact = false;
	g_Wanted.bClearDepthPending = true;
	g_iDepthSlice = -1;
	g_bTransformDirty = g_bLightDirty = g_bFragmentDirty = true;
	SetZWrite( true );
	return true;
}

static void DrawFrame( bool bPacing );

void RageDisplay_SDLGPU::EndFrame()
{
	const Uint64 iStart = SDL_GetPerformanceCounter();
	if( g_iLastEndFrame != 0 )
		g_Stats.iGame += iStart - g_iLastEndFrame;

	g_bInFrame = false;
	g_fColorScale[0] = g_fColorScale[1] = g_fColorScale[2] = 1;
	g_Stats.iCommands += g_Frame.vCommands.size();
	g_Stats.iVertices += g_Frame.vVertices.size();

	FrameSwapping();
	DrawFrame( IsPacing() );
	FrameSwapped();

	/* Keep it for screenshots; the next frame reuses the other one's memory. */
	g_Frame.Swap( g_LastFrame );
	g_Frame.Clear();
	ReleaseDeleted();

	LogStats();
	ProcessStatsOnFlip();
	g_iLastEndFrame = SDL_GetPerformanceCounter();
}

/* Get the next frame's command buffer and the window's buffer to draw it into,
 * waiting for the GPU to finish the frame from g_iFramesInFlight ago and for the
 * window to give a buffer back. */
static void AcquireFrame()
{
	SDL_GPUCommandBuffer *pCmd = SDL_AcquireGPUCommandBuffer( g_pDevice );
	if( pCmd == NULL )
		RageException::Throw( "%s", GPUError("SDL_AcquireGPUCommandBuffer").c_str() );

	/* The swapchain would wait for the GPU anyway; doing it here first tells us
	 * how much of the wait is the GPU and how much is the window. */
	Uint64 iTime = SDL_GetPerformanceCounter();
	SDL_GPUFence *&pOldFence = g_pFrameFence[g_iFenceSlot];
	if( pOldFence != NULL )
	{
		SDL_WaitForGPUFences( g_pDevice, true, &pOldFence, 1 );
		SDL_ReleaseGPUFence( g_pDevice, pOldFence );
		pOldFence = NULL;
	}
	Uint64 iNow = SDL_GetPerformanceCounter();
	g_Stats.iGPUWait += iNow - iTime;
	iTime = iNow;

	SDL_GPUTexture *pTarget = NULL;
	Uint32 iWidth = 0, iHeight = 0;
	if( !SDL_WaitAndAcquireGPUSwapchainTexture(pCmd, g_pWindow, &pTarget, &iWidth, &iHeight) )
		LOG->Warn( "%s", GPUError("SDL_WaitAndAcquireGPUSwapchainTexture").c_str() );
	iNow = SDL_GetPerformanceCounter();
	g_Stats.iAcquire += iNow - iTime;
	g_Stats.iAcquireMax = max( g_Stats.iAcquireMax, iNow - iTime );

	g_pPendingCmd = pCmd;
	g_pPendingTarget = pTarget;
	g_iPendingWidth = iWidth;
	g_iPendingHeight = iHeight;
}

/* Send a frame we have a buffer for but didn't draw, cleared, to give the
 * command buffer and the window's buffer back; before the window or the
 * device changes. */
static void FlushPendingFrame()
{
	if( g_pPendingCmd == NULL )
		return;
	if( g_pPendingTarget != NULL )
	{
		SDL_GPUColorTargetInfo info;
		SDL_zero( info );
		info.texture = g_pPendingTarget;
		info.clear_color.a = 1;
		info.load_op = SDL_GPU_LOADOP_CLEAR;
		info.store_op = SDL_GPU_STOREOP_STORE;
		SDL_EndGPURenderPass( SDL_BeginGPURenderPass(g_pPendingCmd, &info, 1, NULL) );
	}
	SDL_SubmitGPUCommandBuffer( g_pPendingCmd );
	g_pPendingCmd = NULL;
	g_pPendingTarget = NULL;
}

void RageDisplay_SDLGPU::WaitForFrame()
{
	if( !g_bWaitFirst || g_pPendingCmd != NULL || g_pDevice == NULL || IsPacing() )
		return;
	AcquireFrame();
}

/* Send g_Frame to the GPU and show it. */
static void DrawFrame( bool bPacing )
{
	if( g_pPendingCmd == NULL )
	{
		AcquireFrame();
		FrameStats::Mark( FrameStats::PHASE_PRESENT );
	}
	SDL_GPUCommandBuffer *pCmd = g_pPendingCmd;
	SDL_GPUTexture *pTarget = g_pPendingTarget;
	const Uint32 iWidth = g_iPendingWidth, iHeight = g_iPendingHeight;
	g_pPendingCmd = NULL;
	g_pPendingTarget = NULL;

	Uint64 iTime = SDL_GetPerformanceCounter();
	UploadFrame( pCmd, g_Frame );

	/* No texture means the window can't be drawn to now (it's minimized). */
	if( pTarget != NULL )
	{
		PlayFrame( pCmd, g_Frame, pTarget, iWidth, iHeight );
		g_iLastWidth = iWidth;
		g_iLastHeight = iHeight;
	}
	Uint64 iNow = SDL_GetPerformanceCounter();
	g_Stats.iRecord += iNow - iTime;
	iTime = iNow;
	FrameStats::Mark( FrameStats::PHASE_RENDER );

	/* Submitting presents the frame, too. */
	SDL_GPUFence *pFence = SDL_SubmitGPUCommandBufferAndAcquireFence( pCmd );
	iNow = SDL_GetPerformanceCounter();
	g_Stats.iSubmit += iNow - iTime;
	g_iLastPresent = iNow;
	FrameStats::Mark( FrameStats::PHASE_PRESENT );

	/* The frame isn't done until the GPU is, and we need to know how long that
	 * takes to pace frames.  There's nothing else for the CPU to do meanwhile. */
	if( pFence != NULL && bPacing )
	{
		SDL_WaitForGPUFences( g_pDevice, true, &pFence, 1 );
		SDL_ReleaseGPUFence( g_pDevice, pFence );
		pFence = NULL;
	}
	g_pFrameFence[g_iFenceSlot] = pFence;
	g_iFenceSlot = (g_iFenceSlot + 1) % g_iFramesInFlight;
}

RageSurface* RageDisplay_SDLGPU::CreateScreenshot()
{
	/* Draw the last frame again, into a texture we can read. */
	const Uint32 iWidth = max( g_iLastWidth, 1u ), iHeight = max( g_iLastHeight, 1u );
	SDL_GPUTexture *pTarget = CreateGPUTexture( iWidth, iHeight, 1, g_SwapchainFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER );
	if( pTarget == NULL )
		RageException::Throw( "%s", GPUError("Creating the screenshot texture").c_str() );

	SDL_GPUTransferBufferCreateInfo info;
	SDL_zero( info );
	info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	info.size = iWidth*iHeight*4;
	SDL_GPUTransferBuffer *pTransfer = SDL_CreateGPUTransferBuffer( g_pDevice, &info );
	if( pTransfer == NULL )
		RageException::Throw( "%s", GPUError("SDL_CreateGPUTransferBuffer").c_str() );

	SDL_GPUCommandBuffer *pCmd = SDL_AcquireGPUCommandBuffer( g_pDevice );
	PlayFrame( pCmd, g_LastFrame, pTarget, iWidth, iHeight );

	SDL_GPUCopyPass *pPass = SDL_BeginGPUCopyPass( pCmd );
	SDL_GPUTextureRegion src;
	SDL_zero( src );
	src.texture = pTarget;
	src.w = iWidth;
	src.h = iHeight;
	src.d = 1;
	SDL_GPUTextureTransferInfo dst;
	SDL_zero( dst );
	dst.transfer_buffer = pTransfer;
	dst.pixels_per_row = iWidth;
	dst.rows_per_layer = iHeight;
	SDL_DownloadFromGPUTexture( pPass, &src, &dst );
	SDL_EndGPUCopyPass( pPass );

	SDL_GPUFence *pFence = SDL_SubmitGPUCommandBufferAndAcquireFence( pCmd );
	if( pFence )
	{
		SDL_WaitForGPUFences( g_pDevice, true, &pFence, 1 );
		SDL_ReleaseGPUFence( g_pDevice, pFence );
	}

	const PixelFormatDesc &desc = PIXEL_FORMAT_DESC[FMT_RGBA8];
	RageSurface *image = CreateSurface( iWidth, iHeight, desc.bpp,
		desc.masks[0], desc.masks[1], desc.masks[2], 0 );

	const Uint8 *pMapped = (const Uint8 *) SDL_MapGPUTransferBuffer( g_pDevice, pTransfer, false );
	if( pMapped )
	{
		const bool bBGRA = g_SwapchainFormat == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
			g_SwapchainFormat == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
		for( Uint32 y = 0; y < iHeight; ++y )
		{
			const Uint8 *pIn = pMapped + y*iWidth*4;
			Uint8 *pOut = (Uint8 *) image->pixels + y*image->pitch;
			for( Uint32 x = 0; x < iWidth; ++x, pIn += 4, pOut += 4 )
			{
				pOut[0] = bBGRA? pIn[2]: pIn[0];
				pOut[1] = pIn[1];
				pOut[2] = bBGRA? pIn[0]: pIn[2];
				pOut[3] = 255;
			}
		}
		SDL_UnmapGPUTransferBuffer( g_pDevice, pTransfer );
	}

	SDL_ReleaseGPUTransferBuffer( g_pDevice, pTransfer );
	SDL_ReleaseGPUTexture( g_pDevice, pTarget );
	return image;
}

RageDisplay::VideoModeParams RageDisplay_SDLGPU::GetVideoModeParams() const { return g_pWind->GetVideoModeParams(); }
float RageDisplay_SDLGPU::GetRefreshRate() const { return g_pWind->GetRefreshRate(); }
void RageDisplay_SDLGPU::HideWindow() { g_pWind->HideWindow(); }

const RageDisplay::PixelFormatDesc *RageDisplay_SDLGPU::GetPixelFormatDesc( PixelFormat pf ) const
{
	ASSERT( pf < NUM_PIX_FORMATS );
	return &PIXEL_FORMAT_DESC[pf];
}

RageMatrix RageDisplay_SDLGPU::GetOrthoMatrix( float l, float r, float b, float t, float zn, float zf )
{
	RageMatrix m(
		2/(r-l),      0,            0,           0,
		0,            2/(t-b),      0,           0,
		0,            0,            -2/(zf-zn),   0,
		-(r+l)/(r-l), -(t+b)/(t-b), -(zf+zn)/(zf-zn),  1 );
	return m;
}

/*
 * State
 */
void RageDisplay_SDLGPU::SetBlendMode( BlendMode mode )
{
	WantedState &w = g_Wanted;
	w.bBlend = true;

	/* BLEND_NO_EFFECT is almost exclusively used to draw masks to the Z-buffer.
	 * Make sure masks always win the depth test when drawn at the same position. */
	w.iDepthRange = mode == BLEND_NO_EFFECT? 1:0;

	switch( mode )
	{
	case BLEND_NORMAL:		w.BlendSrc = SDL_GPU_BLENDFACTOR_SRC_ALPHA; w.BlendDst = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; break;
	case BLEND_ADD:			w.BlendSrc = SDL_GPU_BLENDFACTOR_SRC_ALPHA; w.BlendDst = SDL_GPU_BLENDFACTOR_ONE; break;
	case BLEND_NO_EFFECT:	w.BlendSrc = SDL_GPU_BLENDFACTOR_ZERO; w.BlendDst = SDL_GPU_BLENDFACTOR_ONE; break;
	/* Expects white premultiplied by alpha: fades from the destination
	 * to its inverse as alpha goes 0 -> 1. */
	case BLEND_INVERT:		w.BlendSrc = SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR; w.BlendDst = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; break;
	default:	ASSERT(0);
	}
}

void RageDisplay_SDLGPU::SetBlending( bool b )
{
	g_Wanted.bBlend = b;
}

bool RageDisplay_SDLGPU::SetColorScale( float r, float g, float b )
{
	const bool bScaled = g_fColorScale[0] != 1 || g_fColorScale[1] != 1 || g_fColorScale[2] != 1;
	const bool bScale = r != 1 || g != 1 || b != 1;
	if( bScale && g_bDrawnThisFrame )
		return false;

	bool bRet = true;
	if( bScaled && !bScale && g_bColorScaleInexact )
	{
		/* The frame was empty when the scale was set, so everything in it was
		 * drawn with the scale: forget it, and the caller draws it again. */
		g_Frame.vVertices.clear();
		g_Frame.vCommands.clear();
		g_Wanted.bClearDepthPending = true;
		g_iDepthSlice = -1;
		g_bDrawnThisFrame = false;
		bRet = false;
	}
	g_fColorScale[0] = r;
	g_fColorScale[1] = g;
	g_fColorScale[2] = b;
	g_bColorScaleInexact = false;
	return bRet;
}

bool RageDisplay_SDLGPU::IsZWriteEnabled() const
{
	return g_Wanted.bZWrite;
}

bool RageDisplay_SDLGPU::IsZTestEnabled() const
{
	return g_Wanted.DepthFunc != SDL_GPU_COMPAREOP_ALWAYS;
}

void RageDisplay_SDLGPU::ClearZBuffer()
{
	/* Cleared when it's next used. */
	g_Wanted.bClearDepthPending = true;
}

void RageDisplay_SDLGPU::SetZWrite( bool b )
{
	g_Wanted.bZWrite = b;
}

void RageDisplay_SDLGPU::SetZTestMode( ZTestMode mode )
{
	WantedState &w = g_Wanted;
	w.bZTest = mode != ZTEST_OFF;
	switch( mode )
	{
	case ZTEST_OFF:				w.DepthFunc = SDL_GPU_COMPAREOP_ALWAYS;			break;
	case ZTEST_WRITE_ON_PASS:	w.DepthFunc = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;	break;
	case ZTEST_WRITE_ON_FAIL:	w.DepthFunc = SDL_GPU_COMPAREOP_GREATER;		break;
	default:	ASSERT( 0 );
	}
}

void RageDisplay_SDLGPU::SetCullMode( CullMode mode )
{
	switch( mode )
	{
	case CULL_BACK:		g_Wanted.CullMode = SDL_GPU_CULLMODE_BACK;	break;
	case CULL_FRONT:	g_Wanted.CullMode = SDL_GPU_CULLMODE_FRONT;	break;
	case CULL_NONE:		g_Wanted.CullMode = SDL_GPU_CULLMODE_NONE;	break;
	default:	ASSERT(0);
	}
}

void RageDisplay_SDLGPU::SetAlphaTest( bool b )
{
	g_Wanted.bAlphaTest = b;
}

void RageDisplay_SDLGPU::SetPolygonMode( PolygonMode pm )
{
	switch( pm )
	{
	case POLYGON_FILL:	g_Wanted.FillMode = SDL_GPU_FILLMODE_FILL; break;
	case POLYGON_LINE:	g_Wanted.FillMode = SDL_GPU_FILLMODE_LINE; break;
	default:	ASSERT(0);
	}
}

void RageDisplay_SDLGPU::SetLineWidth( float fWidth )
{
	/* Lines are one pixel wide, as in core OpenGL on macOS. */
}

void RageDisplay_SDLGPU::ClearAllTextures()
{
	for( int i = 0; i < MAX_TEXTURE_UNITS; ++i )
		g_Wanted.Unit[i].bEnabled = false;
	g_Wanted.iActiveUnit = 0;
}

void RageDisplay_SDLGPU::SetTexture( int iTextureUnitIndex, RageTexture* pTexture )
{
	ASSERT( iTextureUnitIndex >= 0 && iTextureUnitIndex < MAX_TEXTURE_UNITS );
	g_Wanted.iActiveUnit = iTextureUnitIndex;
	WantedState::TexUnit &unit = g_Wanted.Unit[iTextureUnitIndex];
	unit.bEnabled = pTexture != NULL;
	if( pTexture )
		unit.uTexture = pTexture->GetTexHandle();
}

void RageDisplay_SDLGPU::SetTextureModeModulate()
{
	g_Wanted.Unit[g_Wanted.iActiveUnit].iMode = TEXMODE_MODULATE;
}

void RageDisplay_SDLGPU::SetTextureModeGlow( GlowMode m )
{
	switch( m )
	{
	case GLOW_BRIGHTEN:
		g_Wanted.BlendSrc = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
		g_Wanted.BlendDst = SDL_GPU_BLENDFACTOR_ONE;
		return;
	case GLOW_WHITEN:
		g_Wanted.Unit[g_Wanted.iActiveUnit].iMode = TEXMODE_GLOW;
		return;
	}
}

void RageDisplay_SDLGPU::SetTextureModeAdd()
{
	g_Wanted.Unit[g_Wanted.iActiveUnit].iMode = TEXMODE_ADD;
}

void RageDisplay_SDLGPU::SetTextureFiltering( bool b )
{
}

void RageDisplay_SDLGPU::SetTextureWrapping( bool b )
{
	/* This is the wrap mode of the texture on the active unit. */
	const unsigned uTex = g_Wanted.Unit[g_Wanted.iActiveUnit].uTexture;
	if( uTex && uTex < g_Textures.size() )
		g_Textures[uTex].bWrap = b;
}

void RageDisplay_SDLGPU::SetSphereEnironmentMapping( bool b )
{
	g_Wanted.Unit[g_Wanted.iActiveUnit].bSphere = b;
}

void RageDisplay_SDLGPU::SetMaterial(
	const RageColor &emissive,
	const RageColor &ambient,
	const RageColor &diffuse,
	const RageColor &specular,
	float shininess
	)
{
	WantedState &w = g_Wanted;

	/* Without lighting, models still get their basic color and transparency, as
	 * the vertex color. */
	if( w.bLighting )
	{
		w.MatEmissive = emissive;
		w.MatAmbient = ambient;
		w.MatDiffuse = diffuse;
		w.MatSpecular = specular;
		w.fMatShininess = shininess;
	}
	else
	{
		w.ConstColor = RageColor(
			emissive.r + ambient.r + diffuse.r,
			emissive.g + ambient.g + diffuse.g,
			emissive.b + ambient.b + diffuse.b,
			emissive.a + ambient.a + diffuse.a );
	}
}

void RageDisplay_SDLGPU::SetLighting( bool b )
{
	g_Wanted.bLighting = b;
}

void RageDisplay_SDLGPU::SetLightOff( int index )
{
	if( index == 0 )
		g_Wanted.bLight0 = false;
}

void RageDisplay_SDLGPU::SetLightDirectional(
	int index,
	const RageColor &ambient,
	const RageColor &diffuse,
	const RageColor &specular,
	const RageVector3 &dir )
{
	if( index != 0 )
		return;

	WantedState &w = g_Wanted;
	w.bLight0 = true;
	w.LightAmbient = ambient;
	w.LightDiffuse = diffuse;
	w.LightSpecular = specular;

	/* GL moved the light by the modelview matrix it had when this was called,
	 * which is the last draw's.  Keep doing that, so lit actors look the same. */
	RageMatrix mv;
	RageMatrixMultiply( &mv, &g_LastCenteringView, &g_LastWorld );
	w.LightDir = RageVector3(
		dir.x*mv.m[0][0] + dir.y*mv.m[1][0] + dir.z*mv.m[2][0],
		dir.x*mv.m[0][1] + dir.y*mv.m[1][1] + dir.z*mv.m[2][1],
		dir.x*mv.m[0][2] + dir.y*mv.m[1][2] + dir.z*mv.m[2][2] );
}

/*
 * Drawing
 */

/* GL's normal matrix: the inverse transpose of the modelview's upper 3x3, which
 * is the cofactor matrix over the determinant.  Column major, in the columns
 * of a mat4, the way std140 lays out a mat3. */
static void GetNormalMatrix( const RageMatrix &mv, float out[16] )
{
	/* a[r][c] is GL's row r, column c of the modelview. */
	float a[3][3];
	for( int r = 0; r < 3; ++r )
		for( int c = 0; c < 3; ++c )
			a[r][c] = mv.m[c][r];

	float cof[3][3];
	cof[0][0] = a[1][1]*a[2][2] - a[1][2]*a[2][1];
	cof[0][1] = a[1][2]*a[2][0] - a[1][0]*a[2][2];
	cof[0][2] = a[1][0]*a[2][1] - a[1][1]*a[2][0];
	cof[1][0] = a[0][2]*a[2][1] - a[0][1]*a[2][2];
	cof[1][1] = a[0][0]*a[2][2] - a[0][2]*a[2][0];
	cof[1][2] = a[0][1]*a[2][0] - a[0][0]*a[2][1];
	cof[2][0] = a[0][1]*a[1][2] - a[0][2]*a[1][1];
	cof[2][1] = a[0][2]*a[1][0] - a[0][0]*a[1][2];
	cof[2][2] = a[0][0]*a[1][1] - a[0][1]*a[1][0];

	const float fDet = a[0][0]*cof[0][0] + a[0][1]*cof[0][1] + a[0][2]*cof[0][2];
	const float fInv = fDet != 0? 1/fDet: 0;
	memset( out, 0, 16*sizeof(float) );
	for( int r = 0; r < 3; ++r )
		for( int c = 0; c < 3; ++c )
			out[c*4+r] = cof[r][c] * fInv;
}

static const float g_fIdentity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

/* Set part of a uniform block, noting whether it changed. */
template<class T>
static inline void SetBlockField( T *pDest, const T *pSrc, int iCount, bool &bDirty )
{
	if( !memcmp(pDest, pSrc, iCount*sizeof(T)) )
		return;
	memcpy( pDest, pSrc, iCount*sizeof(T) );
	bDirty = true;
}

static inline void SetBlockInt( Sint32 &iDest, int iSrc, bool &bDirty )
{
	if( iDest == iSrc )
		return;
	iDest = iSrc;
	bDirty = true;
}

/* The product of the centering and view matrices, for generation iGen. */
static const RageMatrix &GetCenteringView( unsigned iGen, const RageMatrix *pCentering, const RageMatrix *pView )
{
	static bool bValid = false;
	static unsigned iValidGen;
	static RageMatrix CenteringView;
	if( !bValid || iValidGen != iGen )
	{
		RageMatrixMultiply( &CenteringView, pCentering, pView );
		iValidGen = iGen;
		bValid = true;
	}
	return CenteringView;
}

bool RageDisplay_SDLGPU::PrepareDraw( bool bForceGPU )
{
	WantedState &w = g_Wanted;

	/* GL loaded the texture matrix into the active unit at each draw; the other
	 * unit kept what it had. */
	{
		WantedState::TexUnit &active = w.Unit[w.iActiveUnit];
		const unsigned iTexGen = GetTextureMatrixGeneration();
		if( active.iTexMatrixGen != iTexGen )
		{
			active.TexMatrix = *GetTextureTop();
			active.iTexMatrixGen = iTexGen;
		}
	}

	/* A unit only textures if it's on and has a texture that exists. */
	SDL_GPUTexture *pTex[2] = { NULL, NULL };
	bool bWrap[2] = { false, false };
	for( int i = 0; i < 2; ++i )
	{
		const unsigned uTex = w.Unit[i].uTexture;
		if( w.Unit[i].bEnabled && uTex && uTex < g_Textures.size() && g_Textures[uTex].pTex )
		{
			pTex[i] = g_Textures[uTex].pTex;
			bWrap[i] = g_Textures[uTex].bWrap;
		}
	}
	const int iMode0 = pTex[0]? w.Unit[0].iMode: TEXMODE_OFF;
	const int iMode1 = pTex[1]? w.Unit[1].iMode: TEXMODE_OFF;
	const bool bSphere0 = iMode0 != TEXMODE_OFF && w.Unit[0].bSphere;
	const bool bSphere1 = iMode1 != TEXMODE_OFF && w.Unit[1].bSphere;

	/* Vertices on the CPU leave out the last row's worth of coordinates, which
	 * is only right if the matrices don't do anything projective. */
	const RageMatrix *pWorld = GetWorldTop();
	const RageMatrix &TexMatrix = w.Unit[0].TexMatrix;
	const bool bAffine = pWorld->m[0][3] == 0 && pWorld->m[1][3] == 0 && pWorld->m[2][3] == 0 && pWorld->m[3][3] == 1 &&
		(iMode0 == TEXMODE_OFF || (TexMatrix.m[0][3] == 0 && TexMatrix.m[1][3] == 0 && TexMatrix.m[3][3] == 1));
	const bool bOnCPU = !bForceGPU && !w.bLighting && !bSphere0 && !bSphere1 && bAffine;

	/* Depth.  Most frames never touch the depth buffer; it's only "cleared"
	 * when it's about to be used. */
	const bool bDepth = w.bZWrite || w.bZTest;
	if( bDepth && w.bClearDepthPending )
	{
		NextDepthSlice();
		w.bClearDepthPending = false;
	}

	Command &cmd = g_Draw.Cmd;
	cmd.pPipeline = GetPipeline( PipelineKey(g_Draw.bModel, w.bBlend, w.BlendSrc, w.BlendDst,
		bDepth, bDepth && w.bZWrite, w.DepthFunc, w.CullMode, w.FillMode) );
	for( int i = 0; i < 2; ++i )
	{
		cmd.pTexture[i] = pTex[i];
		cmd.pSampler[i] = g_pSampler[bWrap[i]? 1:0];
	}
	GetDepthRange( w.iDepthRange, cmd.fMinDepth, cmd.fMaxDepth );
	cmd.iViewportX = g_iViewportX;
	cmd.iViewportY = g_iViewportY;

	/* Matrices. */
	TransformBlock &t = g_Transform;
	bool &bTDirty = g_bTransformDirty;
	const unsigned iGen = GetProjectionViewGeneration();
	static unsigned iLastProjectionGen = unsigned(-1);
	if( iLastProjectionGen != iGen )
	{
		SetBlockField( t.Projection, (const float *) GetProjectionTop(), 16, bTDirty );
		iLastProjectionGen = iGen;
	}
	const RageMatrix &CenteringView = GetCenteringView( iGen, GetCentering(), GetViewTop() );
	if( bOnCPU )
	{
		SetBlockField( t.ModelView, (const float *) &CenteringView, 16, bTDirty );
		SetBlockField( t.TexMatrix0, g_fIdentity, 16, bTDirty );
	}
	else
	{
		RageMatrix ModelView;
		RageMatrixMultiply( &ModelView, &CenteringView, pWorld );
		SetBlockField( t.ModelView, (const float *) &ModelView, 16, bTDirty );
		if( w.bLighting || bSphere0 || bSphere1 )
		{
			float fNormal[16];
			GetNormalMatrix( ModelView, fNormal );
			SetBlockField( t.NormalMatrix, fNormal, 16, bTDirty );
		}
		SetBlockField( t.TexMatrix0, (const float *) &w.Unit[0].TexMatrix, 16, bTDirty );
	}
	g_LastWorld = *pWorld;
	g_LastCenteringView = CenteringView;

	if( iMode1 != TEXMODE_OFF )
		SetBlockField( t.TexMatrix1, (const float *) &w.Unit[1].TexMatrix, 16, bTDirty );
	SetBlockInt( t.Flags[0], bSphere0, bTDirty );
	SetBlockInt( t.Flags[1], bSphere1, bTDirty );
	SetBlockInt( t.Flags[2], w.bLighting, bTDirty );
	SetBlockInt( t.Flags[3], w.bLighting && w.bLight0, bTDirty );
	if( g_Draw.bModel )
	{
		SetBlockField( t.ConstColor, (const float *) &w.ConstColor, 4, bTDirty );
		SetBlockInt( t.ConstMode[0], iMode0, bTDirty );
	}

	FragmentBlock &f = g_Fragment;
	const float fScale[4] = { g_fColorScale[0], g_fColorScale[1], g_fColorScale[2], 1 };
	SetBlockField( f.ColorScale, fScale, 4, g_bFragmentDirty );
	SetBlockInt( f.Flags[0], iMode1, g_bFragmentDirty );
	SetBlockInt( f.Flags[1], w.bAlphaTest, g_bFragmentDirty );
	g_bDrawnThisFrame = true;

	/* Scaling before the framebuffer adds or inverts isn't the same as after:
	 * it clamps at 1 instead of at the scale. */
	if( g_fColorScale[0] != 1 || g_fColorScale[1] != 1 || g_fColorScale[2] != 1 )
	{
		if( w.bBlend && ((w.BlendDst == SDL_GPU_BLENDFACTOR_ONE && w.BlendSrc != SDL_GPU_BLENDFACTOR_ZERO) ||
			w.BlendSrc == SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR) )
			g_bColorScaleInexact = true;
	}

	if( w.bLighting )
	{
		LightBlock &l = g_Light;
		SetBlockField( l.MatEmissive, (const float *) &w.MatEmissive, 4, g_bLightDirty );
		SetBlockField( l.MatAmbient, (const float *) &w.MatAmbient, 4, g_bLightDirty );
		SetBlockField( l.MatDiffuse, (const float *) &w.MatDiffuse, 4, g_bLightDirty );
		SetBlockField( l.MatSpecular, (const float *) &w.MatSpecular, 4, g_bLightDirty );
		if( w.bLight0 )
		{
			SetBlockField( l.LightAmbient, (const float *) &w.LightAmbient, 4, g_bLightDirty );
			SetBlockField( l.LightDiffuse, (const float *) &w.LightDiffuse, 4, g_bLightDirty );
			SetBlockField( l.LightSpecular, (const float *) &w.LightSpecular, 4, g_bLightDirty );
		}
		const float fDir[4] = { w.LightDir.x, w.LightDir.y, w.LightDir.z, w.fMatShininess };
		SetBlockField( l.LightDir, fDir, 4, g_bLightDirty );
	}

	RecordBlocks();

	g_Draw.bOnCPU = bOnCPU;
	g_Draw.pWorld = pWorld;
	g_Draw.pTexMatrix0 = &w.Unit[0].TexMatrix;
	g_Draw.iMode0 = Uint8( iMode0 );
	return bOnCPU;
}

/* A float as a half float, which is plenty for a normal.  Anything that small
 * rounds to zero, and anything that big to the largest number there is. */
static inline Uint16 FloatToHalf( float f )
{
	union { float f; Uint32 u; } v;
	v.f = f;
	const Uint32 iSign = (v.u >> 16) & 0x8000;
	const int iExp = int((v.u >> 23) & 0xFF) - 127 + 15;
	if( iExp <= 0 )
		return Uint16( iSign );
	if( iExp >= 31 )
		return Uint16( iSign | 0x7BFF );
	Uint32 h = iSign | (iExp << 10) | ((v.u & 0x7FFFFF) >> 13);
	if( v.u & 0x1000 )
		++h;
	return Uint16( h );
}

/* What fills the vertices of a draw: its matrices and modes, copied out of
 * g_Draw so they stay in registers instead of being read back after every
 * vertex that's written. */
struct VertexFiller
{
	VertexFiller()
	{
		bOnCPU = g_Draw.bOnCPU;
		bTexMatrix = false;
		if( bOnCPU )
		{
			world = *g_Draw.pWorld;
			tex = *g_Draw.pTexMatrix0;
			bTexMatrix = memcmp( &tex, g_fIdentity, sizeof(tex) ) != 0;
		}
		mode = g_Draw.iMode0;
	}

	void Fill( Vertex &out, const RageSpriteVertex &in ) const
	{
		const float x = in.p.x, y = in.p.y, z = in.p.z;
		const float s = in.t.x, t = in.t.y;
		if( bOnCPU )
		{
			out.p[0] = x*world.m[0][0] + y*world.m[1][0] + z*world.m[2][0] + world.m[3][0];
			out.p[1] = x*world.m[0][1] + y*world.m[1][1] + z*world.m[2][1] + world.m[3][1];
			out.p[2] = x*world.m[0][2] + y*world.m[1][2] + z*world.m[2][2] + world.m[3][2];

			/* Most texture matrices don't do anything. */
			if( bTexMatrix )
			{
				out.t[0] = s*tex.m[0][0] + t*tex.m[1][0] + tex.m[3][0];
				out.t[1] = s*tex.m[0][1] + t*tex.m[1][1] + tex.m[3][1];
			}
			else
			{
				out.t[0] = s;
				out.t[1] = t;
			}

			out.n[0] = out.n[1] = out.n[2] = 0;
		}
		else
		{
			out.p[0] = x; out.p[1] = y; out.p[2] = z;
			out.t[0] = s; out.t[1] = t;
			out.n[0] = FloatToHalf( in.n.x );
			out.n[1] = FloatToHalf( in.n.y );
			out.n[2] = FloatToHalf( in.n.z );
		}

		/* RageVColor is BGRA. */
		out.c[0] = in.c.r;
		out.c[1] = in.c.g;
		out.c[2] = in.c.b;
		out.c[3] = in.c.a;

		out.mode[0] = mode;
		out.mode[1] = 0;
	}

	bool bOnCPU, bTexMatrix;
	RageMatrix world, tex;
	Uint8 mode;
};

void RageDisplay_SDLGPU::DrawQuadsInternal( const RageSpriteVertex v[], int iNumVerts )
{
	PrepareDraw();
	const VertexFiller fill;
	iNumVerts &= ~3;
	for( int iStart = 0; iStart < iNumVerts; iStart += MAX_QUAD_VERTICES )
	{
		const int iCount = min( iNumVerts - iStart, int(MAX_QUAD_VERTICES) );
		Vertex *out = RecordVertices( CMD_QUADS, iCount );
		for( int i = 0; i < iCount; ++i )
			fill.Fill( out[i], v[iStart+i] );
	}
}

void RageDisplay_SDLGPU::DrawQuadStripInternal( const RageSpriteVertex v[], int iNumVerts )
{
	/* Quad k of a strip is vertices 2k, 2k+1, 2k+3, 2k+2.  Every pair is in
	 * two quads, so it's only moved once. */
	PrepareDraw();
	if( iNumVerts < 4 )
		return;
	const VertexFiller fill;
	Vertex a, b;
	fill.Fill( a, v[0] );
	fill.Fill( b, v[1] );
	int iQuads = (iNumVerts - 2) / 2;
	int i = 2;
	while( iQuads > 0 )
	{
		const int iBatch = min( iQuads, int(MAX_QUAD_VERTICES/4) );
		Vertex *out = RecordVertices( CMD_QUADS, iBatch*4 );
		for( int q = 0; q < iBatch; ++q, i += 2, out += 4 )
		{
			Vertex c, d;
			fill.Fill( c, v[i] );
			fill.Fill( d, v[i+1] );
			out[0] = a;
			out[1] = b;
			out[2] = d;
			out[3] = c;
			a = c;
			b = d;
		}
		iQuads -= iBatch;
	}
}

void RageDisplay_SDLGPU::DrawFanInternal( const RageSpriteVertex v[], int iNumVerts )
{
	PrepareDraw();
	if( iNumVerts < 3 )
		return;
	const VertexFiller fill;
	Vertex first, prev;
	fill.Fill( first, v[0] );
	fill.Fill( prev, v[1] );
	Vertex *out = RecordVertices( CMD_TRIANGLES, (iNumVerts-2)*3 );
	for( int i = 2; i < iNumVerts; ++i, out += 3 )
	{
		Vertex cur;
		fill.Fill( cur, v[i] );
		out[0] = first;
		out[1] = prev;
		out[2] = cur;
		prev = cur;
	}
}

void RageDisplay_SDLGPU::DrawStripInternal( const RageSpriteVertex v[], int iNumVerts )
{
	/* Triangle i of a strip is vertices i, i+1, i+2, with every other one
	 * flipped to keep the winding. */
	PrepareDraw();
	if( iNumVerts < 3 )
		return;
	const VertexFiller fill;
	Vertex a, b;
	fill.Fill( a, v[0] );
	fill.Fill( b, v[1] );
	Vertex *out = RecordVertices( CMD_TRIANGLES, (iNumVerts-2)*3 );
	for( int i = 0; i+2 < iNumVerts; ++i, out += 3 )
	{
		Vertex c;
		fill.Fill( c, v[i+2] );
		const bool bOdd = (i & 1) != 0;
		out[0] = bOdd? b: a;
		out[1] = bOdd? a: b;
		out[2] = c;
		a = b;
		b = c;
	}
}

void RageDisplay_SDLGPU::DrawTrianglesInternal( const RageSpriteVertex v[], int iNumVerts )
{
	PrepareDraw();
	const VertexFiller fill;
	iNumVerts -= iNumVerts % 3;
	if( iNumVerts == 0 )
		return;
	Vertex *out = RecordVertices( CMD_TRIANGLES, iNumVerts );
	for( int i = 0; i < iNumVerts; ++i )
		fill.Fill( out[i], v[i] );
}

void RageDisplay_SDLGPU::DrawCompiledGeometryInternal( const RageCompiledGeometry *p, int iMeshIndex )
{
	/* Models keep their vertices on the GPU, so the world matrix goes there. */
	g_Draw.bModel = true;
	PrepareDraw( true );
	g_Draw.bModel = false;
	((const RageCompiledGeometrySDLGPU *) p)->Record( iMeshIndex );
}

RageCompiledGeometry* RageDisplay_SDLGPU::CreateCompiledGeometry()
{
	return new RageCompiledGeometrySDLGPU;
}

void RageDisplay_SDLGPU::DeleteCompiledGeometry( RageCompiledGeometry* p )
{
	/* A recorded frame may still draw it. */
	g_DeletedGeometry[0].push_back( p );
}

/*
 * Textures
 */
bool RageDisplay_SDLGPU::SupportsTextureFormat( PixelFormat pixfmt, bool realtime )
{
	/* Everything is made RGBA8 on the way, and paletted images could be too,
	 * but RageDisplay_GLCore can't take them, so the game makes them 16 or 32
	 * bit textures first, as the texture color depth says.  Do the same, so
	 * they look the same. */
	return pixfmt != FMT_PAL;
}

unsigned RageDisplay_SDLGPU::CreateTexture(
	PixelFormat pixfmt,
	RageSurface* img,
	bool bGenerateMipMaps )
{
	ASSERT( img->w == power_of_two(img->w) && img->h == power_of_two(img->h) );

	int iLevels = 1;
	if( bGenerateMipMaps )
		for( int s = max(img->w, img->h); s > 1; s >>= 1 )
			++iLevels;

	SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	if( iLevels > 1 )
		usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;	// for making the mip maps
	SDL_GPUTexture *pTex = CreateGPUTexture( img->w, img->h, iLevels, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, usage );
	if( pTex == NULL )
	{
		LOG->Warn( "%s (%ix%i)", GPUError("SDL_CreateGPUTexture").c_str(), img->w, img->h );
		return 0;
	}
	UploadTexture( pTex, img, 0, 0, img->w, img->h, iLevels > 1 );

	unsigned uHandle;
	if( !g_FreeHandles.empty() )
	{
		uHandle = g_FreeHandles.back();
		g_FreeHandles.pop_back();
	}
	else
	{
		if( g_Textures.empty() )
		{
			/* Handle 0 is no texture. */
			Texture none = { NULL, false };
			g_Textures.push_back( none );
		}
		uHandle = g_Textures.size();
		g_Textures.push_back( Texture() );
	}
	g_Textures[uHandle].pTex = pTex;
	g_Textures[uHandle].bWrap = false;
	return uHandle;
}

void RageDisplay_SDLGPU::UpdateTexture(
	unsigned uTexHandle,
	RageSurface* img,
	int xoffset, int yoffset, int width, int height )
{
	if( uTexHandle == 0 || uTexHandle >= g_Textures.size() || g_Textures[uTexHandle].pTex == NULL )
		return;
	UploadTexture( g_Textures[uTexHandle].pTex, img, xoffset, yoffset, width, height, false );
}

void RageDisplay_SDLGPU::DeleteTexture( unsigned uTexHandle )
{
	if( uTexHandle == 0 || uTexHandle >= g_Textures.size() || g_Textures[uTexHandle].pTex == NULL )
		return;

	/* A recorded frame may still draw it. */
	g_DeletedTextures[0].push_back( g_Textures[uTexHandle].pTex );
	g_Textures[uTexHandle].pTex = NULL;
	g_FreeHandles.push_back( uTexHandle );

	for( int i = 0; i < MAX_TEXTURE_UNITS; ++i )
		if( g_Wanted.Unit[i].uTexture == uTexHandle )
			g_Wanted.Unit[i].uTexture = 0;
}

/*
 * Copyright (c) 2001-2004 Chris Danford, Glenn Maynard
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

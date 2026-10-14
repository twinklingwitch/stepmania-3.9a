#include "global.h"

/* ours may be more up-to-date */
#define __glext_h_
#define GL_GLEXT_LEGACY

#if !defined(DARWIN)
# include <GL/gl.h>
# include <GL/glu.h>
#else
# include <OpenGL/gl.h>
# include <OpenGL/glu.h>
#endif

#undef __glext_h_
#include "glext.h"

#include "RageFile.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "PrefsManager.h" // XXX

/* Windows's broken gl.h defines GL_EXT_paletted_texture incompletely: */
#ifndef GL_TEXTURE_INDEX_SIZE_EXT
#define GL_TEXTURE_INDEX_SIZE_EXT         0x80ED
#endif
#include <set>
#include <sstream>

/* Not in glext.h: */
typedef bool (APIENTRY * PWSWAPINTERVALEXTPROC) (int interval);


/* Extension functions we use.  Put these in a namespace instead of in oglspecs_t,
 * so they can be called like regular functions. */
static struct GLExt_t
{
	PWSWAPINTERVALEXTPROC wglSwapIntervalEXT;
	PFNGLCOLORTABLEPROC glColorTableEXT;
	PFNGLCOLORTABLEPARAMETERIVPROC glGetColorTableParameterivEXT;
	PFNGLACTIVETEXTUREARBPROC glActiveTextureARB;
	PFNGLGENBUFFERSARBPROC glGenBuffersARB;
	PFNGLBINDBUFFERARBPROC glBindBufferARB;
	PFNGLBUFFERDATAARBPROC glBufferDataARB;
	PFNGLBUFFERSUBDATAARBPROC glBufferSubDataARB;
	PFNGLDELETEBUFFERSARBPROC glDeleteBuffersARB;
	PFNGLDRAWRANGEELEMENTSPROC glDrawRangeElements;
	
	void Reset()
	{
		memset( this, 0, sizeof(*this) );
	}
} GLExt;

#include "RageDisplay.h"
#include "RageDisplay_OGL.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "RageException.h"
#include "RageTexture.h"
#include "RageTextureManager.h"
#include "RageMath.h"
#include "RageTypes.h"
#include "GameConstantsAndTypes.h"
#include "StepMania.h"
#include "RageUtil.h"

#include "arch/arch.h"
#include "arch/LowLevelWindow/LowLevelWindow.h"

//
// Globals
//

static bool g_bEXT_texture_env_combine = true;
static bool g_bGL_EXT_bgra = true;

static bool g_bReversePackedPixelsWorks = true;

/* OpenGL system information that generally doesn't change at runtime. */

/* Range and granularity of points and lines: */
float g_line_range[2];
float g_line_granularity;
float g_point_range[2];
float g_point_granularity;

/* OpenGL version * 10: */
int g_glVersion;

/* GLU version * 10: */
int g_gluVersion;

/* Available extensions: */
set<string> g_glExts;

/* We don't actually use normals (we don't tunr on lighting), there's just
 * no GL_T2F_C4F_V3F. */
const GLenum RageSpriteVertexFormat = GL_T2F_C4F_N3F_V3F;


LowLevelWindow *wind;

static void InvalidateAllGeometry();

static RageDisplay::PixelFormatDesc PIXEL_FORMAT_DESC[RageDisplay::NUM_PIX_FORMATS] = {
	{
		/* R8G8B8A8 */
		32,
		{ 0xFF000000,
		  0x00FF0000,
		  0x0000FF00,
		  0x000000FF }
	}, {
		/* R4G4B4A4 */
		16,
		{ 0xF000,
		  0x0F00,
		  0x00F0,
		  0x000F },
	}, {
		/* R5G5B5A1 */
		16,
		{ 0xF800,
		  0x07C0,
		  0x003E,
		  0x0001 },
	}, {
		/* R5G5B5 */
		16,
		{ 0xF800,
		  0x07C0,
		  0x003E,
		  0x0000 },
	}, {
		/* R8G8B8 */
		24,
		{ 0xFF0000,
		  0x00FF00,
		  0x0000FF,
		  0x000000 }
	}, {
		/* Paletted */
		8,
		{ 0,0,0,0 } /* N/A */
	}, {
		/* B8G8R8A8 */
		24,
		{ 0x0000FF,
		  0x00FF00,
		  0xFF0000,
		  0x000000 }
	}, {
		/* A1B5G5R5 */
		16,
		{ 0x7C00,
		  0x03E0,
		  0x001F,
		  0x8000 },
	}
};

static map<GLenum, CString> g_Strings;
static void InitStringMap()
{
	static bool Initialized = false;
	if(Initialized) return;
	Initialized = true;
	#define X(a) g_Strings[a] = #a;
	X(GL_RGBA8);	X(GL_RGBA4);	X(GL_RGB5_A1);	X(GL_RGB5);	X(GL_RGBA);	X(GL_RGB);
	X(GL_BGR);	X(GL_BGRA);
	X(GL_COLOR_INDEX8_EXT);	X(GL_COLOR_INDEX4_EXT);	X(GL_COLOR_INDEX);
	X(GL_UNSIGNED_BYTE);	X(GL_UNSIGNED_SHORT_4_4_4_4); X(GL_UNSIGNED_SHORT_5_5_5_1);
	X(GL_UNSIGNED_SHORT_1_5_5_5_REV);
	X(GL_INVALID_ENUM); X(GL_INVALID_VALUE); X(GL_INVALID_OPERATION);
	X(GL_STACK_OVERFLOW); X(GL_STACK_UNDERFLOW); X(GL_OUT_OF_MEMORY);
}

static CString GLToString( GLenum e )
{
	if( g_Strings.find(e) != g_Strings.end() )
		return g_Strings[e];

	return ssprintf( "%i", int(e) );
}

/* GL_PIXFMT_INFO is used for both texture formats and surface formats.  For example,
 * it's fine to ask for a FMT_RGB5 texture, but to supply a surface matching
 * FMT_RGB8.  OpenGL will simply discard the extra bits.
 *
 * It's possible for a format to be supported as a texture format but not as a
 * surface format.  For example, if packed pixels aren't supported, we can still
 * use GL_RGB5_A1, but we'll have to convert to a supported surface pixel format
 * first.  It's not ideal, since we'll convert to RGBA8 and OGL will convert back,
 * but it works fine.
 */
struct GLPixFmtInfo_t {
	GLenum internalfmt; /* target format */
	GLenum format; /* target format */
	GLenum type; /* data format */
} GL_PIXFMT_INFO[RageDisplay::NUM_PIX_FORMATS] = {
	{
		/* R8G8B8A8 */
		GL_RGBA8,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
	}, {
		/* B4G4R4A4 */
		GL_RGBA4,
		GL_RGBA,
		GL_UNSIGNED_SHORT_4_4_4_4,
	}, {
		/* B5G5R5A1 */
		GL_RGB5_A1,
		GL_RGBA,
		GL_UNSIGNED_SHORT_5_5_5_1,
	}, {
		/* B5G5R5 */
		GL_RGB5,
		GL_RGBA,
		GL_UNSIGNED_SHORT_5_5_5_1,
	}, {
		/* B8G8R8 */
		GL_RGB8,
		GL_RGB,
		GL_UNSIGNED_BYTE,
	}, {
		/* Paletted */
		GL_COLOR_INDEX8_EXT,
		GL_COLOR_INDEX,
		GL_UNSIGNED_BYTE,
	}, {
		/* B8G8R8 */
		GL_RGB8,
		GL_BGR,
		GL_UNSIGNED_BYTE,
	}, {
		/* A1R5G5B5 (matches D3DFMT_A1R5G5B5) */
		GL_RGB5_A1,
		GL_BGRA,
		GL_UNSIGNED_SHORT_1_5_5_5_REV,
	}
};


static void FixLilEndian()
{
#if defined(ENDIAN_LITTLE)
	static bool Initialized = false;
	if( Initialized )
		return;
	Initialized = true;

	for( int i = 0; i < RageDisplay::NUM_PIX_FORMATS; ++i )
	{
		RageDisplay::PixelFormatDesc &pf = PIXEL_FORMAT_DESC[i];

		/* OpenGL and RageSurface handle byte formats differently; we need
		 * to flip non-paletted masks to make them line up. */
		if( GL_PIXFMT_INFO[i].type != GL_UNSIGNED_BYTE || pf.bpp == 8 )
			continue;

		for( int mask = 0; mask < 4; ++mask)
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


void GetGLExtensions(set<string> &ext)
{
    const char *buf = (const char *)glGetString(GL_EXTENSIONS);

	vector<CString> lst;
	split(buf, " ", lst);

	for(unsigned i = 0; i < lst.size(); ++i)
		ext.insert(lst[i]);
}

static void FlushGLErrors()
{
	/* Making an OpenGL call doesn't also flush the error state; if we happen
	 * to have an error from a previous call, then the assert below will fail. 
	 * Flush it. */
	while( glGetError() != GL_NO_ERROR )
		;
}

#define AssertNoGLError() \
{ \
	GLenum error = glGetError(); \
	ASSERT_M( error == GL_NO_ERROR, GLToString(error) ) \
}

/*
 * The GL state we set over and over: almost every actor sets the same states
 * before it draws, and every draw sends the same arrays and matrices.  Remember
 * what we last set, so the driver only hears about changes.  An entry of -1 (or
 * false for the "valid" flags) means unknown, and the next call goes through;
 * anything that changes one of these states without going through the cache
 * has to forget it.  Everything is forgotten when the context is (re)created.
 *
 * Blending (GL_BLEND, glBlendFunc, glDepthRange) is not cached.
 */
namespace
{
	enum { ARRAY_VERTEX, ARRAY_COLOR, ARRAY_TEXCOORD, ARRAY_NORMAL, NUM_ARRAYS };
	enum { TEXENV_MODULATE, TEXENV_ADD, TEXENV_GLOW_WHITEN };

	struct GLStateCache
	{
		bool bBuffersUnbound;	// no buffer object bound to GL_ARRAY_BUFFER or GL_ELEMENT_ARRAY_BUFFER
		int iArrayEnabled[NUM_ARRAYS];
		const void *pArray[NUM_ARRAYS];	// where SetupVertices pointed the array, or NULL

		int iMatrixMode;
		bool bMatricesSent;	// SendCurrentMatrices has run in this context (kept by Invalidate)
		bool bProjectionValid, bModelViewValid, bTextureMatrixValid[MAX_TEXTURE_UNITS];
		RageMatrix Projection, ModelView, TextureMatrix[MAX_TEXTURE_UNITS];
		/* GetCameraMatrixGeneration() when Projection and TextureMatrix were last
		 * known to match the current matrices. */
		unsigned iProjectionGen, iTextureMatrixGen[MAX_TEXTURE_UNITS];

		int iActiveUnit;
		int iTexture2D[MAX_TEXTURE_UNITS];
		/* Whether GL_TEXTURE_2D should be on for each unit.  It only matters when
		 * drawing, so it's only set then; most draws turn texturing off and back
		 * on (ClearAllTextures, then SetTexture), and the driver would revalidate
		 * texture state each time. */
		int iTexture2DWanted[MAX_TEXTURE_UNITS];
		int64_t iBoundTexture[MAX_TEXTURE_UNITS];
		int iTexEnv[MAX_TEXTURE_UNITS];

		/* Wrap mode of each texture object, indexed by texture name: -1 unknown,
		 * 0 clamped, 1 repeating. */
		vector<signed char> TextureWrap;

		int iZWrite, iDepthTest, iDepthFunc, iCullFace, iCullMode;

		GLStateCache(): bMatricesSent( false ) { Invalidate(); }

		void Invalidate()
		{
			bBuffersUnbound = false;
			InvalidateArrays();
			iMatrixMode = -1;
			bProjectionValid = bModelViewValid = false;
			iActiveUnit = -1;
			for( int i = 0; i < MAX_TEXTURE_UNITS; ++i )
			{
				bTextureMatrixValid[i] = false;
				iTexture2D[i] = -1;
				iTexture2DWanted[i] = -1;
				iBoundTexture[i] = -1;
				iTexEnv[i] = -1;
			}
			TextureWrap.clear();
			iZWrite = iDepthTest = iDepthFunc = iCullFace = iCullMode = -1;
		}

		/* Forget the client array state, after something else set it. */
		void InvalidateArrays()
		{
			for( int i = 0; i < NUM_ARRAYS; ++i )
			{
				iArrayEnabled[i] = -1;
				pArray[i] = NULL;
			}
		}

		/* The texture unit that unit-specific calls apply to, or -1. */
		int ActiveUnit() const { return GLExt.glActiveTextureARB? iActiveUnit: 0; }

		/* The texture bound to the active unit, or -1. */
		int64_t ActiveTexture() const
		{
			const int iUnit = ActiveUnit();
			return iUnit == -1? -1: iBoundTexture[iUnit];
		}

		signed char &Wrap( unsigned uTexHandle )
		{
			if( uTexHandle >= TextureWrap.size() )
				TextureWrap.resize( uTexHandle+1, -1 );
			return TextureWrap[uTexHandle];
		}
	};

	GLStateCache g_State;
}

static void FlushTextureEnables();

static void SetClientState( int iArray, GLenum array, bool bOn )
{
	if( g_State.iArrayEnabled[iArray] == int(bOn) )
		return;
	if( bOn )
		glEnableClientState( array );
	else
		glDisableClientState( array );
	g_State.iArrayEnabled[iArray] = bOn;
}

static void SetMatrixMode( GLenum mode )
{
	if( g_State.iMatrixMode == int(mode) )
		return;
	glMatrixMode( mode );
	g_State.iMatrixMode = mode;
}

/* Note that a buffer object may now be bound. */
static void BufferBound()
{
	g_State.bBuffersUnbound = false;
}

static void TurnOffHardwareVBO()
{
	if( GLExt.glBindBufferARB && !g_State.bBuffersUnbound )
	{
		GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
		GLExt.glBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
		g_State.bBuffersUnbound = true;
	}
}

RageDisplay_OGL::RageDisplay_OGL( VideoModeParams p, bool bAllowUnacceleratedRenderer )
{
	LOG->Trace( "RageDisplay_OGL::RageDisplay_OGL()" );
	LOG->MapLog("renderer", "Current renderer: OpenGL");

	FixLilEndian();
	InitStringMap();

	wind = MakeLowLevelWindow();

	try {
		SetVideoMode( p );
	} catch(...) {
		/* SetVideoMode can throw. */
		delete wind;
		throw;
	}

	// Log driver details
	LOG->Info("OGL Vendor: %s", glGetString(GL_VENDOR));
	LOG->Info("OGL Renderer: %s", glGetString(GL_RENDERER));
	LOG->Info("OGL Version: %s", glGetString(GL_VERSION));
	LOG->Info("OGL Extensions: %s", glGetString(GL_EXTENSIONS));
	LOG->Info("OGL Max texture size: %i", GetMaxTextureSize() );
	LOG->Info("GLU Version: %s", gluGetString(GLU_VERSION));

	if( IsSoftwareRenderer() )
	{
		if( !bAllowUnacceleratedRenderer )
		{
			delete wind;
			RageException::ThrowNonfatal(
				"Your system is reporting that OpenGL hardware acceleration is not available.  "
				"Please obtain an updated driver from your video card manufacturer.\n\n" );
		}
		LOG->Warn("This is a software renderer!");
	}

	/* Log this, so if people complain that the radar looks bad on their
	 * system we can compare them: */
	glGetFloatv(GL_LINE_WIDTH_RANGE, g_line_range);
	glGetFloatv(GL_LINE_WIDTH_GRANULARITY, &g_line_granularity);
	glGetFloatv(GL_POINT_SIZE_RANGE, g_point_range);
	glGetFloatv(GL_POINT_SIZE_GRANULARITY, &g_point_granularity);
}

void RageDisplay_OGL::Update(float fDeltaTime)
{
	wind->Update(fDeltaTime);
}

bool RageDisplay_OGL::IsSoftwareRenderer()
{
	return false;
}

RageDisplay_OGL::~RageDisplay_OGL()
{
	delete wind;
}

bool HasExtension(CString ext)
{
	return g_glExts.find(ext) != g_glExts.end();
}

static void CheckPalettedTextures()
{
	CString error;
	do
	{
		if( !HasExtension("GL_EXT_paletted_texture") )
		{
			error = "GL_EXT_paletted_texture missing";
			break;
		}

		if( GLExt.glColorTableEXT == NULL )
		{
			error = "glColorTableEXT missing";
			break;
		}

		if( GLExt.glGetColorTableParameterivEXT == NULL )
		{
			error = "glGetColorTableParameterivEXT missing";
			break;
		}

		/* Check to see if paletted textures really work. */
		GLenum glTexFormat = GL_PIXFMT_INFO[RageDisplay::FMT_PAL].internalfmt;
		GLenum glImageFormat = GL_PIXFMT_INFO[RageDisplay::FMT_PAL].format;
		GLenum glImageType = GL_PIXFMT_INFO[RageDisplay::FMT_PAL].type;

		int bits = 8;

		FlushGLErrors();
#define GL_CHECK_ERROR(f) \
{ \
	GLenum glError = glGetError(); \
	if( glError != GL_NO_ERROR ) { \
		error = ssprintf(f " failed (%s)", GLToString(glError).c_str() ); \
		break; \
	} \
}

		glTexImage2D(GL_PROXY_TEXTURE_2D,
				0, glTexFormat, 
				16, 16, 0,
				glImageFormat, glImageType, NULL);
		GL_CHECK_ERROR( "glTexImage2D" );

		GLuint ifmt = 0;
		glGetTexLevelParameteriv( GL_PROXY_TEXTURE_2D, 0, GLenum(GL_TEXTURE_INTERNAL_FORMAT), (GLint *)&ifmt );
		GL_CHECK_ERROR( "glGetTexLevelParameteriv(GL_TEXTURE_INTERNAL_FORMAT)" );
		if( ifmt != glTexFormat )
		{
			error = ssprintf( "Expected format %s, got %s instead",
					GLToString(glTexFormat).c_str(),
					GLToString(ifmt).c_str() );
			break;
		}

		GLubyte palette[256*4];
		memset(palette, 0, sizeof(palette));
		GLExt.glColorTableEXT(GL_PROXY_TEXTURE_2D, GL_RGBA8, 256, GL_RGBA, GL_UNSIGNED_BYTE, palette);
		GL_CHECK_ERROR( "glColorTableEXT" );

		GLint size = 0;
		glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GLenum(GL_TEXTURE_INDEX_SIZE_EXT), &size);
		GL_CHECK_ERROR( "glGetTexLevelParameteriv(GL_TEXTURE_INDEX_SIZE_EXT)" );
		if( bits > size || size > 8 )
		{
			error = ssprintf("Expected %i-bit palette, got a %i-bit one instead", bits, int(size));
			break;
		}

		GLint RealWidth = 0;
		GLExt.glGetColorTableParameterivEXT(GL_PROXY_TEXTURE_2D, GL_COLOR_TABLE_WIDTH, &RealWidth);
		GL_CHECK_ERROR( "glGetColorTableParameterivEXT(GL_COLOR_TABLE_WIDTH)" );
		if( RealWidth != 1 << bits )
		{
			error = ssprintf("GL_COLOR_TABLE_WIDTH returned %i instead of %i", int(RealWidth), 1 << bits );
			break;
		}

		GLint RealFormat = 0;
		GLExt.glGetColorTableParameterivEXT(GL_PROXY_TEXTURE_2D, GL_COLOR_TABLE_FORMAT, &RealFormat);
		GL_CHECK_ERROR( "glGetColorTableParameterivEXT(GL_COLOR_TABLE_FORMAT)" );
		if( RealFormat != GL_RGBA8 )
		{
			error = ssprintf("GL_COLOR_TABLE_FORMAT returned %s instead of GL_RGBA8", GLToString(RealFormat).c_str() );
			break;
		}
	} while(0);
#undef GL_CHECK_ERROR

	if( error == "" )
		return;

	/* If 8-bit palettes don't work, disable them entirely--don't trust 4-bit
	 * palettes if it can't even get 8-bit ones right. */
	GLExt.glColorTableEXT = NULL;
	GLExt.glGetColorTableParameterivEXT = NULL;
	LOG->Info("Paletted textures disabled: %s.", error.c_str());
}

static void CheckReversePackedPixels()
{
	/* Try to create a texture. */
	FlushGLErrors();
	glTexImage2D(GL_PROXY_TEXTURE_2D,
				0, GL_RGBA, 
				16, 16, 0,
				GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV, NULL);

	const GLenum glError = glGetError();
	if( glError == GL_NO_ERROR )
		g_bReversePackedPixelsWorks = true;
	else
	{
		g_bReversePackedPixelsWorks = false;
		LOG->Info("GL_UNSIGNED_SHORT_1_5_5_5_REV failed (%s), disabled",
			GLToString(glError).c_str() );
	}
}

void SetupExtensions()
{
	const float fGLVersion = strtof( (const char *) glGetString(GL_VERSION), NULL );
	g_glVersion = int(roundf(fGLVersion * 10));
	GetGLExtensions(g_glExts);

	const float fGLUVersion = strtof( (const char *) gluGetString(GLU_VERSION), NULL );
	g_gluVersion = int(roundf(fGLUVersion * 10));

	/* Reset extensions. */
	GLExt.Reset();
	
	/*
	 * Find extension functions.
	 *
	 * X11R6.7.0 (or possibly ATI's drivers) seem to be returning bogus values for glBindBufferARB
	 * if we don't actually check for GL_ARB_vertex_buffer_object. 
	 * https://sf.net/tracker/download.php?group_id=37892&atid=421366&file_id=88086&aid=958820
	 * https://sf.net/tracker/download.php?group_id=37892&atid=421366&file_id=85542&aid=944836
	 *
	 * Let's check them all, to be safe.
	 */

	if( HasExtension("GL_EXT_paletted_texture") )
	{
		GLExt.glColorTableEXT = (PFNGLCOLORTABLEPROC) wind->GetProcAddress("glColorTableEXT");
		GLExt.glGetColorTableParameterivEXT = (PFNGLCOLORTABLEPARAMETERIVPROC) wind->GetProcAddress("glGetColorTableParameterivEXT");
	}

	if( HasExtension("GL_ARB_multitexture") )
		GLExt.glActiveTextureARB = (PFNGLACTIVETEXTUREARBPROC) wind->GetProcAddress("glActiveTextureARB");

	if( HasExtension("GL_ARB_vertex_buffer_object") )
	{
		GLExt.glGenBuffersARB = (PFNGLGENBUFFERSARBPROC) wind->GetProcAddress("glGenBuffersARB");
		GLExt.glBindBufferARB = (PFNGLBINDBUFFERARBPROC) wind->GetProcAddress("glBindBufferARB");
		GLExt.glBufferDataARB = (PFNGLBUFFERDATAARBPROC) wind->GetProcAddress("glBufferDataARB");
		GLExt.glBufferSubDataARB = (PFNGLBUFFERSUBDATAARBPROC) wind->GetProcAddress("glBufferSubDataARB");
		GLExt.glDeleteBuffersARB = (PFNGLDELETEBUFFERSARBPROC) wind->GetProcAddress("glDeleteBuffersARB");
	}

	if( HasExtension("GL_EXT_draw_range_elements") )
		GLExt.glDrawRangeElements = (PFNGLDRAWRANGEELEMENTSPROC) wind->GetProcAddress("glDrawRangeElements");

	g_bEXT_texture_env_combine = HasExtension("GL_EXT_texture_env_combine");
	g_bGL_EXT_bgra = HasExtension("GL_EXT_bgra");
	CheckPalettedTextures();
	CheckReversePackedPixels();
}

void DumpOpenGLDebugInfo()
{
}

void RageDisplay_OGL::ResolutionChanged()
{
 	SetViewport(0,0);

	/* Clear any junk that's in the framebuffer. */
	if( BeginFrame() )
		EndFrame();
}

// Return true if mode change was successful.
// bNewDeviceOut is set true if a new device was created and textures
// need to be reloaded.
CString RageDisplay_OGL::TryVideoMode( VideoModeParams p, bool &bNewDeviceOut )
{
//	LOG->Trace( "RageDisplay_OGL::SetVideoMode( %d, %d, %d, %d, %d, %d )", windowed, width, height, bpp, rate, vsync );
	CString err;
	err = wind->TryVideoMode( p, bNewDeviceOut );
	if( err != "" )
		return err;	// failed to set video mode

	/* We may have a new context. */
	g_State.Invalidate();
	if( bNewDeviceOut )
		g_State.bMatricesSent = false;

	if( bNewDeviceOut )
	{
		/* We have a new OpenGL context, so we have to tell our textures that
		 * their OpenGL texture number is invalid. */
		if(TEXTUREMAN)
			TEXTUREMAN->InvalidateTextures();

		/* Recreate all vertex buffers. */
		InvalidateAllGeometry();
	}

	this->SetDefaultRenderStates();
	DumpOpenGLDebugInfo();

	/* Now that we've initialized, we can search for extensions (some of which
	 * we may need to set up the video mode). */
	SetupExtensions();

	/* Which units we track depends on the extensions. */
	g_State.Invalidate();

	/* Set vsync the Windows way, if we can.  (What other extensions are there
	 * to do this, for other archs?) */
	if( GLExt.wglSwapIntervalEXT )
	    GLExt.wglSwapIntervalEXT(p.vsync);
	
	ResolutionChanged();

	return "";	// successfully set mode
}

void RageDisplay_OGL::SetViewport(int shift_left, int shift_down)
{
	/* left and down are on a 0..SCREEN_WIDTH, 0..SCREEN_HEIGHT scale.
	 * Scale them to the actual viewport range, in framebuffer pixels. */
	int iWidth, iHeight;
	wind->GetFramebufferSize( iWidth, iHeight );
	shift_left = int( shift_left * float(iWidth) / SCREEN_WIDTH );
	shift_down = int( shift_down * float(iHeight) / SCREEN_HEIGHT );

	glViewport(shift_left, -shift_down, iWidth, iHeight);
}

int RageDisplay_OGL::GetMaxTextureSize() const
{
	GLint size;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &size);
	return size;
}

bool RageDisplay_OGL::BeginFrame()
{
	glClearColor( 0,0,0,1 );
	SetZWrite( true );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	return true;
}

void RageDisplay_OGL::EndFrame()
{
	/* The frame isn't done until the GPU is, and we need to know how long that
	 * takes to pace frames.  There's nothing else for the CPU to do meanwhile. */
	if( IsPacing() )
		glFinish();

	FrameSwapping();
	wind->SwapBuffers();
	FrameSwapped();
	ProcessStatsOnFlip();
}

RageSurface* RageDisplay_OGL::CreateScreenshot()
{
	int width, height;
	wind->GetFramebufferSize( width, height );

	const PixelFormatDesc &desc = PIXEL_FORMAT_DESC[FMT_RGBA8];
	RageSurface *image = CreateSurface( width, height, desc.bpp,
		desc.masks[0], desc.masks[1], desc.masks[2], 0 );

	FlushGLErrors();
	glReadPixels(0, 0, width, height, GL_RGBA,
	             GL_UNSIGNED_BYTE, image->pixels);
	AssertNoGLError();

	RageSurfaceUtils::FlipVertically( image );

	return image;
}

RageDisplay::VideoModeParams RageDisplay_OGL::GetVideoModeParams() const { return wind->GetVideoModeParams(); }
float RageDisplay_OGL::GetRefreshRate() const { return wind->GetRefreshRate(); }

static void SetupVertices( const RageSpriteVertex v[], int iNumVerts )
{
	/* Positions, normals and texture coordinates are sent straight out of a
	 * copy of the vertices; only the colors need reordering, since RageVColor
	 * is BGRA. */
	static RageSpriteVertex *Vertex;
	static GLubyte *Color;
	static int Size = 0;
	if(iNumVerts > Size)
	{
		Size = iNumVerts;
		delete [] Vertex;
		delete [] Color;
		Vertex = new RageSpriteVertex[Size];
		Color = new GLubyte[Size*4];
	}

	memcpy( Vertex, v, iNumVerts*sizeof(RageSpriteVertex) );
	for(unsigned i = 0; i < unsigned(iNumVerts); ++i)
	{
		/* Build each color in a local, so it's stored at once. */
		const RageVColor &c = v[i].c;
		const GLubyte rgba[4] = { c.r, c.g, c.b, c.a };
		memcpy( &Color[i*4], rgba, 4 );
	}

	/* The arrays only move when they grow, and the pointers are only replaced
	 * by the compiled geometry, which forgets them; the driver reads through
	 * them at draw time, so only point them again if they've changed. */
	const GLsizei stride = sizeof(RageSpriteVertex);
	SetClientState( ARRAY_VERTEX, GL_VERTEX_ARRAY, true );
	if( g_State.pArray[ARRAY_VERTEX] != Vertex )
	{
		glVertexPointer(3, GL_FLOAT, stride, &Vertex[0].p);
		g_State.pArray[ARRAY_VERTEX] = Vertex;
	}

	SetClientState( ARRAY_COLOR, GL_COLOR_ARRAY, true );
	if( g_State.pArray[ARRAY_COLOR] != Color )
	{
		glColorPointer(4, GL_UNSIGNED_BYTE, 0, Color);
		g_State.pArray[ARRAY_COLOR] = Color;
	}

	SetClientState( ARRAY_TEXCOORD, GL_TEXTURE_COORD_ARRAY, true );
	if( g_State.pArray[ARRAY_TEXCOORD] != Vertex )
	{
		glTexCoordPointer(2, GL_FLOAT, stride, &Vertex[0].t);
		g_State.pArray[ARRAY_TEXCOORD] = Vertex;
	}

	SetClientState( ARRAY_NORMAL, GL_NORMAL_ARRAY, true );
	if( g_State.pArray[ARRAY_NORMAL] != Vertex )
	{
		glNormalPointer(GL_FLOAT, stride, &Vertex[0].n);
		g_State.pArray[ARRAY_NORMAL] = Vertex;
	}
}

static void LoadMatrix( GLenum mode, const RageMatrix &m, bool &bValid, RageMatrix &last )
{
	if( bValid && !memcmp(&last, &m, sizeof(m)) )
		return;
	SetMatrixMode( mode );
	glLoadMatrixf( (const float*)&m );
	bValid = true;
	last = m;
}

void RageDisplay_OGL::SendCurrentMatrices()
{
	/* Usually only the world matrix changes from one draw to the next.  The
	 * others are only compared with what we sent if they may have changed. */
	const unsigned iGen = GetCameraMatrixGeneration();

	if( !g_State.bProjectionValid || g_State.iProjectionGen != iGen )
	{
		LoadMatrix( GL_PROJECTION, *GetProjectionTop(), g_State.bProjectionValid, g_State.Projection );
		g_State.iProjectionGen = iGen;
	}

	/* Keep the product of the centering and view matrices. */
	static bool bCenteringViewValid = false;
	static unsigned iCenteringViewGen;
	static RageMatrix CenteringView;
	if( !bCenteringViewValid || iCenteringViewGen != iGen )
	{
		RageMatrixMultiply( &CenteringView, GetCentering(), GetViewTop() );
		iCenteringViewGen = iGen;
		bCenteringViewValid = true;
	}

	RageMatrix modelView;
	RageMatrixMultiply( &modelView, &CenteringView, GetWorldTop() );
	LoadMatrix( GL_MODELVIEW, modelView, g_State.bModelViewValid, g_State.ModelView );

	/* The texture matrix belongs to the active texture unit. */
	const int iUnit = g_State.ActiveUnit();
	if( iUnit == -1 )
	{
		SetMatrixMode( GL_TEXTURE );
		glLoadMatrixf( (const float*)GetTextureTop() );
	}
	else if( !g_State.bTextureMatrixValid[iUnit] || g_State.iTextureMatrixGen[iUnit] != iGen )
	{
		LoadMatrix( GL_TEXTURE, *GetTextureTop(), g_State.bTextureMatrixValid[iUnit], g_State.TextureMatrix[iUnit] );
		g_State.iTextureMatrixGen[iUnit] = iGen;
	}
	g_State.bMatricesSent = true;
}

class RageCompiledGeometrySWOGL : public RageCompiledGeometry
{
public:
	
	void Allocate( const vector<msMesh> &vMeshes )
	{
		m_vPosition.resize( GetTotalVertices() );
		m_vTexture.resize( GetTotalVertices() );
		m_vNormal.resize( GetTotalVertices() );
		m_vTriangles.resize( GetTotalTriangles() );
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
				m_vPosition[meshInfo.iVertexStart+j] = Vertices[j].p;
				m_vTexture[meshInfo.iVertexStart+j] = Vertices[j].t;
				m_vNormal[meshInfo.iVertexStart+j] = Vertices[j].n;
			}

			for( unsigned j=0; j<Triangles.size(); j++ )
				for( unsigned k=0; k<3; k++ )
				{
					int iVertexIndexInVBO = meshInfo.iVertexStart + Triangles[j].nVertexIndices[k];
					m_vTriangles[meshInfo.iTriangleStart+j].nVertexIndices[k] = (uint16_t) iVertexIndexInVBO;
				}
		}
	}
	void Draw( int iMeshIndex ) const
	{
		TurnOffHardwareVBO();

		const MeshInfo& meshInfo = m_vMeshInfo[iMeshIndex];

		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(3, GL_FLOAT, 0, &m_vPosition[0]);

		glDisableClientState(GL_COLOR_ARRAY);

		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, 0, &m_vTexture[0]);

		glEnableClientState(GL_NORMAL_ARRAY);
		glNormalPointer(GL_FLOAT, 0, &m_vNormal[0]);

		glDrawElements( 
			GL_TRIANGLES, 
			meshInfo.iTriangleCount*3, 
			GL_UNSIGNED_SHORT, 
			&m_vTriangles[0]+meshInfo.iTriangleStart );

		g_State.InvalidateArrays();
	}

protected:
	vector<RageVector3> m_vPosition;
	vector<RageVector2> m_vTexture;
	vector<RageVector3> m_vNormal;
	vector<msTriangle>	m_vTriangles;
};

class RageCompiledGeometryHWOGL : public RageCompiledGeometrySWOGL
{
protected:
	// vertex buffer object names
	GLuint m_nPositions;
	GLuint m_nTextureCoords;
	GLuint m_nNormals;
	GLuint m_nTriangles;

	void AllocateBuffers();
	void UploadData();

public:
	RageCompiledGeometryHWOGL();
	~RageCompiledGeometryHWOGL();

	/* This is called when our OpenGL context is invalidated. */
	void Invalidate();
	
	void Allocate( const vector<msMesh> &vMeshes );
	void Change( const vector<msMesh> &vMeshes );
	void Draw( int iMeshIndex ) const;
};

static set<RageCompiledGeometryHWOGL *> g_GeometryList;
static void InvalidateAllGeometry()
{
	set<RageCompiledGeometryHWOGL *>::iterator it;
	for( it = g_GeometryList.begin(); it != g_GeometryList.end(); ++it )
		(*it)->Invalidate();
}

RageCompiledGeometryHWOGL::RageCompiledGeometryHWOGL()
{
	g_GeometryList.insert( this );
	m_nPositions = m_nTextureCoords = m_nNormals = m_nTriangles = 0;

	AllocateBuffers();
}

RageCompiledGeometryHWOGL::~RageCompiledGeometryHWOGL()
{
	g_GeometryList.erase( this );
	FlushGLErrors();

	GLExt.glDeleteBuffersARB( 1, &m_nPositions );
	AssertNoGLError();
	GLExt.glDeleteBuffersARB( 1, &m_nTextureCoords );
	AssertNoGLError();
	GLExt.glDeleteBuffersARB( 1, &m_nNormals );
	AssertNoGLError();
	GLExt.glDeleteBuffersARB( 1, &m_nTriangles );
	AssertNoGLError();
}

void RageCompiledGeometryHWOGL::AllocateBuffers()
{
	FlushGLErrors();

	if( !m_nPositions )
	{
		GLExt.glGenBuffersARB( 1, &m_nPositions );
		AssertNoGLError();
	}

	if( !m_nTextureCoords )
	{
		GLExt.glGenBuffersARB( 1, &m_nTextureCoords );
		AssertNoGLError();
	}

	if( !m_nNormals )
	{
		GLExt.glGenBuffersARB( 1, &m_nNormals );
		AssertNoGLError();
	}

	if( !m_nTriangles )
	{
		GLExt.glGenBuffersARB( 1, &m_nTriangles );
		AssertNoGLError();
	}
}

void RageCompiledGeometryHWOGL::UploadData()
{
	GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nPositions );
	GLExt.glBufferDataARB( 
		GL_ARRAY_BUFFER_ARB, 
		GetTotalVertices()*sizeof(RageVector3), 
		&m_vPosition[0], 
		GL_STATIC_DRAW_ARB );
//		AssertNoGLError();

	GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nTextureCoords );
	GLExt.glBufferDataARB( 
		GL_ARRAY_BUFFER_ARB, 
		GetTotalVertices()*sizeof(RageVector2), 
		&m_vTexture[0], 
		GL_STATIC_DRAW_ARB );
//		AssertNoGLError();

	GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nNormals );
	GLExt.glBufferDataARB( 
		GL_ARRAY_BUFFER_ARB, 
		GetTotalVertices()*sizeof(RageVector3), 
		&m_vNormal[0], 
		GL_STATIC_DRAW_ARB );
//		AssertNoGLError();

	GLExt.glBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, m_nTriangles );
	GLExt.glBufferDataARB( 
		GL_ELEMENT_ARRAY_BUFFER_ARB, 
		GetTotalTriangles()*sizeof(msTriangle), 
		&m_vTriangles[0], 
		GL_STATIC_DRAW_ARB );
//		AssertNoGLError();
	BufferBound();
}

void RageCompiledGeometryHWOGL::Invalidate()
{
	/* Our vertex buffers no longer exist.  Reallocate and reupload. */
	m_nPositions = m_nTextureCoords = m_nNormals = m_nTriangles = 0;
	AllocateBuffers();
	UploadData();
}

void RageCompiledGeometryHWOGL::Allocate( const vector<msMesh> &vMeshes )
{
	RageCompiledGeometrySWOGL::Allocate( vMeshes );
	BufferBound();
	
	GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nPositions );
	GLExt.glBufferDataARB( 
		GL_ARRAY_BUFFER_ARB, 
		GetTotalVertices()*sizeof(RageVector3), 
		NULL, 
		GL_STATIC_DRAW_ARB );
	AssertNoGLError();

	GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nTextureCoords );
	GLExt.glBufferDataARB( 
		GL_ARRAY_BUFFER_ARB, 
		GetTotalVertices()*sizeof(RageVector2), 
		NULL, 
		GL_STATIC_DRAW_ARB );
	AssertNoGLError();

	GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nNormals );
	GLExt.glBufferDataARB( 
		GL_ARRAY_BUFFER_ARB, 
		GetTotalVertices()*sizeof(RageVector3), 
		NULL, 
		GL_STATIC_DRAW_ARB );
	AssertNoGLError();

	GLExt.glBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, m_nTriangles );
	GLExt.glBufferDataARB( 
		GL_ELEMENT_ARRAY_BUFFER_ARB, 
		GetTotalTriangles()*sizeof(msTriangle), 
		NULL, 
		GL_STATIC_DRAW_ARB );
	AssertNoGLError();
}

void RageCompiledGeometryHWOGL::Change( const vector<msMesh> &vMeshes )
{
	RageCompiledGeometrySWOGL::Change( vMeshes );

	UploadData();
}

void RageCompiledGeometryHWOGL::Draw( int iMeshIndex ) const
{
	FlushGLErrors();

	const MeshInfo& meshInfo = m_vMeshInfo[iMeshIndex];
	if( !meshInfo.iVertexCount || !meshInfo.iTriangleCount )
		return;

	BufferBound();
	g_State.InvalidateArrays();
	glEnableClientState(GL_VERTEX_ARRAY);
	GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nPositions );
	AssertNoGLError();
	glVertexPointer(3, GL_FLOAT, 0, NULL );
	AssertNoGLError();

	glDisableClientState(GL_COLOR_ARRAY);

	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nTextureCoords );
	AssertNoGLError();
	glTexCoordPointer(2, GL_FLOAT, 0, NULL);
	AssertNoGLError();

	// TRICKY:  Don't bind and send normals if lighting is disabled.  This 
	// will save some effort transforming these values.
	GLboolean bLighting;
	glGetBooleanv( GL_LIGHTING, &bLighting );
	GLboolean bTextureGenS;
	glGetBooleanv( GL_TEXTURE_GEN_S, &bTextureGenS );
	GLboolean bTextureGenT;
	glGetBooleanv( GL_TEXTURE_GEN_T, &bTextureGenT );
	if( bLighting || bTextureGenS || bTextureGenT )
	{
		glEnableClientState(GL_NORMAL_ARRAY);
		GLExt.glBindBufferARB( GL_ARRAY_BUFFER_ARB, m_nNormals );
		AssertNoGLError();
		glNormalPointer(GL_FLOAT, 0, NULL);
		AssertNoGLError();
	}
	else
	{
		glDisableClientState(GL_NORMAL_ARRAY);
	}

	GLExt.glBindBufferARB( GL_ELEMENT_ARRAY_BUFFER_ARB, m_nTriangles );
	AssertNoGLError();

#define BUFFER_OFFSET(o) ((char*)(o))

	ASSERT( GLExt.glDrawRangeElements);
	GLExt.glDrawRangeElements( 
		GL_TRIANGLES, 
		meshInfo.iVertexStart,	// minimum array index contained in indices
		meshInfo.iVertexStart+meshInfo.iVertexCount-1,
					// maximum array index contained in indices
		meshInfo.iTriangleCount*3,	// number of elements to be rendered
		GL_UNSIGNED_SHORT,
		BUFFER_OFFSET(meshInfo.iTriangleStart*sizeof(msTriangle)) );
	AssertNoGLError();
}

RageCompiledGeometry* RageDisplay_OGL::CreateCompiledGeometry()
{
	if( GLExt.glGenBuffersARB )
		return new RageCompiledGeometryHWOGL;
	else
		return new RageCompiledGeometrySWOGL;
}

void RageDisplay_OGL::DeleteCompiledGeometry( RageCompiledGeometry* p )
{
	delete p;
}

void RageDisplay_OGL::DrawQuadsInternal( const RageSpriteVertex v[], int iNumVerts )
{
	TurnOffHardwareVBO();
	FlushTextureEnables();
	SendCurrentMatrices();

	SetupVertices( v, iNumVerts );
	glDrawArrays( GL_QUADS, 0, iNumVerts );
}

void RageDisplay_OGL::DrawQuadStripInternal( const RageSpriteVertex v[], int iNumVerts )
{
	TurnOffHardwareVBO();
	FlushTextureEnables();
	SendCurrentMatrices();

	SetupVertices( v, iNumVerts );
	glDrawArrays( GL_QUAD_STRIP, 0, iNumVerts );
}

void RageDisplay_OGL::DrawFanInternal( const RageSpriteVertex v[], int iNumVerts )
{
	TurnOffHardwareVBO();
	FlushTextureEnables();
	SendCurrentMatrices();

	SetupVertices( v, iNumVerts );
	glDrawArrays( GL_TRIANGLE_FAN, 0, iNumVerts );
}

void RageDisplay_OGL::DrawStripInternal( const RageSpriteVertex v[], int iNumVerts )
{
	TurnOffHardwareVBO();
	FlushTextureEnables();
	SendCurrentMatrices();

	SetupVertices( v, iNumVerts );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, iNumVerts );
}

void RageDisplay_OGL::DrawTrianglesInternal( const RageSpriteVertex v[], int iNumVerts )
{
	TurnOffHardwareVBO();
	FlushTextureEnables();
	SendCurrentMatrices();

	SetupVertices( v, iNumVerts );
	glDrawArrays( GL_TRIANGLES, 0, iNumVerts );
}

void RageDisplay_OGL::DrawCompiledGeometryInternal( const RageCompiledGeometry *p, int iMeshIndex )
{
	TurnOffHardwareVBO();
	FlushTextureEnables();
	SendCurrentMatrices();

	p->Draw( iMeshIndex );
}

void RageDisplay_OGL::DrawLineStripInternal( const RageSpriteVertex v[], int iNumVerts, float LineWidth )
{
	TurnOffHardwareVBO();

	if( !GetVideoModeParams().bSmoothLines )
	{
		/* Fall back on the generic polygon-based line strip. */
		RageDisplay::DrawLineStripInternal(v, iNumVerts, LineWidth );
		return;
	}

	FlushTextureEnables();
	SendCurrentMatrices();

	/* Draw a nice AA'd line loop.  One problem with this is that point and line
	 * sizes don't always precisely match, which doesn't look quite right.
	 * It's worth it for the AA, though. */
	glEnable(GL_LINE_SMOOTH);

	/* Our line width is wrt the regular internal SCREEN_WIDTHxSCREEN_HEIGHT screen,
	 * but these width functions actually want raster sizes (that is, actual pixels).
	 * Scale the line width and point size by the average ratio of the scale. */
	int iWidth, iHeight;
	wind->GetFramebufferSize( iWidth, iHeight );
	float WidthVal = float(iWidth) / SCREEN_WIDTH;
	float HeightVal = float(iHeight) / SCREEN_HEIGHT;
	LineWidth *= (WidthVal + HeightVal) / 2;

	/* Clamp the width to the hardware max for both lines and points (whichever
	 * is more restrictive). */
	LineWidth = clamp(LineWidth, g_line_range[0], g_line_range[1]);
	LineWidth = clamp(LineWidth, g_point_range[0], g_point_range[1]);

	/* Hmm.  The granularity of lines and points might be different; for example,
	 * if lines are .5 and points are .25, we might want to snap the width to the
	 * nearest .5, so the hardware doesn't snap them to different sizes.  Does it
	 * matter? */
	glLineWidth(LineWidth);

	/* Draw the line loop: */
	SetupVertices( v, iNumVerts );
	glDrawArrays( GL_LINE_STRIP, 0, iNumVerts );

	glDisable(GL_LINE_SMOOTH);

	/* Round off the corners.  This isn't perfect; the point is sometimes a little
	 * larger than the line, causing a small bump on the edge.  Not sure how to fix
	 * that. */
	glPointSize(LineWidth);

	/* Hack: if the points will all be the same, we don't want to draw
	 * any points at all, since there's nothing to connect.  That'll happen
	 * if both scale factors in the matrix are ~0.  (Actually, I think
	 * it's true if two of the three scale factors are ~0, but we don't
	 * use this for anything 3d at the moment anyway ...)  This is needed
	 * because points aren't scaled like regular polys--a zero-size point
	 * will still be drawn. */
	RageMatrix mat;
	glGetFloatv( GL_MODELVIEW_MATRIX, (float*)mat );

	if(mat.m[0][0] < 1e-5 && mat.m[1][1] < 1e-5) 
		return;

	glEnable(GL_POINT_SMOOTH);

	SetupVertices( v, iNumVerts );
	glDrawArrays( GL_POINTS, 0, iNumVerts );

	glDisable(GL_POINT_SMOOTH);
}

static void SetActiveTextureUnit( int iUnit )
{
	if( g_State.iActiveUnit == iUnit )
		return;
	GLExt.glActiveTextureARB( iUnit == 0? GL_TEXTURE0_ARB: GL_TEXTURE1_ARB );
	g_State.iActiveUnit = iUnit;
}

/* Bind a texture to the active unit. */
static void BindTexture( unsigned uTexHandle )
{
	const int iUnit = g_State.ActiveUnit();
	if( iUnit != -1 && g_State.iBoundTexture[iUnit] == int64_t(uTexHandle) )
		return;
	glBindTexture( GL_TEXTURE_2D, uTexHandle );
	if( iUnit != -1 )
		g_State.iBoundTexture[iUnit] = uTexHandle;
}

/* Returns false if the texture env mode of the active unit is already iMode. */
static bool SetTexEnvState( int iMode )
{
	const int iUnit = g_State.ActiveUnit();
	if( iUnit == -1 )
		return true;
	if( g_State.iTexEnv[iUnit] == iMode )
		return false;
	g_State.iTexEnv[iUnit] = iMode;
	return true;
}

void RageDisplay_OGL::ClearAllTextures()
{
	/* This only changes what FlushTextureEnables will set, so no unit needs to
	 * be made active. */
	for( int i=0; i<MAX_TEXTURE_UNITS; i++ )
		if( i == 0 || GLExt.glActiveTextureARB )
			g_State.iTexture2DWanted[i] = 0;

	// HACK:  Reset the active texture to 0.
	// TODO:  Change all texture functions to take a stage number.
	if( GLExt.glActiveTextureARB )
		SetActiveTextureUnit( 0 );
}

void RageDisplay_OGL::SetTexture( int iTextureUnitIndex, RageTexture* pTexture )
{
	if( GLExt.glActiveTextureARB == NULL )
	{
		// multitexture isn't supported.  Ignore all textures except for 0.
		if( iTextureUnitIndex != 0 )
			return;
	}
	else
	{
		switch( iTextureUnitIndex )
		{
		case 0:
		case 1:
			SetActiveTextureUnit( iTextureUnitIndex );
			break;
		default:
			ASSERT(0);
		}
	}

	g_State.iTexture2DWanted[iTextureUnitIndex] = pTexture != NULL;

	if( pTexture )
		BindTexture( pTexture->GetTexHandle() );
}

/* Turn GL_TEXTURE_2D on or off for each unit as SetTexture and ClearAllTextures
 * last asked, before drawing. */
static void FlushTextureEnables()
{
	const int iUnits = GLExt.glActiveTextureARB? MAX_TEXTURE_UNITS: 1;
	const int iActiveUnit = g_State.iActiveUnit;
	for( int i = 0; i < iUnits; ++i )
	{
		const int bOn = g_State.iTexture2DWanted[i];
		if( bOn == -1 || bOn == g_State.iTexture2D[i] )
			continue;

		if( GLExt.glActiveTextureARB )
			SetActiveTextureUnit( i );
		if( bOn )
			glEnable( GL_TEXTURE_2D );
		else
			glDisable( GL_TEXTURE_2D );
		g_State.iTexture2D[i] = bOn;
	}

	if( GLExt.glActiveTextureARB && iActiveUnit != -1 )
		SetActiveTextureUnit( iActiveUnit );
}
void RageDisplay_OGL::SetTextureModeModulate()
{
	if( SetTexEnvState(TEXENV_MODULATE) )
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

void RageDisplay_OGL::SetTextureModeGlow(GlowMode m)
{
	if(m == GLOW_WHITEN && !g_bEXT_texture_env_combine)
		m = GLOW_BRIGHTEN; /* we can't do GLOW_WHITEN */

	switch(m)
	{
	case GLOW_BRIGHTEN:
		glBlendFunc( GL_SRC_ALPHA, GL_ONE );
		return;

	case GLOW_WHITEN:
		if( !SetTexEnvState(TEXENV_GLOW_WHITEN) )
			return;

		/* Source color is the diffuse color only: */
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
		glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_COMBINE_RGB_EXT), GL_REPLACE);
		glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_SOURCE0_RGB_EXT), GL_PRIMARY_COLOR_EXT);

		/* Source alpha is texture alpha * diffuse alpha: */
		glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_COMBINE_ALPHA_EXT), GL_MODULATE);
		glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_OPERAND0_ALPHA_EXT), GL_SRC_ALPHA);
		glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_SOURCE0_ALPHA_EXT), GL_PRIMARY_COLOR_EXT);
		glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_OPERAND1_ALPHA_EXT), GL_SRC_ALPHA);
		glTexEnvi(GL_TEXTURE_ENV, GLenum(GL_SOURCE1_ALPHA_EXT), GL_TEXTURE);
		return;
	}
}

void RageDisplay_OGL::SetTextureModeAdd()
{
	if( SetTexEnvState(TEXENV_ADD) )
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
}

void RageDisplay_OGL::SetTextureFiltering( bool b )
{

}

void RageDisplay_OGL::SetBlendMode( BlendMode mode )
{
	glEnable(GL_BLEND);

	glDepthRange( 0.05, 1.0 );
	switch( mode )
	{
	case BLEND_NORMAL:
		glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		break;
	case BLEND_ADD:
		glBlendFunc( GL_SRC_ALPHA, GL_ONE );
		break;
	case BLEND_NO_EFFECT:
		/* XXX: Would it be faster and have the same effect to say glDisable(GL_COLOR_WRITEMASK)? */
		glBlendFunc( GL_ZERO, GL_ONE );

		/* This is almost exclusively used to draw masks to the Z-buffer.  Make sure
		 * masks always win the depth test when drawn at the same position. */
		glDepthRange( 0.0, 0.95 );	// this bias is bigger than it needs to be to handle the coplanar case
		break;
	default:
		ASSERT(0);
	}
}

void RageDisplay_OGL::SetBlending( bool b )
{
	if( b )
		glEnable( GL_BLEND );
	else
		glDisable( GL_BLEND );
}

bool RageDisplay_OGL::IsZWriteEnabled() const
{
	if( g_State.iZWrite != -1 )
		return g_State.iZWrite != 0;
	bool a;
	glGetBooleanv( GL_DEPTH_WRITEMASK, (unsigned char*)&a );
	return a;
}

bool RageDisplay_OGL::IsZTestEnabled() const
{
	if( g_State.iDepthFunc != -1 )
		return g_State.iDepthFunc != GL_ALWAYS;
	GLenum a;
	glGetIntegerv( GL_DEPTH_FUNC, (GLint*)&a );
	return a != GL_ALWAYS;
}

void RageDisplay_OGL::ClearZBuffer()
{
	bool write = IsZWriteEnabled();
	SetZWrite( true );
    glClear( GL_DEPTH_BUFFER_BIT );
	SetZWrite( write );
}

void RageDisplay_OGL::SetZWrite( bool b )
{
	if( g_State.iZWrite == int(b) )
		return;
	glDepthMask( b );
	g_State.iZWrite = b;
}

void RageDisplay_OGL::SetZTestMode( ZTestMode mode )
{
	if( g_State.iDepthTest != 1 )
	{
		glEnable( GL_DEPTH_TEST );
		g_State.iDepthTest = 1;
	}
	GLenum func;
	switch( mode )
	{
	case ZTEST_OFF:				func = GL_ALWAYS;	break;
	case ZTEST_WRITE_ON_PASS:	func = GL_LEQUAL;	break;
	case ZTEST_WRITE_ON_FAIL:	func = GL_GREATER;	break;
	default:	ASSERT( 0 );	return;
	}
	if( g_State.iDepthFunc == int(func) )
		return;
	glDepthFunc( func );
	g_State.iDepthFunc = func;
}

void RageDisplay_OGL::SetTextureWrapping( bool b )
{
	/* This sets the wrap mode of the texture bound to the active unit. */
	const int64_t iTexture = g_State.ActiveTexture();
	if( iTexture != -1 && g_State.Wrap(unsigned(iTexture)) == int(b) )
		return;

	GLenum mode = b ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mode );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mode );
	if( iTexture != -1 )
		g_State.Wrap(unsigned(iTexture)) = b;
}

void RageDisplay_OGL::SetMaterial( 
	const RageColor &emissive,
	const RageColor &ambient,
	const RageColor &diffuse,
	const RageColor &specular,
	float shininess
	)
{
	// TRICKY:  If lighting is off, then setting the material 
	// will have no effect.  Even if lighting is off, we still
	// want Models to have basic color and transparency.
	// We can do this fake lighting by setting the vertex color.

	GLboolean bLighting;
	glGetBooleanv( GL_LIGHTING, &bLighting );

	if( bLighting )
	{
		glMaterialfv( GL_FRONT, GL_EMISSION, emissive );
		glMaterialfv( GL_FRONT, GL_AMBIENT, ambient );
		glMaterialfv( GL_FRONT, GL_DIFFUSE, diffuse );
		glMaterialfv( GL_FRONT, GL_SPECULAR, specular );
		glMaterialf( GL_FRONT, GL_SHININESS, shininess );
	}
	else
	{
		glColor4fv( emissive + ambient + diffuse );
	}
}

void RageDisplay_OGL::SetLighting( bool b )
{
	if( b )	glEnable( GL_LIGHTING );
	else	glDisable( GL_LIGHTING );
}

void RageDisplay_OGL::SetLightOff( int index )
{
	glDisable( GL_LIGHT0+index );
}
void RageDisplay_OGL::SetLightDirectional( 
	int index, 
	const RageColor &ambient, 
	const RageColor &diffuse, 
	const RageColor &specular, 
	const RageVector3 &dir )
{
	// Light coordinates are transformed by the modelview matrix, but
	// we are being passed in world-space coords.
	/* (SendCurrentMatrices used to always leave GL_TEXTURE current, so this
	 * has really always pushed the texture matrix.) */
	if( g_State.bMatricesSent )
		SetMatrixMode( GL_TEXTURE );
	glPushMatrix();
	glLoadIdentity();

	glEnable( GL_LIGHT0+index );
	glLightfv(GL_LIGHT0+index, GL_AMBIENT, ambient);
	glLightfv(GL_LIGHT0+index, GL_DIFFUSE, diffuse);
	glLightfv(GL_LIGHT0+index, GL_SPECULAR, specular);
	float position[4] = {dir.x, dir.y, dir.z, 0};
	glLightfv(GL_LIGHT0+index, GL_POSITION, position);

	glPopMatrix();
}

void RageDisplay_OGL::SetCullMode( CullMode mode )
{
	switch( mode )
	{
	case CULL_BACK:
	case CULL_FRONT:
	{
		if( g_State.iCullMode != 1 )
		{
			glEnable( GL_CULL_FACE );
			g_State.iCullMode = 1;
		}
		const GLenum face = mode == CULL_BACK? GL_BACK: GL_FRONT;
		if( g_State.iCullFace != int(face) )
		{
			glCullFace( face );
			g_State.iCullFace = face;
		}
		break;
	}
	case CULL_NONE:
		if( g_State.iCullMode != 0 )
		{
			glDisable( GL_CULL_FACE );
			g_State.iCullMode = 0;
		}
		break;
	default:
		ASSERT(0);
	}
}

const RageDisplay::PixelFormatDesc *RageDisplay_OGL::GetPixelFormatDesc(PixelFormat pf) const
{
	ASSERT( pf < NUM_PIX_FORMATS );
	return &PIXEL_FORMAT_DESC[pf];
}

void RageDisplay_OGL::DeleteTexture( unsigned uTexHandle )
{
	unsigned int uTexID = uTexHandle;

	FlushGLErrors();
	glDeleteTextures(1,reinterpret_cast<GLuint*>(&uTexID));

	/* Units the texture was bound to are now bound to texture 0, and the name
	 * may be reused. */
	for( int i = 0; i < MAX_TEXTURE_UNITS; ++i )
		if( g_State.iBoundTexture[i] == int64_t(uTexHandle) )
			g_State.iBoundTexture[i] = 0;
	if( uTexHandle < g_State.TextureWrap.size() )
		g_State.TextureWrap[uTexHandle] = -1;

	AssertNoGLError();
}


RageDisplay::PixelFormat RageDisplay_OGL::GetImgPixelFormat( RageSurface* &img, bool &FreeImg, int width, int height )
{
	PixelFormat pixfmt = FindPixelFormat( img->format->BitsPerPixel, img->format->Rmask, img->format->Gmask, img->format->Bmask, img->format->Amask );
	
	if( pixfmt == NUM_PIX_FORMATS || !SupportsSurfaceFormat(pixfmt) )
	{
		/* The source isn't in a supported, known pixel format.  We need to convert
		 * it ourself.  Just convert it to RGBA8, and let OpenGL convert it back
		 * down to whatever the actual pixel format is.  This is a very slow code
		 * path, which should almost never be used. */
		pixfmt = FMT_RGBA8;
		ASSERT( SupportsSurfaceFormat(pixfmt) );

		const PixelFormatDesc *pfd = DISPLAY->GetPixelFormatDesc(pixfmt);

		RageSurface *imgconv = CreateSurface( width, height,
			pfd->bpp, pfd->masks[0], pfd->masks[1], pfd->masks[2], pfd->masks[3] );
		RageSurfaceUtils::Blit( img, imgconv, width, height );
		img = imgconv;
		FreeImg = true;
	}
	else
		FreeImg = false;

	return pixfmt;
}

unsigned RageDisplay_OGL::CreateTexture( 
	PixelFormat pixfmt,
	RageSurface* img,
	bool bGenerateMipMaps )
{
	ASSERT( pixfmt < NUM_PIX_FORMATS );
	ASSERT( img->w == power_of_two(img->w) && img->h == power_of_two(img->h) );


	/* Find the pixel format of the image we've been given. */
	bool FreeImg;
	PixelFormat imgpixfmt = GetImgPixelFormat( img, FreeImg, img->w, img->h );

	GLenum glTexFormat = GL_PIXFMT_INFO[pixfmt].internalfmt;
	GLenum glImageFormat = GL_PIXFMT_INFO[imgpixfmt].format;
	GLenum glImageType = GL_PIXFMT_INFO[imgpixfmt].type;

	// HACK:  OpenGL 1.2 types aren't available in GLU 1.3.  Don't call GLU for mip
	// mapping if we're using an OGL 1.2 type and don't have >= GLU 1.3.
	// http://pyopengl.sourceforge.net/documentation/manual/gluBuild2DMipmaps.3G.html
	if( bGenerateMipMaps && g_gluVersion < 13 )
	{
		switch( pixfmt )
		{
		// OpenGL 1.1 types
		case FMT_RGBA8:
		case FMT_RGB8:
		case FMT_PAL:
		case FMT_BGR8:
			break;
		// OpenGL 1.2 types
		default:
			LOG->Trace( "Can't generate mipmaps for type %s because GLU version %.1f is too old.", GLToString(glImageType).c_str(), g_gluVersion/10.f );
			bGenerateMipMaps = false;
			break;
		}
	}

	// allocate OpenGL texture resource
	unsigned int uTexHandle;
	glGenTextures(1, reinterpret_cast<GLuint*>(&uTexHandle));
	ASSERT(uTexHandle);
	g_State.Wrap( uTexHandle ) = -1;
	
	BindTexture( uTexHandle );
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	GLint minFilter;
	if( bGenerateMipMaps )
	{
		if( wind->GetVideoModeParams().bTrilinearFiltering )
			minFilter = GL_LINEAR_MIPMAP_LINEAR;
		else
			minFilter = GL_LINEAR_MIPMAP_NEAREST;
	}
	else
	{
		minFilter = GL_LINEAR;
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);

	if( wind->GetVideoModeParams().bAnisotropicFiltering &&
		HasExtension("GL_EXT_texture_filter_anisotropic") )
	{
		GLfloat largest_supported_anisotropy;
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &largest_supported_anisotropy);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, largest_supported_anisotropy);
	}

	SetTextureWrapping( false );

	glPixelStorei(GL_UNPACK_ROW_LENGTH, img->pitch / img->format->BytesPerPixel);


	if(pixfmt == FMT_PAL)
	{
		/* The image is paletted.  Let's try to set up a paletted texture. */
		GLubyte palette[256*4];
		memset(palette, 0, sizeof(palette));
		int p = 0;
		/* Copy the palette to the simple, unpacked data OGL expects. If
		 * we're color keyed, change it over as we go. */
		for(int i = 0; i < img->format->palette->ncolors; ++i)
		{
			palette[p++] = img->format->palette->colors[i].r;
			palette[p++] = img->format->palette->colors[i].g;
			palette[p++] = img->format->palette->colors[i].b;
			palette[p++] = img->format->palette->colors[i].a;
		}

		/* Set the palette. */
		GLExt.glColorTableEXT(GL_TEXTURE_2D, GL_RGBA8, 256, GL_RGBA, GL_UNSIGNED_BYTE, palette);

		GLint RealFormat = 0;
		GLExt.glGetColorTableParameterivEXT(GL_TEXTURE_2D, GL_COLOR_TABLE_FORMAT, &RealFormat);
		ASSERT( RealFormat == GL_RGBA8);	/* This is a case I don't expect to happen. */
	}


	
	FlushGLErrors();

	if( bGenerateMipMaps )
	{
		GLenum error = gluBuild2DMipmaps(
			GL_TEXTURE_2D, glTexFormat, 
			img->w, img->h,
			glImageFormat, glImageType, img->pixels );

		if( error != 0 )
		{
			ostringstream s;
			s << "gluBuild2DMipmaps(format " << GLToString(glTexFormat) <<
				 ", w " << img->w << ", h " <<  img->h <<
				 ", format " << GLToString(glImageFormat) <<
				 ", type " << GLToString(glImageType) <<
				 "): " << gluErrorString(error);
			LOG->Trace( "%s", s.str().c_str() );

			ASSERT(0);
		}
	}
	else
	{
		glTexImage2D(
			GL_TEXTURE_2D, 0, glTexFormat, 
			img->w, img->h, 0,
			glImageFormat, glImageType, img->pixels);
		
		{
			ostringstream s;
			s << "glTexImage2D(format " << GLToString(glTexFormat) <<
					", " << img->w << "x" <<  img->h <<
					", format " << GLToString(glImageFormat) <<
					", type " << GLToString(glImageType) <<
					", pixfmt " << pixfmt <<
					", imgpixfmt " << imgpixfmt;
			LOG->Trace( "%s", s.str().c_str() );
		}
		GLenum error = glGetError();
		if( error != GL_NO_ERROR )
		{
			ostringstream s;
			s << "glTexImage2D(format " << GLToString(glTexFormat) <<
				 ", " << img->w << "x" <<  img->h <<
				 ", format " << GLToString(glImageFormat) <<
				 ", type " << GLToString(glImageType) <<
				 ", pixfmt " << pixfmt <<
				 ", imgpixfmt " << imgpixfmt <<
				 "): " << GLToString(error);
			LOG->Trace( "%s", s.str().c_str() );

			ASSERT(0);
		}
	}



	/* Sanity check: */
	if( pixfmt == FMT_PAL )
	{
		GLint size = 0;
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GLenum(GL_TEXTURE_INDEX_SIZE_EXT), &size);
		if(size != 8)
			RageException::Throw("Thought paletted textures worked, but they don't.");
	}

	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glFlush();

	if( FreeImg )
		delete img;
	return uTexHandle;
}


void RageDisplay_OGL::UpdateTexture( 
	unsigned uTexHandle, 
	RageSurface* img,
	int xoffset, int yoffset, int width, int height )
{
	BindTexture( uTexHandle );

	bool FreeImg;
	PixelFormat pixfmt = GetImgPixelFormat( img, FreeImg, width, height );

	glPixelStorei(GL_UNPACK_ROW_LENGTH, img->pitch / img->format->BytesPerPixel);

//	GLenum glTexFormat = GL_PIXFMT_INFO[pixfmt].internalfmt;
	GLenum glImageFormat = GL_PIXFMT_INFO[pixfmt].format;
	GLenum glImageType = GL_PIXFMT_INFO[pixfmt].type;

	glTexSubImage2D(GL_TEXTURE_2D, 0,
		xoffset, yoffset,
		width, height,
		glImageFormat, glImageType, img->pixels);

	/* Must unset PixelStore when we're done! */
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glFlush();

	if( FreeImg )
		delete img;
}

void RageDisplay_OGL::SetPolygonMode( PolygonMode pm )
{
	GLenum m;
	switch( pm )
	{
	case POLYGON_FILL:	m = GL_FILL; break;
	case POLYGON_LINE:	m = GL_LINE; break;
	default:	ASSERT(0);	return;
	}
	glPolygonMode( GL_FRONT_AND_BACK, m );
}

void RageDisplay_OGL::SetLineWidth( float fWidth )
{
	glLineWidth( fWidth );
}

CString RageDisplay_OGL::GetTextureDiagnostics( unsigned id ) const
{
	return "";
}

void RageDisplay_OGL::SetAlphaTest( bool b )
{
	glAlphaFunc( GL_GREATER, 0.01f );
	if( b )
		glEnable( GL_ALPHA_TEST );
	else
		glDisable( GL_ALPHA_TEST );
}

RageMatrix RageDisplay_OGL::GetOrthoMatrix( float l, float r, float b, float t, float zn, float zf )
{
	RageMatrix m(
		2/(r-l),      0,            0,           0,
		0,            2/(t-b),      0,           0,
		0,            0,            -2/(zf-zn),   0,
		-(r+l)/(r-l), -(t+b)/(t-b), -(zf+zn)/(zf-zn),  1 );
	return m;
}


/*
 * Although we pair texture formats (eg. GL_RGB8) and surface formats
 * (pairs of eg. GL_RGB8,GL_UNSIGNED_SHORT_5_5_5_1), it's possible for
 * a format to be supported for a texture format but not a surface
 * format.  This is abstracted, so you don't need to know about this
 * as a user calling CreateTexture.
 *
 * One case of this is if packed pixels aren't supported.  We can still
 * use 16-bit color modes, but we have to send it in 32-bit.  Almost
 * everything supports packed pixels.
 *
 * Another case of this is incomplete packed pixels support.  Some implementations
 * neglect GL_UNSIGNED_SHORT_*_REV. 
 */
bool RageDisplay_OGL::SupportsSurfaceFormat( PixelFormat pixfmt )
{
	switch( GL_PIXFMT_INFO[pixfmt].type )
	{
	case GL_UNSIGNED_SHORT_1_5_5_5_REV:
		return g_bGL_EXT_bgra && g_bReversePackedPixelsWorks;
	default:
		return true;
	}
}


bool RageDisplay_OGL::SupportsTextureFormat( PixelFormat pixfmt, bool realtime )
{
	/* If we support a pixfmt for texture formats but not for surface formats, then
	 * we'll have to convert the texture to a supported surface format before uploading.
	 * This is too slow for dynamic textures. */
	if( realtime && !SupportsSurfaceFormat( pixfmt ) )
		return false;

	switch( GL_PIXFMT_INFO[pixfmt].format )
	{
	case GL_COLOR_INDEX:
		return GLExt.glColorTableEXT && GLExt.glGetColorTableParameterivEXT;
	case GL_BGR:
	case GL_BGRA:
		return g_bGL_EXT_bgra;
	default:
		return true;
	}
}

void RageDisplay_OGL::SetSphereEnironmentMapping( bool b )
{
	if( b )
	{
		glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
		glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
		glEnable(GL_TEXTURE_GEN_S);
		glEnable(GL_TEXTURE_GEN_T);
	}
	else
	{
		glDisable(GL_TEXTURE_GEN_S);
		glDisable(GL_TEXTURE_GEN_T);
	}
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


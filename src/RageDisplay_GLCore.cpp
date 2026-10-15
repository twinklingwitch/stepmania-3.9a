#include "global.h"

/*
 * RageDisplay_GLCore: drawing with OpenGL 3.3 core profile (4.1 on macOS).
 *
 * RageDisplay_OGL draws with the OpenGL 1.x fixed function pipeline: texture
 * environment modes, GL's own matrix stacks and lighting, vertex arrays in our
 * memory and GL_QUADS.  Current drivers only keep that working by translating
 * it, and macOS runs it on top of Metal; the translation costs CPU time on
 * every state change and draw.  This draws the same pictures with one small
 * shader program that does what the fixed function pipeline did for the
 * features we use, vertices streamed through a buffer object, and quads as
 * indexed triangles.
 *
 * Draws are queued like in RageDisplay_OGL: vertices are moved by the world
 * matrix (and texture coordinates by the texture matrix) here, and runs of
 * draws that share their GL state go out in one draw call.  Since texturing is
 * done by the shader, whether a draw is textured and how the texture combines
 * with the color (modulate, add, glow) travels with each vertex instead of
 * being GL state, so untextured quads and glow passes don't break up a run.
 * Lighting and sphere mapping need eye space normals, so those draws send the
 * world matrix to GL instead.
 *
 * State is applied lazily: the Set* calls record what's wanted, and each draw
 * brings GL up to date, sending out the queue first if anything it depends on
 * has to change.
 */

/* Keep the system glext.h out; we declare what we use ourselves, so this builds
 * the same against any platform's GL headers. */
#define __glext_h_
#define GL_GLEXT_LEGACY

#if !defined(DARWIN)
# include <GL/gl.h>
#else
# include <OpenGL/gl.h>
#endif
#undef __glext_h_

#include "RageDisplay.h"
#include "RageDisplay_GLCore.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "RageException.h"
#include "RageTexture.h"
#include "RageTextureManager.h"
#include "RageMath.h"
#include "RageTypes.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "GameConstantsAndTypes.h"
#include "arch/arch.h"
#include "arch/LowLevelWindow/LowLevelWindow.h"

#include <set>
#include <stddef.h>

#ifndef APIENTRY
#define APIENTRY
#endif

/* Enums from OpenGL 1.2 to 3.3. */
#define GLC_ARRAY_BUFFER				0x8892
#define GLC_ELEMENT_ARRAY_BUFFER		0x8893
#define GLC_STREAM_DRAW					0x88E0
#define GLC_STATIC_DRAW					0x88E4
#define GLC_DYNAMIC_DRAW				0x88E8
#define GLC_FRAGMENT_SHADER				0x8B30
#define GLC_VERTEX_SHADER				0x8B31
#define GLC_COMPILE_STATUS				0x8B81
#define GLC_LINK_STATUS					0x8B82
#define GLC_INFO_LOG_LENGTH				0x8B84
#define GLC_SHADING_LANGUAGE_VERSION	0x8B8C
#define GLC_TEXTURE0					0x84C0
#define GLC_MAP_WRITE_BIT				0x0002
#define GLC_MAP_INVALIDATE_RANGE_BIT	0x0004
#define GLC_MAP_UNSYNCHRONIZED_BIT		0x0020
#define GLC_NUM_EXTENSIONS				0x821D
#define GLC_MAJOR_VERSION				0x821B
#define GLC_MINOR_VERSION				0x821C
#define GLC_CLAMP_TO_EDGE				0x812F
#define GLC_BGR							0x80E0
#define GLC_BGRA						0x80E1
#define GLC_UNSIGNED_SHORT_4_4_4_4		0x8033
#define GLC_UNSIGNED_SHORT_5_5_5_1		0x8034
#define GLC_UNSIGNED_SHORT_1_5_5_5_REV	0x8366
#define GLC_RGB5						0x8050
#define GLC_RGB8						0x8051
#define GLC_RGBA4						0x8056
#define GLC_RGB5_A1						0x8057
#define GLC_RGBA8						0x8058
#define GLC_TEXTURE_MAX_ANISOTROPY		0x84FE
#define GLC_MAX_TEXTURE_MAX_ANISOTROPY	0x84FF
#define GLC_ALIASED_LINE_WIDTH_RANGE	0x846E
#define GLC_HALF_FLOAT					0x140B

typedef char GLCchar;
typedef ptrdiff_t GLCsizeiptr;
typedef ptrdiff_t GLCintptr;

/* Entry points past OpenGL 1.1, which have to be looked up at runtime (Windows
 * only exports 1.1). */
#define GLC_FUNCTIONS \
	F( void, ActiveTexture, (GLenum) ) \
	F( void, GenBuffers, (GLsizei, GLuint *) ) \
	F( void, DeleteBuffers, (GLsizei, const GLuint *) ) \
	F( void, BindBuffer, (GLenum, GLuint) ) \
	F( void, BufferData, (GLenum, GLCsizeiptr, const void *, GLenum) ) \
	F( void, BufferSubData, (GLenum, GLCintptr, GLCsizeiptr, const void *) ) \
	F( void *, MapBufferRange, (GLenum, GLCintptr, GLCsizeiptr, GLbitfield) ) \
	F( GLboolean, UnmapBuffer, (GLenum) ) \
	F( void, GenVertexArrays, (GLsizei, GLuint *) ) \
	F( void, DeleteVertexArrays, (GLsizei, const GLuint *) ) \
	F( void, BindVertexArray, (GLuint) ) \
	F( void, EnableVertexAttribArray, (GLuint) ) \
	F( void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *) ) \
	F( void, VertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat) ) \
	F( GLuint, CreateShader, (GLenum) ) \
	F( void, ShaderSource, (GLuint, GLsizei, const GLCchar * const *, const GLint *) ) \
	F( void, CompileShader, (GLuint) ) \
	F( void, GetShaderiv, (GLuint, GLenum, GLint *) ) \
	F( void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLCchar *) ) \
	F( void, DeleteShader, (GLuint) ) \
	F( GLuint, CreateProgram, (void) ) \
	F( void, AttachShader, (GLuint, GLuint) ) \
	F( void, BindAttribLocation, (GLuint, GLuint, const GLCchar *) ) \
	F( void, BindFragDataLocation, (GLuint, GLuint, const GLCchar *) ) \
	F( void, LinkProgram, (GLuint) ) \
	F( void, GetProgramiv, (GLuint, GLenum, GLint *) ) \
	F( void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLCchar *) ) \
	F( void, UseProgram, (GLuint) ) \
	F( GLint, GetUniformLocation, (GLuint, const GLCchar *) ) \
	F( void, Uniform1i, (GLint, GLint) ) \
	F( void, Uniform1f, (GLint, GLfloat) ) \
	F( void, Uniform3fv, (GLint, GLsizei, const GLfloat *) ) \
	F( void, Uniform4fv, (GLint, GLsizei, const GLfloat *) ) \
	F( void, UniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *) ) \
	F( void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *) ) \
	F( void, GenerateMipmap, (GLenum) ) \
	F( const GLubyte *, GetStringi, (GLenum, GLuint) ) \
	F( void, DrawElementsBaseVertex, (GLenum, GLsizei, GLenum, const void *, GLint) )

namespace
{
	struct GLCoreFunctions
	{
#define F( ret, name, args ) typedef ret (APIENTRY *name##_t) args; name##_t name;
		GLC_FUNCTIONS
#undef F
	};
	GLCoreFunctions gl;

	LowLevelWindow *g_pWind = NULL;

	/* Bumped for each new GL context; objects made for an older one are gone. */
	unsigned g_iContextGeneration = 0;

	bool g_bAnisotropic = false;
	float g_fLineWidthRange[2] = { 1, 1 };
}

static void LoadFunctions()
{
	const char *szMissing = NULL;
#define F( ret, name, args ) \
	gl.name = (GLCoreFunctions::name##_t) g_pWind->GetProcAddress( "gl" #name ); \
	if( gl.name == NULL && szMissing == NULL ) szMissing = "gl" #name;
	GLC_FUNCTIONS
#undef F
	if( szMissing )
		RageException::ThrowNonfatal( "OpenGL function %s is missing", szMissing );
}

static void FlushGLErrors()
{
	while( glGetError() != GL_NO_ERROR )
		;
}

/*
 * Pixel formats: the same as RageDisplay_OGL, without paletted textures, which
 * core OpenGL doesn't have (they're converted before they get here).
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

struct GLCorePixFmt
{
	GLenum internalfmt, format, type;
};
static const GLCorePixFmt GL_PIXFMT[RageDisplay::NUM_PIX_FORMATS] = {
	{ GLC_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE },
	{ GLC_RGBA4, GL_RGBA, GLC_UNSIGNED_SHORT_4_4_4_4 },
	{ GLC_RGB5_A1, GL_RGBA, GLC_UNSIGNED_SHORT_5_5_5_1 },
	{ GLC_RGB5, GL_RGBA, GLC_UNSIGNED_SHORT_5_5_5_1 },
	{ GLC_RGB8, GL_RGB, GL_UNSIGNED_BYTE },
	{ 0, 0, 0 },	// paletted: not supported
	{ GLC_RGB8, GLC_BGR, GL_UNSIGNED_BYTE },
	{ GLC_RGB5_A1, GLC_BGRA, GLC_UNSIGNED_SHORT_1_5_5_5_REV },
};

static void FixLilEndian()
{
#if defined(ENDIAN_LITTLE)
	static bool bInitialized = false;
	if( bInitialized )
		return;
	bInitialized = true;

	/* OpenGL and RageSurface handle byte formats differently; flip the masks of
	 * the byte formats to make them line up. */
	for( int i = 0; i < RageDisplay::NUM_PIX_FORMATS; ++i )
	{
		RageDisplay::PixelFormatDesc &pf = PIXEL_FORMAT_DESC[i];
		if( GL_PIXFMT[i].type != GL_UNSIGNED_BYTE || pf.bpp == 8 )
			continue;

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

/*
 * The shaders.  They do what the fixed function pipeline did with the state
 * RageDisplay_OGL set:
 *
 * - Texture unit modes (glTexEnv): modulate, add, and "glow" (GL_COMBINE: the
 *   vertex color, with the texture's alpha).  Unit 0's mode comes with each
 *   vertex (0 = not textured); unit 1, only used by models, is a uniform.
 * - The alpha test (GL_GREATER 0.01).
 * - One directional light with GL's lighting equation, the default scene
 *   ambient, no GL_NORMALIZE and no color material, like we had it.
 * - Sphere map texture coordinate generation, per unit.
 * - Texture matrices, per unit.  Unit 1 has no texture coordinates of its own,
 *   so it gets GL's default (0,0,0,1), as it did.
 */
static const char *g_szVertexShader =
"#version 330 core\n"
"uniform mat4 uProjection;\n"
"uniform mat4 uModelView;\n"
"uniform mat3 uNormalMatrix;\n"
"uniform mat4 uTexMatrix0;\n"
"uniform mat4 uTexMatrix1;\n"
"uniform int uSphere0;\n"
"uniform int uSphere1;\n"
"uniform int uLighting;\n"
"uniform int uLight0;\n"
"uniform vec4 uMatEmissive;\n"
"uniform vec4 uMatAmbient;\n"
"uniform vec4 uMatDiffuse;\n"
"uniform vec4 uMatSpecular;\n"
"uniform float uMatShininess;\n"
"uniform vec4 uLightAmbient;\n"
"uniform vec4 uLightDiffuse;\n"
"uniform vec4 uLightSpecular;\n"
"uniform vec3 uLightDir;\n"
"in vec4 aPosition;\n"
"in vec3 aNormal;\n"
"in vec4 aTexCoord;\n"
"in vec4 aColor;\n"
"in vec4 aMode;\n"
"out vec4 vColor;\n"
"out vec4 vTexCoord0;\n"
"out vec4 vTexCoord1;\n"
"flat out int vMode0;\n"
"vec4 SphereMap( vec3 u, vec3 n, vec4 tc )\n"
"{\n"
"	vec3 r = u - 2.0*n*dot(n, u);\n"
"	float m = 2.0*sqrt( r.x*r.x + r.y*r.y + (r.z+1.0)*(r.z+1.0) );\n"
"	return vec4( r.x/m + 0.5, r.y/m + 0.5, tc.z, tc.w );\n"
"}\n"
"void main()\n"
"{\n"
"	vec4 eye = uModelView * aPosition;\n"
"	gl_Position = uProjection * eye;\n"
"	vec3 n = uNormalMatrix * aNormal;\n"
"	if( uLighting != 0 )\n"
"	{\n"
"		vec4 c = uMatEmissive + vec4(0.2, 0.2, 0.2, 1.0) * uMatAmbient;\n"
"		if( uLight0 != 0 )\n"
"		{\n"
"			vec3 L = normalize( uLightDir );\n"
"			c += uLightAmbient * uMatAmbient;\n"
"			float NdotL = dot( n, L );\n"
"			if( NdotL > 0.0 )\n"
"			{\n"
"				c += NdotL * uLightDiffuse * uMatDiffuse;\n"
"				vec3 H = normalize( L + vec3(0.0, 0.0, 1.0) );\n"
"				float NdotH = max( dot(n, H), 0.0 );\n"
"				float s = uMatShininess == 0.0? 1.0: pow( NdotH, uMatShininess );\n"
"				c += s * uLightSpecular * uMatSpecular;\n"
"			}\n"
"		}\n"
"		c.a = uMatDiffuse.a;\n"
"		vColor = clamp( c, 0.0, 1.0 );\n"
"	}\n"
"	else\n"
"		vColor = clamp( aColor, 0.0, 1.0 );\n"
"	vec4 tc0 = aTexCoord;\n"
"	vec4 tc1 = vec4( 0.0, 0.0, 0.0, 1.0 );\n"
"	if( uSphere0 != 0 || uSphere1 != 0 )\n"
"	{\n"
"		vec3 u = normalize( eye.xyz );\n"
"		if( uSphere0 != 0 ) tc0 = SphereMap( u, n, tc0 );\n"
"		if( uSphere1 != 0 ) tc1 = SphereMap( u, n, tc1 );\n"
"	}\n"
"	vTexCoord0 = uTexMatrix0 * tc0;\n"
"	vTexCoord1 = uTexMatrix1 * tc1;\n"
"	vMode0 = int( aMode.x + 0.5 );\n"
"}\n";

static const char *g_szFragmentShader =
"#version 330 core\n"
"uniform sampler2D uTexture0;\n"
"uniform sampler2D uTexture1;\n"
"uniform int uTex1Mode;\n"
"uniform int uAlphaTest;\n"
"uniform vec3 uColorScale;\n"
"in vec4 vColor;\n"
"in vec4 vTexCoord0;\n"
"in vec4 vTexCoord1;\n"
"flat in int vMode0;\n"
"out vec4 FragColor;\n"
"vec4 Combine( int iMode, vec4 prev, vec4 tex )\n"
"{\n"
"	if( iMode == 1 ) return prev * tex;\n"
"	if( iMode == 2 ) return vec4( clamp(prev.rgb + tex.rgb, 0.0, 1.0), prev.a * tex.a );\n"
"	if( iMode == 3 ) return vec4( vColor.rgb, vColor.a * tex.a );\n"
"	return prev;\n"
"}\n"
"void main()\n"
"{\n"
"	vec4 c = vColor;\n"
"	if( vMode0 != 0 ) c = Combine( vMode0, c, textureProj(uTexture0, vTexCoord0) );\n"
"	if( uTex1Mode != 0 ) c = Combine( uTex1Mode, c, textureProj(uTexture1, vTexCoord1) );\n"
"	if( uAlphaTest != 0 && c.a <= 0.01 ) discard;\n"
"	FragColor = vec4( c.rgb * uColorScale, c.a );\n"
"}\n";

namespace
{
	enum { ATTR_POSITION, ATTR_NORMAL, ATTR_TEXCOORD, ATTR_COLOR, ATTR_MODE };
	enum { TEXMODE_OFF, TEXMODE_MODULATE, TEXMODE_ADD, TEXMODE_GLOW };

	enum
	{
		U_PROJECTION, U_MODELVIEW, U_NORMALMATRIX, U_TEXMATRIX0, U_TEXMATRIX1,
		U_SPHERE0, U_SPHERE1, U_LIGHTING, U_LIGHT0,
		U_MATEMISSIVE, U_MATAMBIENT, U_MATDIFFUSE, U_MATSPECULAR, U_MATSHININESS,
		U_LIGHTAMBIENT, U_LIGHTDIFFUSE, U_LIGHTSPECULAR, U_LIGHTDIR,
		U_COLORSCALE,
		U_TEX1MODE, U_ALPHATEST, U_TEXTURE0, U_TEXTURE1,
		NUM_UNIFORMS
	};
	const char *g_szUniformNames[NUM_UNIFORMS] =
	{
		"uProjection", "uModelView", "uNormalMatrix", "uTexMatrix0", "uTexMatrix1",
		"uSphere0", "uSphere1", "uLighting", "uLight0",
		"uMatEmissive", "uMatAmbient", "uMatDiffuse", "uMatSpecular", "uMatShininess",
		"uLightAmbient", "uLightDiffuse", "uLightSpecular", "uLightDir",
		"uColorScale",
		"uTex1Mode", "uAlphaTest", "uTexture0", "uTexture1",
	};

	struct Uniform
	{
		GLint iLocation;
		bool bValid;
		float f[16];
	};
	Uniform g_Uniforms[NUM_UNIFORMS];
	GLuint g_Program = 0;

	/*
	 * What we send for each vertex.  Positions and texture coordinates have as
	 * many components as they need: the draws we move on the CPU have no use
	 * for the fourth.  Normals only matter to lighting and sphere mapping, so
	 * they're half floats, and left empty for everything else.
	 */
	struct Vertex
	{
		float p[3];
		float t[2];
		GLubyte c[4];	// RGBA
		GLushort n[3];	// half floats
		GLubyte mode[2];	// unit 0's texture mode, then padding
	};
	typedef char VertexSizeCheck[sizeof(Vertex) == 32? 1: -1];

	/* The queue.  Indices into it fit in 16 bits. */
	enum { QUEUE_VERTICES = 8192 };
	Vertex g_Queue[QUEUE_VERTICES];
	int g_iQueued = 0;
	bool g_bQueueQuads = false;

	/* The queue goes into this buffer, one run after another; when it fills up,
	 * it's replaced by a fresh one, so we never write over a run GL may still be
	 * reading.  The index buffer turns quads into triangles. */
	const int RING_BYTES = sizeof(Vertex) * 65536;
	GLuint g_StreamBuffer = 0, g_QuadIndexBuffer = 0, g_StreamVAO = 0;
	int g_iRingOffset = 0;

	/* What GL has.  -1 means unknown. */
	struct AppliedState
	{
		int iBlend, iBlendSrc, iBlendDst, iDepthRange;
		int iDepthTest, iDepthMask, iDepthFunc;
		int iCullFace;
		int iPolygonMode;
		float fLineWidth;
		int iActiveUnit;
		int64_t iBound[MAX_TEXTURE_UNITS];
		int64_t iVAO, iArrayBuffer;

		void Invalidate()
		{
			iBlend = iBlendSrc = iBlendDst = iDepthRange = -1;
			iDepthTest = iDepthMask = iDepthFunc = -1;
			iCullFace = -1;
			iPolygonMode = -1;
			fLineWidth = -1;
			iActiveUnit = -1;
			for( int i = 0; i < MAX_TEXTURE_UNITS; ++i )
				iBound[i] = -1;
			iVAO = iArrayBuffer = -1;
		}
	};
	AppliedState g_Applied;

	/* What we've been asked for. */
	struct WantedState
	{
		bool bBlend;
		GLenum BlendSrc, BlendDst;
		int iDepthRange;
		bool bZWrite, bZTest;
		GLenum DepthFunc;
		bool bClearDepthPending;
		int iCullFace;	// 0, GL_BACK or GL_FRONT
		GLenum PolygonMode;
		float fLineWidth;

		int iActiveUnit;
		struct TexUnit
		{
			GLuint Texture;
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

	/* Wrap mode of each texture object, by name: -1 unknown, 0 clamped, 1 repeating. */
	vector<signed char> g_TexWrapApplied, g_TexWrapWanted;

	signed char &WrapEntry( vector<signed char> &v, unsigned uTex )
	{
		if( uTex >= v.size() )
			v.resize( uTex+1, -1 );
		return v[uTex];
	}

	/* Set up by PrepareDraw for the vertices of the draw. */
	struct DrawContext
	{
		bool bOnCPU;	// move vertices by the world and texture matrices here
		const RageMatrix *pWorld;
		const RageMatrix *pTexMatrix0;
		GLubyte iMode0;
	};
	DrawContext g_Draw;

	/* The modelview matrix of the last draw; see SetLightDirectional. */
	RageMatrix g_LastWorld, g_LastCenteringView;
}

/* See SetColorScale. */
static float g_fColorScale[3] = { 1, 1, 1 };
static bool g_bDrawnThisFrame = false;	// what's drawn can't be scaled after the fact
static bool g_bColorScaleInexact = false;	// drew something that doesn't scale linearly

static void FlushQueue();

/*
 * Applying state.  Each of these sends out the queue before changing anything
 * it depends on.
 */
static void SetActiveUnit( int iUnit )
{
	if( g_Applied.iActiveUnit == iUnit )
		return;
	gl.ActiveTexture( GLC_TEXTURE0 + iUnit );
	g_Applied.iActiveUnit = iUnit;
}

static void BindTextureToUnit( int iUnit, GLuint uTex )
{
	if( g_Applied.iBound[iUnit] == int64_t(uTex) )
		return;
	FlushQueue();
	SetActiveUnit( iUnit );
	glBindTexture( GL_TEXTURE_2D, uTex );
	g_Applied.iBound[iUnit] = uTex;
}

static void BindVAO( GLuint uVAO )
{
	if( g_Applied.iVAO == int64_t(uVAO) )
		return;
	gl.BindVertexArray( uVAO );
	g_Applied.iVAO = uVAO;
}

static void BindArrayBuffer( GLuint uBuffer )
{
	if( g_Applied.iArrayBuffer == int64_t(uBuffer) )
		return;
	gl.BindBuffer( GLC_ARRAY_BUFFER, uBuffer );
	g_Applied.iArrayBuffer = uBuffer;
}

static bool UniformDiffers( int u, const float *f, int iCount )
{
	Uniform &un = g_Uniforms[u];
	return !un.bValid || memcmp( un.f, f, iCount*sizeof(float) );
}

static void SetUniformFloats( int u, const float *f, int iCount )
{
	if( !UniformDiffers(u, f, iCount) )
		return;
	FlushQueue();
	Uniform &un = g_Uniforms[u];
	memcpy( un.f, f, iCount*sizeof(float) );
	un.bValid = true;
	switch( iCount )
	{
	case 1:  gl.Uniform1f( un.iLocation, f[0] ); break;
	case 3:  gl.Uniform3fv( un.iLocation, 1, f ); break;
	case 4:  gl.Uniform4fv( un.iLocation, 1, f ); break;
	case 9:  gl.UniformMatrix3fv( un.iLocation, 1, GL_FALSE, f ); break;
	case 16: gl.UniformMatrix4fv( un.iLocation, 1, GL_FALSE, f ); break;
	default: ASSERT(0);
	}
}

static void SetUniformInt( int u, int i )
{
	Uniform &un = g_Uniforms[u];
	const float f = float(i);
	if( un.bValid && un.f[0] == f )
		return;
	FlushQueue();
	un.f[0] = f;
	un.bValid = true;
	gl.Uniform1i( un.iLocation, i );
}

static void ApplyBlendAndDepth()
{
	const WantedState &w = g_Wanted;
	AppliedState &a = g_Applied;

	if( a.iBlend != int(w.bBlend) )
	{
		FlushQueue();
		if( w.bBlend )
			glEnable( GL_BLEND );
		else
			glDisable( GL_BLEND );
		a.iBlend = w.bBlend;
	}
	if( a.iBlendSrc != int(w.BlendSrc) || a.iBlendDst != int(w.BlendDst) )
	{
		FlushQueue();
		glBlendFunc( w.BlendSrc, w.BlendDst );
		a.iBlendSrc = w.BlendSrc;
		a.iBlendDst = w.BlendDst;
	}
	if( a.iDepthRange != w.iDepthRange )
	{
		FlushQueue();
		if( w.iDepthRange )
			glDepthRange( 0.0, 0.95 );
		else
			glDepthRange( 0.05, 1.0 );
		a.iDepthRange = w.iDepthRange;
	}

	/*
	 * Most frames never touch the depth buffer, so the depth test is only on
	 * while something tests or writes Z, and the depth buffer is only cleared
	 * when it's about to be used (see RageDisplay_OGL).
	 */
	if( !w.bZWrite && !w.bZTest )
	{
		if( a.iDepthTest != 0 )
		{
			FlushQueue();
			glDisable( GL_DEPTH_TEST );
			a.iDepthTest = 0;
		}
	}
	else
	{
		if( w.bClearDepthPending )
		{
			FlushQueue();
			/* Clearing is affected by the depth mask. */
			if( a.iDepthMask != 1 )
			{
				glDepthMask( GL_TRUE );
				a.iDepthMask = 1;
			}
			glClear( GL_DEPTH_BUFFER_BIT );
			g_Wanted.bClearDepthPending = false;
		}
		if( a.iDepthTest != 1 )
		{
			FlushQueue();
			glEnable( GL_DEPTH_TEST );
			a.iDepthTest = 1;
		}
	}
	if( a.iDepthMask != int(w.bZWrite) )
	{
		FlushQueue();
		glDepthMask( w.bZWrite );
		a.iDepthMask = w.bZWrite;
	}
	if( a.iDepthFunc != int(w.DepthFunc) )
	{
		FlushQueue();
		glDepthFunc( w.DepthFunc );
		a.iDepthFunc = w.DepthFunc;
	}

	if( a.iCullFace != w.iCullFace )
	{
		FlushQueue();
		if( w.iCullFace == 0 )
			glDisable( GL_CULL_FACE );
		else
		{
			if( a.iCullFace <= 0 )
				glEnable( GL_CULL_FACE );
			glCullFace( w.iCullFace );
		}
		a.iCullFace = w.iCullFace;
	}

	if( a.iPolygonMode != int(w.PolygonMode) )
	{
		FlushQueue();
		glPolygonMode( GL_FRONT_AND_BACK, w.PolygonMode );
		a.iPolygonMode = w.PolygonMode;
	}
	if( a.fLineWidth != w.fLineWidth )
	{
		FlushQueue();
		glLineWidth( w.fLineWidth );
		a.fLineWidth = w.fLineWidth;
	}
}

/* Bind a unit's texture and give it the wrap mode it's supposed to have. */
static void ApplyTexture( int iUnit )
{
	const GLuint uTex = g_Wanted.Unit[iUnit].Texture;
	BindTextureToUnit( iUnit, uTex );

	const signed char iWrap = WrapEntry( g_TexWrapWanted, uTex );
	signed char &iApplied = WrapEntry( g_TexWrapApplied, uTex );
	if( iWrap == -1 || iWrap == iApplied )
		return;
	FlushQueue();
	SetActiveUnit( iUnit );
	const GLint mode = iWrap? GL_REPEAT: GLC_CLAMP_TO_EDGE;
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mode );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mode );
	iApplied = iWrap;
}

/* GL's normal matrix: the inverse transpose of the modelview's upper 3x3, which
 * is the cofactor matrix over the determinant.  Column major, like GL wants. */
static void GetNormalMatrix( const RageMatrix &mv, float out[9] )
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
	for( int r = 0; r < 3; ++r )
		for( int c = 0; c < 3; ++c )
			out[c*3+r] = cof[r][c] * fInv;
}

static const float g_fIdentity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

/*
 * The queue.
 */
static void FlushQueue()
{
	if( g_iQueued == 0 )
		return;
	const int iNum = g_iQueued;
	g_iQueued = 0;

	BindVAO( g_StreamVAO );
	BindArrayBuffer( g_StreamBuffer );

	const int iBytes = iNum * sizeof(Vertex);
#if defined(DARWIN)
	/* macOS's GL is slow to map buffers and may wait for the GPU to do it.  A
	 * new buffer for each run never waits. */
	gl.BufferData( GLC_ARRAY_BUFFER, iBytes, g_Queue, GLC_STREAM_DRAW );
	g_iRingOffset = 0;
#else
	if( g_iRingOffset + iBytes > RING_BYTES )
	{
		/* Start a new buffer; the old one goes away once GL is done with it. */
		gl.BufferData( GLC_ARRAY_BUFFER, RING_BYTES, NULL, GLC_STREAM_DRAW );
		g_iRingOffset = 0;
	}

	void *p = gl.MapBufferRange( GLC_ARRAY_BUFFER, g_iRingOffset, iBytes,
		GLC_MAP_WRITE_BIT | GLC_MAP_INVALIDATE_RANGE_BIT | GLC_MAP_UNSYNCHRONIZED_BIT );
	if( p != NULL )
	{
		memcpy( p, g_Queue, iBytes );
		gl.UnmapBuffer( GLC_ARRAY_BUFFER );
	}
	else
		gl.BufferSubData( GLC_ARRAY_BUFFER, g_iRingOffset, iBytes, g_Queue );
#endif

	const GLint iFirst = g_iRingOffset / sizeof(Vertex);
	if( g_bQueueQuads )
		gl.DrawElementsBaseVertex( GL_TRIANGLES, iNum/4*6, GL_UNSIGNED_SHORT, NULL, iFirst );
	else
		glDrawArrays( GL_TRIANGLES, iFirst, iNum );

	g_iRingOffset += iBytes;
}

/* Make room for iCount more vertices of the given kind, and return where they go. */
static inline Vertex *QueueSpace( bool bQuads, int iCount )
{
	if( g_iQueued && (g_bQueueQuads != bQuads || g_iQueued + iCount > QUEUE_VERTICES) )
		FlushQueue();
	g_bQueueQuads = bQuads;
	Vertex *v = &g_Queue[g_iQueued];
	g_iQueued += iCount;
	return v;
}

/* A float as a half float, which is plenty for a normal.  Anything that small
 * rounds to zero, and anything that big to the largest number there is. */
static inline GLushort FloatToHalf( float f )
{
	union { float f; uint32_t u; } v;
	v.f = f;
	const uint32_t iSign = (v.u >> 16) & 0x8000;
	const int iExp = int((v.u >> 23) & 0xFF) - 127 + 15;
	if( iExp <= 0 )
		return GLushort( iSign );
	if( iExp >= 31 )
		return GLushort( iSign | 0x7BFF );
	uint32_t h = iSign | (iExp << 10) | ((v.u & 0x7FFFFF) >> 13);
	if( v.u & 0x1000 )
		++h;
	return GLushort( h );
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
	GLubyte mode;
};

/*
 * Compiled geometry (models).  The data is kept here too, so it can be sent
 * again if the GL context is recreated.
 */
class RageCompiledGeometryGLCore: public RageCompiledGeometry
{
public:
	RageCompiledGeometryGLCore()
	{
		m_iContextGeneration = 0;
		m_VAO = 0;
		memset( m_Buffers, 0, sizeof(m_Buffers) );
		m_bDirty = true;
	}

	~RageCompiledGeometryGLCore()
	{
		if( m_iContextGeneration != g_iContextGeneration )
			return;
		if( g_Applied.iVAO == int64_t(m_VAO) )
			g_Applied.iVAO = -1;
		if( g_Applied.iArrayBuffer == int64_t(m_Buffers[0]) || g_Applied.iArrayBuffer == int64_t(m_Buffers[1]) ||
			g_Applied.iArrayBuffer == int64_t(m_Buffers[2]) )
			g_Applied.iArrayBuffer = -1;
		gl.DeleteVertexArrays( 1, &m_VAO );
		gl.DeleteBuffers( 4, m_Buffers );
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

	void Draw( int iMeshIndex ) const
	{
		const MeshInfo& meshInfo = m_vMeshInfo[iMeshIndex];
		if( !meshInfo.iVertexCount || !meshInfo.iTriangleCount )
			return;

		if( m_iContextGeneration != g_iContextGeneration )
		{
			/* New context; whatever we had went with the old one. */
			m_iContextGeneration = g_iContextGeneration;
			gl.GenVertexArrays( 1, &m_VAO );
			gl.GenBuffers( 4, m_Buffers );
			BindVAO( m_VAO );
			BindArrayBuffer( m_Buffers[0] );
			gl.VertexAttribPointer( ATTR_POSITION, 3, GL_FLOAT, GL_FALSE, 0, NULL );
			gl.EnableVertexAttribArray( ATTR_POSITION );
			BindArrayBuffer( m_Buffers[1] );
			gl.VertexAttribPointer( ATTR_TEXCOORD, 2, GL_FLOAT, GL_FALSE, 0, NULL );
			gl.EnableVertexAttribArray( ATTR_TEXCOORD );
			BindArrayBuffer( m_Buffers[2] );
			gl.VertexAttribPointer( ATTR_NORMAL, 3, GL_FLOAT, GL_FALSE, 0, NULL );
			gl.EnableVertexAttribArray( ATTR_NORMAL );
			gl.BindBuffer( GLC_ELEMENT_ARRAY_BUFFER, m_Buffers[3] );
			m_bDirty = true;
		}

		BindVAO( m_VAO );
		if( m_bDirty )
		{
			/* Animated models change every frame. */
			BindArrayBuffer( m_Buffers[0] );
			gl.BufferData( GLC_ARRAY_BUFFER, m_vPosition.size()*sizeof(float), m_vPosition.empty()? NULL: &m_vPosition[0], GLC_DYNAMIC_DRAW );
			BindArrayBuffer( m_Buffers[1] );
			gl.BufferData( GLC_ARRAY_BUFFER, m_vTexture.size()*sizeof(float), m_vTexture.empty()? NULL: &m_vTexture[0], GLC_DYNAMIC_DRAW );
			BindArrayBuffer( m_Buffers[2] );
			gl.BufferData( GLC_ARRAY_BUFFER, m_vNormal.size()*sizeof(float), m_vNormal.empty()? NULL: &m_vNormal[0], GLC_DYNAMIC_DRAW );
			gl.BufferData( GLC_ELEMENT_ARRAY_BUFFER, m_vTriangles.size()*sizeof(uint16_t), m_vTriangles.empty()? NULL: &m_vTriangles[0], GLC_DYNAMIC_DRAW );
			m_bDirty = false;
		}

		/* Color and texture mode aren't arrays here; they come from the current
		 * values, like glColor did. */
		const RageColor &c = g_Wanted.ConstColor;
		gl.VertexAttrib4f( ATTR_COLOR, c.r, c.g, c.b, c.a );
		gl.VertexAttrib4f( ATTR_MODE, g_Draw.iMode0, 0, 0, 0 );

		glDrawElements( GL_TRIANGLES, meshInfo.iTriangleCount*3, GL_UNSIGNED_SHORT,
			(const void *) (meshInfo.iTriangleStart*3*sizeof(uint16_t)) );
	}

private:
	vector<float> m_vPosition, m_vTexture, m_vNormal;
	vector<uint16_t> m_vTriangles;

	mutable unsigned m_iContextGeneration;
	mutable GLuint m_VAO;
	mutable GLuint m_Buffers[4];	// positions, texture coordinates, normals, triangles
	mutable bool m_bDirty;
};

/*
 * Setting up a context.
 */
static GLuint CompileShader( GLenum type, const char *szSource )
{
	GLuint shader = gl.CreateShader( type );
	gl.ShaderSource( shader, 1, &szSource, NULL );
	gl.CompileShader( shader );

	GLint iStatus = 0;
	gl.GetShaderiv( shader, GLC_COMPILE_STATUS, &iStatus );
	if( !iStatus )
	{
		char szLog[2048] = "";
		gl.GetShaderInfoLog( shader, sizeof(szLog), NULL, szLog );
		gl.DeleteShader( shader );
		RageException::ThrowNonfatal( "Compiling the %s shader failed: %s",
			type == GLC_VERTEX_SHADER? "vertex":"fragment", szLog );
	}
	return shader;
}

static void InitContext()
{
	LoadFunctions();

	GLint iMajor = 0, iMinor = 0;
	glGetIntegerv( GLC_MAJOR_VERSION, &iMajor );
	glGetIntegerv( GLC_MINOR_VERSION, &iMinor );
	LOG->Info( "OGL Vendor: %s", glGetString(GL_VENDOR) );
	LOG->Info( "OGL Renderer: %s", glGetString(GL_RENDERER) );
	LOG->Info( "OGL Version: %s (core profile %i.%i)", glGetString(GL_VERSION), iMajor, iMinor );
	LOG->Info( "GLSL Version: %s", glGetString(GLC_SHADING_LANGUAGE_VERSION) );
	if( iMajor < 3 || (iMajor == 3 && iMinor < 3) )
		RageException::ThrowNonfatal( "OpenGL 3.3 is needed, but this is %i.%i", iMajor, iMinor );

	g_bAnisotropic = false;
	GLint iExtensions = 0;
	glGetIntegerv( GLC_NUM_EXTENSIONS, &iExtensions );
	for( GLint i = 0; i < iExtensions; ++i )
	{
		const char *szExt = (const char *) gl.GetStringi( GL_EXTENSIONS, i );
		if( szExt && (!strcmp(szExt, "GL_EXT_texture_filter_anisotropic") || !strcmp(szExt, "GL_ARB_texture_filter_anisotropic")) )
			g_bAnisotropic = true;
	}

	glGetFloatv( GLC_ALIASED_LINE_WIDTH_RANGE, g_fLineWidthRange );
	FlushGLErrors();	// forward compatible contexts may not like that one

	/* The program. */
	GLuint vs = CompileShader( GLC_VERTEX_SHADER, g_szVertexShader );
	GLuint fs = CompileShader( GLC_FRAGMENT_SHADER, g_szFragmentShader );
	g_Program = gl.CreateProgram();
	gl.AttachShader( g_Program, vs );
	gl.AttachShader( g_Program, fs );
	gl.BindAttribLocation( g_Program, ATTR_POSITION, "aPosition" );
	gl.BindAttribLocation( g_Program, ATTR_NORMAL, "aNormal" );
	gl.BindAttribLocation( g_Program, ATTR_TEXCOORD, "aTexCoord" );
	gl.BindAttribLocation( g_Program, ATTR_COLOR, "aColor" );
	gl.BindAttribLocation( g_Program, ATTR_MODE, "aMode" );
	gl.BindFragDataLocation( g_Program, 0, "FragColor" );
	gl.LinkProgram( g_Program );
	gl.DeleteShader( vs );
	gl.DeleteShader( fs );

	GLint iStatus = 0;
	gl.GetProgramiv( g_Program, GLC_LINK_STATUS, &iStatus );
	if( !iStatus )
	{
		char szLog[2048] = "";
		gl.GetProgramInfoLog( g_Program, sizeof(szLog), NULL, szLog );
		RageException::ThrowNonfatal( "Linking the shaders failed: %s", szLog );
	}
	gl.UseProgram( g_Program );

	for( int u = 0; u < NUM_UNIFORMS; ++u )
	{
		g_Uniforms[u].iLocation = gl.GetUniformLocation( g_Program, g_szUniformNames[u] );
		g_Uniforms[u].bValid = false;
	}
	gl.Uniform1i( g_Uniforms[U_TEXTURE0].iLocation, 0 );
	gl.Uniform1i( g_Uniforms[U_TEXTURE1].iLocation, 1 );

	/* The stream buffer and its vertex array. */
	g_Applied.Invalidate();
	gl.GenVertexArrays( 1, &g_StreamVAO );
	BindVAO( g_StreamVAO );
	gl.GenBuffers( 1, &g_StreamBuffer );
	BindArrayBuffer( g_StreamBuffer );
	gl.BufferData( GLC_ARRAY_BUFFER, RING_BYTES, NULL, GLC_STREAM_DRAW );
	g_iRingOffset = 0;

	const GLsizei stride = sizeof(Vertex);
	gl.VertexAttribPointer( ATTR_POSITION, 3, GL_FLOAT, GL_FALSE, stride, (const void *) offsetof(Vertex, p) );
	gl.VertexAttribPointer( ATTR_NORMAL, 3, GLC_HALF_FLOAT, GL_FALSE, stride, (const void *) offsetof(Vertex, n) );
	gl.VertexAttribPointer( ATTR_TEXCOORD, 2, GL_FLOAT, GL_FALSE, stride, (const void *) offsetof(Vertex, t) );
	gl.VertexAttribPointer( ATTR_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void *) offsetof(Vertex, c) );
	gl.VertexAttribPointer( ATTR_MODE, 1, GL_UNSIGNED_BYTE, GL_FALSE, stride, (const void *) offsetof(Vertex, mode) );
	for( int i = ATTR_POSITION; i <= ATTR_MODE; ++i )
		gl.EnableVertexAttribArray( i );

	/* Quad q is vertices 4q..4q+3; split it along the 1-3 diagonal, like the
	 * drivers did with GL_QUADS. */
	vector<uint16_t> vIndices( QUEUE_VERTICES/4*6 );
	for( int q = 0; q < QUEUE_VERTICES/4; ++q )
	{
		const uint16_t i = uint16_t( q*4 );
		uint16_t *p = &vIndices[q*6];
		p[0] = i+0; p[1] = i+1; p[2] = i+3;
		p[3] = i+1; p[4] = i+2; p[5] = i+3;
	}
	gl.GenBuffers( 1, &g_QuadIndexBuffer );
	gl.BindBuffer( GLC_ELEMENT_ARRAY_BUFFER, g_QuadIndexBuffer );
	gl.BufferData( GLC_ELEMENT_ARRAY_BUFFER, vIndices.size()*sizeof(uint16_t), &vIndices[0], GLC_STATIC_DRAW );

	g_TexWrapApplied.clear();
	g_TexWrapWanted.clear();
	g_iQueued = 0;
	++g_iContextGeneration;
}

static void InitWantedState()
{
	WantedState &w = g_Wanted;
	w.bBlend = true;
	w.BlendSrc = GL_SRC_ALPHA;
	w.BlendDst = GL_ONE_MINUS_SRC_ALPHA;
	w.iDepthRange = 0;
	w.bZWrite = w.bZTest = false;
	w.DepthFunc = GL_ALWAYS;
	w.bClearDepthPending = true;
	w.iCullFace = 0;
	w.PolygonMode = GL_FILL;
	w.fLineWidth = 1;
	w.iActiveUnit = 0;
	for( int i = 0; i < MAX_TEXTURE_UNITS; ++i )
	{
		w.Unit[i].Texture = 0;
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
}

/*
 * RageDisplay_GLCore
 */
RageDisplay_GLCore::RageDisplay_GLCore( VideoModeParams p )
{
	LOG->Trace( "RageDisplay_GLCore::RageDisplay_GLCore()" );
	LOG->MapLog( "renderer", "Current renderer: OpenGL core profile" );

	FixLilEndian();
	InitWantedState();

	g_pWind = MakeLowLevelWindow();
	g_pWind->SetCoreProfile( true );

	try {
		SetVideoMode( p );
	} catch(...) {
		/* SetVideoMode can throw. */
		delete g_pWind;
		g_pWind = NULL;
		throw;
	}
}

RageDisplay_GLCore::~RageDisplay_GLCore()
{
	delete g_pWind;
	g_pWind = NULL;
}

void RageDisplay_GLCore::Update( float fDeltaTime )
{
	g_pWind->Update( fDeltaTime );
}

CString RageDisplay_GLCore::TryVideoMode( VideoModeParams p, bool &bNewDeviceOut )
{
	FlushQueue();

	CString err = g_pWind->TryVideoMode( p, bNewDeviceOut );
	if( err != "" )
		return err;

	if( bNewDeviceOut )
	{
		/* Throws if the context can't do what we need, so the next renderer
		 * gets a chance. */
		InitContext();

		/* Our textures' names belong to the old context. */
		if( TEXTUREMAN )
			TEXTUREMAN->InvalidateTextures();
	}

	this->SetDefaultRenderStates();
	ResolutionChanged();

	return "";
}

void RageDisplay_GLCore::ResolutionChanged()
{
	SetViewport( 0, 0 );

	/* Clear any junk that's in the framebuffer. */
	if( BeginFrame() )
		EndFrame();
}

void RageDisplay_GLCore::SetViewport( int shift_left, int shift_down )
{
	FlushQueue();

	/* left and down are on a 0..SCREEN_WIDTH, 0..SCREEN_HEIGHT scale.
	 * Scale them to the actual viewport range, in framebuffer pixels. */
	int iWidth, iHeight;
	g_pWind->GetFramebufferSize( iWidth, iHeight );
	shift_left = int( shift_left * float(iWidth) / SCREEN_WIDTH );
	shift_down = int( shift_down * float(iHeight) / SCREEN_HEIGHT );

	glViewport( shift_left, -shift_down, iWidth, iHeight );
}

int RageDisplay_GLCore::GetMaxTextureSize() const
{
	GLint size;
	glGetIntegerv( GL_MAX_TEXTURE_SIZE, &size );
	return size;
}

bool RageDisplay_GLCore::BeginFrame()
{
	FlushQueue();
	glClearColor( 0,0,0,1 );
	SetZWrite( true );
	glClear( GL_COLOR_BUFFER_BIT );
	g_fColorScale[0] = g_fColorScale[1] = g_fColorScale[2] = 1;
	g_bDrawnThisFrame = false;
	g_bColorScaleInexact = false;
	g_Wanted.bClearDepthPending = true;
	return true;
}

void RageDisplay_GLCore::EndFrame()
{
	FlushQueue();
	g_fColorScale[0] = g_fColorScale[1] = g_fColorScale[2] = 1;

	/* The frame isn't done until the GPU is, and we need to know how long that
	 * takes to pace frames.  There's nothing else for the CPU to do meanwhile. */
	if( IsPacing() )
		glFinish();

	FrameSwapping();
	g_pWind->SwapBuffers();
	FrameSwapped();
	ProcessStatsOnFlip();
}

RageSurface* RageDisplay_GLCore::CreateScreenshot()
{
	FlushQueue();

	int width, height;
	g_pWind->GetFramebufferSize( width, height );

	const PixelFormatDesc &desc = PIXEL_FORMAT_DESC[FMT_RGBA8];
	RageSurface *image = CreateSurface( width, height, desc.bpp,
		desc.masks[0], desc.masks[1], desc.masks[2], 0 );

	glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image->pixels );
	RageSurfaceUtils::FlipVertically( image );

	return image;
}

RageDisplay::VideoModeParams RageDisplay_GLCore::GetVideoModeParams() const { return g_pWind->GetVideoModeParams(); }
float RageDisplay_GLCore::GetRefreshRate() const { return g_pWind->GetRefreshRate(); }

const RageDisplay::PixelFormatDesc *RageDisplay_GLCore::GetPixelFormatDesc( PixelFormat pf ) const
{
	ASSERT( pf < NUM_PIX_FORMATS );
	return &PIXEL_FORMAT_DESC[pf];
}

RageMatrix RageDisplay_GLCore::GetOrthoMatrix( float l, float r, float b, float t, float zn, float zf )
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
void RageDisplay_GLCore::SetBlendMode( BlendMode mode )
{
	WantedState &w = g_Wanted;
	w.bBlend = true;

	/* BLEND_NO_EFFECT is almost exclusively used to draw masks to the Z-buffer.
	 * Make sure masks always win the depth test when drawn at the same position. */
	w.iDepthRange = mode == BLEND_NO_EFFECT? 1:0;

	switch( mode )
	{
	case BLEND_NORMAL:		w.BlendSrc = GL_SRC_ALPHA; w.BlendDst = GL_ONE_MINUS_SRC_ALPHA; break;
	case BLEND_ADD:			w.BlendSrc = GL_SRC_ALPHA; w.BlendDst = GL_ONE; break;
	case BLEND_NO_EFFECT:	w.BlendSrc = GL_ZERO; w.BlendDst = GL_ONE; break;
	/* Expects white premultiplied by alpha: fades from the destination
	 * to its inverse as alpha goes 0 -> 1. */
	case BLEND_INVERT:		w.BlendSrc = GL_ONE_MINUS_DST_COLOR; w.BlendDst = GL_ONE_MINUS_SRC_ALPHA; break;
	default:	ASSERT(0);
	}
}

void RageDisplay_GLCore::SetBlending( bool b )
{
	g_Wanted.bBlend = b;
}

bool RageDisplay_GLCore::SetColorScale( float r, float g, float b )
{
	const bool bScaled = g_fColorScale[0] != 1 || g_fColorScale[1] != 1 || g_fColorScale[2] != 1;
	const bool bScale = r != 1 || g != 1 || b != 1;
	if( bScale && g_bDrawnThisFrame )
		return false;

	bool bRet = true;
	if( bScaled && !bScale && g_bColorScaleInexact )
	{
		/* The frame was empty when the scale was set, so everything in it was
		 * drawn with the scale: wipe it, and the caller draws it again. */
		FlushQueue();
		glClear( GL_COLOR_BUFFER_BIT );
		g_Wanted.bClearDepthPending = true;
		g_bDrawnThisFrame = false;
		bRet = false;
	}
	g_fColorScale[0] = r;
	g_fColorScale[1] = g;
	g_fColorScale[2] = b;
	g_bColorScaleInexact = false;
	return bRet;
}

bool RageDisplay_GLCore::IsZWriteEnabled() const
{
	return g_Wanted.bZWrite;
}

bool RageDisplay_GLCore::IsZTestEnabled() const
{
	return g_Wanted.DepthFunc != GL_ALWAYS;
}

void RageDisplay_GLCore::ClearZBuffer()
{
	/* Cleared when it's next used. */
	g_Wanted.bClearDepthPending = true;
}

void RageDisplay_GLCore::SetZWrite( bool b )
{
	g_Wanted.bZWrite = b;
}

void RageDisplay_GLCore::SetZTestMode( ZTestMode mode )
{
	WantedState &w = g_Wanted;
	w.bZTest = mode != ZTEST_OFF;
	switch( mode )
	{
	case ZTEST_OFF:				w.DepthFunc = GL_ALWAYS;	break;
	case ZTEST_WRITE_ON_PASS:	w.DepthFunc = GL_LEQUAL;	break;
	case ZTEST_WRITE_ON_FAIL:	w.DepthFunc = GL_GREATER;	break;
	default:	ASSERT( 0 );
	}
}

void RageDisplay_GLCore::SetCullMode( CullMode mode )
{
	switch( mode )
	{
	case CULL_BACK:		g_Wanted.iCullFace = GL_BACK;	break;
	case CULL_FRONT:	g_Wanted.iCullFace = GL_FRONT;	break;
	case CULL_NONE:		g_Wanted.iCullFace = 0;			break;
	default:	ASSERT(0);
	}
}

void RageDisplay_GLCore::SetAlphaTest( bool b )
{
	g_Wanted.bAlphaTest = b;
}

void RageDisplay_GLCore::SetPolygonMode( PolygonMode pm )
{
	switch( pm )
	{
	case POLYGON_FILL:	g_Wanted.PolygonMode = GL_FILL; break;
	case POLYGON_LINE:	g_Wanted.PolygonMode = GL_LINE; break;
	default:	ASSERT(0);
	}
}

void RageDisplay_GLCore::SetLineWidth( float fWidth )
{
	/* Core contexts may only have 1-pixel lines (macOS does). */
	g_Wanted.fLineWidth = clamp( fWidth, g_fLineWidthRange[0], g_fLineWidthRange[1] );
}

void RageDisplay_GLCore::ClearAllTextures()
{
	for( int i = 0; i < MAX_TEXTURE_UNITS; ++i )
		g_Wanted.Unit[i].bEnabled = false;
	g_Wanted.iActiveUnit = 0;
}

void RageDisplay_GLCore::SetTexture( int iTextureUnitIndex, RageTexture* pTexture )
{
	ASSERT( iTextureUnitIndex >= 0 && iTextureUnitIndex < MAX_TEXTURE_UNITS );
	g_Wanted.iActiveUnit = iTextureUnitIndex;
	WantedState::TexUnit &unit = g_Wanted.Unit[iTextureUnitIndex];
	unit.bEnabled = pTexture != NULL;
	if( pTexture )
		unit.Texture = pTexture->GetTexHandle();
}

void RageDisplay_GLCore::SetTextureModeModulate()
{
	g_Wanted.Unit[g_Wanted.iActiveUnit].iMode = TEXMODE_MODULATE;
}

void RageDisplay_GLCore::SetTextureModeGlow( GlowMode m )
{
	switch( m )
	{
	case GLOW_BRIGHTEN:
		g_Wanted.BlendSrc = GL_SRC_ALPHA;
		g_Wanted.BlendDst = GL_ONE;
		return;
	case GLOW_WHITEN:
		g_Wanted.Unit[g_Wanted.iActiveUnit].iMode = TEXMODE_GLOW;
		return;
	}
}

void RageDisplay_GLCore::SetTextureModeAdd()
{
	g_Wanted.Unit[g_Wanted.iActiveUnit].iMode = TEXMODE_ADD;
}

void RageDisplay_GLCore::SetTextureFiltering( bool b )
{
}

void RageDisplay_GLCore::SetTextureWrapping( bool b )
{
	/* This is the wrap mode of the texture on the active unit. */
	const WantedState::TexUnit &unit = g_Wanted.Unit[g_Wanted.iActiveUnit];
	if( unit.Texture )
		WrapEntry( g_TexWrapWanted, unit.Texture ) = b;
}

void RageDisplay_GLCore::SetSphereEnironmentMapping( bool b )
{
	g_Wanted.Unit[g_Wanted.iActiveUnit].bSphere = b;
}

void RageDisplay_GLCore::SetMaterial(
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

void RageDisplay_GLCore::SetLighting( bool b )
{
	g_Wanted.bLighting = b;
}

void RageDisplay_GLCore::SetLightOff( int index )
{
	if( index == 0 )
		g_Wanted.bLight0 = false;
}

void RageDisplay_GLCore::SetLightDirectional(
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

bool RageDisplay_GLCore::PrepareDraw( bool bForceGPU )
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

	/* A unit only textures if it's on and has a texture. */
	const int iMode0 = (w.Unit[0].bEnabled && w.Unit[0].Texture)? w.Unit[0].iMode: TEXMODE_OFF;
	const int iMode1 = (w.Unit[1].bEnabled && w.Unit[1].Texture)? w.Unit[1].iMode: TEXMODE_OFF;
	const bool bSphere0 = iMode0 != TEXMODE_OFF && w.Unit[0].bSphere;
	const bool bSphere1 = iMode1 != TEXMODE_OFF && w.Unit[1].bSphere;

	/* Vertices on the CPU leave out the last row's worth of coordinates, which
	 * is only right if the matrices don't do anything projective. */
	const RageMatrix *pWorld = GetWorldTop();
	const RageMatrix &TexMatrix = w.Unit[0].TexMatrix;
	const bool bAffine = pWorld->m[0][3] == 0 && pWorld->m[1][3] == 0 && pWorld->m[2][3] == 0 && pWorld->m[3][3] == 1 &&
		(iMode0 == TEXMODE_OFF || (TexMatrix.m[0][3] == 0 && TexMatrix.m[1][3] == 0 && TexMatrix.m[3][3] == 1));
	const bool bOnCPU = !bForceGPU && !w.bLighting && !bSphere0 && !bSphere1 && bAffine;

	ApplyBlendAndDepth();
	if( iMode0 != TEXMODE_OFF )
		ApplyTexture( 0 );
	if( iMode1 != TEXMODE_OFF )
		ApplyTexture( 1 );

	/* Matrices. */
	const unsigned iGen = GetProjectionViewGeneration();
	static unsigned iLastProjectionGen = unsigned(-1);
	if( iLastProjectionGen != iGen || !g_Uniforms[U_PROJECTION].bValid )
	{
		SetUniformFloats( U_PROJECTION, (const float *) GetProjectionTop(), 16 );
		iLastProjectionGen = iGen;
	}
	const RageMatrix &CenteringView = GetCenteringView( iGen, GetCentering(), GetViewTop() );
	if( bOnCPU )
	{
		SetUniformFloats( U_MODELVIEW, (const float *) &CenteringView, 16 );
		SetUniformFloats( U_TEXMATRIX0, g_fIdentity, 16 );
	}
	else
	{
		RageMatrix ModelView;
		RageMatrixMultiply( &ModelView, &CenteringView, pWorld );
		SetUniformFloats( U_MODELVIEW, (const float *) &ModelView, 16 );
		if( w.bLighting || bSphere0 || bSphere1 )
		{
			float fNormal[9];
			GetNormalMatrix( ModelView, fNormal );
			SetUniformFloats( U_NORMALMATRIX, fNormal, 9 );
		}
		SetUniformFloats( U_TEXMATRIX0, (const float *) &w.Unit[0].TexMatrix, 16 );
	}
	g_LastWorld = *pWorld;
	g_LastCenteringView = CenteringView;

	SetUniformInt( U_TEX1MODE, iMode1 );
	if( iMode1 != TEXMODE_OFF )
		SetUniformFloats( U_TEXMATRIX1, (const float *) &w.Unit[1].TexMatrix, 16 );
	SetUniformInt( U_SPHERE0, bSphere0 );
	SetUniformInt( U_SPHERE1, bSphere1 );
	SetUniformInt( U_ALPHATEST, w.bAlphaTest );
	SetUniformFloats( U_COLORSCALE, g_fColorScale, 3 );
	g_bDrawnThisFrame = true;

	/* Scaling before the framebuffer adds or inverts isn't the same as after:
	 * it clamps at 1 instead of at the scale. */
	if( g_fColorScale[0] != 1 || g_fColorScale[1] != 1 || g_fColorScale[2] != 1 )
	{
		if( w.bBlend && ((w.BlendDst == GL_ONE && w.BlendSrc != GL_ZERO) || w.BlendSrc == GL_ONE_MINUS_DST_COLOR) )
			g_bColorScaleInexact = true;
	}

	SetUniformInt( U_LIGHTING, w.bLighting );
	if( w.bLighting )
	{
		SetUniformFloats( U_MATEMISSIVE, w.MatEmissive, 4 );
		SetUniformFloats( U_MATAMBIENT, w.MatAmbient, 4 );
		SetUniformFloats( U_MATDIFFUSE, w.MatDiffuse, 4 );
		SetUniformFloats( U_MATSPECULAR, w.MatSpecular, 4 );
		SetUniformFloats( U_MATSHININESS, &w.fMatShininess, 1 );
		SetUniformInt( U_LIGHT0, w.bLight0 );
		if( w.bLight0 )
		{
			SetUniformFloats( U_LIGHTAMBIENT, w.LightAmbient, 4 );
			SetUniformFloats( U_LIGHTDIFFUSE, w.LightDiffuse, 4 );
			SetUniformFloats( U_LIGHTSPECULAR, w.LightSpecular, 4 );
			SetUniformFloats( U_LIGHTDIR, w.LightDir, 3 );
		}
	}

	g_Draw.bOnCPU = bOnCPU;
	g_Draw.pWorld = pWorld;
	g_Draw.pTexMatrix0 = &w.Unit[0].TexMatrix;
	g_Draw.iMode0 = GLubyte( iMode0 );
	return bOnCPU;
}

void RageDisplay_GLCore::DrawQuadsInternal( const RageSpriteVertex v[], int iNumVerts )
{
	PrepareDraw();
	const VertexFiller fill;
	for( int i = 0; i+3 < iNumVerts; i += 4 )
	{
		Vertex *out = QueueSpace( true, 4 );
		fill.Fill( out[0], v[i+0] );
		fill.Fill( out[1], v[i+1] );
		fill.Fill( out[2], v[i+2] );
		fill.Fill( out[3], v[i+3] );
	}
}

void RageDisplay_GLCore::DrawQuadStripInternal( const RageSpriteVertex v[], int iNumVerts )
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
	for( int i = 2; i+1 < iNumVerts; i += 2 )
	{
		Vertex c, d;
		fill.Fill( c, v[i] );
		fill.Fill( d, v[i+1] );
		Vertex *out = QueueSpace( true, 4 );
		out[0] = a;
		out[1] = b;
		out[2] = d;
		out[3] = c;
		a = c;
		b = d;
	}
}

void RageDisplay_GLCore::DrawFanInternal( const RageSpriteVertex v[], int iNumVerts )
{
	PrepareDraw();
	if( iNumVerts < 3 )
		return;
	const VertexFiller fill;
	Vertex first, prev;
	fill.Fill( first, v[0] );
	fill.Fill( prev, v[1] );
	for( int i = 2; i < iNumVerts; ++i )
	{
		Vertex cur;
		fill.Fill( cur, v[i] );
		Vertex *out = QueueSpace( false, 3 );
		out[0] = first;
		out[1] = prev;
		out[2] = cur;
		prev = cur;
	}
}

void RageDisplay_GLCore::DrawStripInternal( const RageSpriteVertex v[], int iNumVerts )
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
	for( int i = 0; i+2 < iNumVerts; ++i )
	{
		Vertex c;
		fill.Fill( c, v[i+2] );
		Vertex *out = QueueSpace( false, 3 );
		const bool bOdd = (i & 1) != 0;
		out[0] = bOdd? b: a;
		out[1] = bOdd? a: b;
		out[2] = c;
		a = b;
		b = c;
	}
}

void RageDisplay_GLCore::DrawTrianglesInternal( const RageSpriteVertex v[], int iNumVerts )
{
	PrepareDraw();
	const VertexFiller fill;
	for( int i = 0; i+2 < iNumVerts; i += 3 )
	{
		Vertex *out = QueueSpace( false, 3 );
		fill.Fill( out[0], v[i+0] );
		fill.Fill( out[1], v[i+1] );
		fill.Fill( out[2], v[i+2] );
	}
}

void RageDisplay_GLCore::DrawCompiledGeometryInternal( const RageCompiledGeometry *p, int iMeshIndex )
{
	/* Models keep their vertices in GL, so the world matrix goes to GL. */
	PrepareDraw( true );
	FlushQueue();
	p->Draw( iMeshIndex );
}

RageCompiledGeometry* RageDisplay_GLCore::CreateCompiledGeometry()
{
	return new RageCompiledGeometryGLCore;
}

void RageDisplay_GLCore::DeleteCompiledGeometry( RageCompiledGeometry* p )
{
	delete p;
}

/*
 * Textures
 */
static RageDisplay::PixelFormat GetImgPixelFormat( RageSurface* &img, bool &bFreeImg, int width, int height )
{
	RageDisplay::PixelFormat pixfmt = DISPLAY->FindPixelFormat( img->format->BitsPerPixel, img->format->Rmask, img->format->Gmask, img->format->Bmask, img->format->Amask );

	if( pixfmt == RageDisplay::NUM_PIX_FORMATS || pixfmt == RageDisplay::FMT_PAL )
	{
		/* The source isn't in a format GL takes.  Convert it to RGBA8, and let
		 * GL convert it to whatever the texture is.  This is slow, and should
		 * almost never happen. */
		pixfmt = RageDisplay::FMT_RGBA8;
		const RageDisplay::PixelFormatDesc *pfd = DISPLAY->GetPixelFormatDesc( pixfmt );

		RageSurface *imgconv = CreateSurface( width, height,
			pfd->bpp, pfd->masks[0], pfd->masks[1], pfd->masks[2], pfd->masks[3] );
		RageSurfaceUtils::Blit( img, imgconv, width, height );
		img = imgconv;
		bFreeImg = true;
	}
	else
		bFreeImg = false;

	return pixfmt;
}

bool RageDisplay_GLCore::SupportsTextureFormat( PixelFormat pixfmt, bool realtime )
{
	/* No paletted textures in core OpenGL; they're converted before they get here. */
	return pixfmt != FMT_PAL;
}

unsigned RageDisplay_GLCore::CreateTexture(
	PixelFormat pixfmt,
	RageSurface* img,
	bool bGenerateMipMaps )
{
	FlushQueue();
	ASSERT( pixfmt < NUM_PIX_FORMATS && pixfmt != FMT_PAL );
	ASSERT( img->w == power_of_two(img->w) && img->h == power_of_two(img->h) );

	bool bFreeImg;
	PixelFormat imgpixfmt = GetImgPixelFormat( img, bFreeImg, img->w, img->h );

	const GLenum glTexFormat = GL_PIXFMT[pixfmt].internalfmt;
	const GLenum glImageFormat = GL_PIXFMT[imgpixfmt].format;
	const GLenum glImageType = GL_PIXFMT[imgpixfmt].type;

	GLuint uTexHandle;
	glGenTextures( 1, &uTexHandle );
	ASSERT( uTexHandle );

	/* Load it on unit 0. */
	BindTextureToUnit( 0, uTexHandle );
	SetActiveUnit( 0 );

	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	GLint minFilter = GL_LINEAR;
	if( bGenerateMipMaps )
		minFilter = g_pWind->GetVideoModeParams().bTrilinearFiltering? GL_LINEAR_MIPMAP_LINEAR: GL_LINEAR_MIPMAP_NEAREST;
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter );

	if( g_pWind->GetVideoModeParams().bAnisotropicFiltering && g_bAnisotropic )
	{
		GLfloat fLargest;
		glGetFloatv( GLC_MAX_TEXTURE_MAX_ANISOTROPY, &fLargest );
		glTexParameterf( GL_TEXTURE_2D, GLC_TEXTURE_MAX_ANISOTROPY, fLargest );
	}

	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLC_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLC_CLAMP_TO_EDGE );
	WrapEntry( g_TexWrapApplied, uTexHandle ) = 0;
	WrapEntry( g_TexWrapWanted, uTexHandle ) = 0;

	glPixelStorei( GL_UNPACK_ROW_LENGTH, img->pitch / img->format->BytesPerPixel );

	FlushGLErrors();
	glTexImage2D( GL_TEXTURE_2D, 0, glTexFormat, img->w, img->h, 0, glImageFormat, glImageType, img->pixels );
	GLenum error = glGetError();
	if( error != GL_NO_ERROR )
	{
		/* The texture is left empty; that's better than not playing at all. */
		LOG->Warn( "glTexImage2D(format 0x%x, %ix%i, format 0x%x, type 0x%x, pixfmt %i, imgpixfmt %i): error 0x%x",
			glTexFormat, img->w, img->h, glImageFormat, glImageType, pixfmt, imgpixfmt, error );
	}
	if( bGenerateMipMaps )
		gl.GenerateMipmap( GL_TEXTURE_2D );

	glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
	glFlush();

	if( bFreeImg )
		delete img;
	return uTexHandle;
}

void RageDisplay_GLCore::UpdateTexture(
	unsigned uTexHandle,
	RageSurface* img,
	int xoffset, int yoffset, int width, int height )
{
	/* The queue may still draw the old contents. */
	FlushQueue();
	BindTextureToUnit( 0, uTexHandle );
	SetActiveUnit( 0 );

	bool bFreeImg;
	PixelFormat pixfmt = GetImgPixelFormat( img, bFreeImg, width, height );

	glPixelStorei( GL_UNPACK_ROW_LENGTH, img->pitch / img->format->BytesPerPixel );
	glTexSubImage2D( GL_TEXTURE_2D, 0, xoffset, yoffset, width, height,
		GL_PIXFMT[pixfmt].format, GL_PIXFMT[pixfmt].type, img->pixels );
	glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
	glFlush();

	if( bFreeImg )
		delete img;
}

void RageDisplay_GLCore::DeleteTexture( unsigned uTexHandle )
{
	FlushQueue();

	GLuint uTex = uTexHandle;
	glDeleteTextures( 1, &uTex );

	/* Units it was bound to are now bound to nothing, and the name may be reused. */
	for( int i = 0; i < MAX_TEXTURE_UNITS; ++i )
	{
		if( g_Applied.iBound[i] == int64_t(uTexHandle) )
			g_Applied.iBound[i] = 0;
		if( g_Wanted.Unit[i].Texture == uTexHandle )
			g_Wanted.Unit[i].Texture = 0;
	}
	WrapEntry( g_TexWrapApplied, uTexHandle ) = -1;
	WrapEntry( g_TexWrapWanted, uTexHandle ) = -1;
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

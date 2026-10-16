#version 450
/*
 * The vertex shader of RageDisplay_SDLGPU.  It does what RageDisplay_GLCore's
 * does (see there): GL's fixed function transform, one directional light,
 * sphere mapping and texture matrices.  Built twice: for the vertices we
 * stream (sprites, text, everything 2D) and, with MODEL defined, for compiled
 * geometry, whose color and texture mode are uniforms, like glColor was.
 */

layout(set=1, binding=0) uniform Transform
{
	mat4 uProjection;
	mat4 uModelView;
	mat4 uTexMatrix0;
	mat4 uTexMatrix1;
	mat4 uNormalMatrix;	// the upper 3x3
	ivec4 uFlags;		// sphere map unit 0, sphere map unit 1, lighting, light 0
	vec4 uConstColor;	// models: the vertex color
	ivec4 uConstMode;	// models: x = unit 0's texture mode
};

layout(set=1, binding=1) uniform Light
{
	vec4 uMatEmissive;
	vec4 uMatAmbient;
	vec4 uMatDiffuse;
	vec4 uMatSpecular;
	vec4 uLightAmbient;
	vec4 uLightDiffuse;
	vec4 uLightSpecular;
	vec4 uLightDir;		// w: the material's shininess
};

#if defined(MODEL)
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aTexCoord;
#else
/* Metal wants attributes 4 byte aligned, so the normal (three half floats)
 * comes in two pieces, and the texture mode is the third byte of the four
 * that end the vertex: the normal's z, the mode, and padding. */
layout(location=0) in vec3 aPosition;
layout(location=1) in vec2 aNormalXY;
layout(location=2) in vec2 aTexCoord;
layout(location=3) in vec4 aColor;
layout(location=4) in uvec4 aEnd;
layout(location=5) in vec2 aNormalZ;
#endif

layout(location=0) out vec4 vColor;
layout(location=1) out vec4 vTexCoord0;
layout(location=2) out vec4 vTexCoord1;
layout(location=3) flat out int vMode0;

vec4 SphereMap( vec3 u, vec3 n, vec4 tc )
{
	vec3 r = u - 2.0*n*dot(n, u);
	float m = 2.0*sqrt( r.x*r.x + r.y*r.y + (r.z+1.0)*(r.z+1.0) );
	return vec4( r.x/m + 0.5, r.y/m + 0.5, tc.z, tc.w );
}

void main()
{
	vec4 eye = uModelView * vec4( aPosition, 1.0 );
	gl_Position = uProjection * eye;
	/* GL's clip space has z from -w to w; ours, from 0 to w. */
	gl_Position.z = 0.5 * (gl_Position.z + gl_Position.w);

#if defined(MODEL)
	vec3 n = mat3( uNormalMatrix ) * aNormal;
	vec4 color = uConstColor;
	int mode = uConstMode.x;
#else
	vec3 n = mat3( uNormalMatrix ) * vec3( aNormalXY, aNormalZ.x );
	vec4 color = aColor;
	int mode = int( aEnd.z );
#endif

	if( uFlags.z != 0 )
	{
		vec4 c = uMatEmissive + vec4(0.2, 0.2, 0.2, 1.0) * uMatAmbient;
		if( uFlags.w != 0 )
		{
			vec3 L = normalize( uLightDir.xyz );
			c += uLightAmbient * uMatAmbient;
			float NdotL = dot( n, L );
			if( NdotL > 0.0 )
			{
				c += NdotL * uLightDiffuse * uMatDiffuse;
				vec3 H = normalize( L + vec3(0.0, 0.0, 1.0) );
				float NdotH = max( dot(n, H), 0.0 );
				float s = uLightDir.w == 0.0? 1.0: pow( NdotH, uLightDir.w );
				c += s * uLightSpecular * uMatSpecular;
			}
		}
		c.a = uMatDiffuse.a;
		vColor = clamp( c, 0.0, 1.0 );
	}
	else
		vColor = clamp( color, 0.0, 1.0 );

	vec4 tc0 = vec4( aTexCoord, 0.0, 1.0 );
	vec4 tc1 = vec4( 0.0, 0.0, 0.0, 1.0 );
	if( uFlags.x != 0 || uFlags.y != 0 )
	{
		vec3 u = normalize( eye.xyz );
		if( uFlags.x != 0 ) tc0 = SphereMap( u, n, tc0 );
		if( uFlags.y != 0 ) tc1 = SphereMap( u, n, tc1 );
	}
	vTexCoord0 = uTexMatrix0 * tc0;
	vTexCoord1 = uTexMatrix1 * tc1;
	vMode0 = mode;
}

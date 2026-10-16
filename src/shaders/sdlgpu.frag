#version 450
/* The fragment shader of RageDisplay_SDLGPU; see RageDisplay_GLCore's. */

layout(set=2, binding=0) uniform sampler2D uTexture0;
layout(set=2, binding=1) uniform sampler2D uTexture1;

layout(set=3, binding=0) uniform Fragment
{
	vec4 uColorScale;
	ivec4 uFFlags;	// unit 1's texture mode, alpha test
};

layout(location=0) in vec4 vColor;
layout(location=1) in vec4 vTexCoord0;
layout(location=2) in vec4 vTexCoord1;
layout(location=3) flat in int vMode0;

layout(location=0) out vec4 FragColor;

vec4 Combine( int iMode, vec4 prev, vec4 tex )
{
	if( iMode == 1 ) return prev * tex;
	if( iMode == 2 ) return vec4( clamp(prev.rgb + tex.rgb, 0.0, 1.0), prev.a * tex.a );
	if( iMode == 3 ) return vec4( vColor.rgb, vColor.a * tex.a );
	return prev;
}

void main()
{
	vec4 c = vColor;
	if( vMode0 != 0 ) c = Combine( vMode0, c, textureProj(uTexture0, vTexCoord0) );
	if( uFFlags.x != 0 ) c = Combine( uFFlags.x, c, textureProj(uTexture1, vTexCoord1) );
	if( uFFlags.y != 0 && c.a <= 0.01 ) discard;
	FragColor = vec4( c.rgb * uColorScale.rgb, c.a );
}

// default fragment shader

#include "light.fs"
#include "bump.fs"

uniform int LIGHTMAP;
uniform int BUMPMAP;

uniform sampler2D SAMPLER0;
uniform sampler2D SAMPLER1;
uniform sampler2D SAMPLER2;
uniform sampler2D SAMPLER3;

const vec3 two = vec3(2.0);
const vec3 negHalf = vec3(-0.5);


/*
main
*/
void main(void){

	vec4 normalmap;
	vec3 deluxemap;

	vec2 offset = vec2(0.0);

	if(BUMPMAP > 0){  // resolve deluxemap and normalmap
		deluxemap = texture2D(SAMPLER2, gl_TexCoord[1].st).rgb;
		deluxemap = normalize(two * (deluxemap + negHalf));

		normalmap = texture2D(SAMPLER3, gl_TexCoord[0].st);
		normalmap.rgb = normalize(two * (normalmap.rgb + negHalf));

		// and parallax offset
		offset = BumpTexcoord(normalmap.a);
	}

	// sample the diffuse texture, honoring the parallax offset
	vec4 diffuse = texture2D(SAMPLER0, gl_TexCoord[0].st + offset);

	vec3 lightmap;

	if(LIGHTMAP > 0)  // sample the lightmap
		lightmap = texture2D(SAMPLER1, gl_TexCoord[1].st).rgb;
	else  // or use primary color
		lightmap = gl_Color.rgb;

	// add any dynamic lighting and yield a base fragment color
	LightFragment(diffuse, lightmap);

	// add in bump and parallax mapping
	if(BUMPMAP > 0)
		BumpFragment(deluxemap, normalmap.rgb);
}

varying vec2 tex;

varying vec4 ambientLight;
varying vec4 diffuseLight;
varying vec4 specularLight;
varying vec4 diffuseLight2;

varying vec3 lightVec;
varying vec3 lightVec2;
varying vec3 eyeVec;

/* diffuse */
uniform sampler2D SAMPLER0;
/* blend */
uniform sampler2D SAMPLER1;
/* normalmap */
uniform sampler2D SAMPLER2;

uniform float BLENDSCALE;
uniform float GLOWSCALE;
uniform vec4 DEFAULTCOLOR;
uniform vec4 CITYLIGHTCOLOR;

const float specularExp = 32.0;

/* index of refraction for water */
const float n1 = 1.0;
const float n2 = 1.333;
const float eta = n1 / n2;

/* calculate reflection component of Frenel's equations */
float fresnelReflect(in float cos_a)
{
	float cos_b = sqrt(1.0 - ((eta * eta) * ( 1.0 - (cos_a * cos_a))));
	float rs = (n1 * cos_a - n2 * cos_b ) / (n1 * cos_a + n2 * cos_b);
	float rp = (n1 * cos_b - n2 * cos_a ) / (n1 * cos_b + n2 * cos_a);
	return ((rs * rs + rp * rp) / 2.0);
}


void main()
{
	/* blend textures smoothly */
	vec3 diffuseColorA = texture2D(SAMPLER0, tex).rgb;
	vec3 diffuseColorB = texture2D(SAMPLER1, tex).rgb;
	vec4 diffuseColor;
	diffuseColor.rgb = ((1.0 - BLENDSCALE) * diffuseColorA) + (BLENDSCALE * diffuseColorB);
	diffuseColor.a = 1.0;

	/* calculate diffuse reflections */
	vec3 V = normalize(eyeVec);
	vec3 L = normalize(lightVec);
	vec3 N = normalize(texture2D(SAMPLER2, tex).rgb * 2.0 - 1.0);
	float NdotL = clamp(dot(N, L), 0.0, 1.0);
	vec4 reflectColor = diffuseColor * diffuseLight * NdotL;
	
	/* calculate specular reflections */
	float RdotL = clamp(dot(reflect(-L, N), V), 0.0, 1.0);
	float gloss = texture2D(SAMPLER2, tex).a;
	/* NOTE: this "d" here is a hack to compensate for the fact
	 * that we're using orthographic projection.
	 */
	float d = clamp(pow(1.0 + dot(V, L), 0.4), 0.0, 1.0);
	float fresnel = 2.0 * fresnelReflect(NdotL);
	vec4 specularColor = (d * d * fresnel * fresnel) * gloss * pow(RdotL, specularExp) * specularLight;

	/* calculate night illumination */
	float diffuseNightColor = texture2D(SAMPLER0, tex).a;
	float NdotL2 = clamp(dot(N, normalize(lightVec2)), 0.0, 1.0);
	vec4 nightColor = diffuseLight2 * CITYLIGHTCOLOR * diffuseNightColor * NdotL2;

	vec4 color = DEFAULTCOLOR + (ambientLight * diffuseColor) + reflectColor + (0.0 * specularColor) + nightColor;
	vec4 hdrColor = GLOWSCALE *
					( clamp((reflectColor - vec4(0.9, 0.9, 0.9, 0)), 0.0, GLOWSCALE) +
					  1.0 * specularColor + nightColor);
	hdrColor.a = 1.0;

	/* calculate final color */
	gl_FragData[0] = color;
	gl_FragData[1] = hdrColor;
}

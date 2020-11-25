#version 450

layout(binding=0) uniform sampler2D lightmap;
layout(binding=1) uniform sampler2D tex;

layout(location=0) in vec2 v_tex_uv;
layout(location=1) in vec2 v_lightmap_uv;

layout(location=0) out vec4 outColor;

void main() {
	vec3 color = texture(tex, (v_tex_uv+.5)/textureSize(tex, 0)).rgb;
	color *= texture(lightmap, v_lightmap_uv).rgb * 2.;
	outColor = vec4(pow(color, vec3(2.2)), 1.0);
}

#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_query : enable

layout(binding=1, set=1) uniform sampler2D lightmap;
layout(binding=2, set=2) uniform sampler2D tex;
layout(binding=1, set=0) uniform accelerationStructureEXT tlas;

layout(location=0) in vec2 v_tex_uv;
layout(location=1) in vec2 v_lightmap_uv;
layout(location=2) in vec3 v_world_pos;
layout(location=3) in vec3 v_eye_dir;

layout(location=0) out vec4 outColor;

#define SHADOW_SAMPLES 16.

float shadowK(vec3 O, float l, vec3 light_pos) {
	vec3 dir = light_pos - O;
	rayQueryEXT ray_query;
	rayQueryInitializeEXT(ray_query, tlas, gl_RayFlagsTerminateOnFirstHitEXT, 0xff,
		O, l, normalize(dir), length(dir));
	while(rayQueryProceedEXT(ray_query)){}
	return (rayQueryGetIntersectionTypeEXT(ray_query, true) != gl_RayQueryCommittedIntersectionNoneEXT) ? .0 : 1.;
}

float hash(float f) { return fract(sin(f)*47568.5435); }

void main() {
	vec3 color = texture(tex, (v_tex_uv+.5)/textureSize(tex, 0)).rgb;
	//color *= texture(lightmap, v_lightmap_uv).rgb * 2.;

	vec3 sun_dir = normalize(vec3(1.));
	float shadow_factor = 0.;
	float sun_size = 100.;
	for (float si = 0.; si < SHADOW_SAMPLES; ++si) {
		vec3 sun_pos_sample = sun_dir * 1e4;
		sun_pos_sample.x += (hash(v_world_pos.z+si) - .5) * sun_size;
		sun_pos_sample.y += (hash(v_world_pos.x+si+1.) - .5) * sun_size;
		sun_pos_sample.z += (hash(v_world_pos.y+si+2.) - .5) * sun_size;
		shadow_factor += shadowK(v_world_pos, .1, sun_pos_sample);
	}

	color *= max(.3, shadow_factor / SHADOW_SAMPLES);

	//color = fract(v_world_pos);
	outColor = vec4(pow(color, vec3(2.2)), 1.0);
}

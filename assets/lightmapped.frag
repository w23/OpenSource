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

void main() {
	vec3 color = texture(tex, (v_tex_uv+.5)/textureSize(tex, 0)).rgb;
	//color *= texture(lightmap, v_lightmap_uv).rgb * 2.;

	vec3 sun_dir = normalize(vec3(1.));
	rayQueryEXT ray_query;
	rayQueryInitializeEXT(ray_query, tlas, gl_RayFlagsTerminateOnFirstHitEXT, 0xff, v_world_pos, 1., sun_dir, 1e4);
	while(rayQueryProceedEXT(ray_query)){}
	if(rayQueryGetIntersectionTypeEXT(ray_query, true) != gl_RayQueryCommittedIntersectionNoneEXT)
		color *= .3;

	//color = fract(v_world_pos);
	outColor = vec4(pow(color, vec3(2.2)), 1.0);
}

#version 450

layout(location=0) in vec3 a_vertex;
layout(location=1) in vec2 a_lightmap_uv;
layout(location=2) in vec2 a_tex_uv;
//layout(location=3) in vec3 a_color;

//layout(location=0) out vec3 v_color_fixme;
layout(location=0) out vec2 v_tex_uv;
layout(location=1) out vec2 v_lightmap_uv;

layout(binding=0, set=0) uniform UBO {
	mat4 model_view;
	mat4 projection;
} ubo;

void main() {
/*
  vec3 origin = vec3(ubo.viewI * vec4(0, 0, 0, 1));
  worldPos     = vec3(objMatrix * vec4(inPosition, 1.0));
  viewDir      = vec3(worldPos - origin);
*/

	v_lightmap_uv = a_lightmap_uv;
	v_tex_uv = a_tex_uv;
	//v_color_fixme = a_color_fixme;

  //gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);

	gl_Position = ubo.projection * ubo.model_view * vec4(a_vertex, 1.);
}

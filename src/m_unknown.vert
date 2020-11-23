#version 450

layout(location=0) in vec3 a_vertex;
layout(location=1) in vec2 a_lightmap_uv;
layout(location=2) in vec2 a_tex_uv;
//layout(location=3) in vec3 a_color;

//layout(location=0) out vec3 v_color_fixme;
layout(location=0) out vec2 v_tex_uv;
layout(location=1) out vec2 v_lightmap_uv;

layout(push_constant) uniform G {
	mat4 u_mvp;
};

void main() {
	v_lightmap_uv = a_lightmap_uv;
	v_tex_uv = a_tex_uv;
	//v_color_fixme = a_color_fixme;
	gl_Position = u_mvp * vec4(a_vertex, 1.);
}

#version 450

layout(location=0) in vec3 a_vertex;
layout(location=1) in vec3 a_color_fixme;
//layout(location=0) out vec3 v_pos;
layout(location=0) out vec3 v_color_fixme;

layout(push_constant) uniform G {
	mat4 u_mvp;
};

void main() {
	//v_pos = a_vertex;
	v_color_fixme = a_color_fixme;
	gl_Position = u_mvp * vec4(a_vertex, 1.);
}

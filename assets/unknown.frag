#version 450

layout(location=0) in vec3 v_pos;

layout(location=0) out vec4 outColor;

void main() {
	outColor = vec4(fract(v_pos/100.), 1.);
}

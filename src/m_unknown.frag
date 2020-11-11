#version 450
//??? #extension GL_ARB_separate_shader_objects : enable
//
layout(location = 0) in vec3 v_pos;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fract(v_pos/100.), 1.0);
}

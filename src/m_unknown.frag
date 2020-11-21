#version 450
//??? #extension GL_ARB_separate_shader_objects : enable

layout(location=0) in vec3 v_color_fixme;
layout(location=1) in vec2 v_lightmap_uv;

layout(location=0) out vec4 outColor;

layout(binding=0) uniform sampler2D lightmap;

void main() {
    outColor = vec4(v_color_fixme * texture(lightmap, v_lightmap_uv).rgb, 1.0);
    //outColor = vec4(v_color_fixme, 1.0);
		//outColor = vec4(fract(v_lightmap_uv*100.), 0., 1.);
}

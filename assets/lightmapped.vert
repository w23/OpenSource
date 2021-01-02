#version 450
#extension GL_EXT_scalar_block_layout : require

layout(location=0) out vec2 v_tex_uv;
layout(location=1) out vec2 v_lightmap_uv;

layout(binding=0, set=0) uniform UBO {
	mat4 view;
	mat4 projection;
} ubo;

layout(push_constant) uniform G {
	vec3 translation;
};

struct BSPModelVertex {
	vec4 vertex;
	vec4 uvs_lm_tex;
	uint average_color;
};

layout(binding=1, set=0, scalar) readonly buffer VertexBuffer {
	BSPModelVertex vertices[];
} buf;

void main() {
	vec3 vertex = buf.vertices[gl_VertexIndex].vertex.xyz + translation.xyz;
	v_lightmap_uv = buf.vertices[gl_VertexIndex].uvs_lm_tex.xy;
	v_tex_uv = buf.vertices[gl_VertexIndex].uvs_lm_tex.zw;

	gl_Position = ubo.projection * ubo.view * vec4(vertex, 1.);
}

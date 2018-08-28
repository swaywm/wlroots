#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// we use a mat4 since it uses the same size as mat3 due to
// alignment. Easier to deal with (tighly-packed) mat4 then
layout(push_constant, row_major) uniform UBO {
	mat4 proj;
} ubo;

layout(location = 0) out vec2 uv;

// 4 outlining points and uv coords
const vec2[] values = {
	{0, 0},
	{1, 0},
	{1, 1},
	{0, 1},
};

void main() {
	uv = values[gl_VertexIndex % 4];
	gl_Position = ubo.proj * vec4(uv, 0.0, 1.0);
	gl_Position.y = -gl_Position.y; // invert y coord for screen space
}

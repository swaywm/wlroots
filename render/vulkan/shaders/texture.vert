#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(set = 0, binding = 0, row_major) uniform UBO {
	mat4 mat;
} ubo;

layout(location = 0) out vec2 uv;

const vec2[] values = {
	{-1, -1}, // 4 outlining points ...
	{1, -1},
	{1, 1},
	{-1, 1},
};

void main() {
	vec2 pos = values[gl_VertexIndex % 4];
	gl_Position = ubo.mat * vec4(pos, 0.0, 1.0);
	gl_Position.y = -gl_Position.y; // invert y coord for screen space
	uv = (pos + 1) / 2; // move uv range from [-1,1] to [0,1]
}

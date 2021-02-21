#version 450

// we use a mat4 since it uses the same size as mat3 due to
// alignment. Easier to deal with (tighly-packed) mat4 though.
layout(push_constant, row_major) uniform UBO {
	mat4 proj;
	vec2 uv_offset;
	vec2 uv_size;
} data;

layout(location = 0) out vec2 uv;

// 4 outlining points and uv coords
const vec2[] values = {
	{0, 0},
	{1, 0},
	{1, 1},
	{0, 1},
};

void main() {
	vec2 pos = values[gl_VertexIndex % 4];
	uv = data.uv_offset + pos * data.uv_size;
	gl_Position = data.proj * vec4(pos, 0.0, 1.0);
}

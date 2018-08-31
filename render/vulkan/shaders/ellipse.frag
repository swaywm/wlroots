#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;
layout(push_constant) uniform UBO {
	layout(offset = 64) vec4 color;
} data;

void main() {
	float l = length(uv - vec2(0.5, 0.5));
	if (l > 0.5) {
		discard;
	}

	out_color = data.color;
}



#version 450
layout(set = 0, binding = 0) uniform sampler2D uInput;
layout(location = 0) out vec4 oColor;

vec4 blur13(in sampler2D image, in vec2 uv, in vec2 resolution, in vec2 direction) {
	vec4 color = vec4(0.0);
	vec2 off1 = vec2(1.411764705882353) * direction;
	vec2 off2 = vec2(3.2941176470588234) * direction;
	vec2 off3 = vec2(5.176470588235294) * direction;
	color += texture(image, uv) * 0.1964825501511404;
	color += texture(image, uv + (off1 / resolution)) * 0.2969069646728344;
	color += texture(image, uv - (off1 / resolution)) * 0.2969069646728344;
	color += texture(image, uv + (off2 / resolution)) * 0.09447039785044732;
	color += texture(image, uv - (off2 / resolution)) * 0.09447039785044732;
	color += texture(image, uv + (off3 / resolution)) * 0.010381362401148057;
	color += texture(image, uv - (off3 / resolution)) * 0.010381362401148057;
	return color;
}

void main() {
	ivec2 coord = ivec2(gl_FragCoord.xy), resolution = textureSize(uInput, 0);
	oColor = blur13(uInput, vec2(coord) / vec2(resolution), vec2(resolution), vec2(1, 0));
}

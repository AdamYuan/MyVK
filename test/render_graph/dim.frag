#version 450
layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput uInput;
layout(location = 0) out vec4 oColor;

layout(push_constant) uniform uuPushConstant { float uDim; };

void main() { oColor = subpassLoad(uInput) * uDim; }

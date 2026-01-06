#version 430 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
uniform vec2 uScreenSize;
void main() {
    vec2 ndc = (aPos / uScreenSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = aColor;
}
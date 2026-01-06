#version 430 core
layout(local_size_x = 128) in;
layout(rgba8, binding = 0) uniform image2D imgOutput;

struct Vertex {
    float x, y;
    float r, g, b;
};

layout(std430, binding = 1) buffer VertexBuffer {
    Vertex vertices[];
};

layout(std430, binding = 2) buffer IndexBuffer {
    uint indices[];
};

uniform vec4 uBounds; // minLon, minLat, lonRange, latRange
uniform ivec2 uScreenSize;
uniform uint uNumIndices;


void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= uNumIndices - 1) return;

    uint i1 = indices[idx];
    uint i2 = indices[idx+1];
    if (i1 == 0 || i2 == 0) return;

    Vertex iv1 = vertices[i1];
    Vertex iv2 = vertices[i2];
    vec2 v1 = vec2(iv1.x, iv1.y);
    vec2 v2 = vec2(iv2.x, iv2.y);
    
    vec2 boundsMin = uBounds.xy;
    vec2 boundsRange = uBounds.zw;

    vec2 p1 = (v1 - boundsMin) / boundsRange * vec2(uScreenSize);
    vec2 p2 = (v2 - boundsMin) / boundsRange * vec2(uScreenSize);

    vec2 dir = p2 - p1;
    float len = length(dir);
    if (len < 0.1) return;
    
    vec3 color = vec3(iv1.r, iv1.g, iv1.b);
    for (float i = 0; i <= len; i += 0.5) {
        imageStore(imgOutput, ivec2(p1 + (dir/len) * i), vec4(color, 1.0));
    }
}

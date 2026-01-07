#version 430 core
layout(local_size_x = 128) in;

struct InputVertex {
    float lon;
    float lat;
    float r;
    float g;
    float b;
};

struct OutputVertex {
    vec2 pos;
    vec2 _pad;
    vec4 color;
};

layout(std430, binding = 1) readonly buffer InputVBO {
    float inputData[];
};

layout(std430, binding = 2) readonly buffer InputEBO {
    uint indices[];
};

layout(std430, binding = 3) writeonly buffer OutputVBO {
    OutputVertex outputVertices[];
};

layout(std430, binding = 4) writeonly buffer OutputEBO {
    uint outputIndices[];
};

uniform vec4 uBounds;
uniform vec2 uScreenSize;
uniform uint uNumIndices;
uniform float uWidth;

const uint INVALID_IDX = uint(-1);

vec2 mapToScreen(float lon, float lat) {
    float x = (lon - uBounds.x) / uBounds.z;
    float y = (lat - uBounds.y) / uBounds.w;
    return vec2(x * uScreenSize.x, y * uScreenSize.y);
}

InputVertex fetchVertex(uint index) {
    uint base = index * 5;
    InputVertex v;
    v.lon = inputData[base];
    v.lat = inputData[base + 1];
    v.r = inputData[base + 2];
    v.g = inputData[base + 3];
    v.b = inputData[base + 4];
    return v;
}

uint getIndex(uint id) {
  if(id >= uNumIndices) return INVALID_IDX;
  return indices[id] >> 1;
}
bool isEnd(uint id) {
  if(id >= uNumIndices) return true;
  return (indices[id] & 0x1) == 1;
}

void main() {
    uint id = gl_GlobalInvocationID.x;
    if (id >= uNumIndices) return;

    uint idx = getIndex(id);
    // determine if this is the end of a strip
    const bool endPt = isEnd(id);

    uint idxPrev = getIndex(id-1);
    uint idxNext = getIndex(id+1);
    
    InputVertex v = fetchVertex(idx);
    vec2 p = mapToScreen(v.lon, v.lat);
    vec4 color = vec4(v.r, v.g, v.b, 1.0);

    vec2 normal = vec2(0.0, 1.0);
    vec2 p_prev = p;
    vec2 p_next = p;
    
    vec2 dir = vec2(0.0);

    if (idxPrev != INVALID_IDX) {
        InputVertex v_prev = fetchVertex(idxPrev);
        p_prev = mapToScreen(v_prev.lon, v_prev.lat);
        dir += normalize(p - p_prev);
    }
    if (idxNext != INVALID_IDX) {
        InputVertex v_next = fetchVertex(idxNext);
        p_next = mapToScreen(v_next.lon, v_next.lat);
        dir += normalize(p_next - p);
    }

    if (length(dir) > 0.0) {
        dir = normalize(dir);
        normal = vec2(-dir.y, dir.x);
    }

    float halfWidth = uWidth * 0.5;
    outputVertices[idx * 2].pos = p + normal * halfWidth;
    outputVertices[idx * 2]._pad = vec2(0.0);
    outputVertices[idx * 2].color = color;
    outputVertices[idx * 2 + 1].pos = p - normal * halfWidth;
    outputVertices[idx * 2 + 1]._pad = vec2(0.0);
    outputVertices[idx * 2 + 1].color = color;

    uint base = id * 6;
    if (!endPt && idxNext != INVALID_IDX) {
        outputIndices[base + 0] = idx * 2;
        outputIndices[base + 1] = idx * 2 + 1;
        outputIndices[base + 2] = idxNext * 2;
        outputIndices[base + 3] = idxNext * 2;
        outputIndices[base + 4] = idx * 2 + 1;
        outputIndices[base + 5] = idxNext * 2 + 1;
    } else {
        outputIndices[base + 0] = INVALID_IDX;
        outputIndices[base + 1] = INVALID_IDX;
        outputIndices[base + 2] = INVALID_IDX;
        outputIndices[base + 3] = INVALID_IDX;
        outputIndices[base + 4] = INVALID_IDX;
        outputIndices[base + 5] = INVALID_IDX;
    }
}
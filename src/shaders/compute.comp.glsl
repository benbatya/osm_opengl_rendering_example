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
  return indices[id] >> 2;
}

const uint BEGIN_BIT = 1 << 0;
const uint END_BIT = 1 << 1;

bool isBeginning(uint id) {
  if(id >= uNumIndices) return true;
  return (indices[id] & BEGIN_BIT) == BEGIN_BIT;
}
bool isEnd(uint id) {
  if(id >= uNumIndices) return true;
  return (indices[id] & END_BIT) == END_BIT;
}

void main() {
    uint id = gl_GlobalInvocationID.x;
    if (id >= uNumIndices) return;

    uint idx = getIndex(id);
    // determine if this point is the beginning or end of a strip
    const bool beginPt = isBeginning(id);
    const bool endPt = isEnd(id);

    InputVertex v = fetchVertex(idx);
    vec2 p = mapToScreen(v.lon, v.lat);

    vec2 dir = vec2(0.0);
    if (!beginPt) {
        uint idxPrev = getIndex(id-1);
        InputVertex v_prev = fetchVertex(idxPrev);
        vec2 p_prev = mapToScreen(v_prev.lon, v_prev.lat);
        dir += normalize((p - p_prev));
    }
    if (!endPt) {    
        uint idxNext = getIndex(id+1);
        InputVertex v_next = fetchVertex(idxNext);
        vec2 p_next = mapToScreen(v_next.lon, v_next.lat);
        dir += normalize((p_next - p));
    }

    vec2 normal = vec2(0.0);
    if (length(dir) > 0.0) {
        dir = normalize(dir);
        normal = vec2(-dir.y, dir.x);
    }

    vec4 color = vec4(v.r, v.g, v.b, 1.0);
    // vec4 color = vec4(abs(normal), 0.0, 1.0); // color;
    // vec4 color = vec4(beginPt?0.0:1.0, endPt?0.0:1.0, 0.0, 1.0);

    float halfWidth = uWidth * 0.5;
    uint vertIdx = id * 2;
    outputVertices[vertIdx].pos = p + normal * halfWidth;
    outputVertices[vertIdx]._pad = vec2(0.0);
    outputVertices[vertIdx].color = color;
    outputVertices[vertIdx + 1].pos = p - normal * halfWidth;
    outputVertices[vertIdx + 1]._pad = vec2(0.0);
    outputVertices[vertIdx + 1].color = color;

    uint base = id * 6;
    if (!endPt) {
        uint idxNext = getIndex(id+1);
        uint nextVertIdx = idxNext * 2;
        outputIndices[base + 0] = vertIdx;
        outputIndices[base + 1] = vertIdx + 1;
        outputIndices[base + 2] = nextVertIdx;
        outputIndices[base + 3] = nextVertIdx;
        outputIndices[base + 4] = vertIdx + 1;
        outputIndices[base + 5] = nextVertIdx + 1;
    } else {
        outputIndices[base + 0] = INVALID_IDX;
        outputIndices[base + 1] = INVALID_IDX;
        outputIndices[base + 2] = INVALID_IDX;
        outputIndices[base + 3] = INVALID_IDX;
        outputIndices[base + 4] = INVALID_IDX;
        outputIndices[base + 5] = INVALID_IDX;
    }
}
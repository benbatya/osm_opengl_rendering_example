#version 330 core
layout (lines_adjacency) in;
layout (triangle_strip, max_vertices = 4) out;

in VS_OUT {
    vec3 color;
} gs_in[];

out vec3 fColor;

void build_segment(vec4 position, vec3 color, vec4 position2, vec3 color2)
{    
  // calculate the normalized direction from position to position2
  vec2 direction = normalize(position2.xy - position.xy);
  vec2 perp2 = vec2(direction.y, -direction.x) * 0.005;
  vec4 perp = vec4(perp2, 0.0, 0.0);

    fColor = color; // gs_in[0] since there's only one input vertex
    gl_Position = position - perp; // 1:bottom-left   
    EmitVertex();   
    gl_Position = position + perp; // 2:bottom-right
    EmitVertex();
    fColor = color2;
    gl_Position = position2 - perp; // 3:top-left
    EmitVertex();
    gl_Position = position2 + perp; // 4:top-right
    EmitVertex();
    EndPrimitive();
}

void main() {    
    build_segment(gl_in[1].gl_Position, gs_in[1].color, gl_in[2].gl_Position, gs_in[2].color);
}


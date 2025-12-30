#version 330 core
layout (location = 0) in vec2 aPos; // lon, lat in geographic coords
layout (location = 1) in vec3 aColor; // index, r, g (or other)

uniform vec4 uBounds; // (minLon, minLat, lonRange, latRange)

out VS_OUT {
	vec3 color;
} vs_out;

void main()
{
	vs_out.color = aColor;
	// normalize lon/lat into clip-space [-1,1]
	float lon = aPos.x;
	float lat = aPos.y;
	float minLon = uBounds.x;
	float minLat = uBounds.y;
	float lonRange = uBounds.z;
	float latRange = uBounds.w;
	float x = 0.0;
	float y = 0.0;
	if (lonRange != 0.0) {
		x = ((lon - minLon) / lonRange) * 2.0 - 1.0;
	}
	if (latRange != 0.0) {
		y = ((lat - minLat) / latRange) * 2.0 - 1.0;
	}
	gl_Position = vec4(x, y, 0.0, 1.0);
}
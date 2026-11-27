#pragma once
// Orb - small looping sprite animations for the womb HUD (Data/BFHeredity/Orb/<name>.orb: header + BC3 atlas)

namespace Orb
{
	// render thread: draws frame `a_time` of the named orb centred at a_c with diameter a_size; false if it is not available
	bool Draw(void* a_drawList, const char* a_name, float a_cx, float a_cy, float a_size, float a_time, float a_alpha, bool a_loop = true);
	// seconds one pass of the named orb lasts (0 if not loaded yet)
	float Length(const char* a_name);
}

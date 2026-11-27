#pragma once
// A short video in a small window (top right corner of the screen), drawn over the HUD with SKSE Menu Framework's ImGui.
// The file is decoded by Windows Media Foundation on a worker thread; the render thread uploads the newest frame.
// Its sound is a .wav of the same name next to it, played with PlaySound.  Files: Data\BFHeredity\Video\<name>.mp4 / .wav

namespace Video
{
	struct Settings
	{
		float width = 0.30f;   // window width, share of the screen width
		float margin = 0.02f;  // from the top right corner, share of the screen width
		bool  sound = true;
		float fade = 0.4f;  // seconds
	};
	inline Settings g_cfg;

	bool Play(const std::wstring& a_name);  // any thread; false when one is playing or the file is missing
	void Stop();
	void __stdcall Draw();  // SKSE Menu Framework HUD callback
}

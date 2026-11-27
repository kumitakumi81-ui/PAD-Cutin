// SPDX-License-Identifier: GPL-3.0-or-later
// Video.cpp - its own translation unit, so the Windows / Media Foundation headers do not reach the rest of the plugin
#include <Windows.h>
#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmsystem.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "d3d11.lib")

#include "MenuApi.h"
#include "Video.h"

namespace Video
{

	// shared between the decoder thread and the render thread
	inline std::mutex                g_lock;
	inline std::vector<std::uint8_t> g_frame;  // BGRA, top-down
	inline std::uint32_t             g_w = 0, g_h = 0, g_stride = 0;
	inline bool                      g_fresh = false;
	inline std::atomic<bool>         g_playing{ false };
	inline std::atomic<bool>         g_stop{ false };
	inline std::atomic<long long>    g_startNs{ 0 }, g_endNs{ 0 };

	// render thread only
	inline ID3D11Texture2D*          g_tex = nullptr;
	inline ID3D11ShaderResourceView* g_srv = nullptr;
	inline std::uint32_t             g_texW = 0, g_texH = 0;

	inline long long NowNs() { return std::chrono::steady_clock::now().time_since_epoch().count(); }

	inline void Decode(std::wstring a_path)
	{
		HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool co = SUCCEEDED(hr);
		if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
			if (co) CoUninitialize();
			g_playing = false;
			return;
		}
		IMFAttributes* attr = nullptr;
		MFCreateAttributes(&attr, 2);
		attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
		IMFSourceReader* reader = nullptr;
		hr = MFCreateSourceReaderFromURL(a_path.c_str(), attr, &reader);
		attr->Release();
		if (SUCCEEDED(hr)) {
			reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
			reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
			IMFMediaType* type = nullptr;
			MFCreateMediaType(&type);
			type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
			type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
			hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type);
			type->Release();
			UINT32 w = 0, h = 0;
			if (SUCCEEDED(hr)) {
				IMFMediaType* cur = nullptr;
				reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur);
				MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &w, &h);
				cur->Release();
			}
			const long long t0 = NowNs();
			g_startNs = t0;
			while (SUCCEEDED(hr) && !g_stop) {
				DWORD     flags = 0;
				LONGLONG  ts = 0;
				IMFSample* sample = nullptr;
				hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &ts, &sample);
				if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
					if (sample) sample->Release();
					break;
				}
				if (!sample) continue;
				// wait for the frame's time (100 ns units)
				const long long due = t0 + ts * 100;
				while (!g_stop && NowNs() < due) {
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
				}
				IMFMediaBuffer* buf = nullptr;
				if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf))) {
					std::scoped_lock lk(g_lock);
					g_w = w, g_h = h, g_stride = w * 4;
					g_frame.resize(static_cast<std::size_t>(w) * h * 4);
					bool         ok = false;
					IMF2DBuffer* b2 = nullptr;
					if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&b2)))) {
						BYTE* scan0 = nullptr;
						LONG  pitch = 0;
						if (SUCCEEDED(b2->Lock2D(&scan0, &pitch))) {  // pitch < 0: bottom-up rows
							for (UINT32 y = 0; y < h; ++y) {
								std::memcpy(g_frame.data() + static_cast<std::size_t>(y) * g_stride, scan0 + static_cast<std::ptrdiff_t>(y) * pitch, g_stride);
							}
							b2->Unlock2D();
							ok = true;
						}
						b2->Release();
					}
					if (!ok) {
						BYTE* data = nullptr;
						DWORD len = 0;
						if (SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
							if (len >= g_frame.size()) {
								std::memcpy(g_frame.data(), data, g_frame.size());
								ok = true;
							}
							buf->Unlock();
						}
					}
					if (ok && w && h) {
						for (std::size_t i = 3; i < g_frame.size(); i += 4) {
							g_frame[i] = 0xFF;  // RGB32 leaves alpha undefined
						}
						g_fresh = true;
					}
					buf->Release();
				}
				sample->Release();
			}
			reader->Release();
		}
		MFShutdown();
		if (co) CoUninitialize();
		g_endNs = NowNs();
		g_playing = false;
	}

	inline std::filesystem::path Dir() { return std::filesystem::current_path() / "Data" / "BFHeredity" / "Video"; }

	// any thread
	bool Play(const std::wstring& a_name)
	{
		if (g_playing.exchange(true)) {
			return false;
		}
		const auto video = Dir() / (a_name + L".mp4");
		const auto sound = Dir() / (a_name + L".wav");
		std::error_code ec;
		if (!std::filesystem::exists(video, ec)) {
			g_playing = false;
			return false;
		}
		g_stop = false;
		g_endNs = 0;
		{
			std::scoped_lock lk(g_lock);
			g_fresh = false;
			g_w = g_h = 0;
		}
		if (g_cfg.sound && std::filesystem::exists(sound, ec)) {
			PlaySoundW(sound.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
		}
		std::thread(Decode, video.wstring()).detach();
		return true;
	}

	void Stop()
	{
		g_stop = true;
		PlaySoundW(nullptr, nullptr, 0);
	}

	inline void EnsureTexture(ID3D11Device* a_dev, std::uint32_t a_w, std::uint32_t a_h)
	{
		if (g_tex && g_texW == a_w && g_texH == a_h) {
			return;
		}
		if (g_srv) g_srv->Release(), g_srv = nullptr;
		if (g_tex) g_tex->Release(), g_tex = nullptr;
		D3D11_TEXTURE2D_DESC d{};
		d.Width = a_w;
		d.Height = a_h;
		d.MipLevels = 1;
		d.ArraySize = 1;
		d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		d.SampleDesc.Count = 1;
		d.Usage = D3D11_USAGE_DYNAMIC;
		d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (SUCCEEDED(a_dev->CreateTexture2D(&d, nullptr, &g_tex))) {
			a_dev->CreateShaderResourceView(g_tex, nullptr, &g_srv);
			g_texW = a_w, g_texH = a_h;
		}
	}

	// render thread (SKSE Menu Framework HUD callback)
	void __stdcall Draw()
	{
		const long long now = NowNs();
		const long long end = g_endNs.load();
		const float     fade = std::max(g_cfg.fade, 0.01f);
		float           alpha = 1.0f;
		if (g_playing) {
			alpha = std::clamp(static_cast<float>((now - g_startNs.load()) * 1e-9) / fade, 0.0f, 1.0f);
		} else if (end != 0) {
			alpha = 1.0f - static_cast<float>((now - end) * 1e-9) / fade;
			if (alpha <= 0.0f) {
				return;
			}
		} else {
			return;
		}
		auto dev = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::Renderer::GetDevice());
		if (!dev) {
			return;
		}
		{
			std::scoped_lock lk(g_lock);
			if (g_fresh && g_w && g_h) {
				EnsureTexture(dev, g_w, g_h);
				ID3D11DeviceContext* ctx = nullptr;
				dev->GetImmediateContext(&ctx);
				D3D11_MAPPED_SUBRESOURCE m{};
				if (ctx && g_tex && SUCCEEDED(ctx->Map(g_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
					for (std::uint32_t y = 0; y < g_h; ++y) {
						std::memcpy(static_cast<std::uint8_t*>(m.pData) + static_cast<std::size_t>(y) * m.RowPitch, g_frame.data() + static_cast<std::size_t>(y) * g_stride, g_stride);
					}
					ctx->Unmap(g_tex, 0);
				}
				if (ctx) ctx->Release();
				g_fresh = false;
			}
		}
		if (!g_srv || !g_texW) {
			return;
		}
		auto        list = KKMenu::ForegroundDrawList();
		const auto  vp = KKMenu::MainViewportSize();
		const float w = vp.x * std::clamp(g_cfg.width, 0.1f, 0.8f);
		const float h = w * static_cast<float>(g_texH) / static_cast<float>(g_texW);
		const float m = vp.x * std::clamp(g_cfg.margin, 0.0f, 0.2f);
		const KKMenu::ImVec2 a{ vp.x - m - w, m }, b{ vp.x - m, m + h };
		const auto           A = static_cast<std::uint32_t>(alpha * 255.0f) & 0xFF;
		KKMenu::AddRectFilled(list, KKMenu::ImVec2{ a.x - 3, a.y - 3 }, KKMenu::ImVec2{ b.x + 3, b.y + 3 }, (std::min(A, 200u) << 24));
		KKMenu::AddImage(list, g_srv, a, b, (A << 24) | 0x00FFFFFF);
		KKMenu::AddRect(list, KKMenu::ImVec2{ a.x - 3, a.y - 3 }, KKMenu::ImVec2{ b.x + 3, b.y + 3 }, (A << 24) | 0x00D8C8B0, 1.5f);
	}
}

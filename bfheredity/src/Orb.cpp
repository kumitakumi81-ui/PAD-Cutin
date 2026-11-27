// SPDX-License-Identifier: GPL-3.0-or-later
// Orb.cpp - its own translation unit, so the D3D11 headers do not reach the rest of the plugin
#include <Windows.h>
#include <d3d11.h>

#include "MenuApi.h"
#include "Orb.h"

namespace Orb
{
	struct Header
	{
		char          magic[4];
		std::uint32_t fw, fh, cols, rows, frames;
		float         fps;
		std::uint32_t size;
	};
	struct Sheet
	{
		Header                    h{};
		ID3D11ShaderResourceView* srv = nullptr;
		bool                      tried = false;
	};
	inline std::unordered_map<std::string, Sheet> g_sheets;

	Sheet* Get(const char* a_name)
	{
		auto& s = g_sheets[a_name];
		if (s.tried) {
			return s.srv ? &s : nullptr;
		}
		s.tried = true;
		const auto    path = std::filesystem::current_path() / "Data" / "BFHeredity" / "Orb" / (std::string(a_name) + ".orb");
		std::ifstream f(path, std::ios::binary);
		if (!f || !f.read(reinterpret_cast<char*>(&s.h), sizeof(Header)) || std::memcmp(s.h.magic, "ORB1", 4) != 0) {
			return nullptr;
		}
		std::vector<char> data(s.h.size);
		if (!f.read(data.data(), data.size())) {
			return nullptr;
		}
		auto dev = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::Renderer::GetDevice());
		if (!dev) {
			s.tried = false;  // try again once the renderer is up
			return nullptr;
		}
		D3D11_TEXTURE2D_DESC d{};
		d.Width = s.h.fw * s.h.cols;
		d.Height = s.h.fh * s.h.rows;
		d.MipLevels = 1;
		d.ArraySize = 1;
		d.Format = DXGI_FORMAT_BC3_UNORM;
		d.SampleDesc.Count = 1;
		d.Usage = D3D11_USAGE_IMMUTABLE;
		d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA init{};
		init.pSysMem = data.data();
		init.SysMemPitch = (d.Width / 4) * 16;
		ID3D11Texture2D* tex = nullptr;
		if (FAILED(dev->CreateTexture2D(&d, &init, &tex)) || !tex) {
			return nullptr;
		}
		dev->CreateShaderResourceView(tex, nullptr, &s.srv);
		tex->Release();
		return s.srv ? &s : nullptr;
	}

	float Length(const char* a_name)
	{
		auto s = Get(a_name);
		return s && s->h.fps > 0 ? s->h.frames / s->h.fps : 0.0f;
	}

	bool Draw(void* a_dl, const char* a_name, float a_cx, float a_cy, float a_size, float a_time, float a_alpha, bool a_loop, std::uint32_t a_tint)
	{
		auto s = Get(a_name);
		if (!s || !a_dl || a_alpha <= 0.0f) {
			return false;
		}
		int k = static_cast<int>(a_time * s->h.fps);
		k = a_loop ? (k % static_cast<int>(s->h.frames)) : std::min(k, static_cast<int>(s->h.frames) - 1);
		k = std::max(k, 0);
		const float u0 = static_cast<float>(k % s->h.cols) / s->h.cols, v0 = static_cast<float>(k / s->h.cols) / s->h.rows;
		const float du = 1.0f / s->h.cols, dv = 1.0f / s->h.rows;
		const auto  a = static_cast<std::uint32_t>(std::clamp(a_alpha, 0.0f, 1.0f) * 255.0f);
		const float r = a_size * 0.5f;
		KKMenu::AddImageUV(a_dl, s->srv, { a_cx - r, a_cy - r }, { a_cx + r, a_cy + r }, { u0, v0 }, { u0 + du, v0 + dv }, (a << 24) | (a_tint & 0x00FFFFFFu));
		return true;
	}
}

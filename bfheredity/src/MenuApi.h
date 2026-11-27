#pragma once
// Minimal binding to SKSE Menu Framework 3 (QTR-Modding/SKSE-Menu-Framework-3-API, LGPL-2.1): only the calls this plugin
// uses, resolved by name from SKSEMenuFramework.dll exactly as the official header does. Everything is a no-op when the
// framework is not loaded.

namespace KKMenu
{
	struct ImVec2
	{
		float x, y;
	};

	using RenderFunction = void(__stdcall*)();

	inline HMODULE Module()
	{
		static HMODULE m = GetModuleHandleW(L"SKSEMenuFramework");
		return m;
	}

	template <class T>
	T Fn(const char* a_name)
	{
		auto m = Module();
		return m ? reinterpret_cast<T>(GetProcAddress(m, a_name)) : nullptr;
	}

	inline bool Installed() { return Module() != nullptr; }

	// the framework's main window; its first member is std::atomic<bool> IsOpen
	inline void OpenMainWindow()
	{
		static auto f = Fn<void* (*)()>("GetMainWindow");
		if (auto w = f ? f() : nullptr) {
			reinterpret_cast<std::atomic<bool>*>(w)->store(true);
		}
	}

	// A free-standing window that runs a render function wrapped in Begin/End; returns its IsOpen flag.
	inline std::atomic<bool>* AddWindow(RenderFunction a_render)
	{
		static auto f = Fn<void* (*)(RenderFunction)>("AddWindow");
		auto        w = f ? f(a_render) : nullptr;
		return w ? reinterpret_cast<std::atomic<bool>*>(w) : nullptr;
	}

	inline bool Begin(const char* a_name)
	{
		static auto f = Fn<bool (*)(const char*, bool*, int)>("igBegin");
		return f && f(a_name, nullptr, 0);
	}

	inline void End()
	{
		static auto f = Fn<void (*)()>("igEnd");
		if (f) {
			f();
		}
	}

	inline void AddSectionItem(const char* a_path, RenderFunction a_render)
	{
		static auto f = Fn<void (*)(const char*, RenderFunction)>("AddSectionItem");
		if (f) {
			f(a_path, a_render);
		}
	}

	inline bool Checkbox(const char* a_label, bool* a_v)
	{
		static auto f = Fn<bool (*)(const char*, bool*)>("igCheckbox");
		return f && f(a_label, a_v);
	}

	inline bool SliderFloat(const char* a_label, float* a_v, float a_min, float a_max, const char* a_fmt = "%.2f")
	{
		static auto f = Fn<bool (*)(const char*, float*, float, float, const char*, int)>("igSliderFloat");
		return f && f(a_label, a_v, a_min, a_max, a_fmt, 0);
	}

	inline bool SliderInt(const char* a_label, int* a_v, int a_min, int a_max, const char* a_fmt = "%d")
	{
		static auto f = Fn<bool (*)(const char*, int*, int, int, const char*, int)>("igSliderInt");
		return f && f(a_label, a_v, a_min, a_max, a_fmt, 0);
	}

	inline void Text(const char* a_text)
	{
		static auto f = Fn<void (*)(const char*, const char*)>("igTextUnformatted");
		if (f) {
			f(a_text, nullptr);
		}
	}

	inline void TextWrapped(const char* a_text)
	{
		static auto f = Fn<void (*)(const char*, va_list)>("igTextWrappedV");
		if (f) {
			f("%s", reinterpret_cast<va_list>(&a_text));  // x64: a va_list is a pointer to the argument slots
		}
	}

	inline void SeparatorText(const char* a_label)
	{
		static auto f = Fn<void (*)(const char*)>("igSeparatorText");
		if (f) {
			f(a_label);
		}
	}

	inline bool Button(const char* a_label)
	{
		static auto f = Fn<bool (*)(const char*, const ImVec2)>("igButton");
		return f && f(a_label, ImVec2{ 0, 0 });
	}

	inline void SameLine()
	{
		static auto f = Fn<void (*)(float, float)>("igSameLine");
		if (f) {
			f(0.0f, -1.0f);
		}
	}

	inline void Spacing()
	{
		static auto f = Fn<void (*)()>("igSpacing");
		if (f) {
			f();
		}
	}

	inline bool CollapsingHeader(const char* a_label, bool a_openByDefault)
	{
		static auto f = Fn<bool (*)(const char*, int)>("igCollapsingHeader_TreeNodeFlags");
		return f && f(a_label, a_openByDefault ? (1 << 5) : 0);  // ImGuiTreeNodeFlags_DefaultOpen
	}

	inline void Tooltip(const char* a_text)
	{
		static auto hovered = Fn<bool (*)(int)>("igIsItemHovered");
		static auto tip = Fn<void (*)(const char*, va_list)>("igSetTooltipV");
		if (hovered && tip && hovered(0)) {
			tip("%s", reinterpret_cast<va_list>(&a_text));
		}
	}

	inline void BeginDisabled(bool a_disabled)
	{
		static auto f = Fn<void (*)(bool)>("igBeginDisabled");
		if (f) {
			f(a_disabled);
		}
	}

	inline void EndDisabled()
	{
		static auto f = Fn<void (*)()>("igEndDisabled");
		if (f) {
			f();
		}
	}

	// ---- HUD drawing (cut-in letterbox)
	using HudCallback = void(__stdcall*)();

	inline std::int64_t RegisterHudElement(HudCallback a_cb)
	{
		static auto f = Fn<std::int64_t (*)(HudCallback)>("RegisterHudElement");
		return f ? f(a_cb) : 0;
	}

	inline void* ForegroundDrawList()
	{
		static auto f = Fn<void* (*)()>("igGetForegroundDrawList_Nil");
		return f ? f() : nullptr;
	}

	// ImGuiViewport: ImGuiID ID; int Flags; ImVec2 Pos; ImVec2 Size; ...
	inline ImVec2 MainViewportSize()
	{
		static auto f = Fn<void* (*)()>("igGetMainViewport");
		auto        vp = f ? static_cast<char*>(f()) : nullptr;
		return vp ? *reinterpret_cast<ImVec2*>(vp + 16) : ImVec2{ 0, 0 };
	}

	inline void AddRectFilled(void* a_list, ImVec2 a_min, ImVec2 a_max, std::uint32_t a_col)
	{
		static auto f = Fn<void (*)(void*, const ImVec2, const ImVec2, std::uint32_t, float, int)>("ImDrawList_AddRectFilled");
		if (f && a_list) {
			f(a_list, a_min, a_max, a_col, 0.0f, 0);
		}
	}

	// ImGui 1.90: ImTextureID is a void* (the D3D11 backend's ID3D11ShaderResourceView*)
	inline void AddImage(void* a_list, void* a_tex, ImVec2 a_min, ImVec2 a_max, std::uint32_t a_col)
	{
		static auto f = Fn<void (*)(void*, void*, const ImVec2, const ImVec2, const ImVec2, const ImVec2, std::uint32_t)>("ImDrawList_AddImage");
		if (f && a_list && a_tex) {
			f(a_list, a_tex, a_min, a_max, ImVec2{ 0, 0 }, ImVec2{ 1, 1 }, a_col);
		}
	}

	inline void AddRect(void* a_list, ImVec2 a_min, ImVec2 a_max, std::uint32_t a_col, float a_thickness)
	{
		static auto f = Fn<void (*)(void*, const ImVec2, const ImVec2, std::uint32_t, float, int, float)>("ImDrawList_AddRect");
		if (f && a_list) {
			f(a_list, a_min, a_max, a_col, 4.0f, 0, a_thickness);
		}
	}

	inline void AddTriangleFilled(void* a_list, ImVec2 a_p1, ImVec2 a_p2, ImVec2 a_p3, std::uint32_t a_col)
	{
		static auto f = Fn<void (*)(void*, const ImVec2, const ImVec2, const ImVec2, std::uint32_t)>("ImDrawList_AddTriangleFilled");
		if (f && a_list) {
			f(a_list, a_p1, a_p2, a_p3, a_col);
		}
	}

	// colours per corner: upper-left, upper-right, bottom-right, bottom-left
	inline void AddRectGradient(void* a_list, ImVec2 a_min, ImVec2 a_max, std::uint32_t a_ul, std::uint32_t a_ur, std::uint32_t a_br, std::uint32_t a_bl)
	{
		static auto f = Fn<void (*)(void*, const ImVec2, const ImVec2, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t)>(
			"ImDrawList_AddRectFilledMultiColor");
		if (f && a_list) {
			f(a_list, a_min, a_max, a_ul, a_ur, a_br, a_bl);
		}
	}

	// true / false: the current ImGui font has / lacks every one of these code points; nullopt: cannot tell (an older
	// framework without the two exports, or no font yet). Only valid inside a frame (a render or HUD callback).
	// ImWchar is 16 or 32 bit depending on the build; a code point below 0x10000 passed as 32 bit reads the same in both.
	inline bool GlyphQueryAvailable() { return Fn<void* (*)()>("igGetFont") && Fn<const void* (*)(void*, std::uint32_t)>("ImFont_FindGlyphNoFallback"); }

	inline std::optional<bool> FontHasGlyphs(std::initializer_list<std::uint32_t> a_codepoints)
	{
		static auto font = Fn<void* (*)()>("igGetFont");
		static auto find = Fn<const void* (*)(void*, std::uint32_t)>("ImFont_FindGlyphNoFallback");
		void*       f = font && find ? font() : nullptr;
		if (!f) {
			return std::nullopt;
		}
		for (auto c : a_codepoints) {
			if (!find(f, c)) {
				return false;
			}
		}
		return true;
	}

	// text in the current (Japanese-capable) font at any size
	inline void AddText(void* a_list, float a_size, ImVec2 a_pos, std::uint32_t a_col, const char* a_text)
	{
		static auto font = Fn<void* (*)()>("igGetFont");
		static auto f = Fn<void (*)(void*, const void*, float, const ImVec2, std::uint32_t, const char*, const char*, float, const void*)>(
			"ImDrawList_AddText_FontPtr");
		if (f && font && a_list) {
			f(a_list, font(), a_size, a_pos, a_col, a_text, nullptr, 0.0f, nullptr);
		}
	}

	inline void AddCircleFilled(void* a_list, ImVec2 a_c, float a_r, std::uint32_t a_col, int a_seg = 32)
	{
		static auto f = Fn<void (*)(void*, const ImVec2, float, std::uint32_t, int)>("ImDrawList_AddCircleFilled");
		if (f && a_list) {
			f(a_list, a_c, a_r, a_col, a_seg);
		}
	}

	inline void AddCircle(void* a_list, ImVec2 a_c, float a_r, std::uint32_t a_col, float a_thickness, int a_seg = 32)
	{
		static auto f = Fn<void (*)(void*, const ImVec2, float, std::uint32_t, int, float)>("ImDrawList_AddCircle");
		if (f && a_list) {
			f(a_list, a_c, a_r, a_col, a_seg, a_thickness);
		}
	}

	inline void AddLine(void* a_list, ImVec2 a_a, ImVec2 a_b, std::uint32_t a_col, float a_thickness)
	{
		static auto f = Fn<void (*)(void*, const ImVec2, const ImVec2, std::uint32_t, float)>("ImDrawList_AddLine");
		if (f && a_list) {
			f(a_list, a_a, a_b, a_col, a_thickness);
		}
	}

	inline void AddBezierCubic(void* a_list, ImVec2 a_p1, ImVec2 a_p2, ImVec2 a_p3, ImVec2 a_p4, std::uint32_t a_col, float a_thickness)
	{
		static auto f = Fn<void (*)(void*, const ImVec2, const ImVec2, const ImVec2, const ImVec2, std::uint32_t, float, int)>("ImDrawList_AddBezierCubic");
		if (f && a_list) {
			f(a_list, a_p1, a_p2, a_p3, a_p4, a_col, a_thickness, 0);
		}
	}

	inline void AddConvexPolyFilled(void* a_list, const ImVec2* a_pts, int a_n, std::uint32_t a_col)
	{
		static auto f = Fn<void (*)(void*, const ImVec2*, int, std::uint32_t)>("ImDrawList_AddConvexPolyFilled");
		if (f && a_list) {
			f(a_list, a_pts, a_n, a_col);
		}
	}

	inline void AddImageUV(void* a_list, void* a_tex, ImVec2 a_min, ImVec2 a_max, ImVec2 a_uv0, ImVec2 a_uv1, std::uint32_t a_col)
	{
		static auto f = Fn<void (*)(void*, void*, const ImVec2, const ImVec2, const ImVec2, const ImVec2, std::uint32_t)>("ImDrawList_AddImage");
		if (f && a_list && a_tex) {
			f(a_list, a_tex, a_min, a_max, a_uv0, a_uv1, a_col);
		}
	}
}

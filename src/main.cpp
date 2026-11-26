// PAD Cutin - armor break cut-in camera for Procedural Armor Damage
// Camera work adapted from Kagura Style Armor Break 1.2.0, Copyright (C) 2026 miyata0105.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MenuApi.h"

namespace
{
	// ------------------------------------------------------------------ logging
	std::mutex    g_logLock;
	std::ofstream g_log;

	void OpenLog()
	{
		auto dir = SKSE::log::log_directory();
		if (!dir) {
			return;
		}
		std::filesystem::create_directories(*dir);
		g_log.open(*dir / L"PADCutin.log", std::ios::out | std::ios::trunc);
	}

	template <class... Args>
	void Log(std::format_string<Args...> a_fmt, Args&&... a_args)
	{
		std::scoped_lock l(g_logLock);
		if (!g_log.is_open()) {
			return;
		}
		g_log << std::format(a_fmt, std::forward<Args>(a_args)...) << '\n';
		g_log.flush();
	}

	// ------------------------------------------------------------------ settings
	struct Settings
	{
		bool  cutinPlayer = true;
		bool  onBreak = true;          // ArmorTear_OnBroken
		bool  onHeavyTear = false;     // ArmorTear_OnTear below fHeavyTearIntegrity (once per armor until repaired)
		float heavyTearIntegrity = 40.0f;
		float cutinSlowMo = 0.1f;
		float cutinLength = 1.0f;
		float cutinPartDistance = 42.0f;
		float cutinFaceDistance = 75.0f;
		float cutinHitStop = 0.12f;
		bool  cutinFlash = true;
		float cutinFlashStrength = 0.6f;
		bool  cutinLetterbox = true;
		bool  cutinBlur = true;
		float cutinShake = 2.0f;
		bool  cutinRandomAngle = true;
		bool  cutinFaceAction = true;
		float cutinDutch = 10.0f;
		bool  cutinSpeedLines = false;
		bool  cutinVignette = true;
		float cutinCooldown = 15.0f;
		bool  cutinHold = true;
		float cutinHoldRadius = 24.0f;
	};
	Settings g_settings;

	struct Ini
	{
		std::unordered_map<std::string, std::string> kv;  // "section.key" lowercase -> value

		explicit Ini(const std::filesystem::path& a_path)
		{
			std::ifstream f(a_path);
			std::string   line, section;
			while (std::getline(f, line)) {
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				const auto first = line.find_first_not_of(" \t");
				if (first == std::string::npos || line[first] == ';' || line[first] == '#') {
					continue;
				}
				if (line[first] == '[') {
					const auto end = line.find(']', first);
					section = Lower(line.substr(first + 1, end == std::string::npos ? std::string::npos : end - first - 1));
					continue;
				}
				const auto eq = line.find('=');
				if (eq == std::string::npos) {
					continue;
				}
				auto key = line.substr(first, eq - first);
				key.erase(key.find_last_not_of(" \t") + 1);
				auto val = line.substr(eq + 1);
				if (const auto sc = val.find(';'); sc != std::string::npos) {
					val.erase(sc);
				}
				val.erase(0, val.find_first_not_of(" \t"));
				val.erase(val.find_last_not_of(" \t") + 1);
				kv[section + "." + Lower(key)] = val;
			}
		}

		static std::string Lower(std::string a_s)
		{
			for (auto& c : a_s) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return a_s;
		}

		const std::string* Get(const char* a_sec, const char* a_key) const
		{
			auto it = kv.find(Lower(a_sec) + "." + Lower(a_key));
			return it == kv.end() || it->second.empty() ? nullptr : &it->second;
		}

		bool B(const char* a_sec, const char* a_key, bool a_def) const
		{
			auto v = Get(a_sec, a_key);
			if (!v) {
				return a_def;
			}
			const auto l = Lower(*v);
			return l == "1" || l == "true" || l == "yes" || l == "on";
		}

		float F(const char* a_sec, const char* a_key, float a_def) const
		{
			auto v = Get(a_sec, a_key);
			if (!v) {
				return a_def;
			}
			try {
				return std::stof(*v);
			} catch (...) {
				return a_def;
			}
		}
	};

	std::filesystem::path IniPath() { return std::filesystem::absolute(L"Data\\SKSE\\Plugins\\PADCutin.ini"); }

	void LoadSettings()
	{
		const Ini ini(IniPath());
		auto&     s = g_settings;
		const char* G = "General";
		s.cutinPlayer = ini.B(G, "bCutinPlayer", s.cutinPlayer);
		s.onBreak = ini.B(G, "bOnBreak", s.onBreak);
		s.onHeavyTear = ini.B(G, "bOnHeavyTear", s.onHeavyTear);
		s.heavyTearIntegrity = ini.F(G, "fHeavyTearIntegrity", s.heavyTearIntegrity);
		const char* C = "Cutin";
		s.cutinSlowMo = ini.F(C, "fCutinSlowMo", s.cutinSlowMo);
		s.cutinLength = ini.F(C, "fCutinLength", s.cutinLength);
		s.cutinPartDistance = ini.F(C, "fCutinPartDistance", s.cutinPartDistance);
		s.cutinFaceDistance = ini.F(C, "fCutinFaceDistance", s.cutinFaceDistance);
		s.cutinHitStop = ini.F(C, "fCutinHitStop", s.cutinHitStop);
		s.cutinFlash = ini.B(C, "bCutinFlash", s.cutinFlash);
		s.cutinFlashStrength = ini.F(C, "fCutinFlashStrength", s.cutinFlashStrength);
		s.cutinLetterbox = ini.B(C, "bCutinLetterbox", s.cutinLetterbox);
		s.cutinBlur = ini.B(C, "bCutinBlur", s.cutinBlur);
		s.cutinShake = ini.F(C, "fCutinShake", s.cutinShake);
		s.cutinRandomAngle = ini.B(C, "bCutinRandomAngle", s.cutinRandomAngle);
		s.cutinFaceAction = ini.B(C, "bCutinFaceAction", s.cutinFaceAction);
		s.cutinDutch = ini.F(C, "fCutinDutch", s.cutinDutch);
		s.cutinSpeedLines = ini.B(C, "bCutinSpeedLines", s.cutinSpeedLines);
		s.cutinVignette = ini.B(C, "bCutinVignette", s.cutinVignette);
		s.cutinCooldown = ini.F(C, "fCutinCooldown", s.cutinCooldown);
		s.cutinHold = ini.B(C, "bCutinHold", s.cutinHold);
		s.cutinHoldRadius = ini.F(C, "fCutinHoldRadius", s.cutinHoldRadius);
		Log("settings loaded: break={} heavyTear={}({:.0f}) slowmo={:.2f} length={:.2f} part={:.0f} face={:.0f}", s.onBreak, s.onHeavyTear,
			s.heavyTearIntegrity, s.cutinSlowMo, s.cutinLength, s.cutinPartDistance, s.cutinFaceDistance);
	}

	std::filesystem::file_time_type g_iniTime{};

	void WatchIni()
	{
		std::error_code ec;
		const auto      t = std::filesystem::last_write_time(IniPath(), ec);
		if (ec) {
			return;
		}
		if (g_iniTime != std::filesystem::file_time_type{} && t != g_iniTime) {
			SKSE::GetTaskInterface()->AddTask([] { LoadSettings(); });
		}
		g_iniTime = t;
	}
}

namespace
{
#include "Cutin.h"

	// ------------------------------------------------------------------ Procedural Armor Damage events
	std::unordered_set<RE::FormID> g_heavyDone;  // armor that already had its heavy-tear cut-in (main thread)

	std::uint32_t SlotMaskOf(RE::TESForm* a_form)
	{
		auto armo = a_form ? a_form->As<RE::TESObjectARMO>() : nullptr;
		return armo ? armo->GetSlotMask().underlying() : 0u;
	}

	class ModEventSink : public RE::BSTEventSink<SKSE::ModCallbackEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
		{
			if (!a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const std::string_view name = a_event->eventName.c_str() ? a_event->eventName.c_str() : "";
			if (!name.starts_with("ArmorTear_")) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const RE::FormID    id = a_event->sender ? a_event->sender->GetFormID() : 0;
			const std::uint32_t mask = SlotMaskOf(a_event->sender);
			const float         num = a_event->numArg;
			const std::string   ev(name);
			SKSE::GetTaskInterface()->AddTask([ev, id, mask, num] {
				auto player = RE::PlayerCharacter::GetSingleton();
				if (!player) {
					return;
				}
				if (ev == "ArmorTear_OnBroken") {
					Log("PAD: armor {:08X} broken (mask 0x{:X})", id, mask);
					if (g_settings.onBreak) {
						Cutin::Request(player, mask);
					}
				} else if (ev == "ArmorTear_OnTear") {
					if (g_settings.onHeavyTear && num <= g_settings.heavyTearIntegrity && !g_heavyDone.contains(id)) {
						g_heavyDone.insert(id);
						Log("PAD: armor {:08X} heavily torn ({:.0f}%)", id, num);
						Cutin::Request(player, mask);
					}
				} else if (ev == "ArmorTear_OnRepaired") {
					g_heavyDone.erase(id);
				}
			});
			return RE::BSEventNotifyControl::kContinue;
		}
	};
	ModEventSink g_modSink;

	// ------------------------------------------------------------------ ticker
	std::atomic<bool> g_taskPending{ false };

	void Ticker()
	{
		int n = 0;
		for (;;) {
			std::this_thread::sleep_for(50ms);
			if (++n % 20 == 0) {
				WatchIni();
			}
			if (!g_taskPending.exchange(true)) {
				SKSE::GetTaskInterface()->AddTask([] {
					g_taskPending = false;
					Cutin::Poll();
				});
			}
		}
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			LoadSettings();
			Cutin::InstallHooks();
			if (auto src = SKSE::GetModCallbackEventSource()) {
				src->AddEventSink(&g_modSink);
				Log("listening for Procedural Armor Damage events");
			} else {
				Log("ERROR: no mod event source");
			}
			if (KKMenu::Installed()) {
				KKMenu::RegisterHudElement(Cutin::DrawLetterbox);
				Log("letterbox / vignette: SKSE Menu Framework found");
			} else {
				Log("SKSE Menu Framework not loaded: no letterbox / vignette (the camera still works)");
			}
			if (!GetModuleHandleW(L"ProceduralArmorDamage")) {
				Log("WARNING: ProceduralArmorDamage.dll is not loaded");
			}
			std::thread(Ticker).detach();
			Log("ready");
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			SKSE::GetTaskInterface()->AddTask([] {
				Cutin::Stop("game load");
				g_heavyDone.clear();
			});
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, false);
	OpenLog();
	Log("PAD Cutin {} runtime={}", PADCUTIN_VERSION_STRING, REL::Module::get().version().string());
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}

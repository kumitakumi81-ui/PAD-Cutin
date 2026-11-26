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
		// Modesty bridge (Dynamic Feminine Female Modesty Animations): torn clothes get its "Modesty" keyword
		bool  modesty = true;
		float modestyIntegrity = 60.0f;
		bool  modestyCutin = true;
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
		const char* M = "Modesty";
		s.modesty = ini.B(M, "bModesty", s.modesty);
		s.modestyIntegrity = ini.F(M, "fModestyIntegrity", s.modestyIntegrity);
		s.modestyCutin = ini.B(M, "bModestyCutin", s.modestyCutin);
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


	// ------------------------------------------------------------------ Modesty bridge
	// Dynamic Feminine Female Modesty Animations switches the idle / walk set when a garment in the body (32), waist (49) or
	// underwear (52) slot carries its "Modesty" keyword (Modesty_Keyword.esp 000D59). A garment torn below fModestyIntegrity
	// gets that keyword for as long as it stays torn; the repair takes it off again. Kept in the SKSE co-save.
	constexpr std::uint32_t kModestySlots = (1u << (32 - 30)) | (1u << (49 - 30)) | (1u << (52 - 30));
	std::unordered_map<RE::FormID, bool> g_modesty;  // armor -> the keyword was added by us (false: it already had it)

	RE::BGSKeyword* ModestyKeyword()
	{
		static RE::BGSKeyword* s_kw = [] {
			auto dh = RE::TESDataHandler::GetSingleton();
			auto kw = dh ? dh->LookupForm<RE::BGSKeyword>(0xD59, "Modesty_Keyword.esp") : nullptr;
			Log("Modesty keyword {}", kw ? std::format("{:08X}", kw->GetFormID()) : "not found (Modesty_Keyword.esp missing): the Modesty bridge is off"s);
			return kw;
		}();
		return s_kw;
	}

	bool ApplyModesty(RE::FormID a_id)
	{
		auto armo = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_id);
		auto kw = ModestyKeyword();
		if (!armo || !kw || g_modesty.contains(a_id)) {
			return false;
		}
		const bool had = armo->HasKeyword(kw);
		if (!had) {
			armo->AddKeyword(kw);
		}
		g_modesty[a_id] = !had;
		Log("modesty on: {:08X}{}", a_id, had ? " (already had the keyword)" : "");
		return true;
	}

	void ClearModesty(RE::FormID a_id)
	{
		auto it = g_modesty.find(a_id);
		if (it == g_modesty.end()) {
			return;
		}
		auto armo = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_id);
		if (it->second && armo && ModestyKeyword()) {
			armo->RemoveKeyword(ModestyKeyword());
		}
		g_modesty.erase(it);
		Log("modesty off: {:08X}", a_id);
	}

	void ClearAllModesty()
	{
		std::vector<RE::FormID> ids;
		for (auto& [id, added] : g_modesty) {
			ids.push_back(id);
		}
		for (auto id : ids) {
			ClearModesty(id);
		}
	}

	constexpr std::uint32_t kSerUid = 'PADC';
	constexpr std::uint32_t kSerModesty = 'MODS';

	void OnSave(SKSE::SerializationInterface* a_ser)
	{
		if (!a_ser->OpenRecord(kSerModesty, 1)) {
			return;
		}
		const std::uint32_t n = static_cast<std::uint32_t>(g_modesty.size());
		a_ser->WriteRecordData(n);
		for (auto& [id, added] : g_modesty) {
			a_ser->WriteRecordData(id);
		}
	}

	void OnRevert(SKSE::SerializationInterface*)
	{
		ClearAllModesty();
	}

	void OnLoad(SKSE::SerializationInterface* a_ser)
	{
		ClearAllModesty();
		std::uint32_t type, version, length;
		while (a_ser->GetNextRecordInfo(type, version, length)) {
			if (type != kSerModesty) {
				continue;
			}
			std::uint32_t n = 0;
			a_ser->ReadRecordData(n);
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::FormID old = 0, now = 0;
				a_ser->ReadRecordData(old);
				if (a_ser->ResolveFormID(old, now)) {
					ApplyModesty(now);
				}
			}
		}
		Log("co-save: {} torn garment(s) restored", g_modesty.size());
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
					if (g_settings.modesty && (mask & kModestySlots) && num <= g_settings.modestyIntegrity && !g_modesty.contains(id)) {
						if (ApplyModesty(id) && g_settings.modestyCutin) {
							Cutin::Request(player, mask);
						}
					}
					if (g_settings.onHeavyTear && num <= g_settings.heavyTearIntegrity && !g_heavyDone.contains(id)) {
						g_heavyDone.insert(id);
						Log("PAD: armor {:08X} heavily torn ({:.0f}%)", id, num);
						Cutin::Request(player, mask);
					}
				} else if (ev == "ArmorTear_OnRepaired") {
					g_heavyDone.erase(id);
					ClearModesty(id);
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
			ModestyKeyword();
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
	if (auto ser = SKSE::GetSerializationInterface()) {
		ser->SetUniqueID(kSerUid);
		ser->SetSaveCallback(OnSave);
		ser->SetLoadCallback(OnLoad);
		ser->SetRevertCallback(OnRevert);
	}
	return true;
}

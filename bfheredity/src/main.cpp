// BF Heredity - Beeing Female NG: the player's children inherit both parents' looks, and keep the name you give them.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// How it works
//  * BFHeredity.esp holds a pool of copies of the player-baby actors of every race (Beeing Female RS patch). A copy has no
//    pre-baked FaceGen, so the game builds its head from the actor data at run time.
//  * Before each birth the race lists Beeing Female reads (StorageUtil FormList "FW.AddOn.BabyActor_FemalePlayer" /
//    "..._MalePlayer" on the race) are pointed at a free copy, so every child gets its own actor base.
//  * When the child appears, its mother / father (StorageUtil "FW.Child.Mother" / "FW.Child.Father") are read and the copy
//    gets a mix of both: hair colour, hair / eyes / brows (when the part fits the child race), face sliders, skin tone, weight.
//    The mix is rebuilt from a stored seed on every load (SKSE co-save), so it never changes.
//  * Naming: a text box (UIExtensions) when the child is born, and the rename key while looking at any Beeing Female child.
//    The name goes through FWChildActor.SetName, which writes FW.Child.Name, so it survives loading.

#include "PoolData.h"
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
		g_log.open(*dir / L"BFHeredity.log", std::ios::out | std::ios::trunc);
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
		bool          inherit = true;
		bool          nameOnBirth = true;
		std::uint32_t renameKey = 0x31;  // N
		bool          hairStyle = true;
		bool          eyes = true;
		bool          faceShape = true;
		bool          skinTone = true;
		// DNA
		bool          dna = true;
		std::uint32_t collectKey = 0;  // optional hotkey (0 = only through dialogue)
		float         virility100 = 0.9f;  // Beeing Female virility (stored sperm, 0.02 - 1.0) at or above this: 100 %
		float         virility50 = 0.5f;   // at or above this: 50 %, below: 30 %
		float         fhuAmount = 1.0f;
		bool          maleOnly = true;
		bool          animations = true;      // Soulgem Oven's extraction / insertion animations
		float         collectSeconds = 8.0f;  // length of the extraction animation before the DNA drops
		float         insertSeconds = 4.0f;   // length of the insertion animation before it is in
		bool          sounds = true;          // SexLab voices during collection / insertion
		float         voiceInterval = 2.5f;
		// camera (PAD Cutin / KSAB camera work): whole body -> circle -> face -> back
		bool  camera = true;
		// fields read by Cutin.h
		bool  cutinPlayer = true;
		float cutinSlowMo = 0.6f;
		float cutinLength = 2.4f;
		float cutinPartDistance = 150.0f;
		float cutinFaceDistance = 75.0f;
		float cutinHitStop = 0.0f;
		bool  cutinFlash = false;
		float cutinFlashStrength = 0.3f;
		bool  cutinLetterbox = true;
		bool  cutinBlur = false;
		float cutinShake = 0.0f;
		bool  cutinRandomAngle = true;
		bool  cutinFaceAction = false;
		float cutinDutch = 4.0f;
		bool  cutinSpeedLines = false;
		bool  cutinVignette = true;
		float cutinCooldown = 0.0f;
		bool  cutinHold = true;
		float cutinHoldRadius = 24.0f;
	};
	Settings g_settings;

	std::string Trim(std::string s)
	{
		s.erase(0, s.find_first_not_of(" \t\r"));
		s.erase(s.find_last_not_of(" \t\r") + 1);
		return s;
	}

	void LoadSettings()
	{
		std::ifstream f("Data/SKSE/Plugins/BFHeredity.ini");
		std::string   line;
		while (std::getline(f, line)) {
			if (auto sc = line.find(';'); sc != std::string::npos) {
				line.erase(sc);
			}
			auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			auto key = Trim(line.substr(0, eq));
			auto val = Trim(line.substr(eq + 1));
			for (auto& c : key) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			auto b = [&] { return val == "1" || val == "true"; };
			if (key == "binherit") {
				g_settings.inherit = b();
			} else if (key == "bnameonbirth") {
				g_settings.nameOnBirth = b();
			} else if (key == "irenamekey") {
				g_settings.renameKey = static_cast<std::uint32_t>(std::strtoul(val.c_str(), nullptr, 0));
			} else if (key == "bhairstyle") {
				g_settings.hairStyle = b();
			} else if (key == "beyes") {
				g_settings.eyes = b();
			} else if (key == "bfaceshape") {
				g_settings.faceShape = b();
			} else if (key == "bskintone") {
				g_settings.skinTone = b();
			} else if (key == "bdna") {
				g_settings.dna = b();
			} else if (key == "icollectkey") {
				g_settings.collectKey = static_cast<std::uint32_t>(std::strtoul(val.c_str(), nullptr, 0));
			} else if (key == "fvirility100") {
				g_settings.virility100 = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "fvirility50") {
				g_settings.virility50 = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "ffhuamount") {
				g_settings.fhuAmount = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "bmaleonly") {
				g_settings.maleOnly = b();
			} else if (key == "bcamera") {
				g_settings.camera = b();
				g_settings.cutinPlayer = g_settings.camera;
			} else if (key == "fcameraslowmo") {
				g_settings.cutinSlowMo = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "fcamerabodydistance") {
				g_settings.cutinPartDistance = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "fcamerafacedistance") {
				g_settings.cutinFaceDistance = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "bcameraletterbox") {
				g_settings.cutinLetterbox = b();
			} else if (key == "bcameravignette") {
				g_settings.cutinVignette = b();
			} else if (key == "bsounds") {
				g_settings.sounds = b();
			} else if (key == "fvoiceinterval") {
				g_settings.voiceInterval = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "banimations") {
				g_settings.animations = b();
			} else if (key == "fcollectseconds") {
				g_settings.collectSeconds = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "finsertseconds") {
				g_settings.insertSeconds = static_cast<float>(std::atof(val.c_str()));
			}
		}
		Log("settings: inherit={} nameOnBirth={} renameKey=0x{:X} hair={} eyes={} face={} skin={}", g_settings.inherit, g_settings.nameOnBirth,
			g_settings.renameKey, g_settings.hairStyle, g_settings.eyes, g_settings.faceShape, g_settings.skinTone);
	}

#include "Cutin.h"

	// whole body (body slot) -> face, the cut lasting about a_seconds
	void Camera(RE::Actor* a, float a_seconds)
	{
		if (!a || !g_settings.camera) {
			return;
		}
		g_settings.cutinLength = std::clamp(a_seconds / 3.25f, 0.3f, 4.0f);
		Cutin::Request(a, 1u << (32 - 30));
	}

	// ------------------------------------------------------------------ Papyrus calls
	using Callback = std::function<void(const RE::BSScript::Variable&)>;

	class Functor : public RE::BSScript::IStackCallbackFunctor
	{
	public:
		explicit Functor(Callback a_cb) :
			_cb(std::move(a_cb)) {}
		void operator()(RE::BSScript::Variable a_result) override
		{
			if (_cb) {
				auto cb = _cb;
				auto v = a_result;
				SKSE::GetTaskInterface()->AddTask([cb, v] { cb(v); });
			}
		}
		void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

	private:
		Callback _cb;
	};

	template <class... Args>
	bool CallStatic(const char* a_class, const char* a_fn, Callback a_cb, Args&&... a_args)
	{
		auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm) {
			return false;
		}
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb(new Functor(std::move(a_cb)));
		auto args = RE::MakeFunctionArguments(static_cast<std::decay_t<Args>>(a_args)...);
		return vm->DispatchStaticCall(a_class, a_fn, args, cb);
	}

	template <class... Args>
	bool CallMethod(RE::TESForm* a_obj, const char* a_class, const char* a_fn, Callback a_cb, Args&&... a_args)
	{
		auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm || !a_obj) {
			return false;
		}
		auto policy = vm->GetObjectHandlePolicy();
		auto handle = policy->GetHandleForObject(a_obj->GetFormType(), a_obj);
		if (handle == policy->EmptyHandle()) {
			return false;
		}
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb(new Functor(std::move(a_cb)));
		auto args = RE::MakeFunctionArguments(static_cast<std::decay_t<Args>>(a_args)...);
		return vm->DispatchMethodCall(handle, a_class, a_fn, args, cb);
	}

	RE::TESForm* AsForm(const RE::BSScript::Variable& v)
	{
		if (!v.IsObject() || v.IsNoneObject()) {
			return nullptr;
		}
		return v.Unpack<RE::TESForm*>();
	}

	void Notify(const std::string& a_text)
	{
		CallStatic("Debug", "Notification", nullptr, std::string(a_text));
	}

	// ------------------------------------------------------------------ pool state
	enum class State : std::uint8_t
	{
		kFree = 0,
		kBorn = 1,     // actor seen, parents not read yet
		kApplied = 2,  // looks inherited
	};

	// the inherited result, stored as-is so it never depends on the parents still existing / looking the same
	struct Looks
	{
		RE::FormID   hairColor = 0;
		RE::FormID   hair = 0;
		RE::FormID   eyes = 0;
		RE::FormID   brows = 0;
		bool         face = false;
		float        morphs[RE::TESNPC::FaceData::Morphs::kTotal]{};
		std::int32_t presets[RE::TESNPC::FaceData::Parts::kTotal]{};
		bool         skin = false;
		std::uint8_t rgb[3]{};
		bool         hasWeight = false;
		float        weight = 50.0f;
	};

	struct Slot
	{
		RE::FormID    base = 0;  // runtime id of the copy
		int           orig = 0;  // index into PoolData::kOrigs
		State         state = State::kFree;
		RE::FormID    actor = 0;
		RE::FormID    mother = 0;
		RE::FormID    father = 0;
		std::uint32_t seed = 0;
		bool          named = false;
		float         bornAt = 0.0f;
		bool          querying = false;
		Looks         looks;
	};

	std::vector<Slot>                     g_slots;
	std::unordered_map<RE::FormID, int>   g_slotByBase;
	std::atomic_bool                      g_ready = false;
	std::atomic_bool                      g_prompting = false;
	std::uint64_t                         g_promptTick = 0;
	std::deque<RE::FormID>                g_namingQueue;
	std::atomic<std::uint64_t>            g_tick = 0;

	constexpr const char* kPlugin = "BFHeredity.esp";

	void BuildSlots()
	{
		g_slots.clear();
		g_slotByBase.clear();
		auto dh = RE::TESDataHandler::GetSingleton();
		if (!dh || !dh->LookupModByName(kPlugin)) {
			Log("ERROR: {} is not active", kPlugin);
			return;
		}
		for (int o = 0; o < static_cast<int>(std::size(PoolData::kOrigs)); ++o) {
			for (auto id : PoolData::kOrigs[o].clones) {
				auto npc = dh->LookupForm<RE::TESNPC>(id, kPlugin);
				if (!npc) {
					Log("missing pool actor {:X}", id);
					continue;
				}
				Slot s;
				s.base = npc->GetFormID();
				s.orig = o;
				g_slotByBase[s.base] = static_cast<int>(g_slots.size());
				g_slots.push_back(s);
			}
		}
		Log("pool: {} copies", g_slots.size());
	}

	// ------------------------------------------------------------------ appearance
	struct Snapshot  // the copy's own data, to undo our changes on revert
	{
		RE::BGSColorForm*         hairColor = nullptr;
		std::vector<RE::BGSHeadPart*> parts;
		float                     morphs[RE::TESNPC::FaceData::Morphs::kTotal]{};
		std::int32_t              presets[RE::TESNPC::FaceData::Parts::kTotal]{};
		bool                      hasFace = false;
		RE::Color                 skin{};
		float                     weight = 50.0f;
	};
	std::unordered_map<RE::FormID, Snapshot> g_snap;

	void TakeSnapshot(RE::TESNPC* a_npc)
	{
		if (g_snap.contains(a_npc->GetFormID())) {
			return;
		}
		Snapshot s;
		s.hairColor = a_npc->headRelatedData ? a_npc->headRelatedData->hairColor : nullptr;
		for (std::int8_t i = 0; i < a_npc->numHeadParts; ++i) {
			s.parts.push_back(a_npc->headParts[i]);
		}
		if (a_npc->faceData) {
			s.hasFace = true;
			std::memcpy(s.morphs, a_npc->faceData->morphs, sizeof(s.morphs));
			std::memcpy(s.presets, a_npc->faceData->parts, sizeof(s.presets));
		}
		s.skin = a_npc->bodyTintColor;
		s.weight = a_npc->weight;
		g_snap[a_npc->GetFormID()] = std::move(s);
	}

	void RestoreSnapshot(RE::TESNPC* a_npc)
	{
		auto it = g_snap.find(a_npc->GetFormID());
		if (it == g_snap.end()) {
			return;
		}
		auto& s = it->second;
		if (s.hairColor) {
			a_npc->SetHairColor(s.hairColor);
		}
		for (auto p : s.parts) {
			if (p) {
				a_npc->ChangeHeadPart(p);
			}
		}
		if (s.hasFace && a_npc->faceData) {
			std::memcpy(a_npc->faceData->morphs, s.morphs, sizeof(s.morphs));
			std::memcpy(a_npc->faceData->parts, s.presets, sizeof(s.presets));
		}
		a_npc->bodyTintColor = s.skin;
		a_npc->weight = s.weight;
	}

	bool FitsRace(RE::BGSHeadPart* a_part, RE::TESRace* a_race)
	{
		if (!a_part || !a_race) {
			return false;
		}
		return !a_part->validRaces || a_part->validRaces->HasForm(a_race);
	}

	std::vector<RE::BGSHeadPart*> PartsForRace(RE::BGSHeadPart::HeadPartType a_type, RE::TESRace* a_race, bool a_female)
	{
		std::vector<RE::BGSHeadPart*> out;
		auto dh = RE::TESDataHandler::GetSingleton();
		if (!dh || !a_race) {
			return out;
		}
		for (auto p : dh->GetFormArray<RE::BGSHeadPart>()) {
			if (!p || p->type.get() != a_type || !p->flags.all(RE::BGSHeadPart::Flag::kPlayable) || p->flags.all(RE::BGSHeadPart::Flag::kIsExtraPart) || !p->validRaces || !p->validRaces->HasForm(a_race)) {
				continue;
			}
			const bool male = p->flags.all(RE::BGSHeadPart::Flag::kMale);
			const bool female = p->flags.all(RE::BGSHeadPart::Flag::kFemale);
			if ((a_female && !female && male) || (!a_female && !male && female)) {
				continue;
			}
			out.push_back(p);
		}
		return out;
	}

	// does this parent's (adult) race give the copy's child race? (PoolData race table)
	bool RaceMatches(const Slot& a_slot, RE::TESNPC* a_parent)
	{
		if (!a_parent || !a_parent->race) {
			return false;
		}
		auto dh = RE::TESDataHandler::GetSingleton();
		for (auto& r : PoolData::kRaces) {
			if (r.female != a_slot.orig && r.male != a_slot.orig) {
				continue;
			}
			if (dh->LookupForm<RE::TESRace>(r.id, r.plugin) == a_parent->race) {
				return true;
			}
		}
		return false;
	}

	RE::TESNPC* LooksOf(RE::FormID a_id)
	{
		auto a = a_id ? RE::TESForm::LookupByID<RE::Actor>(a_id) : nullptr;
		return a ? a->GetActorBase() : nullptr;
	}

	// mix the parents into a Looks (called once, at birth)
	Looks ComputeLooks(const Slot& a_slot, RE::TESRace* a_race)
	{
		Looks L;
		auto  mom = LooksOf(a_slot.mother);
		auto  dad = LooksOf(a_slot.father);
		if (!mom && !dad) {
			return L;
		}
		std::mt19937 rng(a_slot.seed);
		auto         coin = [&] { return (rng() & 1u) != 0; };
		auto         pick = [&](bool a_dad) -> RE::TESNPC* { return a_dad ? (dad ? dad : mom) : (mom ? mom : dad); };

		bool hairFromDad = coin();
		if (mom && dad && mom->race != dad->race) {
			const bool momRace = RaceMatches(a_slot, mom);
			const bool dadRace = RaceMatches(a_slot, dad);
			if (momRace != dadRace) {
				hairFromDad = momRace;  // the child looks like one parent's race, the hair comes from the other one
			}
		}
		if (auto src = pick(hairFromDad); src && src->headRelatedData && src->headRelatedData->hairColor) {
			L.hairColor = src->headRelatedData->hairColor->GetFormID();
		}
		auto part = [&](RE::BGSHeadPart::HeadPartType a_type) -> RE::FormID {
			const bool first = coin();
			for (bool d : { first, !first }) {
				auto src = pick(d);
				auto p = src ? src->GetHeadPartByType(a_type) : nullptr;
				if (FitsRace(p, a_race)) {
					return p->GetFormID();
				}
			}
			return 0;
		};
		if (g_settings.hairStyle) {
			L.hair = part(RE::BGSHeadPart::HeadPartType::kHair);
			if (!L.hair) {
				auto self = RE::TESForm::LookupByID<RE::TESNPC>(a_slot.base);
				auto list = PartsForRace(RE::BGSHeadPart::HeadPartType::kHair, a_race, self && self->IsFemale());
				if (!list.empty()) {
					L.hair = list[rng() % list.size()]->GetFormID();
				}
			}
			L.brows = part(RE::BGSHeadPart::HeadPartType::kEyebrows);
		}
		if (g_settings.eyes) {
			L.eyes = part(RE::BGSHeadPart::HeadPartType::kEyes);
		}
		auto mf = mom ? mom->faceData : nullptr;
		auto df = dad ? dad->faceData : nullptr;
		if (g_settings.faceShape && (mf || df)) {
			L.face = true;
			std::uniform_real_distribution<float> w(0.25f, 0.75f);
			for (std::size_t i = 0; i < RE::TESNPC::FaceData::Morphs::kTotal; ++i) {
				const float t = w(rng);
				L.morphs[i] = (mf && df) ? mf->morphs[i] * t + df->morphs[i] * (1.0f - t) : (mf ? mf : df)->morphs[i];
			}
			for (std::size_t i = 0; i < RE::TESNPC::FaceData::Parts::kTotal; ++i) {
				auto src = coin() ? (df ? df : mf) : (mf ? mf : df);
				L.presets[i] = src->parts[i];
			}
		}
		if (g_settings.skinTone) {
			L.skin = true;
			std::uniform_real_distribution<float> w(0.3f, 0.7f);
			const float t = w(rng);
			auto        a = mom ? mom->bodyTintColor : dad->bodyTintColor;
			auto        b = dad ? dad->bodyTintColor : mom->bodyTintColor;
			auto        mix = [&](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(std::lround(x * t + y * (1.0f - t))); };
			L.rgb[0] = mix(a.red, b.red);
			L.rgb[1] = mix(a.green, b.green);
			L.rgb[2] = mix(a.blue, b.blue);
		}
		if (mom && dad) {
			L.hasWeight = true;
			L.weight = (mom->weight + dad->weight) * 0.5f;
		}
		return L;
	}

	void WriteLooks(RE::TESNPC* a_npc, const Looks& L)
	{
		if (!a_npc) {
			return;
		}
		TakeSnapshot(a_npc);
		if (auto c = L.hairColor ? RE::TESForm::LookupByID<RE::BGSColorForm>(L.hairColor) : nullptr) {
			a_npc->SetHairColor(c);
		}
		for (auto id : { L.hair, L.eyes, L.brows }) {
			if (auto p = id ? RE::TESForm::LookupByID<RE::BGSHeadPart>(id) : nullptr) {
				a_npc->ChangeHeadPart(p);
			}
		}
		if (L.face && a_npc->faceData) {
			std::memcpy(a_npc->faceData->morphs, L.morphs, sizeof(L.morphs));
			std::memcpy(a_npc->faceData->parts, L.presets, sizeof(L.presets));
		}
		if (L.skin) {
			a_npc->bodyTintColor.red = L.rgb[0];
			a_npc->bodyTintColor.green = L.rgb[1];
			a_npc->bodyTintColor.blue = L.rgb[2];
		}
		if (L.hasWeight) {
			a_npc->weight = L.weight;
		}
	}

	void Refresh3D(RE::FormID a_actor)
	{
		auto a = a_actor ? RE::TESForm::LookupByID<RE::Actor>(a_actor) : nullptr;
		if (a && a->Is3DLoaded()) {
			a->DoReset3D(false);
		}
	}

	// ------------------------------------------------------------------ race lists
	// Beeing Female picks the player baby actor from StorageUtil FormList "FW.AddOn.BabyActor_<Sex>Player" on the race.
	// Point it at the next free copy (or back at the original once the pool for that race is used up).
	RE::TESNPC* NextBase(int a_orig)
	{
		for (auto& s : g_slots) {
			if (s.orig == a_orig && s.state == State::kFree) {
				return RE::TESForm::LookupByID<RE::TESNPC>(s.base);
			}
		}
		auto& o = PoolData::kOrigs[a_orig];
		return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESNPC>(o.id, o.plugin);
	}

	void SetRaceList(RE::TESRace* a_race, const char* a_key, RE::TESNPC* a_base)
	{
		if (!a_race || !a_base) {
			return;
		}
		std::string key = a_key;
		CallStatic("StorageUtil", "FormListClear",
			[a_race, key, a_base](const RE::BSScript::Variable&) {
				CallStatic("StorageUtil", "FormListAdd", nullptr, a_race, std::string(key), a_base, true);
			},
			a_race, std::string(key));
	}

	void RefreshRaceLists()
	{
		if (!g_ready || !g_settings.inherit || g_slots.empty()) {
			return;
		}
		auto dh = RE::TESDataHandler::GetSingleton();
		for (auto& r : PoolData::kRaces) {
			auto race = dh->LookupForm<RE::TESRace>(r.id, r.plugin);
			if (!race) {
				continue;
			}
			SetRaceList(race, "FW.AddOn.BabyActor_FemalePlayer", NextBase(r.female));
			SetRaceList(race, "FW.AddOn.BabyActor_MalePlayer", NextBase(r.male));
		}
	}

	// ------------------------------------------------------------------ naming
	void ApplyName(RE::FormID a_actor, const std::string& a_name)
	{
		auto a = RE::TESForm::LookupByID<RE::Actor>(a_actor);
		if (!a || a_name.empty()) {
			return;
		}
		// FWChildActor.SetName stores FW.Child.Name and sets "<name> <last name>"; other children only get the storage + display name
		const bool viaScript = CallMethod(a, "FWChildActor", "SetName", nullptr, std::string(a_name));
		if (!viaScript) {
			CallStatic("StorageUtil", "SetStringValue", nullptr, a, std::string("FW.Child.Name"), std::string(a_name));
			a->SetDisplayName(a_name.c_str(), true);
		}
		if (auto it = std::find_if(g_slots.begin(), g_slots.end(), [&](auto& s) { return s.actor == a_actor; }); it != g_slots.end()) {
			it->named = true;
		}
		Log("named {:08X}: {}", a_actor, a_name);
		Notify(a_name + "と名付けました");
	}

	void PromptName(RE::FormID a_actor)
	{
		auto a = RE::TESForm::LookupByID<RE::Actor>(a_actor);
		if (!a) {
			return;
		}
		if (g_prompting.exchange(true)) {
			return;
		}
		g_promptTick = g_tick;
		std::string current = a->GetDisplayFullName() ? a->GetDisplayFullName() : "";
		if (auto sp = current.find(" Dovahkiir"); sp != std::string::npos) {
			current.erase(sp);
		}
		const std::string menu = "UITextEntryMenu";
		Log("name prompt for {:08X} ({})", a_actor, current);
		const bool ok = CallStatic("UIExtensions", "InitMenu",
			[a_actor, current, menu](const RE::BSScript::Variable&) {
				CallStatic("UIExtensions", "SetMenuPropertyString",
					[a_actor, menu](const RE::BSScript::Variable&) {
						CallStatic("UIExtensions", "OpenMenu",
							[a_actor, menu](const RE::BSScript::Variable&) {
								CallStatic("UIExtensions", "GetMenuResultString",
									[a_actor](const RE::BSScript::Variable& v) {
										std::string name = v.IsString() ? std::string(v.GetString()) : "";
										name = Trim(name);
										if (!name.empty()) {
											ApplyName(a_actor, name);
										}
										g_prompting = false;
									},
									std::string(menu));
							},
							std::string(menu), static_cast<RE::TESForm*>(nullptr), static_cast<RE::TESForm*>(nullptr));
					},
					std::string(menu), std::string("text"), std::string(current));
			},
			std::string(menu));
		if (!ok) {
			Log("UIExtensions is not available: cannot open the name box");
			g_prompting = false;
		}
	}

	bool CanPrompt()
	{
		auto ui = RE::UI::GetSingleton();
		auto pc = RE::PlayerCharacter::GetSingleton();
		return ui && pc && !ui->GameIsPaused() && !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) && !pc->IsInCombat() && !g_prompting;
	}

	// rename key: whoever the crosshair is on, if Beeing Female knows them as a child
	void TryRenameTarget()
	{
		auto pick = RE::CrosshairPickData::GetSingleton();
		auto ref = pick ? pick->GetActiveTarget().get() : nullptr;
		auto a = ref ? ref->As<RE::Actor>() : nullptr;
		if (!a) {
			return;
		}
		const RE::FormID id = a->GetFormID();
		CallStatic("StorageUtil", "GetFormValue",
			[id](const RE::BSScript::Variable& v) {
				if (AsForm(v)) {
					PromptName(id);
					return;
				}
				CallStatic("StorageUtil", "GetFormValue",
					[id](const RE::BSScript::Variable& v2) {
						if (AsForm(v2)) {
							PromptName(id);
						}
					},
					RE::TESForm::LookupByID<RE::Actor>(id), std::string("FW.Child.Father"), static_cast<RE::TESForm*>(nullptr));
			},
			a, std::string("FW.Child.Mother"), static_cast<RE::TESForm*>(nullptr));
	}

	void CollectDna();

	class InputSink : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
		{
			for (auto e = a_event ? *a_event : nullptr; e; e = e->next) {
				auto b = e->AsButtonEvent();
				if (!b || b->GetDevice() != RE::INPUT_DEVICE::kKeyboard || !b->IsDown()) {
					continue;
				}
				const auto code = b->GetIDCode();
				if (code != g_settings.renameKey && (g_settings.collectKey == 0 || code != g_settings.collectKey)) {
					continue;
				}
				auto ui = RE::UI::GetSingleton();
				if (ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME))) {
					continue;
				}
				if (code == g_settings.renameKey) {
					SKSE::GetTaskInterface()->AddTask(TryRenameTarget);
				} else {
					SKSE::GetTaskInterface()->AddTask(CollectDna);
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
	InputSink g_input;


	// ------------------------------------------------------------------ DNA
	// "J" while aiming at an NPC: collect their DNA. Every NPC gets one of the 128 DNA items in BFHeredity.esp, renamed to
	// "<name>の精液（<race>・<n>%）". Using (drinking) it puts it inside: Fill Her Up inflates with that NPC as the injector, and
	// Beeing Female makes the player pregnant by that NPC with the item's chance (30 / 50 / 100 %, fixed per NPC).
	struct Dna
	{
		RE::FormID    item = 0;
		RE::FormID    owner = 0;
		RE::FormID    race = 0;
		std::uint8_t  tier = 0;  // 30 / 50 / 100, 0 = free
		float         lastDay = -100.0f;
		std::string   name;
	};
	std::vector<Dna> g_dna;

	void BuildDna()
	{
		g_dna.clear();
		auto dh = RE::TESDataHandler::GetSingleton();
		for (auto id : PoolData::kDna) {
			if (auto f = dh ? dh->LookupForm<RE::AlchemyItem>(id, kPlugin) : nullptr) {
				Dna d;
				d.item = f->GetFormID();
				g_dna.push_back(d);
			}
		}
		Log("DNA: {} item slots", g_dna.size());
	}

	void NameDnaItem(const Dna& d)
	{
		if (auto f = RE::TESForm::LookupByID<RE::AlchemyItem>(d.item); f && !d.name.empty()) {
			f->SetFullName(d.name.c_str());
		}
	}

	Dna* DnaByItem(RE::FormID a_item)
	{
		for (auto& d : g_dna) {
			if (d.item == a_item && d.tier) {
				return &d;
			}
		}
		return nullptr;
	}

	// ModEvent.Create / Push* / Send, one after another
	using Step = std::function<void(std::int32_t, std::function<void()>)>;
	void RunModEvent(const std::string& a_name, std::vector<Step> a_steps)
	{
		CallStatic("ModEvent", "Create",
			[a_steps](const RE::BSScript::Variable& v) {
				const std::int32_t h = v.IsInt() ? v.GetSInt() : 0;
				if (!h) {
					Log("ModEvent.Create failed");
					return;
				}
				auto steps = std::make_shared<std::vector<Step>>(a_steps);
				auto idx = std::make_shared<std::size_t>(0);
				auto next = std::make_shared<std::function<void()>>();
				*next = [steps, idx, next, h] {
					if (*idx < steps->size()) {
						auto& st = (*steps)[(*idx)++];
						st(h, *next);
					} else {
						CallStatic("ModEvent", "Send", nullptr, h);
					}
				};
				(*next)();
			},
			std::string(a_name));
	}

	Step PushForm(RE::TESForm* f) { return [f](std::int32_t h, std::function<void()> n) { CallStatic("ModEvent", "PushForm", [n](const RE::BSScript::Variable&) { n(); }, h, f); }; }
	Step PushBool(bool b) { return [b](std::int32_t h, std::function<void()> n) { CallStatic("ModEvent", "PushBool", [n](const RE::BSScript::Variable&) { n(); }, h, b); }; }
	Step PushInt(std::int32_t i) { return [i](std::int32_t h, std::function<void()> n) { CallStatic("ModEvent", "PushInt", [n](const RE::BSScript::Variable&) { n(); }, h, i); }; }
	Step PushFloat(float x) { return [x](std::int32_t h, std::function<void()> n) { CallStatic("ModEvent", "PushFloat", [n](const RE::BSScript::Variable&) { n(); }, h, x); }; }
	Step PushString(std::string s) { return [s](std::int32_t h, std::function<void()> n) { CallStatic("ModEvent", "PushString", [n](const RE::BSScript::Variable&) { n(); }, h, std::string(s)); }; }

	std::string RaceName(RE::TESRace* r)
	{
		const char* n = r ? r->GetFullName() : nullptr;
		return n && *n ? n : "?";
	}

	// run a_fn on the main thread after a_seconds
	void Later(float a_seconds, std::function<void()> a_fn)
	{
		std::thread([a_seconds, a_fn] {
			std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(a_seconds * 1000.0f)));
			SKSE::GetTaskInterface()->AddTask(a_fn);
		}).detach();
	}

	bool Anim(RE::Actor* a_actor, const char* a_event)
	{
		if (!a_actor || !g_settings.animations) {
			return false;
		}
		const bool ok = a_actor->NotifyAnimationGraph(a_event);
		Log("anim {} on {:08X}: {}", a_event, a_actor->GetFormID(), ok ? "ok" : "not available");
		return ok;
	}

	void HoldNpc(RE::Actor* a, bool a_hold)
	{
		CallMethod(a, "Actor", "SetDontMove", nullptr, a_hold);
		CallMethod(a, "Actor", "SetRestrained", nullptr, a_hold);
	}

	void HoldPlayer(bool a_hold)
	{
		if (a_hold) {
			CallStatic("Game", "DisablePlayerControls", nullptr, true, true, false, false, false, true, true, false, static_cast<std::int32_t>(0));
		} else {
			CallStatic("Game", "EnablePlayerControls", nullptr, true, true, true, true, true, true, true, true, static_cast<std::int32_t>(0));
		}
	}

	// SexLab's voice sounds (SexLab.esm): Male01-04 / Female01-08, each Hot / Medium / Mild
	enum class Heat
	{
		kMild,
		kMedium,
		kHot
	};
	void Voice(RE::Actor* a_actor, Heat a_heat)
	{
		if (!a_actor || !g_settings.sounds) {
			return;
		}
		const bool    female = a_actor->GetActorBase() && a_actor->GetActorBase()->GetSex() == RE::SEX::kFemale;
		const auto    n = static_cast<std::uint32_t>((a_actor->GetFormID() * 2654435761u) >> 16) % (female ? 8u : 4u);
		const auto    first = female ? 0x67546u : 0x6755Eu;
		const auto    off = a_heat == Heat::kHot ? 0u : (a_heat == Heat::kMedium ? 1u : 2u);
		auto          snd = RE::TESDataHandler::GetSingleton()->LookupForm(first + n * 3u + off, "SexLab.esm");
		if (snd) {
			CallMethod(snd, "Sound", "Play", nullptr, static_cast<RE::TESObjectREFR*>(a_actor));
		}
	}

	// keeps voicing a_id every voiceInterval while a_until says so, getting hotter over a_total seconds
	void VoiceLoop(RE::FormID a_id, float a_elapsed, float a_total, std::function<bool()> a_until)
	{
		if (!g_settings.sounds || !a_until()) {
			return;
		}
		const float t = a_total > 0.0f ? a_elapsed / a_total : 1.0f;
		Voice(RE::TESForm::LookupByID<RE::Actor>(a_id), t < 0.4f ? Heat::kMild : (t < 0.8f ? Heat::kMedium : Heat::kHot));
		const float step = std::max(1.0f, g_settings.voiceInterval);
		Later(step, [a_id, a_elapsed, a_total, a_until, step] { VoiceLoop(a_id, a_elapsed + step, a_total, a_until); });
	}

	std::atomic_bool g_dnaBusy = false;
	std::atomic_bool g_collecting = false;
	std::atomic_bool g_inserting = false;
	void             GiveDna(RE::FormID a_id);

	void CollectFrom(RE::Actor* a);

	void CollectDna()
	{
		auto pick = RE::CrosshairPickData::GetSingleton();
		auto ref = pick ? pick->GetActiveTarget().get() : nullptr;
		CollectFrom(ref ? ref->As<RE::Actor>() : nullptr);
	}

	void CollectFrom(RE::Actor* a)
	{
		if (!g_settings.dna) {
			return;
		}
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (!a || a == pc || a->IsChild()) {
			return;
		}
		auto base = a->GetActorBase();
		auto race = a->GetRace();
		if (!base || !race || !race->HasKeywordString("ActorTypeNPC")) {
			Notify("この相手からは採取できません");
			return;
		}
		if (g_settings.maleOnly && base->GetSex() != RE::SEX::kMale) {
			Notify("男性からのみ採取できます");
			return;
		}
		if (g_dnaBusy.exchange(true)) {
			return;
		}
		const RE::FormID id = a->GetFormID();
		// extraction: animation (when registered), voice, camera, then the DNA drops after collectSeconds
		const bool anim = Anim(a, "dse-sgo-wanking01-01");
		HoldNpc(a, true);
		g_collecting = true;
		VoiceLoop(id, 0.0f, g_settings.collectSeconds, [] { return g_collecting.load(); });
		Camera(a, g_settings.collectSeconds);
		Later(g_settings.collectSeconds, [id, anim] {
			auto a = RE::TESForm::LookupByID<RE::Actor>(id);
			g_collecting = false;
			Voice(a, Heat::kHot);
			GiveDna(id);
			if (anim) {
				Anim(a, "dse-sgo-wanking01-03");
			}
			Later(anim ? 2.5f : 0.5f, [id, anim] {
				auto a = RE::TESForm::LookupByID<RE::Actor>(id);
				if (anim) {
					Anim(a, "IdleForceDefaultState");
				}
				HoldNpc(a, false);
				g_dnaBusy = false;
			});
		});
	}

	void GiveDna(RE::FormID id)
	{
		auto a = RE::TESForm::LookupByID<RE::Actor>(id);
		if (!a) {
			return;
		}
		// how much the NPC has stored = Beeing Female's virility (recovers with time since his last sex)
		auto ctrl = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0x182A, "BeeingFemale.esm");
		const bool ok = CallMethod(ctrl, "FWController", "GetVirility",
			[id](const RE::BSScript::Variable& v) {
				auto a = RE::TESForm::LookupByID<RE::Actor>(id);
				if (!a) {
					return;
				}
				const float vir = v.IsFloat() ? v.GetFloat() : 1.0f;
				const std::uint8_t tier = vir >= g_settings.virility100 ? 100 : (vir >= g_settings.virility50 ? 50 : 30);
				// one item per NPC and grade
				Dna* d = nullptr;
				for (auto& x : g_dna) {
					if (x.tier == tier && x.owner == id) {
						d = &x;
						break;
					}
				}
				if (!d) {
					for (auto& x : g_dna) {
						if (!x.tier) {
							d = &x;
							break;
						}
					}
					if (!d) {
						Notify("精液の保管枠がいっぱいです");
						return;
					}
					auto race = a->GetRace();
					d->tier = tier;
					d->owner = id;
					d->race = race ? race->GetFormID() : 0;
					d->name = std::string(a->GetDisplayFullName() ? a->GetDisplayFullName() : "?") + "の精液（" + RaceName(race) + "・" + std::to_string(tier) + "%）";
					NameDnaItem(*d);
				}
				d->lastDay = RE::Calendar::GetSingleton()->GetDaysPassed();
				if (auto item = RE::TESForm::LookupByID<RE::AlchemyItem>(d->item)) {
					RE::PlayerCharacter::GetSingleton()->AddObjectToContainer(item, nullptr, 1, nullptr);
				}
				// he has given it: Beeing Female's virility starts recovering from now
				CallStatic("StorageUtil", "SetFloatValue", nullptr, a, std::string("FW.LastSexTime"), d->lastDay);
				Log("DNA collected: {:08X} virility {:.2f} -> {}  {}", id, vir, tier, d->name);
				Notify(d->name + "を採取しました");
			},
			a);
		if (!ok) {
			Notify("Beeing Female が見つからないため、採取できません");
		}
	}

	void InjectDna(Dna& d)
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		auto owner = RE::TESForm::LookupByID<RE::Actor>(d.owner);
		Log("DNA used: {} (owner {:08X} {})", d.name, d.owner, owner ? "found" : "missing");
		// Fill Her Up: inflate with the owner as the injector, by Fill Her Up's own amount for his race
		// (StorageUtil "sr.inflater.race.cum.amount" on the race, 0.75 when not set) x the grade x fFHUAmount
		auto race = RE::TESForm::LookupByID<RE::TESRace>(d.race);
		const float grade = d.tier >= 100 ? 1.0f : (d.tier >= 50 ? 0.75f : 0.5f);
		CallStatic("StorageUtil", "GetFloatValue",
			[pc, owner, grade](const RE::BSScript::Variable& v) {
				const float raceAmount = v.IsFloat() ? v.GetFloat() : 0.75f;
				const float amount = raceAmount * grade * g_settings.fhuAmount;
				Log("Fill Her Up: race amount {:.2f} x grade {:.2f} x {:.2f} = {:.2f}", raceAmount, grade, g_settings.fhuAmount, amount);
				RunModEvent("SR_InflateInjectorEvent", { PushForm(pc), PushForm(owner), PushBool(true), PushInt(1), PushFloat(amount), PushInt(2), PushString("") });
			},
			race, std::string("sr.inflater.race.cum.amount"), 0.75f);
		// Beeing Female: pregnant by the owner with the DNA's chance
		const int roll = static_cast<int>(std::random_device{}() % 100u);
		if (!owner || roll >= d.tier) {
			Log("DNA: no conception (roll {} / {}%)", roll, d.tier);
			return;
		}
		auto ctrl = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0x182A, "BeeingFemale.esm");
		const bool ok = CallMethod(ctrl, "FWController", "Impregnate", nullptr, static_cast<RE::Actor*>(pc), owner, static_cast<std::int32_t>(1));
		Log("DNA: conception by {:08X} (roll {} / {}%) -> {}", d.owner, roll, d.tier, ok ? "sent to Beeing Female" : "Beeing Female controller not found");
	}

	std::deque<RE::FormID> g_pendingDna;

	// the "collect" dialogue line in BFHeredity.esp: when the NPC finishes answering, start collecting from him
	class TopicSink : public RE::BSTEventSink<RE::TESTopicInfoEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESTopicInfoEvent* a_event, RE::BSTEventSource<RE::TESTopicInfoEvent>*) override
		{
			if (!a_event || !a_event->speakerRef || a_event->type.get() != RE::TESTopicInfoEvent::TopicInfoEventType::kTopicEnd) {
				return RE::BSEventNotifyControl::kContinue;
			}
			static RE::FormID s_info = [] {
				auto f = RE::TESDataHandler::GetSingleton()->LookupForm(PoolData::kCollectInfo, kPlugin);
				return f ? f->GetFormID() : 0u;
			}();
			if (!s_info || a_event->topicInfoFormID != s_info) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const RE::FormID who = a_event->speakerRef->GetFormID();
			Later(1.0f, [who] {  // let the dialogue menu close first
				CollectFrom(RE::TESForm::LookupByID<RE::Actor>(who));
			});
			return RE::BSEventNotifyControl::kContinue;
		}
	};
	TopicSink g_topic;

	void StartInsert(RE::FormID a_item)
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		g_dnaBusy = true;
		// insertion: animation (when registered), voice, camera, then it is in after insertSeconds
		const bool anim = Anim(pc, "dse-sgo-insert01-01");
		HoldPlayer(true);
		g_inserting = true;
		VoiceLoop(pc->GetFormID(), 0.0f, g_settings.insertSeconds, [] { return g_inserting.load(); });
		Camera(pc, g_settings.insertSeconds + 1.0f);
		Later(g_settings.insertSeconds, [a_item, anim] {
			g_inserting = false;
			Voice(RE::PlayerCharacter::GetSingleton(), Heat::kHot);
			if (auto d = DnaByItem(a_item)) {
				InjectDna(*d);
			}
			Later(1.0f, [anim] {
				if (anim) {
					Anim(RE::PlayerCharacter::GetSingleton(), "IdleForceDefaultState");
				}
				HoldPlayer(false);
				g_dnaBusy = false;
			});
		});
	}

	class EquipSink : public RE::BSTEventSink<RE::TESEquipEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
		{
			if (!a_event || !a_event->equipped || !a_event->actor || !a_event->actor->IsPlayerRef()) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const RE::FormID item = a_event->baseObject;
			SKSE::GetTaskInterface()->AddTask([item] {
				if (DnaByItem(item)) {
					g_pendingDna.push_back(item);  // played once the inventory is closed
				}
			});
			return RE::BSEventNotifyControl::kContinue;
		}
	};
	EquipSink g_equip;

	// ------------------------------------------------------------------ births
	void ReadParents(int a_idx)
	{
		auto& s = g_slots[a_idx];
		auto  a = RE::TESForm::LookupByID<RE::Actor>(s.actor);
		if (!a || s.querying) {
			return;
		}
		s.querying = true;
		CallStatic("StorageUtil", "GetFormValue",
			[a_idx](const RE::BSScript::Variable& v) {
				auto mom = AsForm(v);
				auto& s = g_slots[a_idx];
				if (!mom) {
					s.querying = false;  // not set yet, ask again next tick
					return;
				}
				s.mother = mom->GetFormID();
				CallStatic("StorageUtil", "GetFormValue",
					[a_idx](const RE::BSScript::Variable& v2) {
						auto& s = g_slots[a_idx];
						auto  dad = AsForm(v2);
						s.father = dad ? dad->GetFormID() : 0;
						s.querying = false;
						s.state = State::kApplied;
						if (g_settings.inherit) {
							auto npc = RE::TESForm::LookupByID<RE::TESNPC>(s.base);
							s.looks = ComputeLooks(s, npc ? npc->race : nullptr);
							WriteLooks(npc, s.looks);
							Refresh3D(s.actor);
						}
						Log("child {:08X} (base {:08X}): mother {:08X} father {:08X} seed {:08X}", s.actor, s.base, s.mother, s.father, s.seed);
						if (g_settings.nameOnBirth && !s.named) {
							g_namingQueue.push_back(s.actor);
						}
						RefreshRaceLists();
					},
					RE::TESForm::LookupByID<RE::Actor>(s.actor), std::string("FW.Child.Father"), static_cast<RE::TESForm*>(nullptr));
			},
			a, std::string("FW.Child.Mother"), static_cast<RE::TESForm*>(nullptr));
	}

	void Poll()
	{
		if (!g_ready || g_slots.empty()) {
			return;
		}
		const auto tick = ++g_tick;
		// a copy that is still free but now has an actor in the world = a new child
		bool any = false;
		if (auto pl = RE::ProcessLists::GetSingleton()) {
			for (auto& h : pl->highActorHandles) {
				auto a = h.get();
				if (!a) {
					continue;
				}
				auto base = a->GetActorBase();
				auto it = base ? g_slotByBase.find(base->GetFormID()) : g_slotByBase.end();
				if (it == g_slotByBase.end()) {
					continue;
				}
				auto& s = g_slots[it->second];
				if (s.state == State::kFree) {
					s.state = State::kBorn;
					s.actor = a->GetFormID();
					s.seed = std::random_device{}() ^ (s.actor * 2654435761u);
					s.bornAt = static_cast<float>(tick);
					any = true;
					Log("new child {:08X} on copy {:08X}", s.actor, s.base);
				}
			}
		}
		if (any) {
			RefreshRaceLists();  // the next birth must not land on the same copy
		}
		for (int i = 0; i < static_cast<int>(g_slots.size()); ++i) {
			if (g_slots[i].state == State::kBorn) {
				ReadParents(i);
			}
		}
		if (!g_pendingDna.empty() && !g_dnaBusy) {
			auto ui = RE::UI::GetSingleton();
			if (ui && !ui->GameIsPaused() && !ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME) && !ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME)) {
				auto item = g_pendingDna.front();
				g_pendingDna.pop_front();
				StartInsert(item);
			}
		}
		if (g_prompting && tick - g_promptTick > 300) {
			g_prompting = false;  // the name box never reported back
		}
		if (!g_namingQueue.empty() && CanPrompt() && tick % 3 == 0) {
			auto id = g_namingQueue.front();
			g_namingQueue.pop_front();
			Notify("赤ちゃんが生まれました。名前をつけてください");
			PromptName(id);
		}
		if (tick % 120 == 0) {
			RefreshRaceLists();  // Beeing Female may reload its add-on lists
		}
	}

	void Ticker()
	{
		for (;;) {
			std::this_thread::sleep_for(1s);
			if (g_ready) {
				SKSE::GetTaskInterface()->AddTask(Poll);
			}
		}
	}

	// ------------------------------------------------------------------ co-save
	constexpr std::uint32_t kSerUid = 'BFHR';
	constexpr std::uint32_t kSerSlots = 'SLOT';
	constexpr std::uint32_t kSerDna = 'DNAS';

	void OnSave(SKSE::SerializationInterface* a_ser)
	{
		if (!a_ser->OpenRecord(kSerSlots, 1)) {
			return;
		}
		std::uint32_t n = 0;
		for (auto& s : g_slots) {
			n += s.state != State::kFree ? 1u : 0u;
		}
		a_ser->WriteRecordData(n);
		for (auto& s : g_slots) {
			if (s.state == State::kFree) {
				continue;
			}
			a_ser->WriteRecordData(s.base);
			a_ser->WriteRecordData(s.actor);
			a_ser->WriteRecordData(s.mother);
			a_ser->WriteRecordData(s.father);
			a_ser->WriteRecordData(s.seed);
			std::uint8_t st = static_cast<std::uint8_t>(s.state);
			a_ser->WriteRecordData(st);
			std::uint8_t named = s.named ? 1 : 0;
			a_ser->WriteRecordData(named);
			a_ser->WriteRecordData(s.looks);
		}
		if (!a_ser->OpenRecord(kSerDna, 1)) {
			return;
		}
		std::uint32_t nd = 0;
		for (auto& d : g_dna) {
			nd += d.tier ? 1u : 0u;
		}
		a_ser->WriteRecordData(nd);
		for (auto& d : g_dna) {
			if (!d.tier) {
				continue;
			}
			a_ser->WriteRecordData(d.item);
			a_ser->WriteRecordData(d.owner);
			a_ser->WriteRecordData(d.race);
			a_ser->WriteRecordData(d.tier);
			a_ser->WriteRecordData(d.lastDay);
			const std::uint32_t len = static_cast<std::uint32_t>(d.name.size());
			a_ser->WriteRecordData(len);
			a_ser->WriteRecordData(d.name.data(), len);
		}
	}

	void ResetAll()
	{
		for (auto& s : g_slots) {
			if (s.state == State::kApplied) {
				if (auto npc = RE::TESForm::LookupByID<RE::TESNPC>(s.base)) {
					RestoreSnapshot(npc);
				}
			}
			const auto base = s.base;
			const auto orig = s.orig;
			s = Slot{};
			s.base = base;
			s.orig = orig;
		}
		g_namingQueue.clear();
		g_prompting = false;
		for (auto& d : g_dna) {
			if (d.tier) {
				if (auto f = RE::TESForm::LookupByID<RE::AlchemyItem>(d.item)) {
					f->SetFullName("DNA");
				}
			}
			const auto item = d.item;
			d = Dna{};
			d.item = item;
		}
	}

	void OnRevert(SKSE::SerializationInterface*)
	{
		ResetAll();
	}

	void OnLoad(SKSE::SerializationInterface* a_ser)
	{
		ResetAll();
		std::uint32_t type, version, length;
		int           restored = 0;
		while (a_ser->GetNextRecordInfo(type, version, length)) {
			if (type == kSerDna) {
				std::uint32_t n = 0;
				a_ser->ReadRecordData(n);
				for (std::uint32_t i = 0; i < n; ++i) {
					Dna           x;
					std::uint32_t len = 0;
					a_ser->ReadRecordData(x.item);
					a_ser->ReadRecordData(x.owner);
					a_ser->ReadRecordData(x.race);
					a_ser->ReadRecordData(x.tier);
					a_ser->ReadRecordData(x.lastDay);
					a_ser->ReadRecordData(len);
					x.name.resize(len);
					if (len) {
						a_ser->ReadRecordData(x.name.data(), len);
					}
					RE::FormID it = 0, ow = 0, rc = 0;
					if (!a_ser->ResolveFormID(x.item, it)) {
						continue;
					}
					a_ser->ResolveFormID(x.owner, ow);
					a_ser->ResolveFormID(x.race, rc);
					for (auto& d : g_dna) {
						if (d.item == it) {
							d = x;
							d.item = it;
							d.owner = ow;
							d.race = rc;
							NameDnaItem(d);
						}
					}
				}
				continue;
			}
			if (type != kSerSlots) {
				continue;
			}
			std::uint32_t n = 0;
			a_ser->ReadRecordData(n);
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::FormID    base = 0, actor = 0, mother = 0, father = 0;
				std::uint32_t seed = 0;
				std::uint8_t  st = 0, named = 0;
				a_ser->ReadRecordData(base);
				a_ser->ReadRecordData(actor);
				a_ser->ReadRecordData(mother);
				a_ser->ReadRecordData(father);
				a_ser->ReadRecordData(seed);
				a_ser->ReadRecordData(st);
				a_ser->ReadRecordData(named);
				Looks looks;
				a_ser->ReadRecordData(looks);
				for (auto* id : { &looks.hairColor, &looks.hair, &looks.eyes, &looks.brows }) {
					RE::FormID r = 0;
					*id = (*id && a_ser->ResolveFormID(*id, r)) ? r : 0;
				}
				RE::FormID b = 0, ac = 0, mo = 0, fa = 0;
				if (!a_ser->ResolveFormID(base, b)) {
					continue;
				}
				auto it = g_slotByBase.find(b);
				if (it == g_slotByBase.end()) {
					continue;
				}
				a_ser->ResolveFormID(actor, ac);
				if (mother) {
					a_ser->ResolveFormID(mother, mo);
				}
				if (father) {
					a_ser->ResolveFormID(father, fa);
				}
				auto& s = g_slots[it->second];
				s.actor = ac;
				s.mother = mo;
				s.father = fa;
				s.seed = seed;
				s.state = static_cast<State>(st);
				s.named = named != 0;
				s.looks = looks;
				if (s.state == State::kApplied && g_settings.inherit) {
					WriteLooks(RE::TESForm::LookupByID<RE::TESNPC>(s.base), s.looks);
				}
				++restored;
			}
		}
		Log("co-save: {} child(ren) restored", restored);
	}

	// ------------------------------------------------------------------ startup
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			LoadSettings();
			Cutin::InstallHooks();
			if (KKMenu::Installed()) {
				KKMenu::RegisterHudElement(Cutin::DrawLetterbox);
			}
			std::thread([] {
				for (;;) {
					std::this_thread::sleep_for(50ms);
					SKSE::GetTaskInterface()->AddTask([] { Cutin::Poll(); });
				}
			}).detach();
			BuildSlots();
			BuildDna();
			if (auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
				holder->AddEventSink<RE::TESEquipEvent>(&g_equip);
				holder->AddEventSink<RE::TESTopicInfoEvent>(&g_topic);
			}
			if (auto idm = RE::BSInputDeviceManager::GetSingleton()) {
				idm->AddEventSink(&g_input);
			}
			if (!GetModuleHandleW(L"BeeingFemale")) {
				Log("WARNING: BeeingFemale.dll is not loaded");
			}
			std::thread(Ticker).detach();
			Log("ready");
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			g_ready = false;
			SKSE::GetTaskInterface()->AddTask([] { Cutin::Stop("game load"); });
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			SKSE::GetTaskInterface()->AddTask([] {
				g_ready = true;
				for (auto& s : g_slots) {
					if (s.state == State::kApplied) {
						Refresh3D(s.actor);
					}
				}
				RefreshRaceLists();
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
	Log("BF Heredity {} runtime={}", BFH_VERSION_STRING, REL::Module::get().version().string());
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	if (auto ser = SKSE::GetSerializationInterface()) {
		ser->SetUniqueID(kSerUid);
		ser->SetSaveCallback(OnSave);
		ser->SetLoadCallback(OnLoad);
		ser->SetRevertCallback(OnRevert);
	}
	return true;
}

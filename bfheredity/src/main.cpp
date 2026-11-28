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

#include "Video.h"
#include "Orb.h"

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
		bool          useOCum = true;           // OCum Ascended: its look on her after insertion
		bool          conceptionVideo = true;   // the moment she conceives: Data/BFHeredity/Video/conception.mp4 + .wav
		bool          softAbs = true;           // while pregnant, take the abs morphs back (Skill Based Muscle's + the ones built into the body)
		float         bakedAbs = 1.0f;          // MuscleAbs / MuscleMoreAbs_v2 built into the body in BodySlide (Wonder Woman preset = 1.0)
		float         softAbsStrength = 1.0f;   // 1 = flat at the last stage
		bool          soloScene = true;
		std::string   markName = "060";          // Bal's Mark: LewdMarks design (SlaveTats sections LewdMarks / LewdMarks-glow)
		float         markDays = 7.0f;           // days per stage of the curse
		bool          blush = true;              // raise SexLab Aroused exposure at those moments, so Blush When Aroused reddens her face
		bool          insertFace = true;        // her face during insertion (nervous, the moment it goes in, relief)
		bool          insertShake = true;       // camera shake + controller rumble the moment it goes in
		bool          leakAfterInsert = true;   // after a vial is poured in, some may come back out later (closed-legs walk + drip)
		float         leakChance = 0.6f;
		float         leakSeconds = 45.0f;
		bool          leakCamera = true;        // the moment it starts: third person, camera eases in low and gives a small jolt
		bool          leakReaction = true;      // the moment it starts: she stops short, flinches, glances around, a small gasp
		float         leakSlowmo = 0.35f;       // the moment it starts: time runs at this speed for half a second (1 = off)
		bool          leakTrail = true;         // wet trails flowing down the inner thighs while she leaks
		bool          wombHud = true;           // the womb HUD (cycle, what is inside, whose child) and the floating lines
		float         hudScale = 1.0f;
		std::int32_t  hudCorner = 0;            // 0 bottom left, 1 bottom right, 2 top left, 3 top right
		bool          moodFace = true;          // flushed face when aroused
		bool          shameFace = true;         // embarrassed face and blush when seen naked
		bool          spiritAllies = true;      // Bal's Mark: Karen, Lilian and Taranis fight Molag Bal with her
		bool          fetusModel = true;        // a 3D child of the father's race inside her womb (seen through Pseudo X-Ray)
		float         fetusX = 0.0f, fetusY = -5.0f, fetusZ = -3.0f;  // from the belly bone, in her own frame (right / forward / up)
		float         fetusScale = 1.0f;
		float         fetusPitch = 0.0f, fetusYaw = 0.0f;  // degrees, turn the child inside her
		// live chaurus egg
		bool          liveEgg = true;           // the item drops at all
		float         eggSacChance = 0.15f;     // harvesting a chaurus egg sac also gives a live egg
		float         eggKillChance = 0.10f;    // a chaurus the player kills carries one
		float         eggDays = 5.0f;           // days she carries them (without Estrus Unified)
		std::int32_t  eggCount = 3;             // eggs she lays         // "collect" plays a SexLab / OStim solo scene (OStrands etc. draw it) instead of Soulgem Oven's animation
		int           assistFramework = 0;      // "help him" collection: 0 = OStim if loaded else SexLab, 1 = SexLab, 2 = OStim
		bool          naturalConception = true;  // the DNA goes in as Beeing Female sperm: a child only when she ovulates (BF's own rules)
		float         testMinDays = 1.0f;        // a pregnancy test shows positive this many days after conception
		float         gentleMinHours = 6.0f, gentleMaxHours = 12.0f;  // the gentle ovulation drug works after this long
		bool          ovulationVideo = true;  // a short video in a corner window when the player starts to ovulate (Beeing Female)
		std::uint32_t videoTestKey = 0;       // plays it at once (0 = off)
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
		float insertCameraSeconds = 7.0f;  // insertion shot: behind -> front -> whole body -> face
		float insertCameraZoom = 1.0f;     // insertion camera distance (smaller = closer)
		bool  dropBottle = true;           // the collected bottle drops at the giver's feet (otherwise straight into the inventory)
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

	void LoadSettingsFile(const char* a_path)
	{
		std::ifstream f(a_path);
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
			auto b = [&] { return val == "1" || val == "true" || val == "True" || val == "TRUE"; };
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
			} else if (key == "finsertcamerazoom") {
				g_settings.insertCameraZoom = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "bconceptionvideo") {
				g_settings.conceptionVideo = b();
			} else if (key == "bsoftabs") {
				g_settings.softAbs = b();
			} else if (key == "fbakedabs") {
				g_settings.bakedAbs = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "fsoftabsstrength") {
				g_settings.softAbsStrength = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "bliveegg") {
				g_settings.liveEgg = b();
			} else if (key == "feggsacchance") {
				g_settings.eggSacChance = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "feggkillchance") {
				g_settings.eggKillChance = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "feggdays") {
				g_settings.eggDays = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "ieggcount") {
				g_settings.eggCount = std::atoi(val.c_str());
			} else if (key == "smarkname") {
				g_settings.markName = val;
			} else if (key == "fmarkdays") {
				g_settings.markDays = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "bblush") {
				g_settings.blush = b();
			} else if (key == "binsertface") {
				g_settings.insertFace = b();
			} else if (key == "binsertshake") {
				g_settings.insertShake = b();
			} else if (key == "bleakafterinsert") {
				g_settings.leakAfterInsert = b();
			} else if (key == "fleakchance") {
				g_settings.leakChance = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "fleakseconds") {
				g_settings.leakSeconds = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "bleakcamera") {
				g_settings.leakCamera = b();
			} else if (key == "bleakreaction") {
				g_settings.leakReaction = b();
			} else if (key == "bleaktrail") {
				g_settings.leakTrail = b();
			} else if (key == "fleakslowmo") {
				g_settings.leakSlowmo = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "bwombhud") {
				g_settings.wombHud = b();
			} else if (key == "fhudscale") {
				g_settings.hudScale = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "ihudcorner") {
				g_settings.hudCorner = std::atoi(val.c_str());
			} else if (key == "bmoodface") {
				g_settings.moodFace = b();
			} else if (key == "bshameface") {
				g_settings.shameFace = b();
			} else if (key == "bfetusmodel") {
				g_settings.fetusModel = b();
			} else if (key == "ffetusx") {
				g_settings.fetusX = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "ffetusy") {
				g_settings.fetusY = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "ffetusz") {
				g_settings.fetusZ = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "ffetusscale") {
				g_settings.fetusScale = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "ffetuspitch") {
				g_settings.fetusPitch = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "ffetusyaw") {
				g_settings.fetusYaw = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "bspiritallies") {
				g_settings.spiritAllies = b();
			} else if (key == "bsoloscene") {
				g_settings.soloScene = b();
			} else if (key == "buseocum") {
				g_settings.useOCum = b();
			} else if (key == "iassistframework") {
				g_settings.assistFramework = std::atoi(val.c_str());
			} else if (key == "bnaturalconception") {
				g_settings.naturalConception = b();
			} else if (key == "ftestmindays") {
				g_settings.testMinDays = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "fgentleminhours") {
				g_settings.gentleMinHours = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "fgentlemaxhours") {
				g_settings.gentleMaxHours = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "bovulationvideo") {
				g_settings.ovulationVideo = b();
			} else if (key == "ivideotestkey") {
				g_settings.videoTestKey = static_cast<std::uint32_t>(std::strtoul(val.c_str(), nullptr, 0));
			} else if (key == "fvideowidth") {
				Video::g_cfg.width = static_cast<float>(std::atof(val.c_str()));
			} else if (key == "bvideosound") {
				Video::g_cfg.sound = b();
			} else if (key == "bdropbottle") {
				g_settings.dropBottle = b();
			} else if (key == "finsertcameraseconds") {
				g_settings.insertCameraSeconds = static_cast<float>(std::atof(val.c_str()));
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
	}

	// the plugin ini, then MCM Helper's defaults and the player's MCM choices on top
	void LoadSettings(bool a_log = true)
	{
		LoadSettingsFile("Data/SKSE/Plugins/BFHeredity.ini");
		LoadSettingsFile("Data/MCM/Config/BFHeredity/settings.ini");
		LoadSettingsFile("Data/MCM/Settings/BFHeredity.ini");
		g_settings.cutinPlayer = g_settings.camera;
		if (!a_log) {
			return;
		}
		Log("settings: inherit={} nameOnBirth={} renameKey=0x{:X} hair={} eyes={} face={} skin={}", g_settings.inherit, g_settings.nameOnBirth,
			g_settings.renameKey, g_settings.hairStyle, g_settings.eyes, g_settings.faceShape, g_settings.skinTone);
	}

#include "Cutin.h"
#include "WombHud.h"

	// whole body (body slot) -> face, the cut lasting about a_seconds
	void Camera(RE::Actor* a, float a_seconds, bool a_orbit = false, float a_orbitFrom = 0.0f, std::uint32_t a_mask = 1u << (32 - 30))
	{
		if (!a || !g_settings.camera) {
			return;
		}
		g_settings.cutinLength = std::clamp(a_seconds / 3.25f, 0.3f, 4.0f);
		Cutin::Request(a, a_mask, a_orbit, a_orbitFrom);
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
				if (g_settings.videoTestKey != 0 && code == g_settings.videoTestKey) {
					static bool odd = false;  // each press: ovulation, conception, ovulation, ...
					Video::Play((odd = !odd) ? L"ovulation" : L"conception");
					continue;
				}
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

	void HoldPlayer(bool a_hold);

	// controls come back only once the cut-in camera has fully let go: the free camera restores the control state it saw when it
	// was entered (disabled), so enabling them while it is still active leaves the player stuck
	void ReleasePlayerAfterCamera(int a_tries = 0)
	{
		if ((Cutin::g.active || Cutin::g_pending) && a_tries < 60) {
			Later(0.25f, [a_tries] { ReleasePlayerAfterCamera(a_tries + 1); });
			return;
		}
		Later(0.2f, [] { HoldPlayer(false); });
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
		LoadSettings(false);  // MCM changes take effect at once
		const RE::FormID id = a->GetFormID();
		// extraction: animation (when registered), voice, camera, then the DNA drops after collectSeconds
		const bool anim = Anim(a, "dse-sgo-wanking01-01");
		HoldNpc(a, true);
		g_collecting = true;
		VoiceLoop(id, 0.0f, g_settings.collectSeconds, [] { return g_collecting.load(); });
		Camera(a, g_settings.collectSeconds, true, 0.5f);  // close on his hands from the side, round to the front, waist-up, face
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

	float g_virOverride = -1.0f;  // OCum's stored share for the next GiveDnaBF (-1 = ask Beeing Female)

	void GiveDnaBF(RE::FormID id)
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
				const float vir = g_virOverride >= 0.0f ? g_virOverride : (v.IsFloat() ? v.GetFloat() : 1.0f);
				g_virOverride = -1.0f;
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
				bool dropped = false;
				if (auto item = RE::TESForm::LookupByID<RE::AlchemyItem>(d->item)) {
					// the bottle drops at his feet, a little in front of him; the player picks it up
					if (g_settings.dropBottle) {
						if (auto ref = a->PlaceObjectAtMe(item, false)) {
							const float  h = a->GetAngleZ();
							RE::NiPoint3 at = a->GetPosition() + RE::NiPoint3{ std::sin(h) * 40.0f, std::cos(h) * 40.0f, 12.0f };
							ref->SetPosition(at);
							dropped = true;
						}
					}
					if (!dropped) {
						RE::PlayerCharacter::GetSingleton()->AddObjectToContainer(item, nullptr, 1, nullptr);
					}
				}
				// he has given it: Beeing Female's virility starts recovering from now
				CallStatic("StorageUtil", "SetFloatValue", nullptr, a, std::string("FW.LastSexTime"), d->lastDay);
				Log("DNA collected: {:08X} virility {:.2f} -> {}  {}", id, vir, tier, d->name);
				Notify(dropped ? d->name + "の瓶が足元に落ちた" : d->name + "を採取しました");
			},
			a);
		if (!ok) {
			Notify("Beeing Female が見つからないため、採取できません");
		}
	}

	// the grade always comes from Beeing Female's virility (OCum, OStrands and the like only draw it)
	void GiveDna(RE::FormID id) { GiveDnaBF(id); }

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
		auto ctrl0 = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0x182A, "BeeingFemale.esm");
		if (g_settings.naturalConception && owner) {
			// Beeing Female's own sperm: it stays some days, a child only if she ovulates meanwhile, contraception counts
			const float amount = d.tier >= 100 ? 1.0f : (d.tier >= 50 ? 0.6f : 0.35f);
			const bool  ok = CallMethod(ctrl0, "FWController", "AddSperm", nullptr, static_cast<RE::Actor*>(pc), owner, amount);
			Log("DNA: sperm of {:08X} x{:.2f} -> {}", d.owner, amount, ok ? "Beeing Female" : "Beeing Female controller not found");
			return;
		}
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

	// ------------------------------------------------------------------ pregnancy items (0.6)
	RE::FormID Own(std::uint32_t a_local)
	{
		auto f = RE::TESDataHandler::GetSingleton()->LookupForm(a_local, kPlugin);
		return f ? f->GetFormID() : 0;
	}
	RE::TESQuest* BFController() { return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0x182A, "BeeingFemale.esm"); }
	float         GameDays()
	{
		auto c = RE::Calendar::GetSingleton();
		return c ? c->GetDaysPassed() : 0.0f;
	}

	// Beeing Female's state of the player: 0 follicular, 1 ovulation, 2 luteal, 3 menstruation, 4-7 pregnant (trimesters, labour), 8 recovery
	void CycleState(std::function<void(int)> a_cb)
	{
		CallStatic(
			"StorageUtil", "GetIntValue", [a_cb](const RE::BSScript::Variable& v) { a_cb(v.IsInt() ? v.GetSInt() : -1); },
			static_cast<RE::TESForm*>(RE::PlayerCharacter::GetSingleton()), std::string("FW.CurrentState"), static_cast<std::int32_t>(-1));
	}

	double g_ovulationDue = 0.0;  // game days; the gentle drug's ovulation (co-save)

	// whether a drug can still start an ovulation in this cycle; says why not
	bool DrugCanWork(int a_state)
	{
		if (a_state < 0) {
			Notify("体に変化はなかった（Beeing Female の状態が分からない）");
		} else if (a_state >= 4) {
			Notify("……お腹の子がいる今は、何も起こらないようだ");
		} else if (a_state == 1) {
			Notify("もう体は準備ができている気がする");
		} else if (a_state == 2) {
			Notify("今の周期では、もう効かないようだ");
		} else {
			return true;
		}
		return false;
	}

	void StartOvulation(const char* a_why)
	{
		CallMethod(BFController(), "FWController", "ChangeState", nullptr, static_cast<RE::Actor*>(RE::PlayerCharacter::GetSingleton()), static_cast<std::int32_t>(1));
		Log("cycle: ovulation started by {}", a_why);
		Notify("お腹の奥が、じんわりと熱い……");
	}

	void UseDrugInstant()
	{
		CycleState([](int st) {
			if (DrugCanWork(st)) {
				StartOvulation("the instant drug");
			}
		});
	}

	void UseDrugGentle()
	{
		CycleState([](int st) {
			if (!DrugCanWork(st)) {
				return;
			}
			if (g_ovulationDue > 0.0) {
				Notify("前の薬がまだ効いている途中だ");
				return;
			}
			static std::mt19937 s_rng{ std::random_device{}() };
			const float lo = std::max(g_settings.gentleMinHours, 0.0f), hi = std::max(g_settings.gentleMaxHours, lo);
			const float hours = std::uniform_real_distribution<float>(lo, hi)(s_rng);
			g_ovulationDue = GameDays() + hours / 24.0;
			Log("cycle: the gentle drug works in {:.1f} h", hours);
			Notify("苦い薬だ……効くまで、少し時間がかかりそうだ");
		});
	}

	void CheckGentleDrug()
	{
		if (g_ovulationDue <= 0.0 || GameDays() < g_ovulationDue) {
			return;
		}
		g_ovulationDue = 0.0;
		CycleState([](int st) {
			if (st == 0 || st == 3) {
				StartOvulation("the gentle drug");
			}
		});
	}

	// SexLab Aroused exposure through its "slaUpdateExposure" mod event (Blush When Aroused reads it and reddens her face)
	void Blush(float a_amount)
	{
		if (!g_settings.blush || !RE::TESDataHandler::GetSingleton()->LookupModByName("SexLabAroused.esm")) {
			return;
		}
		CallStatic(
			"ModEvent", "Create",
			[a_amount](const RE::BSScript::Variable& v) {
				const std::int32_t h = v.IsInt() ? v.GetSInt() : 0;
				if (!h) {
					return;
				}
				CallStatic(
					"ModEvent", "PushForm",
					[h, a_amount](const RE::BSScript::Variable&) {
						CallStatic(
							"ModEvent", "PushFloat",
							[h](const RE::BSScript::Variable&) { CallStatic("ModEvent", "Send", nullptr, h); },
							h, a_amount);
					},
					h, static_cast<RE::TESForm*>(RE::PlayerCharacter::GetSingleton()));
			},
			std::string("slaUpdateExposure"));
	}

	// ------------------------------------------------------------------ quest: Bal's Mark
	struct BalState
	{
		double       start = 0.0;  // game day the curse took hold (0 = none)
		std::int32_t qstage = 0;   // our copy of the quest's stage
		std::uint8_t trials = 0;   // 1 Dibella, 2 Mara, 4 the tainted blood
		std::uint8_t slow = 0;     // the Vigilant's charm: half speed
		std::uint8_t spawned = 0;  // the first avatar has appeared
		std::uint8_t final = 0;    // the ritual's avatar is out
		std::uint8_t dormant = 0;  // lifted by the short road (the root sleeps)
		std::uint8_t blood = 0;    // a vampire already carries the tainted blood
	};
	BalState     g_bal;
	std::int32_t g_balShown = -1;  // stage * 2 + glow, as worn now
	// 0.9: relapse, the Vigilant's road, the champion, the dark road (separate record so older saves still load)
	struct BalExtra
	{
		double        failDay = 0.0;      // DA10 failed: the nightmare comes a couple of days later
		double        dormantAt = 0.0;    // the short road put the anchor to sleep
		double        stage4Since = 0.0;  // the mark has been complete since
		double        nextChamp = 0.0;    // the champion's next visit
		RE::FormID    lastSpawn = 0;      // the avatar we placed last (ref)
		RE::FormID    champRef = 0;       // the champion now out (ref)
		std::uint16_t vigMask = 0;        // Vigilant main quests already counted
		std::uint8_t  relapsed = 0;
		std::uint8_t  cured = 0;          // the root is gone (stage 100)
		std::uint8_t  dark = 0;           // yielded (stage 200)
		std::uint8_t  chSeen = 0;         // the mark answered Coldharbour
		std::uint8_t  alliesOut = 0;      // Karen, Lilian and Taranis came to the fight
	};
	std::vector<RE::FormID> g_spirits;  // their refs, this session
	BalExtra g_balx;

	RE::TESQuest* BalQuest() { return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(PoolData::kBalQuest, kPlugin); }
	void          BalStage(std::int32_t a_stage)
	{
		g_bal.qstage = a_stage;
		if (auto q = BalQuest()) {
			CallMethod(q, "Quest", "SetCurrentStageID", nullptr, a_stage);
		}
		Log("bal: stage {}", a_stage);
	}
	void BalObjective(std::int32_t a_index, bool a_done)
	{
		if (auto q = BalQuest()) {
			if (a_done) {
				CallMethod(q, "Quest", "SetObjectiveCompleted", nullptr, a_index, true);
			} else {
				CallMethod(q, "Quest", "SetObjectiveDisplayed", nullptr, a_index, true, false);
			}
		}
	}
	void Fade(float a_hold)
	{
		CallStatic("Game", "FadeOutGame", nullptr, true, true, 0.0f, 1.2f);
		Later(a_hold, [] { CallStatic("Game", "FadeOutGame", nullptr, false, true, 0.0f, 1.5f); });
	}
	int BalCurseStage()
	{
		if (g_bal.start <= 0.0 || g_bal.dormant) {
			return 0;
		}
		const double rate = g_bal.slow ? 0.5 : 1.0;
		const double p = (GameDays() - g_bal.start) * rate / std::max(0.5f, g_settings.markDays);
		return std::clamp(1 + static_cast<int>(p), 1, 4);
	}
	void BalWear(int a_stage, bool a_glow)
	{
		const std::int32_t key = a_stage * 2 + (a_glow ? 1 : 0);
		if (key == g_balShown) {
			return;
		}
		g_balShown = key;
		auto        pc = static_cast<RE::Actor*>(RE::PlayerCharacter::GetSingleton());
		std::string name = g_settings.markName;
		for (const char* sec : { "LewdMarks", "LewdMarks-glow" }) {
			CallStatic("SlaveTats", "simple_remove_tattoo", nullptr, pc, std::string(sec), name, true, true);
		}
		if (a_stage > 0) {
			static const float kAlpha[] = { 0.35f, 0.55f, 0.8f, 1.0f };
			CallStatic("SlaveTats", "simple_add_tattoo", nullptr, pc, std::string(a_glow ? "LewdMarks-glow" : "LewdMarks"), name, static_cast<std::int32_t>(0), true, true, kAlpha[std::clamp(a_stage, 1, 4) - 1]);
		}
		CallStatic("SlaveTats", "synchronize_tattoos", nullptr, pc, true);
		Log("bal: mark stage {} glow {}", a_stage, a_glow);
	}
	// Molag Bal himself when Vigilant is installed (its Coldharbour boss, no scripts on it); else our Dremora avatar
	RE::TESNPC* BalVigilantBoss() { return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESNPC>(0x08DE94, "Vigilant.esm"); }
	bool IsBalAvatar(RE::Actor* a)
	{
		if (!a) {
			return false;
		}
		auto base = a->GetActorBase();
		return a->GetFormID() == g_balx.lastSpawn || a->GetFormID() == g_balx.champRef || (base && base->GetFormID() == Own(PoolData::kBalAvatar));
	}
	// the Vigilant's own Molag Bal, met in Coldharbour (not one we placed)
	bool IsVigilantMolagBal(RE::Actor* a)
	{
		auto boss = BalVigilantBoss();
		return a && boss && a->GetActorBase() == boss && !IsBalAvatar(a);
	}
	RE::TESObjectREFR* SpawnAvatar(RE::TESObjectREFR* a_at, float a_dx, float a_dy, float a_scale, bool a_dremora = false)
	{
		RE::TESNPC* npc = a_dremora ? nullptr : BalVigilantBoss();
		if (npc) {
			a_scale = 1.0f;  // the boss is already tall
		} else {
			npc = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESNPC>(PoolData::kBalAvatar, kPlugin);
		}
		if (!a_at || !npc) {
			return nullptr;
		}
		auto ref = a_at->PlaceObjectAtMe(npc, false);
		if (!ref) {
			return nullptr;
		}
		RE::TESObjectREFR* r = ref.get();
		g_balx.lastSpawn = r->GetFormID();
		CallMethod(r, "ObjectReference", "MoveTo", nullptr, a_at, a_dx, a_dy, 0.0f, true);
		CallMethod(r, "ObjectReference", "SetScale", nullptr, a_scale);
		CallMethod(r, "Actor", "StartCombat", nullptr, static_cast<RE::Actor*>(RE::PlayerCharacter::GetSingleton()));
		return r;
	}
	void Face(std::int32_t a_mood, std::int32_t a_strength, std::int32_t a_eyes);

	// the curse taking hold: she sinks to her knees, black smoke is drawn into her lower belly, the mark surfaces.
	// a_full = first time (long); relapse uses the short one
	// attach a vanilla art object to the lower belly so the effect converges there instead of on the whole body
	void BellyArt(RE::FormID a_art, float a_seconds)
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		auto art = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSArtObject>(a_art, "Skyrim.esm");
		auto root = pc ? pc->Get3D(false) : nullptr;
		if (!art || !root) {
			return;
		}
		RE::NiAVObject* node = root->GetObjectByName("NPC Belly");
		if (!node) {
			node = root->GetObjectByName("NPC Pelvis [Pelv]");
		}
		if (!node) {
			node = root->GetObjectByName("NPC Spine [Spn0]");
		}
		pc->ApplyArtObject(art, a_seconds, nullptr, false, false, node, false);
	}
	// a short swell of the belly, as if something went in
	void BellyPulse(float a_amount)
	{
		auto ref = static_cast<RE::TESObjectREFR*>(RE::PlayerCharacter::GetSingleton());
		CallStatic("NiOverride", "SetBodyMorph", nullptr, ref, std::string("PregnancyBelly"), std::string("BFHeredity.Curse"), a_amount);
		CallStatic("NiOverride", "UpdateModelWeight", nullptr, ref);
		Later(0.45f, [ref] {
			CallStatic("NiOverride", "ClearBodyMorph", nullptr, ref, std::string("PregnancyBelly"), std::string("BFHeredity.Curse"));
			CallStatic("NiOverride", "UpdateModelWeight", nullptr, ref);
		});
	}

	void BalCurseFx(bool a_full)
	{
		auto  pc = static_cast<RE::Actor*>(RE::PlayerCharacter::GetSingleton());
		auto  ref = static_cast<RE::TESObjectREFR*>(pc);
		auto* dh = RE::TESDataHandler::GetSingleton();
		const float len = a_full ? 6.0f : 3.5f;
		HoldPlayer(true);
		// camera: circles from her side to the front, settles on the belly, ends on her face
		Camera(pc, len, true, 0.5f, 1u << (49 - 30));
		if (auto idle = dh->LookupForm<RE::TESIdleForm>(0x0E8E52, "Skyrim.esm")) {  // IdleKneelingEnter
			CallMethod(pc, "Actor", "PlayIdle", nullptr, idle);
		}
		if (auto imod = dh->LookupForm<RE::TESImageSpaceModifier>(0x10F503, "Skyrim.esm")) {  // NightingaleStrifeCastImod: dark pulse
			CallMethod(imod, "ImageSpaceModifier", "Apply", nullptr, a_full ? 1.0f : 0.6f);
		}
		if (auto vfx = dh->LookupForm<RE::BGSReferenceEffect>(0x10F506, "Skyrim.esm")) {  // NightingaleStrifeAbsorbTargetVFX: dark streams drawn in
			CallMethod(vfx, "VisualEffect", "Play", nullptr, ref, len * 0.7f, static_cast<RE::TESObjectREFR*>(nullptr));
		}
		if (auto sh = dh->LookupForm<RE::TESEffectShader>(0x075272, "Skyrim.esm")) {  // ReanimateFXShader: black-violet haze on the body
			CallMethod(sh, "EffectShader", "Play", nullptr, ref, len * 0.45f);  // only at first, then it gathers at the belly
		}
		// the haze gathers and is drawn into the lower belly
		Later(len * 0.15f, [len] {
			BellyArt(0x10F507, len * 0.35f);  // NightingaleStrifeAbsorbTargetPointFX: dark point the streams converge on
			BellyArt(0x0531AE, len * 0.35f);  // SoulTrapTargetPointFX: violet swirl sinking in
		});
		Face(9, 70, 30);  // fear
		CallStatic("Game", "ShakeCamera", nullptr, static_cast<RE::TESObjectREFR*>(nullptr), 0.25f, len * 0.4f);
		CallStatic("Game", "ShakeController", nullptr, 0.3f, 0.5f, len * 0.4f);
		Later(len * 0.45f, [len] {
			Blush(35);
			Face(11, 80, 60);  // pain / sorrow, eyes half shut
			CallStatic("Game", "ShakeCamera", nullptr, static_cast<RE::TESObjectREFR*>(nullptr), 0.5f, 0.6f);
			CallStatic("Game", "ShakeController", nullptr, 0.6f, 0.8f, 0.5f);
			BellyPulse(0.12f);
			BalWear(1, true);  // mark surfaces, glowing
		});
		Later(len, [] {
			auto pc = static_cast<RE::Actor*>(RE::PlayerCharacter::GetSingleton());
			if (auto idle = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESIdleForm>(0x0E8E53, "Skyrim.esm")) {  // IdleKneelingExit
				CallMethod(pc, "Actor", "PlayIdle", nullptr, idle);
			}
			Face(-1, 0, -1);
			ReleasePlayerAfterCamera();
		});
		Later(len + 4.0f, [] { BalWear(BalCurseStage(), false); });
	}

	void BalBegin()
	{
		BalCurseFx(true);
		Notify("黒い煙が下腹部に染み込んでいく……");
		Later(3.0f, [] {
			g_bal.start = GameDays();
			BalStage(10);
			Later(2.0f, [] {
				Notify("「勝ったのはお前だ。だが、お前の胎はもう我がものだ」");
				BalStage(20);
				BalObjective(20, false);
			});
		});
	}
	void BalTrial(std::uint8_t a_bit, std::int32_t a_stage)
	{
		g_bal.trials |= a_bit;
		BalStage(a_stage);
		BalObjective(a_stage, true);
		// the mark fades by one stage: progress set back to the start of the stage below
		const int    now = BalCurseStage();
		const double rate = g_bal.slow ? 0.5 : 1.0;
		const double p = std::max(0, now - 2) * static_cast<double>(std::max(0.5f, g_settings.markDays));
		g_bal.start = GameDays() - p / rate;
		Notify("紋様が少し薄くなった");
		if ((g_bal.trials & 7) == 7) {
			Later(2.0f, [] {
				BalStage(50);
				BalObjective(50, false);
			});
		}
	}
	void          BalCure();
	bool          HasVigilant();
	std::uint16_t VigilantDoneMask();
	void          BalEnd()
	{
		if (g_balx.relapsed) {  // second ritual (no Vigilant installed): the root goes too
			BalCure();
			return;
		}
		g_balx.dormantAt = GameDays();
		g_balx.vigMask = HasVigilant() ? VigilantDoneMask() : 0;
		g_bal.dormant = 1;
		g_bal.final = 0;
		BalWear(0, false);
		BalStage(60);
		BalObjective(50, true);
		Notify("胎に繋がれた鎖が砕けた。紋様がゆっくりと消えていく……");
	}

	// Vigilant main quests: the Skyrim chapters, Children of Oblivion, Coldharbour
	constexpr RE::FormID kVigQuests[] = { 0x005CE2, 0x005CE3, 0x006271, 0x00627F, 0x0082EA, 0x0098C9, 0x009E68, 0x00A3FE,
		0x00EA8A, 0x00EFF7, 0x011B75, 0x065932, 0x12F24E, 0x1363DB, 0x08F2F4 };
	std::uint16_t VigilantDoneMask()
	{
		std::uint16_t m = 0;
		auto          dh = RE::TESDataHandler::GetSingleton();
		for (std::size_t i = 0; i < std::size(kVigQuests); ++i) {
			auto q = dh->LookupForm<RE::TESQuest>(kVigQuests[i], "Vigilant.esm");
			if (q && q->IsCompleted()) {
				m |= static_cast<std::uint16_t>(1u << i);
			}
		}
		return m;
	}
	bool HasVigilant() { return RE::TESDataHandler::GetSingleton()->LookupModByName("Vigilant.esm") != nullptr; }

	void BalCure()
	{
		for (auto id : g_spirits) {
			if (auto r = RE::TESForm::LookupByID<RE::TESObjectREFR>(id)) {
				CallMethod(r, "ObjectReference", "Disable", nullptr, true);
			}
		}
		g_spirits.clear();
		g_balx.cured = 1;
		g_balx.relapsed = 0;
		g_bal.dormant = 1;
		g_bal.final = 0;
		BalWear(0, false);
		BalStage(100);
		BalObjective(70, true);
		BalObjective(80, true);
		BalObjective(85, true);
		BalObjective(50, true);
		if (auto q = BalQuest()) {
			CallMethod(q, "Quest", "CompleteQuest", nullptr);
		}
		Fade(2.5f);
		Notify("錨は根ごと砕けた。胎は、もう誰のものでもない");
	}

	void BalCurseFx(bool a_full);
	void BalRelapse(const char* a_why)
	{
		if (!g_bal.dormant || g_balx.cured || g_balx.dark || g_balx.dormantAt <= 0.0) {
			return;
		}
		Log("bal: relapse ({})", a_why);
		g_bal.dormant = 0;
		g_balx.relapsed = 1;
		g_balx.vigMask = VigilantDoneMask();
		const double rate = g_bal.slow ? 0.5 : 1.0;
		g_bal.start = GameDays() - std::max(0.5f, g_settings.markDays) / rate;  // back at stage 2
		g_balShown = -1;
		BalStage(70);
		BalObjective(70, false);
		Notify(a_why);
		BalCurseFx(false);
	}

	void BalDark()
	{
		g_balx.dark = 1;
		g_bal.slow = 0;
		g_bal.start = GameDays() - 10.0 * std::max(0.5f, g_settings.markDays);  // stays complete
		BalStage(200);
		if (auto q = BalQuest()) {
			CallMethod(q, "Quest", "CompleteQuest", nullptr);
		}
		Blush(40.0f);
		BalCurseFx(false);
		Notify("「よかろう。お前の胎は、永遠に我が主のものだ」");
	}

	void BalChampion()
	{
		auto msg = RE::TESForm::LookupByID(Own(PoolData::kBalChampionMsg));
		if (!msg) {
			return;
		}
		g_balx.nextChamp = GameDays() + 1.0;  // not again while the box is up
		CallMethod(msg, "Message", "Show",
			[](const RE::BSScript::Variable& v) {
				const int pick = v.IsInt() ? v.GetSInt() : 0;
				if (pick == 1) {
					BalDark();
					return;
				}
				auto pc = RE::PlayerCharacter::GetSingleton();
				if (auto r = SpawnAvatar(pc, 0.0f, 500.0f, 1.2f, true)) {
					g_balx.champRef = r->GetFormID();
					r->SetDisplayName("モラグ・バルの勇者", true);
				}
				g_balx.nextChamp = GameDays() + 5.0;
			},
			0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	}
	// every poll: the first avatar at the altar, the mark's stage and glow
	void CheckBal()
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return;
		}
		if (!g_bal.spawned && g_bal.start <= 0.0) {
			auto da10 = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0x22F08, "Skyrim.esm");
			auto cell = pc->GetParentCell();
			if (da10 && da10->GetCurrentStageID() >= 200 && da10->GetCurrentStageID() < 500 && cell && cell->GetFormID() == 0x18BE9) {
				if (SpawnAvatar(RE::TESForm::LookupByID<RE::TESObjectREFR>(0xC7B73), 220.0f, 0.0f, 1.3f)) {
					g_bal.spawned = 1;
					Notify("「我が器にふさわしいか、試してやろう」");
					Log("bal: avatar at the altar");
				}
			}
		}
		const float now = GameDays();
		// failure route: DA10 failed -> nightmare a couple of days later (see the sleep sink)
		if (!g_bal.spawned && g_bal.start <= 0.0 && g_balx.failDay <= 0.0) {
			auto da10 = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0x22F08, "Skyrim.esm");
			if (da10 && da10->GetCurrentStageID() >= 500) {
				g_balx.failDay = now;
				Log("bal: DA10 failed, nightmare pending");
			}
		}
		if (g_bal.dormant && g_bal.start > 0.0 && g_balx.dormantAt <= 0.0 && !g_balx.cured && !g_balx.dark) {
			g_balx.dormantAt = now;  // a 0.8 save, lifted before this version
			g_balx.vigMask = HasVigilant() ? VigilantDoneMask() : 0;
		}
		// the sleeping anchor wakes
		if (g_bal.dormant && !g_balx.cured && !g_balx.dark && g_balx.dormantAt > 0.0) {
			static auto sanguinare = RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(0x0B8780, "Skyrim.esm");
			if (sanguinare && pc->HasSpell(sanguinare)) {
				BalRelapse("噛まれた首筋から、下腹部へ冷たい疼きが走る。紋様が戻ってきた……");
				return;
			}
			if (now - g_balx.dormantAt >= 30.0f) {
				Fade(2.0f);
				BalRelapse("夢の中で囁きが聞こえた。「まだ終わっていない」……目覚めると、紋様が戻っていた");
				return;
			}
			if (HasVigilant() && (VigilantDoneMask() & ~g_balx.vigMask)) {
				BalRelapse("モラグ・バルの手下と戦ううちに、下腹部が疼き始めた。紋様が戻ってきた……");
				return;
			}
			CycleState([](int c) {
				if (c >= 4 && c <= 7) {
					BalRelapse("身ごもった瞬間、下腹部に紋様が浮かび上がった……");
				}
			});
		} else if (g_bal.dormant) {
			g_balx.vigMask = HasVigilant() ? VigilantDoneMask() : 0;  // baseline while asleep
		}
		const int st = BalCurseStage();
		if (st == 0) {
			if (g_balShown > 0) {
				BalWear(0, false);
			}
			return;
		}
		// the Vigilant's road: each main quest done thins the mark; Coldharbour wakes it
		if (g_balx.relapsed && g_bal.qstage == 80 && g_settings.spiritAllies) {
			auto fall = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0x00EFF7, "Vigilant.esm");  // "Endless Fall": mother, daughter, the cursed knight
			if (fall && fall->IsCompleted()) {
				BalObjective(80, true);
				BalStage(85);
				BalObjective(85, false);
				Notify("カレンとリリアン、呪われた騎士タラニスの魂が囁いた。「コールドハーバーで、私たちも共に」");
			}
		}
		if (g_bal.qstage == 85 && !g_balx.alliesOut && pc->IsInCombat()) {
			auto boss = BalVigilantBoss();
			if (auto pl = RE::ProcessLists::GetSingleton(); boss && pl) {
				for (auto& h : pl->highActorHandles) {
					auto a = h.get();
					if (!a || a->IsDead() || a->GetActorBase() != boss || IsBalAvatar(a.get()) || a->GetPosition().GetDistance(pc->GetPosition()) > 5000.0f) {
						continue;
					}
					g_balx.alliesOut = 1;
					auto* dh = RE::TESDataHandler::GetSingleton();
					auto  ghost = dh->LookupForm<RE::TESEffectShader>(0x03B6CB, "Skyrim.esm");  // GhostFXShader
					const RE::FormID who[3] = { 0x170A33, 0x170A32, 0x170A31 };  // mother, daughter, knight (Vigilant's own spirits of them)
					const float      off[3][2] = { { -220.0f, -120.0f }, { 220.0f, -120.0f }, { 0.0f, 260.0f } };
					for (int i = 0; i < 3; ++i) {
						auto npc = dh->LookupForm<RE::TESNPC>(who[i], "Vigilant.esm");
						auto ref = npc ? pc->PlaceObjectAtMe(npc, false) : nullptr;
						if (!ref) {
							continue;
						}
						RE::TESObjectREFR* r = ref.get();
						g_spirits.push_back(r->GetFormID());
						CallMethod(r, "ObjectReference", "MoveTo", nullptr, static_cast<RE::TESObjectREFR*>(pc), off[i][0], off[i][1], 0.0f, true);
						CallMethod(r, "Actor", "SetPlayerTeammate", nullptr, true, true);
						CallMethod(r, "Actor", "IgnoreFriendlyHits", nullptr, true);
						if (ghost) {
							CallMethod(ghost, "EffectShader", "Play", nullptr, r, -1.0f);
						}
						CallMethod(r, "Actor", "StartCombat", nullptr, a.get());
					}
					Notify("「娘には指一本触れさせない」「お母さん、一緒に！」「我が剣、今度こそ主のために」");
					break;
				}
			}
		}
		if (g_balx.relapsed && g_bal.qstage >= 80 && g_bal.qstage < 100) {
			const std::uint16_t m = VigilantDoneMask();
			if (const std::uint16_t fresh = m & ~g_balx.vigMask) {
				g_balx.vigMask = m;
				const double rate = g_bal.slow ? 0.5 : 1.0;
				g_bal.start = std::min(static_cast<double>(now), g_bal.start + 0.5 * std::max(0.5f, g_settings.markDays) / rate);
				Notify("警戒者として戦うたび、紋様が少し薄くなる");
			}
			auto ch = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0x12F24E, "Vigilant.esm");
			if (!g_balx.chSeen && ch && (ch->IsRunning() || ch->IsCompleted())) {
				g_balx.chSeen = 1;
				BalWear(st, true);
				Blush(25.0f);
				Notify("コールドハーバーの空気に、紋様が強く光って応えた……");
			}
		}
		// the champion comes for a complete mark
		if (st == 4 && !g_balx.dark && !g_bal.final) {
			if (g_balx.stage4Since <= 0.0) {
				g_balx.stage4Since = now;
			} else if (now - g_balx.stage4Since >= 7.0 && now >= g_balx.nextChamp && !pc->IsInCombat()) {
				BalChampion();
			}
		} else if (st < 4) {
			g_balx.stage4Since = 0.0;
		}
		CycleState([st](int cycle) {
			const bool glow = cycle == 1;
			if (glow && st >= 3 && g_balShown >= 0 && !(g_balShown & 1)) {
				Notify("下腹部の紋様が熱を帯びて光っている……");
				Blush(20.0f);
			}
			BalWear(st, glow);
		});
	}

	void UseContraceptive()
	{
		if (BalCurseStage() >= 2) {
			Notify("紋様が熱を帯び、避妊の備えが焼け落ちた……");
			Log("bal: contraception refused by the mark");
			return;
		}
		CallMethod(BFController(), "FWController", "AddContraception", nullptr, static_cast<RE::Actor*>(RE::PlayerCharacter::GetSingleton()), 100.0f);
		Log("contraception +100");
		Notify("避妊の備えをした");
	}

	void UsePregnancyTest()
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		CycleState([pc](int st) {
			if (st < 4 || st > 7) {
				Notify("……陰性。印は浮かばなかった");
				Log("pregnancy test: negative (state {})", st);
				return;
			}
			CallStatic(
				"StorageUtil", "GetFloatValue",
				[pc, st](const RE::BSScript::Variable& v) {
					const float since = GameDays() - (v.IsFloat() ? v.GetFloat() : 0.0f);
					if (since < g_settings.testMinDays) {
						Notify("……陰性？　まだ早すぎるのかもしれない");
						Log("pregnancy test: too early ({:.2f} days)", since);
						return;
					}
					if (auto pos = RE::TESForm::LookupByID<RE::TESBoundObject>(Own(PoolData::kPositiveTest))) {
						pc->AddObjectToContainer(pos, nullptr, 1, nullptr);
					}
					Notify("……陽性。くっきりと印が浮かんだ");
					Log("pregnancy test: positive (state {}, {:.1f} days)", st, since);
					// the father may now be told: he joins BFH_FatherFaction, which opens his dialogue line
					CallStatic(
						"StorageUtil", "FormListGet",
						[](const RE::BSScript::Variable& f) {
							auto dad = AsForm(f) ? AsForm(f)->As<RE::Actor>() : nullptr;
							auto fac = RE::TESForm::LookupByID<RE::TESFaction>(Own(PoolData::kFatherFaction));
							if (dad && fac && !dad->IsInFaction(fac)) {
								dad->AddToFaction(fac, 0);
								Log("father {:08X} can be told", dad->GetFormID());
							}
						},
						static_cast<RE::TESForm*>(pc), std::string("FW.ChildFather"), static_cast<std::int32_t>(0));
				},
				static_cast<RE::TESForm*>(pc), std::string("FW.LastConception"), 0.0f);
		});
	}

	// ------------------------------------------------------------------ live chaurus egg
	// Estrus Unified (when installed) carries them: its own belly, eggs moving, laying, hatchlings. Without it a
	// simple version: the belly grows for eggDays, then she lays eggCount chaurus eggs.
	double g_eggStart = 0.0, g_eggDue = 0.0;  // game days (co-save); 0 = not carrying (simple version)
	float  g_eggMorph = -1.0f;

	bool HasEstrusUnified() { return RE::TESDataHandler::GetSingleton()->LookupModByName("EstrusUnified.esp") != nullptr; }

	bool IsChaurus(RE::Actor* a)
	{
		auto r = a ? a->GetRace() : nullptr;
		if (!r) {
			return false;
		}
		auto dh = RE::TESDataHandler::GetSingleton();
		static const std::pair<RE::FormID, const char*> kRaces[] = {
			{ 0x131EB, "Skyrim.esm" }, { 0xA5601, "Skyrim.esm" }, { 0x15136, "Dawnguard.esm" }, { 0x51FB, "Dawnguard.esm" }
		};
		for (auto& [id, plugin] : kRaces) {
			if (dh->LookupForm(id, plugin) == r) {
				return true;
			}
		}
		return false;
	}

	void EggMorph(float a_value)
	{
		if (std::abs(a_value - g_eggMorph) < 0.04f) {
			return;
		}
		g_eggMorph = a_value;
		auto ref = static_cast<RE::TESObjectREFR*>(RE::PlayerCharacter::GetSingleton());
		if (a_value <= 0.0f) {
			CallStatic("NiOverride", "ClearBodyMorph", nullptr, ref, std::string("PregnancyBelly"), std::string("BFHeredity.Egg"));
		} else {
			CallStatic("NiOverride", "SetBodyMorph", nullptr, ref, std::string("PregnancyBelly"), std::string("BFHeredity.Egg"), a_value);
		}
		CallStatic("NiOverride", "UpdateModelWeight", nullptr, ref);
	}

	void LayEggsSimple(int a_count)
	{
		g_eggStart = g_eggDue = 0.0;
		EggMorph(0.0f);
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (auto eggs = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESBoundObject>(0x3AD56, "Skyrim.esm"); eggs && a_count > 0) {
			pc->AddObjectToContainer(eggs, nullptr, a_count, nullptr);
		}
		Notify(a_count > 0 ? "シャウラスの卵を産み落とした……" : "卵が流れ出た……");
		Log("egg: laid {} (simple)", a_count);
	}

	void CarryEggs()
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		auto simple = [] {
			LoadSettings(false);
			g_eggStart = GameDays();
			g_eggDue = g_eggStart + std::max(0.5f, g_settings.eggDays);
			g_eggMorph = -1.0f;
			EggMorph(0.05f);
			Notify("お腹の奥で、卵がかすかに動いた……");
			Log("egg: carrying (simple), lays on day {:.2f}", g_eggDue);
		};
		if (g_eggDue > 0.0) {
			Notify("もう卵を宿している");
			return;
		}
		if (!HasEstrusUnified()) {
			simple();
			return;
		}
		CallStatic(
			"EstrusUnifiedAPI", "RequestPregnancy",
			[simple](const RE::BSScript::Variable& v) {
				if (v.IsBool() && v.GetBool()) {
					Notify("お腹の奥で、卵がかすかに動いた……");
					Log("egg: carrying (Estrus Unified)");
				} else {
					Log("egg: Estrus Unified said no, simple version");
					simple();
				}
			},
			static_cast<RE::Actor*>(pc), static_cast<std::int32_t>(1));
	}

	void UseVermifuge()
	{
		if (g_eggDue > 0.0) {
			LayEggsSimple(1);  // they come out early and few
			return;
		}
		if (!HasEstrusUnified()) {
			Notify("何も起こらなかった");
			return;
		}
		auto pc = RE::PlayerCharacter::GetSingleton();
		CallStatic(
			"EstrusUnifiedAPI", "GetPregnancyType",
			[](const RE::BSScript::Variable& v) {
				const int type = v.IsInt() ? v.GetSInt() : 0;
				if (type <= 0) {
					Notify("何も起こらなかった");
					return;
				}
				CallStatic("EstrusUnifiedAPI", "PlayBirth", nullptr, static_cast<RE::Actor*>(RE::PlayerCharacter::GetSingleton()), static_cast<std::int32_t>(type), static_cast<std::int32_t>(1));
				Log("egg: vermifuge, Estrus Unified birth (type {})", type);
			},
			static_cast<RE::Actor*>(pc));
	}

	void CheckEggs()
	{
		if (g_eggDue <= 0.0) {
			return;
		}
		const double now = GameDays();
		if (now >= g_eggDue) {
			LoadSettings(false);
			LayEggsSimple(std::max(1, g_settings.eggCount));
			return;
		}
		const double span = std::max(0.01, g_eggDue - g_eggStart);
		EggMorph(static_cast<float>(std::clamp((now - g_eggStart) / span, 0.05, 1.0)));
	}

	// where live eggs come from: an egg sac's harvest, a chaurus the player kills

	// ------------------------------------------------------------------ 0.10: Blood Pact (Lordbound's Garshakur)
	struct PactState
	{
		std::int32_t stage = 0;
		float        armedDay = 0.0f;
		float        snoozeUntil = 0.0f;
		RE::FormID   captives[3]{};
		RE::FormID   borak = 0;
		RE::FormID   rival = 0;
		std::uint8_t route = 0;  // 1 Imperial, 2 Stormcloak, 3 Orc
		std::uint8_t partner = 0;  // 1 Gorek, 2 Borak
		std::uint8_t talked = 0;   // 1 Eira, 2 Livia, 4 Helga
		std::uint8_t escaped = 0;
		std::uint8_t maiden = 0;
		// 0.14: acts 3-5
		RE::FormID   spy = 0;
		RE::FormID   boss = 0;
		std::int32_t kids = -1;     // children she had when the forge birth was awaited
		float        nextAct = 0.0f;
		std::uint8_t wave = 0;
	};
	PactState g_pact;
	constexpr std::uint32_t kPactV1Size = offsetof(PactState, spy);
	struct NightState
	{
		std::int32_t stage = 0;
		std::uint8_t offered = 0;
		std::uint8_t method = 0;   // 1 in her body, 2 a bottle
		std::uint8_t place = 0;    // where the bottle is: 1 boots 2 corset 3 hair 4 inside 5 buried
		std::uint8_t pad = 0;
		RE::FormID   vial = 0;     // the bottle's item while it is out of the inventory
		float        offerAt = 0.0f;
		float        retryAt = 0.0f;
		float        t0 = 0.0f;    // when the seed was taken (her scent grows from here)
		float        lastCheck = 0.0f;
	};
	NightState g_night;
	RE::Actor* NightBorak();
	void       NightStage(std::int32_t a_stage);
	void       NightLoveScene(RE::Actor* a);

	constexpr const char* kLordbound = "Druadach.esm";
	bool          HasLordbound() { return RE::TESDataHandler::GetSingleton()->LookupModByName(kLordbound) != nullptr; }
	RE::TESQuest* PactQuest() { return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(PoolData::kPactQuest, kPlugin); }
	RE::Actor*    Gorek() { return RE::TESDataHandler::GetSingleton()->LookupForm<RE::Actor>(0x02FB3D, kLordbound); }
	// Gorek, or the keep's marker when his reference is not loaded
	RE::TESObjectREFR* GorekOrKeep()
	{
		if (auto g = Gorek()) {
			return g;
		}
		return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectREFR>(0x040973, kLordbound);
	}
	RE::TESFaction* OwnFaction(std::uint32_t a_id) { return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESFaction>(a_id, kPlugin); }
	void          JoinFaction(RE::Actor* a, std::uint32_t a_id)
	{
		if (auto f = OwnFaction(a_id); a && f) {
			CallMethod(a, "Actor", "AddToFaction", nullptr, f);
		}
	}
	void PactStage(std::int32_t a_stage)
	{
		static constexpr std::int32_t kObj[] = { 5, 20, 25, 30, 35, 40, 42, 43, 45, 50, 60, 65, 70, 75, 80, 90, 100, 110 };
		auto       q = PactQuest();
		const auto has = [](std::int32_t st) { return std::find(std::begin(kObj), std::end(kObj), st) != std::end(kObj); };
		if (q && has(g_pact.stage)) {
			CallMethod(q, "Quest", "SetObjectiveCompleted", nullptr, g_pact.stage, true);
		}
		g_pact.stage = a_stage;
		if (q) {
			CallMethod(q, "Quest", "SetCurrentStageID", nullptr, a_stage);
			if (has(a_stage)) {
				CallMethod(q, "Quest", "SetObjectiveDisplayed", nullptr, a_stage, true, false);
			}
			if (a_stage == 150 || a_stage == 200) {
				CallMethod(q, "Quest", "CompleteQuest", nullptr);
			}
		}
		Log("pact: stage {}", a_stage);
	}
	RE::TESObjectREFR* PlaceOwn(std::uint32_t a_npc, RE::TESObjectREFR* a_at, float a_dx, float a_dy)
	{
		auto npc = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESNPC>(a_npc, kPlugin);
		if (!npc || !a_at) {
			return nullptr;
		}
		auto ref = a_at->PlaceObjectAtMe(npc, false);
		if (!ref) {
			return nullptr;
		}
		CallMethod(ref.get(), "ObjectReference", "MoveTo", nullptr, a_at, a_dx, a_dy, 0.0f, true);
		return ref.get();
	}
	void PactAbduct()
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		JoinFaction(pc, g_pact.route == 1 ? PoolData::kPactImperial : g_pact.route == 2 ? PoolData::kPactStorm : PoolData::kPactOrc);
		JoinFaction(Gorek(), PoolData::kPactChief);
		Fade(3.5f);
		PactStage(10);
		Notify("襲撃隊の縄が手首に食い込む。他にも捕まった女たちがいる……");
		Later(1.5f, [] {
			auto pc = RE::PlayerCharacter::GetSingleton();
			auto marker = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectREFR>(0x081BD5, kLordbound);  // the stockade
			if (!marker) {
				return;
			}
			CallMethod(pc, "ObjectReference", "MoveTo", nullptr, marker, 0.0f, 0.0f, 0.0f, true);
			Later(1.5f, [marker] {
				const std::uint32_t who[3] = { PoolData::kPactEira, PoolData::kPactLivia, PoolData::kPactHelga };
				for (int i = 0; i < 3; ++i) {
					if (auto r = PlaceOwn(who[i], marker, -120.0f + 120.0f * i, 160.0f)) {
						g_pact.captives[i] = r->GetFormID();
					}
				}
				PactStage(20);
			});
		});
	}
	void PactSummoned()
	{
		Notify("オークの戦士が囲いの扉を開けた。「族長がお呼びだ」");
		Later(2.0f, [] {
			Fade(2.5f);
			Later(1.2f, [] {
				JoinFaction(Gorek(), PoolData::kPactChief);
				if (auto g = GorekOrKeep()) {
					CallMethod(RE::PlayerCharacter::GetSingleton(), "ObjectReference", "MoveTo", nullptr, g, 0.0f, -180.0f, 0.0f, true);
				}
				PactStage(30);
			});
		});
	}
	void PactEscape()
	{
		Fade(4.0f);
		Notify("見張りの交代の隙に、エイラと崖を下りた。西へ、マルカルスへ……");
		g_pact.escaped = 1;
		Later(1.8f, [] {
			if (auto hamal = RE::TESForm::LookupByID<RE::TESObjectREFR>(0x01E766)) {
				CallMethod(RE::PlayerCharacter::GetSingleton(), "ObjectReference", "MoveTo", nullptr, hamal, 0.0f, -200.0f, 0.0f, true);
			}
			PactStage(25);
		});
	}
	void PactCleanse()
	{
		CycleState([](int st) {
			if (st >= 4 && st <= 7) {
				Notify("「お腹の子が生まれてから、もう一度いらっしゃい。それまでここで休んで」");
				return;
			}
			Fade(5.0f);
			Blush(25.0f);
			Later(2.5f, [] {
				g_pact.maiden = 1;
				PactStage(150);
				Notify("香油と湯気の中で祈りが満ちる。……乙女の祝福を受けた");
			});
		});
	}
	// ---- acts 3-5 (0.14)
	RE::TESObjectREFR* PlaceBase(std::uint32_t a_base, RE::TESObjectREFR* a_at, float a_dx, float a_dy)
	{
		auto base = RE::TESForm::LookupByID<RE::TESNPC>(a_base);
		if (!base || !a_at) {
			return nullptr;
		}
		auto ref = a_at->PlaceObjectAtMe(base, false);
		if (!ref) {
			return nullptr;
		}
		CallMethod(ref.get(), "ObjectReference", "MoveTo", nullptr, a_at, a_dx, a_dy, 0.0f, true);
		return ref.get();
	}
	void PactSpawnSpy()
	{
		auto at = GorekOrKeep();
		auto spy = PlaceBase(0x0ABEE2, at, 260.0f, 120.0f);  // LvlThalmorMageMale, in a merchant's clothes
		if (!spy) {
			return;
		}
		auto a = spy->As<RE::Actor>();
		CallMethod(spy, "ObjectReference", "SetDisplayName", nullptr, std::string("流れの行商人ファリンデル"), true);
		CallMethod(spy, "ObjectReference", "RemoveAllItems", nullptr, static_cast<RE::TESObjectREFR*>(nullptr), false, false);
		for (std::uint32_t id : { 0x04223Cu, 0x04223Du, 0x04223Bu }) {
			if (auto arm = RE::TESForm::LookupByID<RE::TESObjectARMO>(id)) {
				CallMethod(a, "Actor", "EquipItem", nullptr, static_cast<RE::TESForm*>(arm), false, true);
			}
		}
		JoinFaction(a, PoolData::kPactSpyFaction);
		g_pact.spy = spy->GetFormID();
		PactStage(65);
	}
	void PactSpyUnmasked()
	{
		auto spy = RE::TESForm::LookupByID<RE::Actor>(g_pact.spy);
		if (!spy) {
			return;
		}
		Notify("行商人が袖から短剣を抜いた。その耳は……アルトマーだ！");
		CallMethod(spy, "ObjectReference", "SetDisplayName", nullptr, std::string("サルモールの密偵"), true);
		CallMethod(spy, "Actor", "StartCombat", nullptr, static_cast<RE::Actor*>(RE::PlayerCharacter::GetSingleton()));
		PactStage(70);
	}
	void PactSpyDead(RE::Actor* a)
	{
		if (auto note = RE::TESForm::LookupByID<RE::TESBoundObject>(Own(PoolData::kPactLetter))) {
			a->AddObjectToContainer(note, nullptr, 1, nullptr);
		}
		g_pact.spy = 0;
		Notify("密偵の懐に、封蝋の押された書状がある");
	}
	void PactRaid()
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		Notify("見張りの角笛が鳴り響く！「サルモールだ！　門を守れ！」");
		const std::uint32_t troops[] = { 0x02B35E, 0x02B35E, 0x02B361, 0x02B362 };
		for (int i = 0; i < 4; ++i) {
			if (auto r = PlaceBase(troops[i], pc, -500.0f + 330.0f * i, 1400.0f)) {
				CallMethod(r, "Actor", "StartCombat", nullptr, static_cast<RE::Actor*>(pc));
			}
		}
		Later(25.0f, [] {
			auto pc = RE::PlayerCharacter::GetSingleton();
			if (auto b = PlaceBase(0x07DCAA, pc, 0.0f, 1600.0f)) {  // LvlThalmorBoss
				CallMethod(b, "ObjectReference", "SetDisplayName", nullptr, std::string("審問官ヴァランディル"), true);
				CallMethod(b, "Actor", "StartCombat", nullptr, static_cast<RE::Actor*>(pc));
				g_pact.boss = b->GetFormID();
				Notify("「その子を渡せ。自治領の名において」――審問官ヴァランディルが現れた");
				PactStage(100);
			}
		});
	}
	void PactForgeBirth()
	{
		Fade(5.0f);
		Blush(30.0f);
		Notify("炉の火が赤々と燃える中、一族の女たちに手を握られて、子が産声を上げた");
		Later(1.5f, [] {
			if (auto keep = GorekOrKeep()) {
				CallMethod(RE::PlayerCharacter::GetSingleton(), "ObjectReference", "MoveTo", nullptr, keep, 0.0f, -220.0f, 0.0f, true);
			}
		});
		Later(3.0f, [] { PactStage(90); });
		Later(14.0f, [] { PactRaid(); });
	}
	void PactReward()
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (auto gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0x00000F)) {
			pc->AddObjectToContainer(gold, nullptr, 1500, nullptr);
		}
		JoinFaction(pc, PoolData::kPactOrc);
		Fade(3.0f);
		const char* end = g_pact.route == 1 ? "ガルシャクールの戦士団は、帝国軍の側で戦うと誓った" :
		                  g_pact.route == 2 ? "ガルシャクールの戦士団は、ウルフリックのもとへ馬を進めた" :
		                                      "ガルシャクールの炉に、二つの民の子の名が刻まれた";
		Notify(end);
		PactStage(200);
	}

	void CheckPact()
	{
		if (!HasLordbound()) {
			return;
		}
		auto pc = RE::PlayerCharacter::GetSingleton();
		static bool s_elig = false;
		if (!s_elig) {
			s_elig = true;
			JoinFaction(pc, PoolData::kPactEligible);
		}
		const float now = GameDays();
		auto        dh = RE::TESDataHandler::GetSingleton();
		if (g_pact.stage == 0 && g_pact.route == 0) {
			auto orc = dh->LookupForm<RE::TESQuest>(0x05C571, kLordbound);
			auto imp = dh->LookupForm<RE::TESQuest>(0x018784, kLordbound);
			if (orc && orc->IsCompleted()) {
				g_pact.route = 3;
			} else if (imp && imp->IsCompleted()) {
				g_pact.route = 1;
			}
			if (g_pact.route) {
				g_pact.armedDay = now;
				Log("pact: armed (route {})", g_pact.route);
			}
		}
		// the raiders come while she is out in the Druadach mountains
		if (((g_pact.stage == 0 && g_pact.route) || g_pact.stage == 5) && now >= g_pact.snoozeUntil &&
			(g_pact.stage == 5 || now - g_pact.armedDay >= 0.5f) && !pc->IsInCombat() && pc->GetParentCell() && !pc->GetParentCell()->IsInteriorCell()) {
			auto ws = pc->GetWorldspace();
			auto file = ws ? ws->GetFile(0) : nullptr;
			if (file && std::string_view(file->GetFilename()) == kLordbound) {
				g_pact.snoozeUntil = now + 0.1f;
				if (auto msg = RE::TESForm::LookupByID(Own(PoolData::kPactAbductMsg))) {
					CallMethod(msg, "Message", "Show",
						[](const RE::BSScript::Variable& v) {
							if ((v.IsInt() ? v.GetSInt() : 1) == 0) {
								PactAbduct();
							} else {
								g_pact.snoozeUntil = GameDays() + 3.0f;
							}
						},
						0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
				}
			}
		}
		// a child of any orc, at any point before the trials are done, also counts
		if (g_pact.stage >= 10 && g_pact.stage < 45 && !g_pact.escaped) {
			CycleState([](int st) {
				if (st < 4 || st > 7) {
					return;
				}
				CallStatic(
					"StorageUtil", "FormListGet",
					[](const RE::BSScript::Variable& v) {
						auto dad = AsForm(v) ? AsForm(v)->As<RE::Actor>() : nullptr;
						auto race = dad ? dad->GetRace() : nullptr;
						const char* rn = race ? race->GetFullName() : "";
						const bool  orc = race && (race->GetFormID() == 0x013747 || race->GetFormID() == 0x0A82B9 || std::strstr(rn, "Orc") || std::strstr(rn, "オーク"));
						if (!orc || g_pact.stage >= 45 || g_pact.stage < 10) {
							return;
						}
						if (!g_pact.partner) {
							g_pact.partner = 3;
						}
						Blush(25.0f);
						Notify(std::string("下腹部に新しい命の気配がある。") + dad->GetDisplayFullName() + "の子……オークの血を引く子だ");
						PactStage(50);
					},
					static_cast<RE::TESForm*>(RE::PlayerCharacter::GetSingleton()), std::string("FW.ChildFather"), static_cast<std::int32_t>(0));
			});
		}
		if (g_pact.stage == 45) {
			CycleState([](int st) {
				if (st >= 4 && st <= 7 && g_pact.stage == 45) {
					Blush(25.0f);
					Notify("下腹部に新しい命の気配がある。二つの民の血を引く子だ");
					PactStage(50);
				}
			});
		}
		// act 3: a day after the father heard the news, the rumours have a face
		if (g_pact.stage == 60 && !g_pact.escaped) {
			if (g_pact.nextAct <= 0.0f) {
				g_pact.nextAct = now + 1.0f;
			} else if (now >= g_pact.nextAct && Gorek() && Gorek()->Is3DLoaded()) {
				g_pact.nextAct = 0.0f;
				Notify("要塞の女たちが囁いている。「よそ者の産む子は谷に災いを呼ぶ」……そんな噂を、誰が？");
				PactSpawnSpy();
			}
		}
		// anyone may have killed them, not only her
		if (g_pact.stage == 70 && g_pact.spy) {
			if (auto spy = RE::TESForm::LookupByID<RE::Actor>(g_pact.spy); spy && spy->IsDead()) {
				PactSpyDead(spy);
			}
		}
		if (g_pact.stage == 100 && g_pact.boss) {
			if (auto b = RE::TESForm::LookupByID<RE::Actor>(g_pact.boss); b && b->IsDead()) {
				g_pact.boss = 0;
				Notify("審問官が崩れ落ちた。残ったサルモール兵は谷へ逃げていく");
				PactStage(110);
			}
		}
		// act 4: wait for the birth (any birth while the forge watch is on)
		if (g_pact.stage == 80) {
			CallStatic(
				"StorageUtil", "GetIntValue",
				[](const RE::BSScript::Variable& v) {
					const int n = v.IsInt() ? v.GetSInt() : 0;
					if (g_pact.stage != 80) {
						return;
					}
					if (g_pact.kids < 0) {
						g_pact.kids = n;
					} else if (n > g_pact.kids) {
						PactForgeBirth();
					}
				},
				static_cast<RE::TESForm*>(pc), std::string("FW.NumChilds"), static_cast<std::int32_t>(0));
		}
	}

	// failure route: a nightmare after DA10 failed
	class BalSleepSink : public RE::BSTEventSink<RE::TESSleepStopEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESSleepStopEvent*, RE::BSTEventSource<RE::TESSleepStopEvent>*) override
		{
			SKSE::GetTaskInterface()->AddTask([] {
				if (g_bal.spawned || g_bal.start > 0.0 || g_balx.failDay <= 0.0 || GameDays() - g_balx.failDay < 2.0f) {
					return;
				}
				g_bal.spawned = 1;
				Fade(2.5f);
				Notify("眠りの中で、冷たい鎖の音がした。「逃げられると思ったか」");
				Later(2.0f, [] {
					SpawnAvatar(RE::PlayerCharacter::GetSingleton(), 0.0f, 350.0f, 1.3f);
				});
			});
			return RE::BSEventNotifyControl::kContinue;
		}
	};
	BalSleepSink g_balSleep;

	class EggSourceSink : public RE::BSTEventSink<RE::TESContainerChangedEvent>, public RE::BSTEventSink<RE::TESDeathEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
		{
			if (a_event && a_event->newContainer == 0x14 && a_event->baseObj == Own(PoolData::kPactLetter) && g_pact.stage == 70) {
				SKSE::GetTaskInterface()->AddTask([] { PactStage(75); });
				return RE::BSEventNotifyControl::kContinue;
			}
			if (a_event && a_event->newContainer == 0x14 && a_event->baseObj == Own(PoolData::kTaintedBlood) && !(g_bal.trials & 4)) {
				SKSE::GetTaskInterface()->AddTask([] { BalTrial(4, 43); });
				return RE::BSEventNotifyControl::kContinue;
			}
			if (!a_event || a_event->newContainer != 0x14 || a_event->oldContainer != 0 || a_event->baseObj != 0x3AD56) {
				return RE::BSEventNotifyControl::kContinue;
			}
			SKSE::GetTaskInterface()->AddTask([] {
				LoadSettings(false);
				static std::mt19937 rng{ std::random_device{}() };
				if (!g_settings.liveEgg || std::uniform_real_distribution<float>(0, 1)(rng) >= g_settings.eggSacChance) {
					return;
				}
				if (auto egg = RE::TESForm::LookupByID<RE::TESBoundObject>(Own(PoolData::kLiveEgg))) {
					RE::PlayerCharacter::GetSingleton()->AddObjectToContainer(egg, nullptr, 1, nullptr);
					Log("egg: from a sac");
				}
			});
			return RE::BSEventNotifyControl::kContinue;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override
		{
			if (!a_event || !a_event->dead || !a_event->actorDying || !a_event->actorKiller || !a_event->actorKiller->IsPlayerRef()) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const RE::FormID who = a_event->actorDying->GetFormID();
			SKSE::GetTaskInterface()->AddTask([who] {
				auto a = RE::TESForm::LookupByID<RE::Actor>(who);
				if (a && IsVigilantMolagBal(a) && g_bal.qstage >= 70 && g_bal.qstage < 100) {
					BalCure();
					return;
				}
				if (a && g_pact.spy && a->GetFormID() == g_pact.spy) {
					PactSpyDead(a);
					return;
				}
				if (a && g_pact.boss && a->GetFormID() == g_pact.boss && g_pact.stage == 100) {
					g_pact.boss = 0;
					Notify("審問官が崩れ落ちた。残ったサルモール兵は谷へ逃げていく");
					PactStage(110);
					return;
				}
				if (a && g_pact.rival && a->GetFormID() == g_pact.rival && g_pact.stage == 43) {
					g_pact.rival = 0;
					Notify("ボラク「よくやった。これで誰も文句は言えん」");
					PactStage(45);
					return;
				}
				if (a && a->GetFormID() == g_balx.champRef && g_balx.champRef) {
					g_balx.champRef = 0;
					g_balx.nextChamp = GameDays() + 5.0;
					Notify("勇者は黒い煙になって消えた。「また来る……」");
					return;
				}
				if (a && IsBalAvatar(a)) {
					if (g_bal.final) {
						BalEnd();
					} else if (g_bal.start <= 0.0) {
						BalBegin();
					}
					return;
				}
				if (a && g_bal.qstage >= 40 && g_bal.qstage < 60 && !(g_bal.trials & 4) && !g_bal.blood) {
					auto vamp = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSKeyword>(0xA82BB, "Skyrim.esm");
					auto blood = RE::TESForm::LookupByID<RE::TESBoundObject>(Own(PoolData::kTaintedBlood));
					if (vamp && blood && a->HasKeyword(vamp)) {
						a->AddObjectToContainer(blood, nullptr, 1, nullptr);
						g_bal.blood = 1;
						Notify("吸血鬼の亡骸から、黒い血の気配がする");
					}
				}
				LoadSettings(false);
				static std::mt19937 rng{ std::random_device{}() };
				if (!a || !g_settings.liveEgg || !IsChaurus(a) || std::uniform_real_distribution<float>(0, 1)(rng) >= g_settings.eggKillChance) {
					return;
				}
				if (auto egg = RE::TESForm::LookupByID<RE::TESBoundObject>(Own(PoolData::kLiveEgg))) {
					a->AddObjectToContainer(egg, nullptr, 1, nullptr);
					Log("egg: in a chaurus {:08X}", who);
				}
			});
			return RE::BSEventNotifyControl::kContinue;
		}
	};
	EggSourceSink g_eggSources;

	bool UsePregnancyItem(RE::FormID a_item)
	{
		if (a_item == Own(PoolData::kVermifuge)) {
			UseVermifuge();
			return true;
		}
		if (a_item == Own(PoolData::kDrugInstant)) {
			UseDrugInstant();
		} else if (a_item == Own(PoolData::kDrugGentle)) {
			UseDrugGentle();
		} else if (a_item == Own(PoolData::kPregnancyTest)) {
			UsePregnancyTest();
		} else if (a_item == Own(PoolData::kContraceptive)) {
			UseContraceptive();
		} else {
			return false;
		}
		return true;
	}

	// ------------------------------------------------------------------ "I'll help you": collection through SexLab or OStim
	void GiveDna(RE::FormID id);

	// asks a_running every second (after a short start) until it says no, then a_done; gives up after ~10 minutes
	void WaitWhile(std::function<void(std::function<void(bool)>)> a_running, std::function<void()> a_done, int a_left = 600)
	{
		Later(1.0f, [a_running, a_done, a_left] {
			a_running([a_running, a_done, a_left](bool running) {
				if (running && a_left > 0) {
					WaitWhile(a_running, a_done, a_left - 1);
				} else {
					a_done();
				}
			});
		});
	}

	void AssistDone(RE::FormID a_npc)
	{
		Log("assist: scene over, collecting from {:08X}", a_npc);
		GiveDna(a_npc);
		g_dnaBusy = false;
	}

	bool StartSceneOStim(RE::Actor* a_npc, bool a_solo)
	{
		if (!RE::TESDataHandler::GetSingleton()->LookupModByName("OStim.esp")) {
			return false;
		}
		const RE::FormID           id = a_npc->GetFormID();
		std::vector<RE::Actor*> actors{ a_npc };
		if (!a_solo) {
			actors.push_back(RE::PlayerCharacter::GetSingleton());
		}
		CallStatic(
			"OLibrary", "GetRandomSceneWithAction",
			[id, a_solo](const RE::BSScript::Variable& v) {
				const std::string scene = v.IsString() ? std::string(v.GetString()) : std::string();
				auto              npc = RE::TESForm::LookupByID<RE::Actor>(id);
				if (scene.empty() || !npc) {
					Notify("OStim に合うアニメーションが見つかりません");
					Log("assist: OStim has no handjob scene");
					g_dnaBusy = false;
					return;
				}
				std::vector<RE::Actor*> actors{ npc };
				if (!a_solo) {
					actors.push_back(RE::PlayerCharacter::GetSingleton());
				}
				CallStatic(
					"OThread", "QuickStart",
					[id, scene](const RE::BSScript::Variable& t) {
						const int tid = t.IsInt() ? t.GetSInt() : -1;
						Log("assist: OStim scene {} thread {}", scene, tid);
						if (tid < 0) {
							Notify("OStim のシーンを始められませんでした");
							g_dnaBusy = false;
							return;
						}
						WaitWhile([tid](std::function<void(bool)> a_answer) {
							CallStatic("OThread", "IsRunning", [a_answer](const RE::BSScript::Variable& r) { a_answer(r.IsBool() && r.GetBool()); }, static_cast<std::int32_t>(tid));
						},
							[id] { AssistDone(id); });
					},
					actors, scene, static_cast<RE::TESObjectREFR*>(nullptr));
			},
			actors, std::string(a_solo ? "malemasturbation" : "handjob"));
		return true;
	}

	bool StartSceneSexLab(RE::Actor* a_npc, bool a_solo)
	{
		auto sl = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0xD62, "SexLab.esm");
		if (!sl) {
			return false;
		}
		const RE::FormID id = a_npc->GetFormID();
		RE::Actor*       none = nullptr;
		const bool       ok = CallMethod(
            sl, "SexLabFramework", "QuickStart",
            [id](const RE::BSScript::Variable&) {
                Log("assist: SexLab scene asked");
                // the thread takes a moment to pick up the actors; then wait while he is in it
                Later(4.0f, [id] {
                    WaitWhile([id](std::function<void(bool)> a_answer) {
                        auto sl2 = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0xD62, "SexLab.esm");
                        CallMethod(sl2, "SexLabFramework", "IsActorActive", [a_answer](const RE::BSScript::Variable& r) { a_answer(r.IsBool() && r.GetBool()); },
                            RE::TESForm::LookupByID<RE::Actor>(id));
                    },
                        [id] { AssistDone(id); });
                });
            },
            a_solo ? a_npc : static_cast<RE::Actor*>(RE::PlayerCharacter::GetSingleton()), a_solo ? none : a_npc, none, none, none, none, std::string("BFHCollect"),
            std::string(a_solo ? "Masturbation" : "Handjob"));
		return ok;
	}

	// a_solo: he does it himself in a SexLab / OStim scene (so OStrands, OCum, 3D CumFX and the like draw it); else the player helps
	void SceneCollect(RE::Actor* a, bool a_solo)
	{
		if (!g_settings.dna || !a) {
			return;
		}
		if (g_dnaBusy.exchange(true)) {
			return;
		}
		LoadSettings(false);
		const bool hasOStim = RE::TESDataHandler::GetSingleton()->LookupModByName("OStim.esp") != nullptr;
		const int  which = g_settings.assistFramework == 0 ? (hasOStim ? 2 : 1) : g_settings.assistFramework;
		const bool ok = which == 2 ? StartSceneOStim(a, a_solo) : StartSceneSexLab(a, a_solo);
		Log("scene collect ({}): {} with {:08X}: {}", a_solo ? "solo" : "assist", which == 2 ? "OStim" : "SexLab", a->GetFormID(), ok ? "asked" : "not available");
		if (!ok) {
			Notify(which == 2 ? "OStim が見つかりません" : "SexLab が見つかりません");
			g_dnaBusy = false;
		}
	}

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
			const RE::FormID who = a_event->speakerRef->GetFormID();
			if (a_event->topicInfoFormID == Own(PoolData::kAssistInfo)) {
				Later(1.0f, [who] { SceneCollect(RE::TESForm::LookupByID<RE::Actor>(who), false); });
				return RE::BSEventNotifyControl::kContinue;
			}
			if (const auto info = a_event->topicInfoFormID; info == Own(PoolData::kBalAsk) || info == Own(PoolData::kBalAsk2)) {
				BalObjective(20, true);
				BalStage(30);
				BalObjective(30, false);
				return RE::BSEventNotifyControl::kContinue;
			} else if (info == Own(PoolData::kBalVigil)) {
				BalObjective(30, true);
				g_bal.slow = 1;
				if (auto charm = RE::TESForm::LookupByID<RE::TESBoundObject>(Own(PoolData::kBalCharm))) {
					RE::PlayerCharacter::GetSingleton()->AddObjectToContainer(charm, nullptr, 1, nullptr);
				}
				BalStage(40);
				for (std::int32_t o : { 41, 42, 43 }) {
					BalObjective(o, false);
				}
				return RE::BSEventNotifyControl::kContinue;
			} else if (info == Own(PoolData::kBalDibella)) {
				Fade(4.0f);
				Blush(20.0f);
				Later(2.0f, [] { BalTrial(1, 41); });
				return RE::BSEventNotifyControl::kContinue;
			} else if (info == Own(PoolData::kBalMara)) {
				Later(1.0f, [] { BalTrial(2, 42); });
				return RE::BSEventNotifyControl::kContinue;
			} else if (info == Own(PoolData::kPactOrder)) {
				g_pact.route = 2;
				PactStage(5);
				return RE::BSEventNotifyControl::kContinue;
			} else if (info == Own(PoolData::kPactEiraTalk) || info == Own(PoolData::kPactLiviaTalk) || info == Own(PoolData::kPactHelgaTalk)) {
				g_pact.talked |= info == Own(PoolData::kPactEiraTalk) ? 1 : info == Own(PoolData::kPactLiviaTalk) ? 2 : 4;
				if (g_pact.talked == 7 && g_pact.stage == 20) {
					Later(3.0f, [] { PactSummoned(); });
				}
				return RE::BSEventNotifyControl::kContinue;
			} else if (info == Own(PoolData::kPactEscape)) {
				Later(1.0f, [] { PactEscape(); });
				return RE::BSEventNotifyControl::kContinue;
			} else if (info == Own(PoolData::kPactDeal1) || info == Own(PoolData::kPactDeal2) || info == Own(PoolData::kPactDeal3)) {
				PactStage(35);
				return RE::BSEventNotifyControl::kContinue;
			} else if (info == Own(PoolData::kPactBlood)) {
				PactStage(40);
				Later(0.5f, [] {
					if (!g_pact.borak) {
						if (auto r = PlaceOwn(PoolData::kPactBorak, GorekOrKeep(), 200.0f, 0.0f)) {
							g_pact.borak = r->GetFormID();
						}
					}
				});
				return RE::BSEventNotifyControl::kContinue;
			} else if (info == Own(PoolData::kPactPickChief)) {
				g_pact.partner = 1;
				PactStage(42);
				return RE::BSEventNotifyControl::kContinue;
			} else if (info == Own(PoolData::kPactPickBorak)) {
				g_pact.partner = 2;
				PactStage(43);
				Later(2.0f, [] {
					auto pc = RE::PlayerCharacter::GetSingleton();
					if (auto r = PlaceOwn(PoolData::kPactRival, pc, 0.0f, 450.0f)) {
						g_pact.rival = r->GetFormID();
						CallMethod(r, "Actor", "StartCombat", nullptr, static_cast<RE::Actor*>(pc));
						Notify("「よそ者の女を連れて族長気取りか、ボラク！」");
					}
				});
				return RE::BSEventNotifyControl::kContinue;
			} else if (info == Own(PoolData::kPactCup)) {
				Fade(3.0f);
				Blush(20.0f);
				Later(1.5f, [] { PactStage(45); });
				return RE::BSEventNotifyControl::kContinue;
			} else if (info == Own(PoolData::kPactTellChief) || info == Own(PoolData::kPactTellBorak)) {
				PactStage(60);
				return RE::BSEventNotifyControl::kContinue;
			} else if (info == Own(PoolData::kNightPrice)) {
				Later(1.0f, [] {
					auto b = NightBorak();
					Fade(3.0f);
					NightStage(20);
					Later(3.0f, [] {
						auto msg = RE::TESForm::LookupByID(Own(PoolData::kNightHowMsg));
						if (!msg) {
							return;
						}
						CallMethod(msg, "Message", "Show",
							[](const RE::BSScript::Variable& v) {
								auto b = NightBorak();
								if (!b) {
									return;
								}
								if ((v.IsInt() ? v.GetSInt() : 0) == 0) {
									NightLoveScene(b);
								} else {
									NightStage(25);
									SceneCollect(b, true);
								}
							},
							0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
					});
					(void)b;
				});
			} else if (info == Own(PoolData::kNightPry)) {
				g_night.retryAt = static_cast<float>(GameDays()) + 2.0f;
				NightStage(35);
			} else if (info == Own(PoolData::kPactSpyTalk)) {
				Later(1.0f, [] { PactSpyUnmasked(); });
			} else if (info == Own(PoolData::kPactLetterChief) || info == Own(PoolData::kPactLetterBorak)) {
				g_pact.kids = -1;
				PactStage(80);
			} else if (info == Own(PoolData::kPactEndChief) || info == Own(PoolData::kPactEndBorak)) {
				Later(1.5f, [] { PactReward(); });
			} else if (info == Own(PoolData::kPactCleanse)) {
				Later(1.0f, [] { PactCleanse(); });
				return RE::BSEventNotifyControl::kContinue;
			} else if (info == Own(PoolData::kBalRelapse)) {
				BalObjective(70, true);
				if (HasVigilant()) {
					g_balx.vigMask = VigilantDoneMask();
					BalStage(80);
					BalObjective(80, false);
				} else {  // no Coldharbour to go to: a second, stronger ritual takes the root
					BalStage(50);
					BalObjective(50, false);
					Notify("警戒者は、もう一度最後の儀式を行うと言った");
				}
				return RE::BSEventNotifyControl::kContinue;
			} else if (info == Own(PoolData::kBalRitual)) {
				Fade(3.0f);
				Later(2.0f, [] {
					g_bal.final = 1;
					Notify("コールドハーバーの冷たい風が吹きつける……");
					SpawnAvatar(RE::PlayerCharacter::GetSingleton(), 0.0f, 400.0f, 1.45f);
				});
				return RE::BSEventNotifyControl::kContinue;
			}
			for (std::size_t i = 0; i < std::size(PoolData::kTellInfos); ++i) {
				if (a_event->topicInfoFormID == Own(PoolData::kTellInfos[i])) {
					Log("told {:08X} (line {})", who, i);
					return RE::BSEventNotifyControl::kContinue;
				}
			}
			if (!s_info || a_event->topicInfoFormID != s_info) {
				return RE::BSEventNotifyControl::kContinue;
			}
			Later(1.0f, [who] {  // let the dialogue menu close first
				LoadSettings(false);
				auto a = RE::TESForm::LookupByID<RE::Actor>(who);
				if (g_settings.soloScene && a && !a->IsChild() && a->GetActorBase() && (!g_settings.maleOnly || a->GetActorBase()->GetSex() == RE::SEX::kMale)) {
					SceneCollect(a, true);
				} else {
					CollectFrom(a);
				}
			});
			return RE::BSEventNotifyControl::kContinue;
		}
	};
	TopicSink g_topic;

	// her face: SetExpressionOverride moods (12 surprise, 13 puzzled, 10 happy); eyes through Mfg Fix when it is there
	void Face(std::int32_t a_mood, std::int32_t a_strength, std::int32_t a_eyes = -1)
	{
		auto pc = static_cast<RE::Actor*>(RE::PlayerCharacter::GetSingleton());
		if (a_mood < 0) {
			CallMethod(pc, "Actor", "ClearExpressionOverride", nullptr);
			CallStatic("MfgConsoleFunc", "ResetPhonemeModifier", nullptr, pc);
			return;
		}
		CallMethod(pc, "Actor", "SetExpressionOverride", nullptr, a_mood, a_strength);
		if (a_eyes >= 0) {
			CallStatic("MfgConsoleFunc", "SetModifier", nullptr, pc, static_cast<std::int32_t>(0), a_eyes);  // blink left
			CallStatic("MfgConsoleFunc", "SetModifier", nullptr, pc, static_cast<std::int32_t>(1), a_eyes);  // blink right
		}
	}

	// the moment it goes in: a short shake, the pad rumbles, her face reacts
	void InsertMoment()
	{
		LoadSettings(false);
		if (g_settings.insertShake) {
			CallStatic("Game", "ShakeCamera", nullptr, static_cast<RE::TESObjectREFR*>(nullptr), 0.35f, 0.45f);
			CallStatic("Game", "ShakeController", nullptr, 0.45f, 0.6f, 0.35f);
		}
		Blush(25.0f);
		if (g_settings.insertFace) {
			Face(12, 90, 55);  // surprise, eyes half shut
			Later(1.6f, [] { Face(10, 45, 25); });  // then a soft, relieved look
		}
	}

	// ------------------------------------------------------------------ 0.15: leaking (closed-legs walk via OAR, Fill Her Up's drip)
	// While she is in BFH_LeakingFaction our OAR sub-mod swaps in the Modesty pack's closed-legs walk / run / idle.
	// Fill Her Up's own drip armour (sr_vagLeak / sr_analLeak) shows it on her body; she keeps walking.
	struct LeakState
	{
		bool          on = false;
		int           kind = 0;     // 1 front, 2 back
		RE::FormID    armour = 0;   // the drip piece we put on
		RE::FormID    displaced = 0;  // what it pushed off that slot
		std::uint32_t serial = 0;
	};
	LeakState g_leak;

	// wet trails down the inner thighs: our own skinned meshes whose texture keeps flowing down; a longer one swapped in as it goes on
	RE::FormID g_trail = 0;
	void TrailOff()
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return;
		}
		for (auto id : PoolData::kLeakTrail) {
			if (auto arm = RE::TESForm::LookupByID<RE::TESObjectARMO>(Own(id))) {
				CallMethod(static_cast<RE::Actor*>(pc), "Actor", "UnequipItem", nullptr, static_cast<RE::TESForm*>(arm), false, true);
				CallMethod(static_cast<RE::TESObjectREFR*>(pc), "ObjectReference", "RemoveItem", nullptr, static_cast<RE::TESForm*>(arm), static_cast<std::int32_t>(99), true,
					static_cast<RE::TESObjectREFR*>(nullptr));
			}
		}
		g_trail = 0;
	}
	void TrailWear(int a_kind, int a_stage)  // a_kind 1 front / 2 back, a_stage 1..3
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || !g_settings.leakTrail) {
			return;
		}
		const auto idx = (a_kind == 2 ? 3 : 0) + std::clamp(a_stage, 1, 3) - 1;
		auto       arm = RE::TESForm::LookupByID<RE::TESObjectARMO>(Own(PoolData::kLeakTrail[idx]));
		if (!arm) {
			return;
		}
		const auto prev = g_trail;
		pc->AddObjectToContainer(arm, nullptr, 1, nullptr);
		CallMethod(static_cast<RE::Actor*>(pc), "Actor", "EquipItem", nullptr, static_cast<RE::TESForm*>(arm), false, true);
		g_trail = arm->GetFormID();
		if (prev && prev != g_trail) {
			if (auto old = RE::TESForm::LookupByID<RE::TESObjectARMO>(prev)) {
				CallMethod(static_cast<RE::TESObjectREFR*>(pc), "ObjectReference", "RemoveItem", nullptr, static_cast<RE::TESForm*>(old), static_cast<std::int32_t>(1), true,
					static_cast<RE::TESObjectREFR*>(nullptr));
			}
		}
	}

	void LeakStop()
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return;
		}
		if (auto fac = RE::TESForm::LookupByID<RE::TESFaction>(Own(PoolData::kLeakFaction))) {
			CallMethod(static_cast<RE::Actor*>(pc), "Actor", "RemoveFromFaction", nullptr, fac);
		}
		if (g_leak.armour) {
			if (auto arm = RE::TESForm::LookupByID<RE::TESObjectARMO>(g_leak.armour)) {
				CallMethod(static_cast<RE::Actor*>(pc), "Actor", "UnequipItem", nullptr, static_cast<RE::TESForm*>(arm), false, true);
				CallMethod(static_cast<RE::TESObjectREFR*>(pc), "ObjectReference", "RemoveItem", nullptr, static_cast<RE::TESForm*>(arm), static_cast<std::int32_t>(1), true,
					static_cast<RE::TESObjectREFR*>(nullptr));
			}
		}
		if (g_leak.displaced) {
			if (auto prev = RE::TESForm::LookupByID<RE::TESObjectARMO>(g_leak.displaced)) {
				CallMethod(static_cast<RE::Actor*>(pc), "Actor", "EquipItem", nullptr, static_cast<RE::TESForm*>(prev), false, true);
			}
		}
		TrailOff();
		if (g_leak.on) {
			Face(-1, 0);
		}
		g_leak = LeakState{ false, 0, 0, 0, g_leak.serial };
	}

	// a_kind 1 = front, 2 = back; lasts a_seconds, then she is herself again

	// the moment a leak starts: slow motion, the camera drawn in low, and her reaction
	struct LeakCam { bool saved = false; bool wasFirst = false; float zoom = 0.0f; RE::NiPoint3 off{}; };
	LeakCam g_leakCam;

	RE::ThirdPersonState* ThirdPerson()
	{
		auto cam = RE::PlayerCamera::GetSingleton();
		if (!cam || !cam->currentState || cam->currentState->id != RE::CameraState::kThirdPerson) {
			return nullptr;
		}
		return static_cast<RE::ThirdPersonState*>(cam->currentState.get());
	}

	void LeakCamRestore()
	{
		if (!g_leakCam.saved) {
			return;
		}
		g_leakCam.saved = false;
		if (auto tp = ThirdPerson()) {
			tp->targetZoomOffset = g_leakCam.zoom;
			tp->posOffsetExpected = g_leakCam.off;
		}
		if (g_leakCam.wasFirst) {
			if (auto cam = RE::PlayerCamera::GetSingleton()) {
				cam->ForceFirstPerson();
			}
		}
	}

	void LeakMoment()
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || pc->IsInCombat() || pc->IsOnMount() || Cutin::g.active) {
			return;
		}
		// slow motion: half a real second
		if (g_settings.leakSlowmo > 0.05f && g_settings.leakSlowmo < 0.99f) {
			if (auto t = RE::BSTimer::GetSingleton()) {
				t->SetGlobalTimeMultiplier(g_settings.leakSlowmo, true);
				Later(0.5f, [] {
					if (auto t2 = RE::BSTimer::GetSingleton()) {
						t2->SetGlobalTimeMultiplier(1.0f, true);
					}
				});
			}
		}
		// camera: third person, eased in and down to the hips, a small jolt, then back after ~2.5 s
		if (g_settings.leakCamera && !g_leakCam.saved) {
			auto cam = RE::PlayerCamera::GetSingleton();
			if (cam) {
				g_leakCam.wasFirst = cam->IsInFirstPerson();
				if (g_leakCam.wasFirst) {
					cam->ForceThirdPerson();
				}
				Later(0.15f, [] {
					if (auto tp = ThirdPerson()) {
						g_leakCam.zoom = tp->targetZoomOffset;
						g_leakCam.off = tp->posOffsetExpected;
						g_leakCam.saved = true;
						tp->targetZoomOffset = 0.0f;            // pulled in close
						tp->posOffsetExpected.z -= 28.0f;        // looking at the hips
						tp->posOffsetExpected.x += 12.0f;        // a little off to the side
					}
					CallStatic("Game", "ShakeCamera", nullptr, static_cast<RE::TESObjectREFR*>(nullptr), 0.18f, 0.35f);
					CallStatic("Game", "ShakeController", nullptr, 0.2f, 0.35f, 0.3f);
				});
				Later(2.8f, [] { LeakCamRestore(); });
			}
		}
		// her reaction: stop short and flinch, a small gasp, then glance around
		if (g_settings.leakReaction) {
			HoldPlayer(true);
			if (!Anim(pc, "IdleNervous")) {
				Anim(pc, "IdleWipeBrow");
			}
			Voice(pc, Heat::kMild);
			Face(9, 55, 40);  // startled
			Later(1.4f, [] {
				Face(13, 70, 25);  // flustered, eyes down
			});
			Later(2.2f, [] {
				auto p = RE::PlayerCharacter::GetSingleton();
				Anim(p, "IdleStop");
				HoldPlayer(false);
			});
		}
	}

	void LeakStart(int a_kind, float a_seconds)
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || g_leak.on) {
			return;
		}
		auto fac = RE::TESForm::LookupByID<RE::TESFaction>(Own(PoolData::kLeakFaction));
		if (fac) {
			pc->AddToFaction(fac, 0);
		}
		g_leak.on = true;
		g_leak.kind = a_kind;
		const std::uint32_t ser = ++g_leak.serial;
		// Fill Her Up's drip piece when it is installed
		auto dh = RE::TESDataHandler::GetSingleton();
		if (auto arm = dh->LookupForm<RE::TESObjectARMO>(a_kind == 2 ? 0x3319 : 0x3318, "sr_FillHerUp.esp")) {
			const auto slot = arm->GetSlotMask().get();
			if (auto worn = pc->GetWornArmor(slot); worn && worn != arm) {
				g_leak.displaced = worn->GetFormID();
			}
			pc->AddObjectToContainer(arm, nullptr, 1, nullptr);
			CallMethod(static_cast<RE::Actor*>(pc), "Actor", "EquipItem", nullptr, static_cast<RE::TESForm*>(arm), false, true);
			g_leak.armour = arm->GetFormID();
		}
		Face(13, 60, 20);  // flustered
		Blush(20.0f);
		LeakMoment();
		// the trail starts short and creeps down toward the knees
		TrailWear(a_kind, 1);
		Later(std::max(4.0f, a_seconds * 0.25f), [ser, a_kind] {
			if (g_leak.on && g_leak.serial == ser) {
				TrailWear(a_kind, 2);
			}
		});
		Later(std::max(8.0f, a_seconds * 0.55f), [ser, a_kind] {
			if (g_leak.on && g_leak.serial == ser) {
				TrailWear(a_kind, 3);
			}
		});
		Notify(a_kind == 2 ? "……後ろから、じわりと漏れてくる" : "……脚のあいだを、じわりと伝っていく");
		Log("leak start kind {} for {:.0f}s", a_kind, a_seconds);
		Later(a_seconds, [ser] {
			if (g_leak.on && g_leak.serial == ser) {
				LeakStop();
			}
		});
	}

	bool g_hideOnly = false;  // the Night Pact: the bottle goes in to be hidden, not used
	void HiddenVialIn(RE::FormID a_item);
	void StartInsert(RE::FormID a_item)
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		g_dnaBusy = true;
		LoadSettings(false);  // MCM changes take effect at once
		// insertion: animation (when registered), voice, camera, then it is in after insertSeconds
		static std::mt19937 s_rng{ std::random_device{}() };
		// SGO4's two insertions, plus SexLab Body Search's "Extra Pocket" when it is installed (its coin bag shows our item)
		auto*       dh = RE::TESDataHandler::GetSingleton();
		auto        pocket = dh->LookupForm<RE::TESObjectANIO>(0xBFB55, "BodySearch.esp");
		const int   pick = static_cast<int>(s_rng() % (pocket ? 3u : 2u));
		const char* insertAnim = pick == 2 ? "ExtraPocketPullin" : (pick == 1 ? "dse-sgo-insert02-01" : "dse-sgo-insert01-01");
		const bool  egg = a_item == Own(PoolData::kLiveEgg);
		const char* held = egg ? "BFHeredity\\AobjChaurusEgg.nif" : "BFHeredity\\AobjSemenBottle.nif";
		if (auto anio = dh->LookupForm<RE::TESObjectANIO>(0x8E5, kPlugin)) {
			anio->SetModel(held);
		}
		if (pocket) {
			pocket->SetModel(pick == 2 ? held : "AnimObjects\\BodySearch\\AOBakaCoinbag.nif");
		}
		Log("insert: {}", insertAnim);
		const bool          anim = Anim(pc, insertAnim);
		HoldPlayer(true);
		g_inserting = true;
		if (g_settings.insertFace) {
			Face(13, 55, 15);  // nervous while she gets ready
		}
		Blush(10.0f);
		VoiceLoop(pc->GetFormID(), 0.0f, g_settings.insertSeconds, [] { return g_inserting.load(); });
		// behind -> slowly round to the front (whole body) -> face; the player is held until the shot is over
		const float camLen = g_settings.camera ? std::max(g_settings.insertSeconds + 1.0f, g_settings.insertCameraSeconds) : g_settings.insertSeconds + 1.0f;
		Camera(pc, camLen, true);
		Later(g_settings.insertSeconds, [a_item, anim, camLen] {
			g_inserting = false;
			InsertMoment();
			Voice(RE::PlayerCharacter::GetSingleton(), Heat::kHot);
			if (g_hideOnly) {
				g_hideOnly = false;
				HiddenVialIn(a_item);
			} else if (a_item == Own(PoolData::kLiveEgg)) {
				CarryEggs();
			} else if (auto d = DnaByItem(a_item)) {
				InjectDna(*d);
				if (g_settings.leakAfterInsert && std::uniform_real_distribution<float>(0, 1)(s_rng) < g_settings.leakChance) {
					const float wait = std::uniform_real_distribution<float>(25.0f, 70.0f)(s_rng);
					Later(wait, [] { LeakStart(1, g_settings.leakSeconds); });
				}
				// OCum's look of it on her (its mesh / overlay, by OCum's own settings)
				if (auto om = g_settings.useOCum ? RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0xF38, "OCum.esp") : nullptr) {
					const std::int32_t intensity = d->tier >= 100 ? 4 : (d->tier >= 50 ? 2 : 1);
					CallMethod(om, "OCumMaleScript", "ApplyCumVaginal", nullptr, static_cast<RE::Actor*>(RE::PlayerCharacter::GetSingleton()), intensity, static_cast<std::int32_t>(0));
				}
			}
			Later(std::max(1.0f, camLen - g_settings.insertSeconds), [anim] {
				if (anim) {
					Anim(RE::PlayerCharacter::GetSingleton(), "IdleForceDefaultState");
				}
				Face(-1, 0);
				if (auto pocket = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESObjectANIO>(0xBFB55, "BodySearch.esp")) {
					pocket->SetModel("AnimObjects\\BodySearch\\AOBakaCoinbag.nif");  // Body Search's own pocket animation keeps its bag
				}
				ReleasePlayerAfterCamera();
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
				if (UsePregnancyItem(item)) {
					return;
				}
				if (DnaByItem(item) || item == Own(PoolData::kLiveEgg)) {
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

	// Beeing Female's cycle: StorageUtil "FW.CurrentState" on the woman (0 follicular, 1 ovulation, 2 luteal, 3 menstruation, ...)
	std::atomic<int> g_lastCycle{ -1 };
	// pregnant: a growing belly has no six-pack. The abs morphs are taken back under our own key in step with the stage.
	std::atomic<float> g_absStage{ -1.0f };
	std::mutex         g_absLock;
	std::map<std::string, float> g_absSet;

	void ApplySoftAbs(int a_state)
	{
		LoadSettings(false);
		float t = 0.0f;
		if (g_settings.softAbs) {
			switch (a_state) {
			case 4: t = 0.4f; break;
			case 5: t = 0.75f; break;
			case 6:
			case 7: t = 1.0f; break;
			case 8: t = 0.5f; break;  // recovering
			default: break;
			}
		}
		t *= g_settings.softAbsStrength;
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return;
		}
		auto ref = static_cast<RE::TESObjectREFR*>(pc);
		for (const char* name : { "MuscleAbs", "MuscleMoreAbs_v2" }) {
			const std::string morph(name);
			CallStatic(
				"NiOverride", "GetBodyMorph",
				[morph, t](const RE::BSScript::Variable& v) {
					const float sbm = v.IsFloat() ? v.GetFloat() : 0.0f;
					const float want = t > 0.0f ? -(sbm + g_settings.bakedAbs) * t : 0.0f;
					{
						std::scoped_lock l(g_absLock);
						auto it = g_absSet.find(morph);
						const float had = it == g_absSet.end() ? 0.0f : it->second;
						if (std::abs(had - want) < 0.01f && it != g_absSet.end()) {
							return;
						}
						g_absSet[morph] = want;
					}
					auto pc = RE::PlayerCharacter::GetSingleton();
					auto ref = static_cast<RE::TESObjectREFR*>(pc);
					if (want == 0.0f) {
						CallStatic("NiOverride", "ClearBodyMorph", nullptr, ref, morph, std::string("BFHeredity"));
					} else {
						CallStatic("NiOverride", "SetBodyMorph", nullptr, ref, morph, std::string("BFHeredity"), want);
					}
					CallStatic("NiOverride", "UpdateModelWeight", nullptr, ref);
					Log("soft abs: {} = {:.2f}", morph, want);
				},
				ref, morph, std::string("Skill Based Muscle.esp"));
		}
	}


	// ------------------------------------------------------------------ 0.11: the womb HUD
	float g_stateSince = 0.0f;  // our own clock for the state, when Beeing Female's is not readable
	int   g_lastSperm = -1;
	std::string FormName(RE::TESForm* f)
	{
		auto a = f ? f->As<RE::Actor>() : nullptr;
		const char* n = a ? a->GetDisplayFullName() : (f ? f->GetName() : nullptr);
		return n && *n ? n : "";
	}
	void FatherName(std::function<void(std::string)> a_cb)
	{
		CallStatic(
			"StorageUtil", "FormListGet", [a_cb](const RE::BSScript::Variable& v) { a_cb(FormName(AsForm(v))); },
			static_cast<RE::TESForm*>(RE::PlayerCharacter::GetSingleton()), std::string("FW.ChildFather"), static_cast<std::int32_t>(0));
	}
	void WombState(int a_last, int a_st)
	{
		Womb::g_show = g_settings.wombHud;
		Womb::g_scale = std::clamp(g_settings.hudScale, 0.4f, 3.0f);
		Womb::g_corner = g_settings.hudCorner & 3;
		{
			std::scoped_lock l(Womb::g_lock);
			Womb::g_data.state = a_st;
		}
		if (a_last == a_st) {
			return;
		}
		g_stateSince = GameDays();
		if (a_last < 0) {
			return;
		}
		const auto pink = Womb::Col(255, 170, 210, 1.0f), gold = Womb::Col(255, 220, 140, 1.0f), red = Womb::Col(230, 90, 110, 1.0f);
		if (a_st == 1) {
			Womb::Push("排卵しました", "卵が、ゆっくりと降りてくる……", gold);
			Blush(8.0f);
		} else if (a_st == 4 && a_last <= 3) {
			Womb::g_flashReq = true;
			FatherName([pink](std::string n) { Womb::Push("受精しました", n.empty() ? "胎の奥で、ふたつがひとつになった" : n + "の子を、宿した", pink); });
		} else if (a_st == 3) {
			Womb::Push("月のものが来た", "今回は、授からなかった", red);
		} else if (a_st == 5) {
			Womb::Push("お腹が膨らみ始めた", "小さな命が、確かにここにいる", pink);
		} else if (a_st == 6) {
			Womb::Push("妊娠後期", "もうすぐ会える", pink);
		} else if (a_st == 8 && a_last == 7) {
			Womb::Push("生まれた", "胎が、ゆっくりと元に戻っていく", gold);
		}
	}
	int RaceLook(RE::Actor* a)
	{
		if (!a) {
			return Womb::kHuman;
		}
		auto        race = a->GetRace();
		const auto  id = race ? race->GetFormID() : 0;
		const char* rn = race ? race->GetFullName() : "";
		auto        kw = [a](RE::FormID f) {
            auto k = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSKeyword>(f, "Skyrim.esm");
            return k && a->HasKeyword(k);
		};
		if (kw(0x013797)) {  // ActorTypeDaedra
			return Womb::kDaedra;
		}
		if (kw(0x0A82BB)) {  // Vampire
			return Womb::kVampire;
		}
		if (id == 0x013747 || id == 0x0A82B9 || std::strstr(rn, "Orc") || std::strstr(rn, "オーク")) {
			return Womb::kOrc;
		}
		if (id == 0x013745 || id == 0x088845 || std::strstr(rn, "Khajiit") || std::strstr(rn, "カジート")) {
			return Womb::kKhajiit;
		}
		if (id == 0x013740 || id == 0x08883A || std::strstr(rn, "Argonian") || std::strstr(rn, "アルゴニアン")) {
			return Womb::kArgonian;
		}
		if (id == 0x013743 || id == 0x013749 || id == 0x013742 || id == 0x088840 || id == 0x088884 || id == 0x08883D || std::strstr(rn, "Elf") ||
			std::strstr(rn, "エルフ") || std::strstr(rn, "アルトマー") || std::strstr(rn, "ボズマー") || std::strstr(rn, "ダンマー")) {
			return Womb::kElf;
		}
		if (kw(0x013795) || kw(0x013798) || id == 0x0CDD84) {  // creatures, animals, werewolves
			return Womb::kBeast;
		}
		return Womb::kHuman;
	}
	void UpdateWomb()
	{
		auto pc = static_cast<RE::TESForm*>(RE::PlayerCharacter::GetSingleton());
		CallStatic(
			"StorageUtil", "GetFloatValue",
			[](const RE::BSScript::Variable& v) {
				const float enter = v.IsFloat() ? v.GetFloat() : 0.0f;
				const float since = enter > 0.0f ? enter : g_stateSince;
				std::scoped_lock l(Womb::g_lock);
				Womb::g_data.daysIn = std::max(0.0f, GameDays() - since);
			},
			pc, std::string("FW.StateEnterTime"), 0.0f);
		CallStatic(
			"StorageUtil", "FormListCount",
			[pc](const RE::BSScript::Variable& v) {
				const int n = v.IsInt() ? v.GetSInt() : 0;
				{
					std::scoped_lock l(Womb::g_lock);
					Womb::g_data.sperm = n;
				}
				const int was = std::exchange(g_lastSperm, n);
				if (n <= 0) {
					return;
				}
				{
					std::scoped_lock l(Womb::g_lock);
					Womb::g_data.donorN = std::min(n, 6);
				}
				for (int i = 0; i < std::min(n, 6); ++i) {
					CallStatic(
						"StorageUtil", "FormListGet",
						[i](const RE::BSScript::Variable& f) {
							const int look = RaceLook(AsForm(f) ? AsForm(f)->As<RE::Actor>() : nullptr);
							std::scoped_lock l(Womb::g_lock);
							Womb::g_data.donors[i] = look;
						},
						pc, std::string("FW.SpermName"), static_cast<std::int32_t>(n - 1 - i));
				}
				CallStatic(
					"StorageUtil", "FormListGet",
					[n, was](const RE::BSScript::Variable& f) {
						const std::string who = FormName(AsForm(f));
						{
							std::scoped_lock l(Womb::g_lock);
							Womb::g_data.donor = who.empty() ? "" : "いちばん新しいのは " + who;
						}
						if (was >= 0 && n > was && g_lastCycle.load() >= 0 && g_lastCycle.load() <= 3) {
							Womb::Push("胎内に注がれた", who.empty() ? "奥へ、奥へと熱が満ちていく" : who + "の熱が、奥へと満ちていく", Womb::Col(255, 200, 225, 1.0f));
						}
					},
					pc, std::string("FW.SpermName"), static_cast<std::int32_t>(n - 1));
			},
			pc, std::string("FW.SpermName"));
		CallStatic(
			"StorageUtil", "FormListGet",
			[](const RE::BSScript::Variable& v) {
				auto        dad = AsForm(v) ? AsForm(v)->As<RE::Actor>() : nullptr;
				const int   look = dad ? RaceLook(dad) : -1;
				std::scoped_lock l(Womb::g_lock);
				Womb::g_data.father = FormName(AsForm(v));
				Womb::g_data.fatherLook = look;
				Womb::g_data.fatherId = dad ? dad->GetFormID() : 0;
				Womb::g_data.fatherRace = dad && dad->GetRace() ? dad->GetRace()->GetFormID() : 0;
			},
			pc, std::string("FW.ChildFather"), static_cast<std::int32_t>(0));
		CallStatic(
			"StorageUtil", "GetIntValue",
			[](const RE::BSScript::Variable& v) {
				std::scoped_lock l(Womb::g_lock);
				Womb::g_data.unborn = v.IsInt() ? v.GetSInt() : 0;
			},
			pc, std::string("FW.NumChilds"), static_cast<std::int32_t>(0));
		CallStatic(
			"OSLAroused_ModInterface", "GetArousal",
			[](const RE::BSScript::Variable& v) {
				std::scoped_lock l(Womb::g_lock);
				Womb::g_data.arousal = v.IsFloat() ? v.GetFloat() : (v.IsInt() ? static_cast<float>(v.GetSInt()) : Womb::g_data.arousal);
			},
			static_cast<RE::Actor*>(RE::PlayerCharacter::GetSingleton()));
	}

	// ------------------------------------------------------------------ 0.11: flushed when aroused, shy when seen naked
	void Face(std::int32_t a_mood, std::int32_t a_strength, std::int32_t a_eyes);
	void CheckMood()
	{
		static float s_next = 0.0f;  // real seconds
		static float s_clock = 0.0f;
		s_clock += 1.0f;
		if (s_clock < s_next || g_inserting || Cutin::g.active || Cutin::g_pending) {
			return;
		}
		auto pc = RE::PlayerCharacter::GetSingleton();
		auto ui = RE::UI::GetSingleton();
		if (!pc || pc->IsInCombat() || (ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)))) {
			return;
		}
		float arousal;
		{
			std::scoped_lock l(Womb::g_lock);
			arousal = Womb::g_data.arousal;
		}
		if (g_settings.shameFace && !pc->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kBody)) {
			static auto npcKw = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSKeyword>(0x013794, "Skyrim.esm");
			bool        seen = false;
			if (auto pl = RE::ProcessLists::GetSingleton()) {
				for (auto& h : pl->highActorHandles) {
					auto a = h.get();
					if (a && a.get() != pc && !a->IsDead() && !a->IsHostileToActor(pc) && (!npcKw || a->HasKeyword(npcKw)) &&
						a->GetPosition().GetDistance(pc->GetPosition()) < 900.0f) {
						seen = true;
						break;
					}
				}
			}
			if (seen) {
				Face(13, 60, 40);  // flustered
				Blush(12.0f);
				Notify("……見られてる");
				Later(6.0f, [] { Face(-1, 0, -1); });
				s_next = s_clock + 60.0f;
				return;
			}
		}
		if (g_settings.moodFace && arousal >= 70.0f) {
			Face(10, static_cast<std::int32_t>(25 + (arousal - 70.0f)), 35);  // flushed, heavy-lidded
			Blush(4.0f);
			Later(8.0f, [] { Face(-1, 0, -1); });
			s_next = s_clock + 45.0f;
		}
	}

	bool IsOrc(RE::Actor* a)
	{
		auto        race = a ? a->GetRace() : nullptr;
		const char* rn = race ? race->GetFullName() : "";
		return race && (race->GetFormID() == 0x013747 || race->GetFormID() == 0x0A82B9 || std::strstr(rn, "Orc") || std::strstr(rn, "オーク"));
	}

	// ------------------------------------------------------------------ 0.14.1: Night Pact (honey trap, a Blood Pact side quest)
	RE::TESQuest* NightQuest() { return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(PoolData::kNightQuest, kPlugin); }
	void NightStage(std::int32_t a_stage)
	{
		static constexpr std::int32_t kObj[] = { 10, 20, 30, 35 };
		const auto has = [](std::int32_t st) { return std::find(std::begin(kObj), std::end(kObj), st) != std::end(kObj); };
		auto q = NightQuest();
		if (q && has(g_night.stage)) {
			CallMethod(q, "Quest", "SetObjectiveCompleted", nullptr, g_night.stage, true);
		}
		g_night.stage = a_stage;
		if (q) {
			if (a_stage == 10 && !q->IsRunning()) {
				CallMethod(q, "Quest", "Start", nullptr);
			}
			CallMethod(q, "Quest", "SetCurrentStageID", nullptr, a_stage);
			if (has(a_stage)) {
				CallMethod(q, "Quest", "SetObjectiveDisplayed", nullptr, a_stage, true, true);
			}
			if (a_stage == 100) {
				CallMethod(q, "Quest", "CompleteQuest", nullptr);
			}
		}
		Log("night: stage {}", a_stage);
	}
	RE::Actor* NightBorak()
	{
		if (g_pact.borak) {
			if (auto b = RE::TESForm::LookupByID<RE::Actor>(g_pact.borak)) {
				return b;
			}
		}
		if (auto r = PlaceOwn(PoolData::kPactBorak, GorekOrKeep(), 200.0f, 0.0f)) {
			g_pact.borak = r->GetFormID();
			return r->As<RE::Actor>();
		}
		return nullptr;
	}
	// the first DNA bottle she carries
	RE::TESBoundObject* NightFindVial()
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		auto counts = pc->GetInventoryCounts();
		for (auto id : PoolData::kDna) {
			auto item = RE::TESForm::LookupByID<RE::TESBoundObject>(Own(id));
			if (auto it = item ? counts.find(item) : counts.end(); it != counts.end() && it->second > 0) {
				return item;
			}
		}
		return nullptr;
	}
	void HiddenVialIn(RE::FormID a_item)
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (auto item = RE::TESForm::LookupByID<RE::TESBoundObject>(a_item)) {
			pc->RemoveItem(item, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			g_night.vial = a_item;
		}
		Notify("小瓶はもう、どこにも見えない");
	}
	// a love scene with him (OStim, else SexLab); then Beeing Female gets his seed
	void NightLoveScene(RE::Actor* a)
	{
		const RE::FormID id = a->GetFormID();
		auto done = [id] {
			auto pc = RE::PlayerCharacter::GetSingleton();
			auto him = RE::TESForm::LookupByID<RE::Actor>(id);
			auto ctrl = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0x182A, "BeeingFemale.esm");
			if (him && ctrl) {
				CallMethod(ctrl, "FWController", "AddSperm", nullptr, static_cast<RE::Actor*>(pc), him, 1.0f);
			}
			g_night.method = 1;
			g_night.t0 = static_cast<float>(GameDays());
			Blush(30.0f);
			Notify("彼の温もりと匂いが、まだ体に残っている。……急いで要塞を出なければ");
			NightStage(30);
		};
		if (RE::TESDataHandler::GetSingleton()->LookupModByName("OStim.esp")) {
			std::vector<RE::Actor*> actors{ a, RE::PlayerCharacter::GetSingleton() };
			CallStatic(
				"OLibrary", "GetRandomSceneWithAction",
				[id, done](const RE::BSScript::Variable& v) {
					const std::string scene = v.IsString() ? std::string(v.GetString()) : std::string();
					auto npc = RE::TESForm::LookupByID<RE::Actor>(id);
					if (!npc) {
						return;
					}
					std::vector<RE::Actor*> act{ npc, RE::PlayerCharacter::GetSingleton() };
					CallStatic(
						"OThread", "QuickStart",
						[done](const RE::BSScript::Variable& t) {
							const int tid = t.IsInt() ? t.GetSInt() : -1;
							if (tid < 0) {
								Notify("OStim のシーンを始められませんでした");
								return;
							}
							WaitWhile([tid](std::function<void(bool)> a_answer) {
								CallStatic("OThread", "IsRunning", [a_answer](const RE::BSScript::Variable& r) { a_answer(r.IsBool() && r.GetBool()); }, static_cast<std::int32_t>(tid));
							},
								done);
						},
						act, scene, static_cast<RE::TESObjectREFR*>(nullptr));
				},
				actors, std::string("vaginalsex"));
			return;
		}
		auto sl = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0xD62, "SexLab.esm");
		if (!sl) {
			Fade(3.0f);
			Later(2.0f, done);
			return;
		}
		RE::Actor* none = nullptr;
		CallMethod(sl, "SexLabFramework", "QuickStart",
			[id, done](const RE::BSScript::Variable&) {
				Later(4.0f, [id, done] {
					WaitWhile([id](std::function<void(bool)> a_answer) {
						auto sl2 = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0xD62, "SexLab.esm");
						CallMethod(sl2, "SexLabFramework", "IsActorActive", [a_answer](const RE::BSScript::Variable& r) { a_answer(r.IsBool() && r.GetBool()); },
							RE::TESForm::LookupByID<RE::Actor>(id));
					},
						done);
				});
			},
			static_cast<RE::Actor*>(RE::PlayerCharacter::GetSingleton()), a, none, none, none, none, std::string("BFHNight"), std::string("Vaginal"));
	}
	void NightChooseHide()
	{
		auto msg = RE::TESForm::LookupByID(Own(PoolData::kNightHideMsg));
		if (!msg) {
			return;
		}
		CallMethod(msg, "Message", "Show",
			[](const RE::BSScript::Variable& v) {
				const int b = v.IsInt() ? v.GetSInt() : 0;
				g_night.place = static_cast<std::uint8_t>(b + 1);
				auto vial = NightFindVial();
				if (!vial) {
					return;
				}
				if (g_night.place == 4) {
					g_hideOnly = true;
					StartInsert(vial->GetFormID());
				} else if (g_night.place == 5) {
					RE::PlayerCharacter::GetSingleton()->RemoveItem(vial, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					g_night.vial = vial->GetFormID();
					Notify("小瓶を布に包み、門の外の茂みの根元に埋めた。あとで掘り出せばいい");
				} else {
					static const char* where[] = { "", "ブーツの中敷きの下に滑り込ませた", "コルセットの縫い目に押し込んだ", "結い上げた髪の奥に挿した" };
					Notify(std::string("小瓶を") + where[g_night.place]);
				}
			},
			0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	}
	void NightCaught(RE::Actor* a_guard)
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		std::string who = a_guard ? a_guard->GetDisplayFullName() : "見張り";
		if (g_night.method == 2) {
			if (auto vial = NightFindVial()) {
				pc->RemoveItem(vial, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
			Notify(who + "「待て。……なんだ、この小瓶は。ボラクの匂いがするぞ」");
		} else {
			Notify(who + "「待て。……その匂い、ボラクの寝所から来たな。娼婦の真似事か」");
		}
		Fade(3.0f);
		Later(1.5f, [] {
			if (auto keep = GorekOrKeep()) {
				CallMethod(RE::PlayerCharacter::GetSingleton(), "ObjectReference", "MoveTo", nullptr, keep, 2600.0f, 0.0f, 0.0f, true);
			}
			Notify("要塞から叩き出された。ボラクも族長に叱責されたらしい。……数日は近づけない");
		});
		g_night.vial = 0;
		g_night.method = 0;
		g_night.place = 0;
		g_night.retryAt = static_cast<float>(GameDays()) + 2.0f;
		NightStage(35);
	}
	void NightEscaped()
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		// hidden inside her, the seal may have given way on the run: then it is in her, not in the bottle
		static std::mt19937 s_rng{ std::random_device{}() };
		if (g_night.vial && g_night.place == 4 && std::uniform_real_distribution<float>(0, 1)(s_rng) < 0.35f) {
			auto d = DnaByItem(g_night.vial);
			auto owner = d ? RE::TESForm::LookupByID<RE::Actor>(d->owner) : nullptr;
			auto ctrl = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESQuest>(0x182A, "BeeingFemale.esm");
			if (owner && ctrl) {
				const float amount = d->tier >= 100 ? 1.0f : (d->tier >= 50 ? 0.6f : 0.35f);
				CallMethod(ctrl, "FWController", "AddSperm", nullptr, static_cast<RE::Actor*>(pc), owner, amount);
			}
			g_night.vial = 0;
			Blush(30.0f);
			Notify("……走っている間に、小瓶の封が緩んでいた。中身はもう瓶の中にはない");
			LeakStart(1, g_settings.leakSeconds);
		}
		if (g_night.vial) {
			if (auto item = RE::TESForm::LookupByID<RE::TESBoundObject>(g_night.vial)) {
				pc->AddObjectToContainer(item, nullptr, 1, nullptr);
			}
			Notify(g_night.place == 5 ? "茂みの根元から小瓶を掘り出した" : "隠していた小瓶を取り出した");
			g_night.vial = 0;
		}
		if (auto gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0x00000F)) {
			pc->AddObjectToContainer(gold, nullptr, 500, nullptr);
		}
		Blush(15.0f);
		NightStage(100);
	}
	void CheckNight()
	{
		if (!HasLordbound()) {
			return;
		}
		auto        pc = RE::PlayerCharacter::GetSingleton();
		const float now = static_cast<float>(GameDays());
		// the offer: Imperial / Stormcloak envoy, while the chief's deal stands and no child is on the way
		if (g_night.stage == 0 && !g_night.offered && (g_pact.route == 1 || g_pact.route == 2) && g_pact.stage >= 30 && g_pact.stage < 45 && !g_pact.escaped) {
			if (g_night.offerAt <= 0.0f) {
				g_night.offerAt = now + 0.5f;
			} else if (now >= g_night.offerAt && !pc->IsInCombat()) {
				g_night.offered = 1;
				if (auto msg = RE::TESForm::LookupByID(Own(PoolData::kNightStartMsg))) {
					CallMethod(msg, "Message", "Show",
						[](const RE::BSScript::Variable& v) {
							if ((v.IsInt() ? v.GetSInt() : 1) == 0) {
								NightBorak();
								NightStage(10);
							}
						},
						0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
				}
			}
		}
		if (g_night.stage == 35 && now >= g_night.retryAt) {
			NightBorak();
			NightStage(10);
		}
		// a bottle: wait until she holds it, then hide it
		if (g_night.stage == 25 && NightFindVial()) {
			g_night.method = 2;
			g_night.t0 = now;
			NightStage(30);
			Later(1.0f, [] { NightChooseHide(); });
		}
		if (g_night.stage != 30) {
			return;
		}
		// out of the stronghold: done
		auto keep = GorekOrKeep();
		if (keep && pc->GetParentCell() && !pc->GetParentCell()->IsInteriorCell() && pc->GetPosition().GetDistance(keep->GetPosition()) > 4200.0f) {
			NightEscaped();
			return;
		}
		// the watch: an orc who sees her may stop her (scent grows for two hours; sneaking halves the reach)
		if (now - g_night.lastCheck < 6.0f / 86400.0f * 10.0f) {  // about every 6 real seconds
			return;
		}
		g_night.lastCheck = now;
		const float scent = std::clamp((now - g_night.t0) * 12.0f, 0.0f, 1.0f);
		float       reach = g_night.method == 1 ? 350.0f + 900.0f * scent : 550.0f;
		if (pc->IsSneaking()) {
			reach *= 0.5f;
		}
		static constexpr float kFind[] = { 0.0f, 0.35f, 0.25f, 0.3f, 0.0f, 0.0f };  // by hiding place
		const float find = g_night.method == 1 ? 0.25f + 0.5f * scent : kFind[std::min<int>(g_night.place, 5)];
		auto pl = RE::ProcessLists::GetSingleton();
		static std::mt19937 rng{ std::random_device{}() };
		for (auto& h : pl->highActorHandles) {
			auto ap = h.get();
			auto a = ap.get();
			if (!a || a->IsDead() || a->GetFormID() == g_pact.borak || !IsOrc(a) || a->IsPlayerTeammate()) {
				continue;
			}
			if (a->GetPosition().GetDistance(pc->GetPosition()) > reach) {
				continue;
			}
			bool los = false;
			if (!a->HasLineOfSight(pc, los) || !los) {
				continue;
			}
			if (std::uniform_real_distribution<float>(0, 1)(rng) < find) {
				NightCaught(a);
			} else {
				Notify(std::string(a->GetDisplayFullName()) + "がこちらをじろりと見た。……何も言わずに行かせてくれた");
			}
			return;
		}
	}

	// the camera sinks toward her lower belly, then the film takes over: as if looking inside
	void WombCinematic(std::wstring a_name, std::wstring a_fallback)
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		auto play = [a_name, a_fallback] {
			if (!Video::Play(a_name) && a_name != a_fallback) {
				Video::Play(a_fallback);
			}
		};
		if (!g_settings.camera || !pc || pc->IsInCombat()) {
			play();
			return;
		}
		Camera(pc, 3.2f, false, 0.0f, 1u << (49 - 30));  // waist: settles on the lower belly
		Later(0.9f, [] { BellyPulse(0.06f); });
		Later(1.7f, play);
	}


	// ------------------------------------------------------------------ 0.13: the child inside her (a 3D model on the belly bone)
	struct FetusSlot
	{
		RE::NiPointer<RE::NiNode> node;
		RE::NiNode*               parent = nullptr;
		std::string               path;
	};
	FetusSlot g_fetus;

	void FetusDetach()
	{
		if (g_fetus.node && g_fetus.node->parent) {
			RE::NiPointer<RE::NiAVObject> out;
			g_fetus.node->parent->DetachChild(g_fetus.node.get(), out);
		}
		g_fetus = FetusSlot{};
	}

	RE::NiNode* BellyNode(RE::PlayerCharacter* pc)
	{
		auto root = pc ? pc->Get3D(false) : nullptr;
		if (!root) {
			return nullptr;
		}
		for (const char* n : { "NPC Belly", "NPC Pelvis [Pelv]", "NPC Spine [Spn0]" }) {
			if (auto o = root->GetObjectByName(n); o && o->AsNode()) {
				return o->AsNode();
			}
		}
		return nullptr;
	}

	// which fetus model: the father's race, mixed with the mother's, plus a per-father stable pick
	bool IsRace(std::uint32_t id, std::initializer_list<std::uint32_t> ids) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }
	std::string FetusVariant(int look, std::uint32_t dadId, std::uint32_t dadRace, RE::PlayerCharacter* pc)
	{
		const std::uint32_t momRace = pc && pc->GetRace() ? pc->GetRace()->GetFormID() : 0;
		const std::uint32_t h = (dadId * 2654435761u) ^ 0x9E3779B9u;
		const bool dadRedguard = IsRace(dadRace, { 0x013748, 0x088846 });
		const bool dadNord = IsRace(dadRace, { 0x013746, 0x088794 });
		const bool momRedguard = IsRace(momRace, { 0x013748, 0x088846 });
		const bool momNord = IsRace(momRace, { 0x013746, 0x088794 });
		switch (look) {
		case Womb::kOrc:
			return (h >> 7) & 1 ? "orc2" : "orc";
		case Womb::kElf:
			return "elf";
		case Womb::kKhajiit:
			return "khajiit";
		case Womb::kArgonian:
			return "argonian";
		default:
			break;
		}
		if ((dadRedguard && momNord) || (dadNord && momRedguard)) {
			return "rgnord";  // Redguard x Nord: dark skin, golden curls
		}
		if (dadRedguard || momRedguard) {
			return "human2";  // Redguard half
		}
		return "human";
	}

	// main thread: which model should be in her now, attach / swap / remove it
	void UpdateFetus()
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		int  st, look;
		float daysIn;
		std::uint32_t dadId, dadRace;
		{
			std::scoped_lock l(Womb::g_lock);
			st = Womb::g_data.state;
			look = Womb::g_data.fatherLook;
			daysIn = Womb::g_data.daysIn;
			dadId = Womb::g_data.fatherId;
			dadRace = Womb::g_data.fatherRace;
		}
		std::string want;
		if (g_settings.fetusModel && st >= 5 && st <= 7) {
			const int step = st == 5 ? (daysIn < Womb::Length(5) * 0.5f ? 1 : 2) : 3;
			const std::string kind = FetusVariant(look, dadId, dadRace, pc);
			want = std::string("BFHeredity\\Fetus\\fetus_") + kind + "_" + std::to_string(step) + ".nif";
		}
		auto belly = BellyNode(pc);
		if (want.empty() || !belly) {
			if (g_fetus.node) {
				FetusDetach();
			}
			return;
		}
		if (want == g_fetus.path && g_fetus.node && g_fetus.node->parent == belly) {
			return;
		}
		FetusDetach();
		RE::NiPointer<RE::NiNode>        model;
		RE::BSModelDB::DBTraits::ArgsType args{};
		if (RE::BSModelDB::Demand(want.c_str(), model, args) != RE::BSResource::ErrorCode::kNone || !model) {
			Log("fetus: cannot load {}", want);
			g_fetus.path = want;  // do not retry every second
			return;
		}
		belly->AttachChild(model.get(), true);
		g_fetus.node = model;
		g_fetus.parent = belly;
		g_fetus.path = want;
		Log("fetus: {} on {}", want, belly->name.c_str());
	}

	// often: keep it upright in her own frame, at the offset from the belly bone
	void FetusPose()
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		auto n = g_fetus.node.get();
		auto root = pc ? pc->Get3D(false) : nullptr;
		if (!n || !n->parent || !root) {
			return;
		}
		const auto& pw = n->parent->world;
		const auto& rw = root->world;
		const RE::NiPoint3 off{ g_settings.fetusX, g_settings.fetusY, g_settings.fetusZ };
		const RE::NiPoint3 worldOff = rw.rotate * off;
		const auto         inv = pw.rotate.Transpose();
		const float        ps = pw.scale > 0.0f ? pw.scale : 1.0f;
		n->local.translate = (inv * worldOff) / ps;
		const float   px = g_settings.fetusPitch * 0.0174533f, yz = g_settings.fetusYaw * 0.0174533f;
		RE::NiMatrix3 rx, rz;
		rx.entry[0][0] = 1.0f; rx.entry[0][1] = 0.0f; rx.entry[0][2] = 0.0f;
		rx.entry[1][0] = 0.0f; rx.entry[1][1] = std::cos(px); rx.entry[1][2] = -std::sin(px);
		rx.entry[2][0] = 0.0f; rx.entry[2][1] = std::sin(px); rx.entry[2][2] = std::cos(px);
		rz.entry[0][0] = std::cos(yz); rz.entry[0][1] = -std::sin(yz); rz.entry[0][2] = 0.0f;
		rz.entry[1][0] = std::sin(yz); rz.entry[1][1] = std::cos(yz); rz.entry[1][2] = 0.0f;
		rz.entry[2][0] = 0.0f; rz.entry[2][1] = 0.0f; rz.entry[2][2] = 1.0f;
		const RE::NiMatrix3 turn = rz * rx;
		n->local.rotate = inv * rw.rotate * turn;
		n->local.scale = std::clamp(g_settings.fetusScale, 0.05f, 5.0f) / ps;
		RE::NiUpdateData ud;
		n->Update(ud);
	}

	void CheckOvulation()
	{
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) {
			return;
		}
		CallStatic(
			"StorageUtil", "GetIntValue",
			[](const RE::BSScript::Variable& a_v) {
				const int st = a_v.IsInt() ? a_v.GetSInt() : -1;
				if (st < 0) {
					return;
				}
				const int last = g_lastCycle.exchange(st);
				ApplySoftAbs(st);  // cheap when nothing changed
				WombState(last, st);
				if (last >= 0 && last <= 3 && st >= 4 && st <= 7) {
					Log("cycle: conceived");
					LoadSettings(false);
					if (g_settings.conceptionVideo) {
						// an orc father gets his own film (green, fierce); others the usual one
						CallStatic(
							"StorageUtil", "FormListGet",
							[](const RE::BSScript::Variable& v) {
								auto dad = AsForm(v) ? AsForm(v)->As<RE::Actor>() : nullptr;
								WombCinematic(IsOrc(dad) ? L"conception_orc" : L"conception", L"conception");
							},
							static_cast<RE::TESForm*>(RE::PlayerCharacter::GetSingleton()), std::string("FW.ChildFather"), static_cast<std::int32_t>(0));
					}
				}
				if (last != -1 && last != 1 && st == 1) {
					Log("cycle: ovulation started");
					LoadSettings(false);
					if (g_settings.ovulationVideo) {
						WombCinematic(L"ovulation", L"ovulation");
					}
				}
			},
			static_cast<RE::TESForm*>(pc), std::string("FW.CurrentState"), static_cast<std::int32_t>(-1));
	}

	void Poll()
	{
		if (g_ready) {
			CheckOvulation();
			CheckGentleDrug();
			CheckEggs();
			CheckBal();
			CheckPact();
			CheckNight();
			UpdateWomb();
			CheckMood();
			UpdateFetus();
		}
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
	constexpr std::uint32_t kSerCycle = 'OVUL';
	constexpr std::uint32_t kSerEggs = 'EGGS';
	constexpr std::uint32_t kSerBal = 'BALM';

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
		if (a_ser->OpenRecord(kSerCycle, 1)) {
			a_ser->WriteRecordData(g_ovulationDue);
		}
		if (a_ser->OpenRecord(kSerEggs, 1)) {
			a_ser->WriteRecordData(g_eggStart);
			a_ser->WriteRecordData(g_eggDue);
		}
		if (a_ser->OpenRecord(kSerBal, 1)) {
			a_ser->WriteRecordData(g_bal);
		}
		if (a_ser->OpenRecord('BALX', 1)) {
			a_ser->WriteRecordData(g_balx);
		}
		if (a_ser->OpenRecord('PACT', 2)) {
			a_ser->WriteRecordData(g_pact);
		}
		if (a_ser->OpenRecord('NGHT', 1)) {
			a_ser->WriteRecordData(g_night);
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
		g_ovulationDue = 0.0;
		g_eggStart = g_eggDue = 0.0;
		g_eggMorph = -1.0f;
		g_bal = BalState{};
		g_balx = BalExtra{};
		g_pact = PactState{};
		g_night = NightState{};
		g_balShown = -1;
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
			if (type == kSerCycle) {
				a_ser->ReadRecordData(g_ovulationDue);
				continue;
			}
			if (type == kSerBal) {
				a_ser->ReadRecordData(g_bal);
				continue;
			}
			if (type == 'NGHT') {
				g_night = NightState{};
				a_ser->ReadRecordData(&g_night, std::min<std::uint32_t>(length, sizeof(NightState)));
				continue;
			}
			if (type == 'PACT') {
				g_pact = PactState{};
				a_ser->ReadRecordData(&g_pact, std::min<std::uint32_t>(length, version >= 2 ? sizeof(PactState) : kPactV1Size));
				for (RE::FormID* id : { &g_pact.captives[0], &g_pact.captives[1], &g_pact.captives[2], &g_pact.borak, &g_pact.rival, &g_pact.spy, &g_pact.boss }) {
					RE::FormID n = 0;
					*id = (*id && a_ser->ResolveFormID(*id, n)) ? n : 0;
				}
				continue;
			}
			if (type == 'BALX') {
				a_ser->ReadRecordData(g_balx);
				for (RE::FormID* id : { &g_balx.lastSpawn, &g_balx.champRef }) {
					RE::FormID n = 0;
					*id = (*id && a_ser->ResolveFormID(*id, n)) ? n : 0;
				}
				continue;
			}
			if (type == kSerEggs) {
				a_ser->ReadRecordData(g_eggStart);
				a_ser->ReadRecordData(g_eggDue);
				continue;
			}
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
				KKMenu::RegisterHudElement(Video::Draw);
				KKMenu::RegisterHudElement(Womb::Hud);
			}
			std::thread([] {
				for (;;) {
					std::this_thread::sleep_for(50ms);
					SKSE::GetTaskInterface()->AddTask([] { Cutin::Poll(); FetusPose(); });
				}
			}).detach();
			BuildSlots();
			BuildDna();
			if (auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
				holder->AddEventSink<RE::TESEquipEvent>(&g_equip);
				holder->AddEventSink<RE::TESTopicInfoEvent>(&g_topic);
				holder->AddEventSink<RE::TESContainerChangedEvent>(&g_eggSources);
				holder->AddEventSink<RE::TESDeathEvent>(&g_eggSources);
				holder->AddEventSink<RE::TESSleepStopEvent>(&g_balSleep);
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
			g_lastCycle = -1;  // the state of the loaded game is learnt first, not taken for a change
			{
				std::scoped_lock l(g_absLock);
				g_absSet.clear();  // morphs live in the save; learn them again
			}
			Video::Stop();
			SKSE::GetTaskInterface()->AddTask([] { Cutin::Stop("game load"); });
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			SKSE::GetTaskInterface()->AddTask([] {
				if (auto t = RE::BSTimer::GetSingleton()) { t->SetGlobalTimeMultiplier(1.0f, true); }
				LeakStop();  // the faction lives in the save; never come back stuck in the leaking walk
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

#pragma once
// PAD Cutin - armor break cut-in for Procedural Armor Damage.
// Camera work adapted from Kagura Style Armor Break 1.2.0 (Cutin.h), Copyright (C) 2026 miyata0105, GPL-3.0-or-later.
// Changes: driven by Procedural Armor Damage's ArmorTear_OnBroken event; player only; the camera frames the broken
// armor's body region (upper body, waist, hands, arms, legs, feet, head); KSAB's body physics, blush, expression,
// cloth burst and caption parts removed.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Included inside main.cpp's anonymous namespace (uses g_settings / Log).
// The camera runs in the free-camera state: third-person camera mods (SmoothCam) leave that state alone, so the
// transform reported from the hooks below is what renders.

namespace Cutin
{
	using SClock = std::chrono::steady_clock;

	// ---------------------------------------------------------------- math
	inline RE::NiPoint3 Norm(RE::NiPoint3 v)
	{
		const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		return l > 1e-5f ? RE::NiPoint3{ v.x / l, v.y / l, v.z / l } : RE::NiPoint3{ 0, 1, 0 };
	}
	inline RE::NiPoint3 Cross(const RE::NiPoint3& a, const RE::NiPoint3& b)
	{
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}
	// camera frame: column 0 = right, 1 = forward, 2 = up (the camera looks along local +Y)
	inline RE::NiMatrix3 Frame(RE::NiPoint3 fwd, RE::NiPoint3 up)
	{
		fwd = Norm(fwd);
		const auto right = Norm(Cross(fwd, up));
		up = Norm(Cross(right, fwd));
		RE::NiMatrix3 m;
		const RE::NiPoint3 c[3] = { right, fwd, up };
		for (int j = 0; j < 3; ++j) {
			m.entry[0][j] = c[j].x;
			m.entry[1][j] = c[j].y;
			m.entry[2][j] = c[j].z;
		}
		return m;
	}
	inline RE::NiQuaternion Slerp(const RE::NiQuaternion& a, RE::NiQuaternion b, float t)
	{
		float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
		if (d < 0) {
			b = { -b.w, -b.x, -b.y, -b.z };
			d = -d;
		}
		RE::NiQuaternion r;
		if (d > 0.9995f) {
			r = { a.w + t * (b.w - a.w), a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z) };
		} else {
			const float th = std::acos(d), s = std::sin(th);
			const float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
			r = { wa * a.w + wb * b.w, wa * a.x + wb * b.x, wa * a.y + wb * b.y, wa * a.z + wb * b.z };
		}
		const float l = std::sqrt(r.w * r.w + r.x * r.x + r.y * r.y + r.z * r.z);
		return { r.w / l, r.x / l, r.y / l, r.z / l };
	}
	// Move the camera from a to b around c (angle and distance interpolated), so a move to the other side of the body
	// goes round it instead of straight through it.
	inline RE::NiPoint3 Orbit(const RE::NiPoint3& c, const RE::NiPoint3& a, const RE::NiPoint3& b, float k)
	{
		const float ax = a.x - c.x, ay = a.y - c.y, bx = b.x - c.x, by = b.y - c.y;
		const float ra = std::sqrt(ax * ax + ay * ay), rb = std::sqrt(bx * bx + by * by);
		if (ra < 1.0f || rb < 1.0f) {
			return a + (b - a) * k;
		}
		const float ya = std::atan2(ay, ax);
		float       dy = std::atan2(by, bx) - ya;
		while (dy > 3.14159265f) {
			dy -= 6.2831853f;
		}
		while (dy < -3.14159265f) {
			dy += 6.2831853f;
		}
		const float y = ya + dy * k, r = ra + (rb - ra) * k;
		return { c.x + r * std::cos(y), c.y + r * std::sin(y), a.z + (b.z - a.z) * k };
	}

	inline float Ease(float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	// rotation by angle a about unit axis k (Rodrigues)
	inline RE::NiMatrix3 AxisAngle(const RE::NiPoint3& k, float a)
	{
		const float   c = std::cos(a), s = std::sin(a), v = 1 - c;
		RE::NiMatrix3 m;
		m.entry[0][0] = c + k.x * k.x * v;
		m.entry[0][1] = k.x * k.y * v - k.z * s;
		m.entry[0][2] = k.x * k.z * v + k.y * s;
		m.entry[1][0] = k.y * k.x * v + k.z * s;
		m.entry[1][1] = c + k.y * k.y * v;
		m.entry[1][2] = k.y * k.z * v - k.x * s;
		m.entry[2][0] = k.z * k.x * v - k.y * s;
		m.entry[2][1] = k.z * k.y * v + k.x * s;
		m.entry[2][2] = c + k.z * k.z * v;
		return m;
	}

	inline bool SameM(const RE::NiMatrix3& a, const RE::NiMatrix3& b)
	{
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 3; ++c) {
				if (std::abs(a.entry[r][c] - b.entry[r][c]) > 1e-4f) {
					return false;
				}
			}
		}
		return true;
	}

	// ---------------------------------------------------------------- body regions
	enum class Part
	{
		Body,   // cuirass / clothes (slot 32)
		Waist,  // waist, leg garments
		Hands,
		Arms,
		Feet,
		Legs,
		Head  // helmet
	};

	inline Part PartFromMask(std::uint32_t m)
	{
		auto has = [m](int slot) { return (m & (1u << (slot - 30))) != 0; };
		if (has(32) || has(46) || has(56)) {
			return Part::Body;
		}
		if (has(49) || has(52) || has(53) || has(54)) {
			return Part::Waist;
		}
		if (has(33)) {
			return Part::Hands;
		}
		if (has(34)) {
			return Part::Arms;
		}
		if (has(37)) {
			return Part::Feet;
		}
		if (has(30) || has(31) || has(41) || has(42) || has(43)) {
			return Part::Head;
		}
		if (has(38)) {
			return Part::Legs;
		}
		return Part::Body;
	}

	inline const char* PartName(Part p)
	{
		switch (p) {
		case Part::Body:
			return "body";
		case Part::Waist:
			return "waist";
		case Part::Hands:
			return "hands";
		case Part::Arms:
			return "arms";
		case Part::Feet:
			return "feet";
		case Part::Head:
			return "head";
		default:
			return "legs";
		}
	}

	inline RE::NiAVObject* Node(RE::Actor* a, std::string_view n)
	{
		auto root = a ? a->Get3D(false) : nullptr;
		return root ? root->GetObjectByName(n) : nullptr;
	}

	inline bool PairMid(RE::Actor* a, std::string_view l, std::string_view r, RE::NiPoint3& out)
	{
		auto nl = Node(a, l), nr = Node(a, r);
		if (!nl || !nr) {
			return false;
		}
		out = (nl->world.translate + nr->world.translate) * 0.5f;
		return true;
	}

	// the steady bone each region is framed on (the camera follows it, so the shot does not wobble with the limbs)
	inline std::string_view SteadyBone(Part p)
	{
		switch (p) {
		case Part::Body:
			return "NPC Spine2 [Spn2]";
		case Part::Waist:
			return "NPC Pelvis [Pelv]";
		default:
			return "";
		}
	}

	inline bool Anchor(RE::Actor* a, Part p, RE::NiPoint3& out)
	{
		switch (p) {
		case Part::Body:
			if (auto n = Node(a, "NPC Spine2 [Spn2]")) {
				out = n->world.translate;
				return true;
			}
			return false;
		case Part::Waist:
			if (auto n = Node(a, "NPC Pelvis [Pelv]")) {
				out = n->world.translate;
				return true;
			}
			return false;
		case Part::Hands:
			return PairMid(a, "NPC L Hand [LHnd]", "NPC R Hand [RHnd]", out);
		case Part::Arms:
			return PairMid(a, "NPC L Forearm [LLar]", "NPC R Forearm [RLar]", out);
		case Part::Feet:
			return PairMid(a, "NPC L Foot [Lft ]", "NPC R Foot [Rft ]", out);
		case Part::Head:
			if (auto n = Node(a, "NPC Head [Head]")) {
				out = n->world.translate + RE::NiPoint3{ 0, 0, 6 };
				return true;
			}
			return false;
		default:
			return PairMid(a, "NPC L Calf [LClf]", "NPC R Calf [RClf]", out);
		}
	}

	// Skinned meshes are authored in the bind pose facing model +Y. The skin-to-bone rotation of the head bone therefore
	// says which head-bone axis points out of the face, whatever pose the head is in now.
	inline bool HeadLocalForward(RE::Actor* a, RE::NiPoint3& o_dir)
	{
		auto root = a ? a->Get3D(false) : nullptr;
		bool found = false;
		if (!root) {
			return false;
		}
		RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geo) {
			auto skin = a_geo->GetGeometryRuntimeData().skinInstance.get();
			auto data = skin ? skin->skinData.get() : nullptr;
			if (!data || !skin->bones) {
				return RE::BSVisit::BSVisitControl::kContinue;
			}
			for (std::uint32_t i = 0; i < data->GetBoneCount(); ++i) {
				auto b = skin->bones[i];
				if (b && b->name == "NPC Head [Head]") {
					const auto& R = data->GetBoneDataSkinToBone(i).rotate;
					o_dir = Norm(RE::NiPoint3{ R.entry[0][1], R.entry[1][1], R.entry[2][1] });
					found = true;
					return RE::BSVisit::BSVisitControl::kStop;
				}
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		return found;
	}

	// ---------------------------------------------------------------- state (main thread only)
	struct State
	{
		bool               active = false;
		RE::ActorHandle    target;
		Part               part = Part::Body;
		SClock::time_point start, lastFrame;
		RE::NiPoint3       fromPos{};
		RE::NiQuaternion   fromRot{ 1, 0, 0, 0 };
		RE::NiPoint3       pos{};
		RE::NiQuaternion   rot{ 1, 0, 0, 0 };
		bool               have = false;
		bool               enteredFree = false;
		bool               wasFirstPerson = false;
		float              savedMult = 1.0f;
		float              curMult = 1.0f;
		RE::NiPoint3       faceLocal{};  // out-of-the-face direction in head-bone space (from the bind pose)
		bool               haveFaceLocal = false;
		RE::NiPointer<RE::NiAVObject> aimNode;
		RE::NiPoint3       aimLocal{};
		float              side = 0.22f, rise = 4.0f, faceSide = -0.12f;  // this cut-in's camera angle (randomised)
		float              faceRise = 1.0f, faceDistMul = 1.0f, faceAimDrop = 12.0f;
		float              hitStop = 0.0f;
		RE::NiPoint3       holdAt{};
		bool               hold = false;
		int                held = 0;
		float              heldMax = 0;
		bool               blurOn = false;
		bool               faceFxOn = false;
		float              roll = 0.0f;  // dutch angle on the face shot (radians)
		RE::NiPointer<RE::NiAVObject> headNode;
		RE::NiMatrix3      headBase{}, headWritten{};
		bool               headValid = false;
		bool               orbit = false;  // BF Heredity insertion shot: behind -> round to the front (whole body) -> face
		float              orbitSign = 1.0f;
		float              orbitFrom = 0.0f;  // where the circling starts: 0 = behind, 0.5 = from the side
	};

	inline State                    g;
	inline bool                     g_hidHud = false;
	inline std::atomic<float>       g_letterboxTarget{ 0.0f };
	inline std::atomic<float>       g_faceFxTarget{ 0.0f };
	inline std::atomic<long long>   g_faceStartNs{ 0 };
	inline std::atomic<RE::Actor*>  g_animActor{ nullptr };

	struct Pending
	{
		RE::ActorHandle    actor;
		std::uint32_t      mask = 0;
		bool               orbit = false;
		float              orbitFrom = 0.0f;
		SClock::time_point since;
	};
	inline std::optional<Pending> g_pending;
	inline SClock::time_point     g_lastEnd{};

	inline float Sec(SClock::time_point a_since) { return std::chrono::duration<float>(SClock::now() - a_since).count(); }

	inline bool InFreeCam(RE::PlayerCamera* c)
	{
		return c && c->currentState && c->currentState->id == RE::CameraState::kFree;
	}

	inline float FaceTime()
	{
		const long long ns = g_faceStartNs.load();
		if (ns == 0) {
			return -1.0f;
		}
		return static_cast<float>(static_cast<double>(SClock::now().time_since_epoch().count() - ns) * 1e-9);
	}

	// timeline (real seconds, scaled by fCutinLength)
	struct Times
	{
		float in, partHold, toFace, faceHold, out;
	};
	inline Times T()
	{
		const float L = std::clamp(g_settings.cutinLength, 0.3f, 4.0f);
		return Times{ 0.35f * L, 1.50f * L, 1.90f * L, 2.90f * L, 3.25f * L };
	}

	// ---------------------------------------------------------------- screen effects (vanilla image space modifiers)
	inline RE::TESImageSpaceModifier* Imod(RE::FormID a_id)
	{
		return RE::TESForm::LookupByID<RE::TESImageSpaceModifier>(a_id);
	}
	constexpr RE::FormID kFlashImod = 0x0007E8FD;  // alduinExplosionFlashImod: white flash
	constexpr RE::FormID kBlurImod = 0x00101DAC;   // CGDragonBlurImod: radial blur

	inline void StopBlur()
	{
		if (g.blurOn) {
			if (auto im = Imod(kBlurImod)) {
				RE::ImageSpaceModifierInstanceForm::Stop(im);
			}
			g.blurOn = false;
		}
	}

	inline std::uint32_t Rgba(int r, int gg, int b, float a)
	{
		return (static_cast<std::uint32_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f) << 24) | (static_cast<std::uint32_t>(b) << 16) |
		       (static_cast<std::uint32_t>(gg) << 8) | static_cast<std::uint32_t>(r);
	}

	// Face shot overlays: darkened edges and (optional) flickering speed lines converging on the face.
	inline void DrawFaceFx(void* dl, KKMenu::ImVec2 sz, float dt)
	{
		static float s_k = 0.0f;
		const float  target = g_faceFxTarget.load();
		s_k += std::clamp(target - s_k, -dt * 3.0f, dt * 6.0f);
		if (s_k <= 0.001f) {
			return;
		}
		const float W = sz.x, H = sz.y;
		if (g_settings.cutinVignette) {
			const float a = 0.55f * s_k;
			const auto  dark = Rgba(0, 0, 0, a), clear = Rgba(0, 0, 0, 0.0f);
			const float bw = W * 0.28f, bh = H * 0.32f;
			KKMenu::AddRectGradient(dl, { 0, 0 }, { bw, H }, dark, clear, clear, dark);
			KKMenu::AddRectGradient(dl, { W - bw, 0 }, { W, H }, clear, dark, dark, clear);
			KKMenu::AddRectGradient(dl, { 0, 0 }, { W, bh }, dark, dark, clear, clear);
			KKMenu::AddRectGradient(dl, { 0, H - bh }, { W, H }, clear, clear, dark, dark);
		}
		if (g_settings.cutinSpeedLines) {
			static std::uint32_t s_seed = 1;
			static float         s_acc = 0.0f;
			s_acc += dt;
			if (s_acc > 0.07f) {
				s_acc = 0.0f;
				s_seed = s_seed * 1664525u + 1013904223u;
			}
			std::uint32_t r = s_seed;
			auto          rnd = [&r]() { r = r * 1664525u + 1013904223u; return static_cast<float>((r >> 8) & 0xFFFF) / 65535.0f; };
			const KKMenu::ImVec2 c{ W * 0.5f, H * 0.45f };
			const float          R = std::sqrt(W * W + H * H) * 0.6f;
			for (int i = 0; i < 64; ++i) {
				const float ang = (i + rnd() * 0.8f) / 64.0f * 6.2831853f;
				const float dx = std::cos(ang), dy = std::sin(ang);
				const float inner = H * (0.30f + 0.12f * rnd());
				const float w = H * (0.004f + 0.010f * rnd());
				const KKMenu::ImVec2 o1{ c.x + dx * R - dy * w, c.y + dy * R + dx * w };
				const KKMenu::ImVec2 o2{ c.x + dx * R + dy * w, c.y + dy * R - dx * w };
				const KKMenu::ImVec2 in{ c.x + dx * inner, c.y + dy * inner };
				KKMenu::AddTriangleFilled(dl, o1, o2, in, Rgba(255, 255, 255, (0.35f + 0.3f * rnd()) * s_k));
			}
		}
	}

	// HUD callback (render thread): cinematic bars sliding in and out, and the face shot overlays
	inline void __stdcall DrawLetterbox()
	{
		static float s_k = 0.0f;
		static auto  s_last = SClock::now();
		const auto   now = SClock::now();
		const float  dt = std::clamp(std::chrono::duration<float>(now - s_last).count(), 0.0f, 0.1f);
		s_last = now;
		const float target = g_letterboxTarget.load();
		s_k += std::clamp(target - s_k, -dt * 4.0f, dt * 4.0f);
		const auto size = KKMenu::MainViewportSize();
		auto       dl = KKMenu::ForegroundDrawList();
		if (!dl || size.x <= 0.0f) {
			return;
		}
		DrawFaceFx(dl, size, dt);
		if (s_k <= 0.001f) {
			return;
		}
		const float h = size.y * 0.11f * s_k;
		KKMenu::AddRectFilled(dl, { 0, 0 }, { size.x, h }, 0xFF000000u);
		KKMenu::AddRectFilled(dl, { 0, size.y - h }, { size.x, size.y }, 0xFF000000u);
	}

	// ---------------------------------------------------------------- start / stop (main thread)
	inline const char* PhysicsBusy(RE::Actor* a)
	{
		if (a->IsInRagdollState()) {
			return "ragdoll";
		}
		if (auto st = a->AsActorState(); st && st->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal) {
			return "knocked down";
		}
		if (a->IsInKillMove()) {
			return "killmove";
		}
		if (a->IsOnMount()) {
			return "mounted";
		}
		if (a->IsInMidair()) {
			return "in the air";
		}
		return nullptr;
	}

	inline void StillController(RE::Actor* a)
	{
		if (auto cc = a ? a->GetCharController() : nullptr) {
			cc->SetLinearVelocityImpl(RE::hkVector4(0.0f, 0.0f, 0.0f, 0.0f));
			cc->pushDelta = RE::hkVector4(0.0f, 0.0f, 0.0f, 0.0f);
			cc->fallTime = 0.0f;
		}
	}

	// every frame of the cut-in: the character stays where she stood (within fCutinHoldRadius)
	inline void Hold(RE::Actor* a)
	{
		if (!g.hold || !a || a->IsInRagdollState()) {
			return;
		}
		const RE::NiPoint3 at = a->GetPosition();
		RE::NiPoint3       d = at - g.holdAt;
		const float        dist = d.Length();
		const float        r = std::max(g_settings.cutinHoldRadius, 0.0f);
		if (dist <= r) {
			return;
		}
		g.heldMax = std::max(g.heldMax, dist);
		++g.held;
		a->SetPosition(g.holdAt + d * (r / dist), true);
		StillController(a);
	}

	inline void Stop(const char* a_why)
	{
		g_pending.reset();
		g_letterboxTarget = 0.0f;
		g_faceFxTarget = 0.0f;
		g_faceStartNs = 0;
		StopBlur();
		if (auto head = g.headNode.get(); head && g.headValid && SameM(head->local.rotate, g.headWritten)) {
			head->local.rotate = g.headBase;
		}
		g.headNode.reset();
		g.headValid = false;
		if (!g.active) {
			return;
		}
		g.active = false;
		if (g.hold) {
			if (auto a = g.target.get(); a && !a->IsInRagdollState()) {
				Hold(a.get());
				StillController(a.get());
			}
		}
		g_animActor = nullptr;
		if (auto t = RE::BSTimer::GetSingleton()) {
			t->SetGlobalTimeMultiplier(g.savedMult > 0.01f ? g.savedMult : 1.0f, true);
		}
		auto cam = RE::PlayerCamera::GetSingleton();
		if (cam && g.enteredFree && InFreeCam(cam)) {
			cam->ToggleFreeCameraMode(false);
		}
		g.enteredFree = false;
		if (g_hidHud) {
			if (auto ui = RE::UI::GetSingleton()) {
				ui->ShowMenus(true);
			}
			g_hidHud = false;
		}
		if (g.wasFirstPerson && cam) {
			cam->ForceFirstPerson();
		}
		g.target = {};
		g_lastEnd = SClock::now();
		Log("cut-in end ({})", a_why);
	}

	// if another mod takes the free camera away, the camera update stops coming: end the cut-in here
	inline void Tick()
	{
		if (g.active) {
			const float t = Sec(g.start);
			const bool  taken = t > 0.0f && !InFreeCam(RE::PlayerCamera::GetSingleton());
			if (taken || t > T().out + 2.0f) {
				g.enteredFree = false;
				g.wasFirstPerson = false;
				Stop(taken ? "the camera was taken by something else" : "overran");
			}
		}
	}

	inline void Start(RE::Actor* a, std::uint32_t a_mask, bool a_orbit = false, float a_orbitFrom = 0.0f)
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto cam = RE::PlayerCamera::GetSingleton();
		auto ui = RE::UI::GetSingleton();
		if (!a || !player || !cam || !cam->cameraRoot || g.active || !g_settings.cutinPlayer || !a->Is3DLoaded()) {
			return;
		}
		if (g_lastEnd.time_since_epoch().count() != 0 && Sec(g_lastEnd) < g_settings.cutinCooldown) {
			Log("cut-in skipped: cooldown");
			return;
		}
		if (ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME))) {
			return;
		}
		if (InFreeCam(cam)) {
			Log("cut-in skipped: the free camera is in use");
			return;
		}
		if (const char* busy = PhysicsBusy(a)) {
			Log("cut-in skipped: {}", busy);
			return;
		}
		const Part   p = PartFromMask(a_mask);
		RE::NiPoint3 probe;
		if (!Anchor(a, p, probe)) {
			Log("cut-in skipped: no bone for {}", PartName(p));
			return;
		}
		g = State{};
		g.target = a->GetHandle();
		g.hold = g_settings.cutinHold;
		g.holdAt = a->GetPosition();
		g.part = p;
		g.fromPos = cam->cameraRoot->world.translate;
		g.fromRot = RE::NiQuaternion(cam->cameraRoot->world.rotate);
		g.wasFirstPerson = cam->IsInFirstPerson();
		if (g.wasFirstPerson) {
			cam->ForceThirdPerson();
		}
		cam->ToggleFreeCameraMode(false);
		g.enteredFree = true;
		// a different angle each time: from the left or right, from below, level or a little above
		if (g_settings.cutinRandomAngle) {
			static std::mt19937 s_rng{ std::random_device{}() };
			std::uniform_real_distribution<float> u(0.0f, 1.0f);
			const float sign = u(s_rng) < 0.5f ? -1.0f : 1.0f;
			g.side = sign * (0.12f + 0.28f * u(s_rng));
			const float r = u(s_rng);
			g.rise = r < 0.35f ? -12.0f : r < 0.75f ? 4.0f : 14.0f;  // low-angle / level / slightly above
			g.faceSide = -sign * (0.05f + 0.15f * u(s_rng));
			switch (static_cast<int>(u(s_rng) * 4.0f) % 4) {
			case 0:  // front bust shot
				g.faceRise = 1.0f, g.faceDistMul = 1.0f, g.faceAimDrop = 12.0f;
				break;
			case 1:  // low angle, looking up
				g.faceRise = -22.0f, g.faceDistMul = 0.95f, g.faceAimDrop = 10.0f;
				break;
			case 2:  // slightly from above, a little closer
				g.faceRise = 18.0f, g.faceDistMul = 0.8f, g.faceAimDrop = 6.0f;
				break;
			default:  // three-quarter view from the side
				g.faceSide = -sign * 0.38f, g.faceRise = 4.0f, g.faceDistMul = 0.95f, g.faceAimDrop = 10.0f;
				break;
			}
			g.roll = (u(s_rng) < 0.5f ? -1.0f : 1.0f) * (0.6f + 0.4f * u(s_rng));
		} else {
			g.roll = 1.0f;
		}
		g.roll *= std::clamp(g_settings.cutinDutch, 0.0f, 30.0f) * 0.0174533f;
		g.orbit = a_orbit;
		g.orbitFrom = std::clamp(a_orbitFrom, 0.0f, 0.95f);
		g.orbitSign = g.side < 0.0f ? -1.0f : 1.0f;
		if (a_orbit) {
			g.faceSide = 0.0f, g.faceRise = 1.0f, g.faceDistMul = 1.0f, g.faceAimDrop = 12.0f;  // ends straight on the face
		}
		g.headNode.reset(Node(a, "NPC Head [Head]"));
		g_faceStartNs = 0;
		g_faceFxTarget = 0.0f;
		// hit-stop: the world all but freezes for a beat at the break before the slow motion starts
		g.hitStop = std::clamp(g_settings.cutinHitStop, 0.0f, 0.5f);
		if (auto t = RE::BSTimer::GetSingleton()) {
			g.savedMult = RE::BSTimer::QGlobalTimeMultiplier();
			const float m = g.hitStop > 0.0f ? 0.01f : std::clamp(g_settings.cutinSlowMo, 0.02f, 1.0f);
			t->SetGlobalTimeMultiplier(m, true);
			g.curMult = m;
		}
		if (g_settings.cutinFlash) {
			if (auto im = Imod(kFlashImod)) {
				RE::ImageSpaceModifierInstanceForm::Trigger(im, std::clamp(g_settings.cutinFlashStrength, 0.0f, 1.0f), nullptr);
			}
		}
		g_letterboxTarget = g_settings.cutinLetterbox ? 1.0f : 0.0f;
		if (ui && ui->IsShowingMenus()) {
			ui->ShowMenus(false);  // no HUD over the cut-in
			g_hidHud = true;
		}
		g.haveFaceLocal = HeadLocalForward(a, g.faceLocal);
		if (auto sn = SteadyBone(p).empty() ? nullptr : Node(a, SteadyBone(p))) {
			const float sc = sn->world.scale > 1e-4f ? sn->world.scale : 1.0f;
			g.aimNode.reset(sn);
			g.aimLocal = (sn->world.rotate.Transpose() * (probe - sn->world.translate)) / sc;
		}
		g_animActor = a;
		g.start = g.lastFrame = SClock::now() + std::chrono::milliseconds(static_cast<int>(g.hitStop * 1000.0f));
		g.active = true;
		Log("cut-in start: {} (mask 0x{:X}) side={:.2f} rise={:.0f} face rise={:.0f} dist x{:.2f}", PartName(p), a_mask, g.side, g.rise, g.faceRise,
			g.faceDistMul);
	}

	// any thread: a break asks for a cut-in; it starts on the next ticker update (main thread)
	inline void Request(RE::Actor* a, std::uint32_t a_mask, bool a_orbit = false, float a_orbitFrom = 0.0f)
	{
		if (!a || g.active || g_pending) {
			return;
		}
		g_pending = Pending{ a->GetHandle(), a_mask, a_orbit, a_orbitFrom, SClock::now() };
	}

	// main thread, every ticker update
	inline void Poll()
	{
		Tick();
		if (!g_pending) {
			return;
		}
		auto a = g_pending->actor.get();
		const auto mask = g_pending->mask;
		const bool  orbit = g_pending->orbit;
		const float from = g_pending->orbitFrom;
		g_pending.reset();
		if (a) {
			Start(a.get(), mask, orbit, from);
		}
	}

	// ---------------------------------------------------------------- per frame (camera update, main thread)
	inline void Shot(RE::Actor* a, const RE::NiPoint3& a_at, float a_dist, float a_side, float a_rise, RE::NiPoint3& o_pos, RE::NiQuaternion& o_rot,
		const RE::NiPoint3* a_dir = nullptr)
	{
		const float        h = a->data.angle.z;
		const RE::NiPoint3 fwd = a_dir ? *a_dir : RE::NiPoint3{ std::sin(h), std::cos(h), 0 };
		const RE::NiPoint3 up{ 0, 0, 1 };
		const RE::NiPoint3 right = Cross(fwd, up);
		o_pos = a_at + fwd * a_dist + right * (a_dist * a_side) + up * a_rise;
		o_rot = RE::NiQuaternion(Frame(a_at - o_pos, up));
	}

	inline float PartDistance(Part p)
	{
		const float d = g_settings.cutinPartDistance;
		switch (p) {
		case Part::Body:
			return d * 1.6f;  // the whole upper body in frame
		case Part::Waist:
		case Part::Legs:
			return d * 1.4f;
		case Part::Head:
			return d * 1.2f;
		default:
			return d;
		}
	}

	inline void Update(RE::FreeCameraState* a_free)
	{
		auto a = g.target.get();
		if (!a || !a->Is3DLoaded() || a->IsDead()) {
			SKSE::GetTaskInterface()->AddTask([] { Stop("target gone"); });
			return;
		}
		if (a->IsInRagdollState()) {
			SKSE::GetTaskInterface()->AddTask([] { Stop("ragdoll"); });
			return;
		}
		Hold(a.get());
		Times tm = T();
		if (g.orbit) {  // in / round to the front / to the face / face hold / out, over the same total length
			const float D = tm.out;
			tm = Times{ 0.10f * D, 0.62f * D, 0.74f * D, 0.92f * D, D };
		}
		const float t = Sec(g.start);  // negative during the hit-stop
		// camera shake from the break: strong at the hit, gone in about half a second (real time)
		RE::NiPoint3 shake{};
		{
			const float ts = t + g.hitStop;
			const float amp = std::max(g_settings.cutinShake, 0.0f) * std::exp(-ts / 0.18f);
			if (amp > 0.02f) {
				shake = RE::NiPoint3{ std::sin(ts * 71.0f), std::sin(ts * 53.0f + 1.7f), std::sin(ts * 61.0f + 0.6f) } * amp;
			}
		}
		if (t < 0.0f) {  // hit-stop: hold the player's view, shaken
			g.pos = g.fromPos + shake;
			g.rot = g.fromRot;
			g.have = true;
			a_free->translation = g.pos;
			return;
		}
		if (g.curMult < 0.015f) {  // hit-stop over: into the slow motion
			if (auto tmr = RE::BSTimer::GetSingleton()) {
				const float m = std::clamp(g_settings.cutinSlowMo, 0.02f, 1.0f);
				tmr->SetGlobalTimeMultiplier(m, true);
				g.curMult = m;
			}
		}
		if (g_settings.cutinBlur && !g.blurOn && t < tm.in) {  // radial blur while the camera rushes in
			if (auto im = Imod(kBlurImod)) {
				RE::ImageSpaceModifierInstanceForm::Trigger(im, 1.0f, nullptr);
				g.blurOn = true;
			}
		} else if (g.blurOn && t >= tm.in) {
			StopBlur();
		}
		if (t >= tm.out) {
			SKSE::GetTaskInterface()->AddTask([] { Stop("done"); });
			return;
		}

		RE::NiPoint3 partAt, faceAt;
		if (auto sn = g.aimNode.get()) {
			const float sc = sn->world.scale > 1e-4f ? sn->world.scale : 1.0f;
			partAt = sn->world.translate + (sn->world.rotate * g.aimLocal) * sc;
		} else if (!Anchor(a.get(), g.part, partAt)) {
			partAt = a->GetPosition() + RE::NiPoint3{ 0, 0, 90 };
		}
		auto head = Node(a.get(), "NPC Head [Head]");
		faceAt = head ? head->world.translate - RE::NiPoint3{ 0, 0, g.faceAimDrop } : partAt + RE::NiPoint3{ 0, 0, 12 };

		RE::NiPoint3     pPos, fPos;
		RE::NiQuaternion pRot, fRot;
		const bool       low = g.part == Part::Feet || g.part == Part::Legs;
		Shot(a.get(), partAt, PartDistance(g.part), g.side, low ? 18.0f : g.rise, pPos, pRot);
		// face: follow where the face points, half-way towards the body's facing
		RE::NiPoint3 faceDir{ std::sin(a->data.angle.z), std::cos(a->data.angle.z), 0 };
		if (head && g.haveFaceLocal) {
			RE::NiPoint3 hf = Norm(head->world.rotate * g.faceLocal);
			hf.z = std::clamp(hf.z, -0.45f, 0.3f);
			faceDir = Norm(hf * 0.4f + faceDir * 0.6f);
		}
		Shot(a.get(), faceAt, g_settings.cutinFaceDistance * g.faceDistMul, g.faceSide, g.faceRise, fPos, fRot, &faceDir);

		RE::NiPoint3     pos;
		RE::NiQuaternion rot;
		// orbit shot: the whole body framed from behind, the camera slowly circling round to the front at the same distance
		RE::NiPoint3     bodyAt = a->GetPosition() + RE::NiPoint3{ 0, 0, 72.0f * a->GetScale() };
		float            bodyDist = std::max(g_settings.cutinPartDistance, 60.0f) * 2.3f;
		// close on the action (her hands at the waist) from behind, coming round to the front and easing out to waist-up
		RE::NiPoint3 lowAt = bodyAt - RE::NiPoint3{ 0, 0, 30.0f * a->GetScale() };
		if (auto pel = Node(a.get(), "NPC Pelvis [Pelv]")) {
			lowAt = pel->world.translate;
			RE::NiPoint3 hands;
			if (PairMid(a.get(), "NPC L Hand [LHnd]", "NPC R Hand [RHnd]", hands)) {
				lowAt = lowAt * 0.4f + hands * 0.6f;  // follows the hands as they move
			}
		}
		const float zoom = std::clamp(g_settings.insertCameraZoom, 0.3f, 3.0f);
		const float lowDist = std::max(g_settings.cutinPartDistance, 60.0f) * 0.7f * zoom;
		if (auto sp = Node(a.get(), "NPC Spine1 [Spn1]")) {
			bodyAt = sp->world.translate;  // waist-up rather than the whole body
		}
		bodyDist = std::max(g_settings.cutinPartDistance, 60.0f) * 1.5f * zoom;
		auto        ring = [&](float a_turn, float a_wide, RE::NiPoint3& o_pos, RE::NiQuaternion& o_rot) {
            const float        th = a->data.angle.z + 3.14159265f * (1.0f - a_turn) * g.orbitSign;  // a_turn 0 = behind, 1 = in front
            const RE::NiPoint3 at = lowAt + (bodyAt - lowAt) * a_wide;
            const float        dist = lowDist + (bodyDist - lowDist) * a_wide;
            o_pos = at + RE::NiPoint3{ std::sin(th), std::cos(th), 0 } * dist + RE::NiPoint3{ 0, 0, 14.0f * a_wide };
            o_rot = RE::NiQuaternion(Frame(at - o_pos, RE::NiPoint3{ 0, 0, 1 }));
		};
		if (g.orbit && t < tm.toFace) {
			RE::NiPoint3     rPos;
			RE::NiQuaternion rRot;
			if (t < tm.in) {
				ring(g.orbitFrom, 0.0f, rPos, rRot);
				const float k = Ease(t / tm.in);
				pos = g.fromPos + (rPos - g.fromPos) * k;
				rot = Slerp(g.fromRot, rRot, k);
			} else if (t < tm.partHold) {
				// round to the front over most of the phase, then hold the whole body a moment
				const float u = std::clamp((t - tm.in) / (0.80f * (tm.partHold - tm.in)), 0.0f, 1.0f);
				ring(g.orbitFrom + (1.0f - g.orbitFrom) * Ease(u), Ease(u), pos, rot);
			} else {
				ring(1.0f, 1.0f, rPos, rRot);
				const float k = Ease((t - tm.partHold) / (tm.toFace - tm.partHold));
				pos = Orbit(faceAt, rPos, fPos, k);
				rot = Slerp(rRot, fRot, k);
			}
		} else if (t < tm.in) {
			const float k = Ease(t / tm.in);
			pos = Orbit(partAt, g.fromPos, pPos, k);
			rot = Slerp(g.fromRot, pRot, k);
		} else if (t < tm.partHold) {
			const float k = (t - tm.in) / (tm.partHold - tm.in);  // slow push-in while holding
			pos = pPos + (partAt - pPos) * (0.12f * k);
			rot = pRot;
		} else if (t < tm.toFace) {
			const float        k = Ease((t - tm.partHold) / (tm.toFace - tm.partHold));
			const RE::NiPoint3 held = pPos + (partAt - pPos) * 0.12f;
			pos = Orbit((partAt + faceAt) * 0.5f, held, fPos, k);
			rot = Slerp(pRot, fRot, k);
		} else if (t < tm.faceHold) {
			const float k = (t - tm.toFace) / (tm.faceHold - tm.toFace);
			pos = fPos + (faceAt - fPos) * (0.22f * Ease(k));  // slow push-in on the face
			rot = fRot;
		} else {
			const float        k = Ease((t - tm.faceHold) / (tm.out - tm.faceHold));
			const RE::NiPoint3 held = fPos + (faceAt - fPos) * 0.22f;
			pos = Orbit(faceAt, held, g.fromPos, k);
			rot = Slerp(fRot, g.fromRot, k);
		}
		// face shot: dutch angle and a slight handheld drift; the overlays go with it
		if (t >= tm.partHold && t < tm.out) {
			const float in = std::clamp((t - tm.partHold) / std::max(tm.toFace - tm.partHold, 0.01f), 0.0f, 1.0f);
			const float outk = t > tm.faceHold ? 1.0f - std::clamp((t - tm.faceHold) / std::max(tm.out - tm.faceHold, 0.01f), 0.0f, 1.0f) : 1.0f;
			const float k = Ease(in) * outk;
			RE::NiMatrix3      R = rot.ToRotation();
			const RE::NiPoint3 camFwd{ R.entry[0][1], R.entry[1][1], R.entry[2][1] };
			rot = RE::NiQuaternion(AxisAngle(Norm(camFwd), g.roll * k) * R);
			pos += RE::NiPoint3{ std::sin(t * 1.3f) * 0.5f, std::sin(t * 1.7f + 1.0f) * 0.4f, std::sin(t * 1.1f + 2.0f) * 0.35f } * k;
			if (!g.faceFxOn) {
				g.faceFxOn = true;
				g_faceStartNs = SClock::now().time_since_epoch().count();
				g_faceFxTarget = 1.0f;
			}
			if (t >= tm.faceHold) {
				g_faceFxTarget = 0.0f;
			}
		}
		pos += shake;
		g.pos = pos;
		g.rot = rot;
		g.have = true;
		a_free->translation = pos;
	}

	// ---------------------------------------------------------------- hooks
	struct FreeCamUpdate
	{
		static void thunk(RE::FreeCameraState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_next)
		{
			func(a_this, a_next);
			if (g.active) {
				Update(a_this);
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct FreeCamGetRotation
	{
		static void thunk(RE::FreeCameraState* a_this, RE::NiQuaternion& a_out)
		{
			func(a_this, a_out);
			if (g.active && g.have) {
				a_out = g.rot;
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct FreeCamGetTranslation
	{
		static void thunk(RE::FreeCameraState* a_this, RE::NiPoint3& a_out)
		{
			func(a_this, a_out);
			if (g.active && g.have) {
				a_out = g.pos;
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// The face shot's small reaction: a startled little dip of the head that eases back, with a fine tremble.
	// Applied right after the character's own animation update (which rewrites every bone).
	inline void HeadAction(RE::Actor* a)
	{
		auto        head = g.headNode.get();
		auto        parent = head ? head->parent : nullptr;
		const float tt = FaceTime();
		if (!parent || !g_settings.cutinFaceAction) {
			return;
		}
		auto&         lr = head->local.rotate;
		RE::NiMatrix3 base = lr;
		if (g.headValid && SameM(lr, g.headWritten)) {
			base = g.headBase;
		}
		float yaw = 0.0f, pitch = 0.0f;
		if (tt > 0.0f && tt < 4.0f) {
			auto sm = [](float e0, float e1, float x) { x = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return x * x * (3 - 2 * x); };
			const float flinch = sm(0.0f, 0.18f, tt);
			const float settle = sm(0.25f, 1.0f, tt);
			const float fade = 1.0f - sm(2.6f, 3.8f, tt);
			yaw = (0.004f * std::sin(tt * 37.0f) + 0.003f * std::sin(tt * 23.0f)) * fade;
			pitch = (0.05f * flinch * (1.0f - 0.6f * settle) + 0.003f * std::sin(tt * 29.0f + 1.0f)) * fade;
		}
		const float        h = a->data.angle.z;
		const RE::NiPoint3 up{ 0, 0, 1 }, right{ std::cos(h), -std::sin(h), 0 };
		const auto         pt = parent->world.rotate.Transpose();
		lr = AxisAngle(Norm(pt * up), yaw) * AxisAngle(Norm(pt * right), pitch) * base;
		g.headBase = base;
		g.headWritten = lr;
		g.headValid = true;
		RE::NiUpdateData ud{};
		head->Update(ud);
	}

	template <int Tag>
	struct UpdateAnimHook
	{
		static void thunk(RE::Actor* a_this, float a_delta)
		{
			func(a_this, a_delta);
			if (g.active && a_this && a_this == g_animActor.load(std::memory_order_relaxed)) {
				HeadAction(a_this);
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	inline void InstallHooks()
	{
		REL::Relocation<std::uintptr_t> vPlayer{ RE::VTABLE_PlayerCharacter[0] };
		UpdateAnimHook<1>::func = vPlayer.write_vfunc(0x7D, UpdateAnimHook<1>::thunk);
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_FreeCameraState[0] };
		const bool                      vr = REL::Module::IsVR();
		FreeCamUpdate::func = vtbl.write_vfunc(vr ? 0x4 : 0x3, FreeCamUpdate::thunk);
		FreeCamGetRotation::func = vtbl.write_vfunc(vr ? 0x5 : 0x4, FreeCamGetRotation::thunk);
		FreeCamGetTranslation::func = vtbl.write_vfunc(vr ? 0x6 : 0x5, FreeCamGetTranslation::thunk);
		Log("cut-in camera hooks installed");
	}
}

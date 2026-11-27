#pragma once
// The womb HUD: a small, living picture of what Beeing Female is doing inside her.
// Data comes from the main thread (Womb::Update); drawing happens in the menu framework's HUD callback.

namespace Womb
{
	struct Data
	{
		int         state = -1;  // Beeing Female: 0 follicular, 1 ovulation, 2 luteal, 3 menstruation, 4-7 pregnant, 8 recovery
		float       daysIn = 0.0f;  // days in this state
		int         sperm = 0;      // donors still alive inside
		int         unborn = 0;
		float       arousal = 0.0f;  // 0-100
		std::string father;          // whose child
		std::string donor;           // the latest donor
		int         donors[6]{};     // race look of the donors still inside (Womb::Race)
		int         donorN = 0;
	};
	// how their seed looks in the HUD
	enum Race
	{
		kHuman, kOrc, kElf, kKhajiit, kArgonian, kVampire, kDaedra, kBeast
	};
	struct Look
	{
		int   r, g, b;
		float head, tail, wiggle, glow;
		const char* name;
	};
	inline const Look& LookOf(int a_race)
	{
		static const Look k[] = {
			{ 255, 255, 255, 2.2f, 9.0f, 9.0f, 0.0f, "人" },
			{ 110, 225, 140, 3.0f, 8.0f, 12.0f, 0.25f, "オーク" },
			{ 255, 236, 170, 1.8f, 12.0f, 7.0f, 0.35f, "エルフ" },
			{ 255, 180, 90, 2.4f, 10.0f, 10.0f, 0.15f, "カジート" },
			{ 90, 225, 205, 2.2f, 11.0f, 5.0f, 0.2f, "アルゴニアン" },
			{ 225, 40, 70, 2.2f, 9.0f, 8.0f, 0.4f, "吸血鬼" },
			{ 190, 95, 255, 2.6f, 9.0f, 11.0f, 0.5f, "デイドラ" },
			{ 205, 175, 140, 3.2f, 10.0f, 13.0f, 0.1f, "獣" },
		};
		return k[std::clamp(a_race, 0, 7)];
	}
	inline std::atomic<bool> g_flashReq{ false };

	inline std::mutex g_lock;
	inline Data       g_data;
	inline bool       g_show = true;
	inline float      g_scale = 1.0f;
	inline int        g_corner = 0;  // 0 bottom left, 1 bottom right, 2 top left, 3 top right

	// floating lines: "排卵しました", "受精しました" ...
	struct Toast
	{
		std::string   text;
		std::string   sub;
		std::uint32_t col;
		float         age = 0.0f;
	};
	inline std::deque<Toast> g_toasts;

	inline void Push(std::string a_text, std::string a_sub, std::uint32_t a_col)
	{
		std::scoped_lock l(g_lock);
		g_toasts.push_back({ std::move(a_text), std::move(a_sub), a_col, 0.0f });
		while (g_toasts.size() > 3) {
			g_toasts.pop_front();
		}
	}

	inline std::uint32_t Col(int r, int g, int b, float a)
	{
		return (static_cast<std::uint32_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f) << 24) | (static_cast<std::uint32_t>(b) << 16) |
		       (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(r);
	}
	inline KKMenu::ImVec2 V(float x, float y) { return { x, y }; }
	inline KKMenu::ImVec2 Bez(KKMenu::ImVec2 a, KKMenu::ImVec2 b, KKMenu::ImVec2 c, KKMenu::ImVec2 d, float t)
	{
		const float u = 1 - t;
		return { u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
			u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y };
	}

	// typical lengths (days) of each state, for the ring and the progress of things inside
	inline float Length(int st)
	{
		static constexpr float k[] = { 8.0f, 2.0f, 9.0f, 4.0f, 10.0f, 10.0f, 10.0f, 1.0f, 5.0f };
		return st >= 0 && st < 9 ? k[st] : 1.0f;
	}
	inline const char* Name(int st)
	{
		switch (st) {
		case 0:
			return "卵胞期";
		case 1:
			return "排卵期";
		case 2:
			return "黄体期";
		case 3:
			return "月経期";
		case 4:
			return "妊娠初期";
		case 5:
			return "妊娠中期";
		case 6:
			return "妊娠後期";
		case 7:
			return "陣痛";
		case 8:
			return "産後の回復期";
		default:
			return "";
		}
	}
	inline const char* Mood(int st, float arousal, int sperm)
	{
		switch (st) {
		case 0:
			return sperm ? "奥に、誰かの熱がまだ残っている" : "胎の奥で、小さな卵が育っている";
		case 1:
			return sperm ? "卵が、待っていた熱に囲まれていく……" : (arousal > 60 ? "下腹部が熱を帯びて、疼いている" : "卵が降りてきた。いちばん孕みやすい時");
		case 2:
			return sperm ? "まだ誰かの熱が、奥で卵を探している" : "胎は静かに、次の時を待っている";
		case 3:
			return "胎がそっと、内側を清めている";
		case 4:
			return "小さな命が、胎にしがみついた";
		case 5:
			return "お腹の中で、ときどき小さく蹴られる";
		case 6:
			return "重たくて、愛おしい。もうすぐ会える";
		case 7:
			return "その時が来た……";
		case 8:
			return "胎がゆっくりと、元に戻っていく";
		default:
			return "";
		}
	}


	// seed swimming in from the rim toward the egg, each in its donor's colours
	inline void DrawSperm(void* dl, const Data& d, KKMenu::ImVec2 c, float rOut, float rIn, float s, float t)
	{
		const int n = std::min(16, 5 + d.sperm * 3);
		for (int i = 0; i < n; ++i) {
			const Look& L = LookOf(d.donorN ? d.donors[i % d.donorN] : kHuman);
			const float a0 = i * 2.39996f;
			const float ph = std::fmod(t * 0.16f + i * 0.137f, 1.0f);
			const float dist = rIn + (rOut - rIn) * (1.0f - ph);
			const float a = a0 + 0.35f * std::sin(t * 1.3f + i) * (1.0f - ph);
			const float dx = -std::cos(a), dy = -std::sin(a);  // heading inward
			const float fade = std::sin(3.14159f * ph);
			const auto  head = V(c.x + std::cos(a) * dist, c.y + std::sin(a) * dist);
			const int   seg = 6;
			KKMenu::ImVec2 prev = head;
			for (int j = 1; j <= seg; ++j) {
				const float k = static_cast<float>(j) / seg;
				const float w = std::sin(t * L.wiggle + i * 2.1f + k * 5.0f) * 3.2f * s * k;
				const auto  p = V(head.x - dx * L.tail * s * k * 1.3f - dy * w, head.y - dy * L.tail * s * k * 1.3f + dx * w);
				KKMenu::AddLine(dl, prev, p, Col(L.r, L.g, L.b, 0.55f * fade * (1.0f - k * 0.5f)), (1.4f - 0.6f * k) * s);
				prev = p;
			}
			if (L.glow > 0) {
				KKMenu::AddCircleFilled(dl, head, L.head * 2.6f * s, Col(L.r, L.g, L.b, L.glow * 0.5f * fade), 10);
			}
			KKMenu::AddCircleFilled(dl, head, L.head * s, Col(L.r, L.g, L.b, 0.95f * fade), 10);
		}
	}

	inline void Draw(void* dl, KKMenu::ImVec2 vp, float dt, float t)
	{
		Data d;
		{
			std::scoped_lock l(g_lock);
			d = g_data;
		}
		const float s = 1.15f * g_scale * vp.y / 1080.0f;
		const float w = 330.0f * s, h = 240.0f * s, pad = 24.0f * s;
		const float x0 = (g_corner & 1) ? vp.x - w - pad : pad;
		const float y0 = (g_corner & 2) ? pad + 40.0f * s : vp.y - h - pad - 90.0f * s;
		const float pulse = 0.5f + 0.5f * std::sin(t * (2.0f + d.arousal / 40.0f));
		const float heat = std::clamp(d.arousal / 100.0f, 0.0f, 1.0f);

		// panel
		KKMenu::AddRectGradient(dl, V(x0, y0), V(x0 + w, y0 + h), Col(40, 8, 30, 0.55f), Col(40, 8, 30, 0.25f), Col(10, 0, 10, 0.05f), Col(20, 0, 15, 0.45f));
		KKMenu::AddRect(dl, V(x0, y0), V(x0 + w, y0 + h), Col(255, 150, 200, 0.25f + 0.2f * heat * pulse), 1.0f);

		// the womb: uterus, tubes, ovaries
		const KKMenu::ImVec2 c = V(x0 + 105.0f * s, y0 + 120.0f * s);
		const float          R = 46.0f * s;
		const bool           preg = d.state >= 4 && d.state <= 7;
		const float          grow = preg ? std::clamp(((d.state - 4) * 10.0f + d.daysIn) / 31.0f, 0.0f, 1.0f) : 0.0f;
		const float          Rw = R * (1.0f + 0.45f * grow);
		// the womb: an AI-painted orb (Data/BFHeredity/Orb) when present, else the drawn uterus
		const float          prog = std::clamp(d.daysIn / Length(d.state), 0.0f, 1.0f);
		static float         s_flashT = -100.0f;
		if (g_flashReq.exchange(false)) {
			s_flashT = t;
		}
		const char* orbName = d.state == 3 ? "menses" : (d.state >= 5 && d.state <= 7) ? "preg" : d.state == 4 ? (d.daysIn < 3.0f ? "morula" : "implant") : "egg";
		const float orbD = 178.0f * s;
		bool        orbOk = false;
		if (t - s_flashT < Orb::Length("flash")) {
			orbOk = Orb::Draw(dl, "flash", c.x, c.y, orbD, t - s_flashT, 1.0f, false);
		} else {
			orbOk = Orb::Draw(dl, orbName, c.x, c.y, orbD, t, d.state == 8 || d.state == 2 ? 0.75f : 1.0f);
		}
		if (orbOk) {
			KKMenu::AddCircleFilled(dl, c, orbD * 0.62f, Col(255, 90, 160, 0.05f + 0.10f * heat * pulse), 40);  // warmth
			if (d.sperm > 0 && d.state < 4 && t - s_flashT >= Orb::Length("flash")) {
				DrawSperm(dl, d, c, orbD * 0.47f, orbD * 0.12f, s, t);
			}
		} else {
			KKMenu::ImVec2       body[14];
			for (int i = 0; i < 14; ++i) {  // pear: wide fundus up top, narrowing to the cervix
				const float a = 3.14159f * 2.0f * i / 14.0f;
				const float sx = std::cos(a), sy = std::sin(a);
				const float narrow = sy > 0 ? 1.0f - 0.55f * sy : 1.0f;
				body[i] = V(c.x + sx * Rw * 0.95f * narrow, c.y + sy * Rw * (sy > 0 ? 1.25f : 0.8f));
			}
			const float glow = 0.35f + 0.35f * heat + (d.state == 1 ? 0.2f * pulse : 0.0f);
			const auto  wallCol = d.state == 3 ? Col(190, 40, 60, 0.75f) : Col(235, 120, 165, 0.55f + 0.25f * glow);
			KKMenu::AddCircleFilled(dl, c, Rw * 1.5f, Col(255, 90, 160, 0.08f + 0.12f * heat * pulse), 40);  // warmth
			KKMenu::AddConvexPolyFilled(dl, body, 14, wallCol);
			// inner cavity
			KKMenu::ImVec2 cav[12];
			for (int i = 0; i < 12; ++i) {
				const float a = 3.14159f * 2.0f * i / 12.0f;
				const float sx = std::cos(a), sy = std::sin(a);
				const float narrow = sy > 0 ? 1.0f - 0.7f * sy : 1.0f;
				cav[i] = V(c.x + sx * Rw * 0.62f * narrow, c.y - 4.0f * s + sy * Rw * (sy > 0 ? 0.95f : 0.5f));
			}
			KKMenu::AddConvexPolyFilled(dl, cav, 12, d.state == 3 ? Col(120, 10, 25, 0.8f) : Col(120, 30, 70, 0.75f));
			// tubes and ovaries
			const KKMenu::ImVec2 tl = V(c.x - Rw * 0.8f, c.y - Rw * 0.55f), tr = V(c.x + Rw * 0.8f, c.y - Rw * 0.55f);
			const KKMenu::ImVec2 ol = V(c.x - Rw * 1.75f, c.y - Rw * 0.1f), orr = V(c.x + Rw * 1.75f, c.y - Rw * 0.1f);
			const KKMenu::ImVec2 l1 = V(c.x - Rw * 1.3f, c.y - Rw * 1.2f), l2 = V(c.x - Rw * 1.9f, c.y - Rw * 0.9f);
			KKMenu::AddBezierCubic(dl, tl, l1, l2, ol, Col(235, 120, 165, 0.8f), 4.0f * s);
			KKMenu::AddBezierCubic(dl, tr, V(c.x + Rw * 1.3f, c.y - Rw * 1.2f), V(c.x + Rw * 1.9f, c.y - Rw * 0.9f), orr, Col(235, 120, 165, 0.8f), 4.0f * s);
			KKMenu::AddCircleFilled(dl, ol, 10.0f * s, Col(245, 170, 200, 0.9f));
			KKMenu::AddCircleFilled(dl, orr, 10.0f * s, Col(245, 170, 200, 0.9f));

				// the egg
			KKMenu::ImVec2 egg = ol;
			bool           showEgg = false;
			if (d.state == 0) {  // a follicle swelling on the ovary
				KKMenu::AddCircleFilled(dl, V(ol.x, ol.y - 4.0f * s), (3.0f + 6.0f * prog) * s, Col(255, 230, 180, 0.6f + 0.3f * prog));
			} else if (d.state == 1) {  // released, drifting down the tube
				egg = Bez(ol, l2, l1, tl, std::min(1.0f, prog * 1.1f));
				showEgg = true;
			} else if (d.state == 2) {
				egg = V(c.x, c.y - Rw * 0.2f);
				showEgg = prog < 0.5f;
			}
			if (showEgg) {
				KKMenu::AddCircleFilled(dl, egg, (12.0f + 4.0f * pulse) * s, Col(255, 220, 140, 0.18f));
				KKMenu::AddCircleFilled(dl, egg, 6.0f * s, Col(255, 236, 190, 0.95f));
			}
			// sperm swimming up from the cervix toward the egg (or the fundus)
			if (d.sperm > 0 && d.state < 4) {
				const int            n = std::min(14, 4 + d.sperm * 3);
				const KKMenu::ImVec2 cx = V(c.x, c.y + Rw * 1.15f);
				const KKMenu::ImVec2 goal = showEgg ? egg : V(c.x, c.y - Rw * 0.35f);
				for (int i = 0; i < n; ++i) {
					const float ph = std::fmod(t * 0.18f + i * 0.137f, 1.0f);
					const float wig = std::sin(t * 9.0f + i * 2.1f) * 5.0f * s * (1.0f - ph);
					const float ox = (std::fmod(i * 0.618f, 1.0f) - 0.5f) * 30.0f * s * (1.0f - ph);
					const auto  p = V(cx.x + (goal.x - cx.x) * ph + wig + ox, cx.y + (goal.y - cx.y) * ph);
					const float dx = (goal.x - cx.x), dy = (goal.y - cx.y), dl2 = std::sqrt(dx * dx + dy * dy) + 1e-3f;
					KKMenu::AddLine(dl, p, V(p.x - dx / dl2 * 9.0f * s - wig * 0.6f, p.y - dy / dl2 * 9.0f * s), Col(255, 255, 255, 0.55f), 1.2f * s);
					KKMenu::AddCircleFilled(dl, p, 2.2f * s, Col(255, 255, 255, 0.85f), 8);
				}
			}
			// the child
			if (preg) {
				const auto  fc = V(c.x, c.y - Rw * 0.05f);
				const float fr = (5.0f + 30.0f * grow) * s;
				const float beat = std::pow(std::max(0.0f, std::sin(t * 7.5f)), 8.0f);
				KKMenu::AddCircleFilled(dl, fc, fr * 1.5f, Col(255, 200, 220, 0.18f + 0.12f * beat));
				KKMenu::AddCircleFilled(dl, fc, fr, Col(255, 210, 200, 0.85f));
				KKMenu::AddCircleFilled(dl, V(fc.x + fr * 0.35f, fc.y - fr * 0.45f), fr * 0.45f, Col(255, 220, 210, 0.9f));
				KKMenu::AddCircleFilled(dl, V(fc.x - fr * 0.2f, fc.y + fr * 0.1f), fr * 0.12f + 1.5f * s, Col(255, 80, 120, 0.4f + 0.6f * beat));  // heart
			}

		}

		// cycle ring around the womb
		{
			const int   order[4] = { 3, 0, 1, 2 };
			float       total = 0;
			for (int i : order) {
				total += Length(i);
			}
			const float rr = orbOk ? orbD * 0.54f : Rw * 2.25f;
			float       a0 = -3.14159f * 0.5f;
			for (int i : order) {
				const float span = 6.28318f * Length(i) / total;
				const auto  col = i == 3 ? Col(200, 50, 70, 0.7f) : i == 0 ? Col(240, 150, 190, 0.6f) : i == 1 ? Col(255, 215, 120, 0.9f) : Col(170, 120, 220, 0.6f);
				const int   segs = 10;
				for (int k = 0; k < segs; ++k) {
					const float a = a0 + span * k / segs, b = a0 + span * (k + 1) / segs - 0.01f;
					KKMenu::AddLine(dl, V(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr), V(c.x + std::cos(b) * rr, c.y + std::sin(b) * rr), col,
						(d.state == i ? 5.0f : 3.0f) * s);
				}
				if (d.state == i) {
					const float m = a0 + span * prog;
					KKMenu::AddCircleFilled(dl, V(c.x + std::cos(m) * rr, c.y + std::sin(m) * rr), (5.0f + 2.0f * pulse) * s, Col(255, 255, 255, 0.95f));
				}
				a0 += span;
			}
		}

		// words
		const float tx = x0 + 215.0f * s;
		char        buf[160];
		KKMenu::AddText(dl, 22.0f * s, V(tx, y0 + 18.0f * s), Col(255, 190, 220, 0.95f), Name(d.state));
		std::snprintf(buf, sizeof(buf), "%d日目", static_cast<int>(d.daysIn) + 1);
		KKMenu::AddText(dl, 15.0f * s, V(tx, y0 + 46.0f * s), Col(255, 230, 240, 0.7f), buf);
		if (preg && !d.father.empty()) {
			KKMenu::AddText(dl, 14.0f * s, V(tx, y0 + 76.0f * s), Col(255, 210, 225, 0.9f), "宿しているのは");
			KKMenu::AddText(dl, 17.0f * s, V(tx, y0 + 96.0f * s), Col(255, 240, 200, 0.95f), d.father.c_str());
			std::snprintf(buf, sizeof(buf), "の子%s", d.unborn > 1 ? "（ふたり以上）" : "");
			KKMenu::AddText(dl, 14.0f * s, V(tx, y0 + 120.0f * s), Col(255, 210, 225, 0.9f), buf);
		} else if (d.sperm > 0 && d.state < 4) {
			std::snprintf(buf, sizeof(buf), "胎内の精：%d人分", d.sperm);
			KKMenu::AddText(dl, 14.0f * s, V(tx, y0 + 76.0f * s), Col(255, 240, 240, 0.85f), buf);
			if (!d.donor.empty()) {
				KKMenu::AddText(dl, 14.0f * s, V(tx, y0 + 96.0f * s), Col(255, 220, 200, 0.75f), d.donor.c_str());
			}
		}
		std::snprintf(buf, sizeof(buf), "昂り %d", static_cast<int>(d.arousal));
		KKMenu::AddRectFilled(dl, V(tx, y0 + 150.0f * s), V(tx + 80.0f * s, y0 + 156.0f * s), Col(80, 30, 60, 0.6f));
		KKMenu::AddRectFilled(dl, V(tx, y0 + 150.0f * s), V(tx + 80.0f * s * heat, y0 + 156.0f * s), Col(255, 90 + static_cast<int>(60 * pulse), 150, 0.9f));
		KKMenu::AddText(dl, 13.0f * s, V(tx, y0 + 160.0f * s), Col(255, 220, 235, 0.7f), buf);
		// a whispered line under the picture
		KKMenu::AddText(dl, 14.0f * s, V(x0 + 14.0f * s, y0 + h - 26.0f * s), Col(255, 225, 238, 0.65f + 0.25f * pulse * heat), Mood(d.state, d.arousal, d.sperm));

		// toasts, centre of the screen: rise and fade
		std::scoped_lock l(g_lock);
		float            ty = vp.y * 0.30f;
		for (auto it = g_toasts.begin(); it != g_toasts.end();) {
			it->age += dt;
			const float life = 5.5f;
			if (it->age > life) {
				it = g_toasts.erase(it);
				continue;
			}
			const float in = std::clamp(it->age / 0.6f, 0.0f, 1.0f), out = std::clamp((life - it->age) / 1.2f, 0.0f, 1.0f);
			const float a = in * out;
			const float rise = (1.0f - in) * 30.0f * s + it->age * 4.0f * s;
			const float size = (34.0f + 6.0f * (1.0f - in)) * s;
			const float approxW = size * 0.95f * static_cast<float>(it->text.size()) / 3.0f;  // UTF-8 Japanese: 3 bytes a character
			const float px = vp.x * 0.5f - approxW * 0.5f;
			KKMenu::AddRectGradient(dl, V(px - 60.0f * s, ty - rise - 8.0f * s), V(px + approxW + 60.0f * s, ty - rise + size + 30.0f * s), Col(30, 0, 20, 0.0f),
				Col(30, 0, 20, 0.0f), Col(30, 0, 20, 0.45f * a), Col(30, 0, 20, 0.45f * a));
			KKMenu::AddText(dl, size, V(px + 2.0f * s, ty - rise + 2.0f * s), Col(0, 0, 0, 0.5f * a), it->text.c_str());
			KKMenu::AddText(dl, size, V(px, ty - rise), (it->col & 0x00FFFFFFu) | (static_cast<std::uint32_t>(a * 255.0f) << 24), it->text.c_str());
			if (!it->sub.empty()) {
				const float sub = 17.0f * s;
				const float sw = sub * 0.95f * static_cast<float>(it->sub.size()) / 3.0f;
				KKMenu::AddText(dl, sub, V(vp.x * 0.5f - sw * 0.5f, ty - rise + size + 6.0f * s), Col(255, 230, 240, 0.85f * a), it->sub.c_str());
			}
			ty += size + 46.0f * s;
			++it;
		}
	}

	inline void __stdcall Hud()
	{
		static auto s_last = std::chrono::steady_clock::now();
		static auto s_start = s_last;
		const auto  now = std::chrono::steady_clock::now();
		const float dt = std::clamp(std::chrono::duration<float>(now - s_last).count(), 0.0f, 0.1f);
		s_last = now;
		const float t = std::chrono::duration<float>(now - s_start).count();
		auto        dl = KKMenu::ForegroundDrawList();
		const auto  vp = KKMenu::MainViewportSize();
		if (!dl || vp.x <= 0.0f) {
			return;
		}
		int st;
		{
			std::scoped_lock l(g_lock);
			st = g_data.state;
		}
		if (g_show && st >= 0) {
			Draw(dl, vp, dt, t);
		}
	}
}

#include "FWM/Menu.h"

#include "FWM/Battle.h"
#include "FWM/Camera.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Rules.h"
#include "FWM/Settings.h"

#pragma warning(push)
#pragma warning(disable: 4099 5054)
#include "SKSEMenuFramework.h"
#pragma warning(pop)

namespace FWM::Menu
{
	namespace
	{
		namespace Im = ImGuiMCP;
		namespace Draw = ImGuiMCP::ImDrawListManager;
		namespace MF = SKSEMenuFramework;
		using V = Im::ImVec2;
		using U32 = Im::ImU32;

#ifdef PlaySound
#	undef PlaySound  // the Windows macro, not the game's function
#endif

		// The battle plan draws on the render thread; sounds play from the main one.
		void Sound(const char* a_id)
		{
			if (!Settings::Get().sounds) {
				return;
			}
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([a_id] { RE::PlaySound(a_id); });
			}
		}

		// BATTLE_PLAN_BEGIN: the battle plan's draw code, kept in one block so it can be
		// compiled and rendered outside the game.

		// Hard near-black panels, hairline frames that run past their corners, one oxblood
		// accent. Chosen things stay white; the red sits behind them.
		constexpr U32 kGlass = IM_COL32(15, 15, 16, 236);
		constexpr U32 kGlassEdge = IM_COL32(255, 255, 255, 54);
		constexpr U32 kTile = IM_COL32(255, 255, 255, 8);
		constexpr U32 kTileHover = IM_COL32(255, 255, 255, 20);
		constexpr U32 kAccent = IM_COL32(158, 36, 34, 255);
		constexpr U32 kAccentDim = IM_COL32(150, 146, 140, 255);
		constexpr U32 kEdge = IM_COL32(212, 66, 58, 255);  // thin red lines and red words on black
		constexpr U32 kText = IM_COL32(236, 232, 226, 255);
		constexpr U32 kTextDim = IM_COL32(170, 166, 160, 255);
		constexpr U32 kTextFaint = IM_COL32(108, 106, 102, 255);
		constexpr U32 kRed = IM_COL32(214, 58, 52, 255);
		constexpr U32 kGreen = IM_COL32(132, 200, 140, 255);
		constexpr U32 kAmber = IM_COL32(236, 172, 72, 255);
		constexpr U32 kHealth = IM_COL32(178, 44, 40, 255);
		constexpr U32 kMagicka = IM_COL32(84, 130, 204, 255);
		constexpr U32 kStamina = IM_COL32(118, 174, 94, 255);
		constexpr U32 kFocusRing = IM_COL32(236, 232, 226, 255);
		constexpr U32 kShade = IM_COL32(0, 0, 0, 255);

		constexpr std::uint32_t kPadUp = 0x1, kPadDown = 0x2, kPadLeft = 0x4, kPadRight = 0x8;
		constexpr std::uint32_t kPadLB = 0x100, kPadRB = 0x200, kPadA = 0x1000, kPadB = 0x2000, kPadX = 0x4000, kPadY = 0x8000;
		constexpr std::uint32_t kEscape = 0x01;

		MF::Model::WindowInterface*            plan{ nullptr };
		std::unique_ptr<MF::Model::InputEvent> inputRegistration;
		std::unique_ptr<MF::Model::HudElement> hudRegistration;

		std::atomic<int>  padDx{ 0 }, padDy{ 0 }, padShoulder{ 0 };
		std::atomic<bool> padA{ false }, padB{ false }, padX{ false }, padY{ false };
		std::atomic<bool> padUsed{ false };
		std::uint32_t     padHeld{ 0 };
		std::atomic<bool> closeRequested{ false };
		std::atomic<bool> justOpened{ false };

		// --- Motion ------------------------------------------------------------------

		float Clamp01(float a_t) { return std::clamp(a_t, 0.0f, 1.0f); }

		float EaseOut(float a_t)
		{
			const float t = 1.0f - Clamp01(a_t);
			return 1.0f - t * t * t;
		}

		// Overshoots a little before settling: for things that pop in.
		float EaseOutBack(float a_t)
		{
			const float t = Clamp01(a_t) - 1.0f;
			return 1.0f + t * t * (2.7f * t + 1.7f);
		}

		float Smooth(float a_t)
		{
			const float t = Clamp01(a_t);
			return t * t * (3.0f - 2.0f * t);
		}

		// Frame-rate independent approach toward a target.
		float Follow(float a_current, float a_target, float a_dt, float a_rate)
		{
			return a_target + (a_current - a_target) * std::exp(-a_rate * a_dt);
		}

		// Progress of one part of a staggered entrance, eased.
		float Stage(float a_t, float a_start, float a_length) { return EaseOut((a_t - a_start) / a_length); }

		// Named values that ease toward wherever the frame says they should be.
		struct Tweens
		{
			std::unordered_map<std::uint64_t, float> values;
			float                                    dt{ 0.0f };

			static std::uint64_t Key(std::string_view a_tag, std::uint64_t a_index)
			{
				std::uint64_t h = 1469598103934665603ull;
				for (const char c : a_tag) {
					h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
				}
				return h ^ (a_index * 0x9E3779B97F4A7C15ull);
			}

			float Toward(std::string_view a_tag, std::uint64_t a_index, float a_target, float a_rate)
			{
				const auto key = Key(a_tag, a_index);
				auto       it = values.find(key);
				if (it == values.end()) {
					it = values.emplace(key, a_target).first;
				}
				it->second = Follow(it->second, a_target, dt, a_rate);
				return it->second;
			}

			float& Raw(std::string_view a_tag, std::uint64_t a_index, float a_initial)
			{
				return values.try_emplace(Key(a_tag, a_index), a_initial).first->second;
			}
		};

		U32 Alpha(U32 a_color, float a_alpha)
		{
			const auto a = static_cast<U32>(std::clamp(static_cast<float>((a_color >> 24) & 0xFF) * a_alpha, 0.0f, 255.0f));
			return (a_color & 0x00FFFFFF) | (a << 24);
		}

		U32 Mix(U32 a_from, U32 a_to, float a_t)
		{
			const float t = Clamp01(a_t);
			U32         out = 0;
			for (int shift = 0; shift < 32; shift += 8) {
				const float a = static_cast<float>((a_from >> shift) & 0xFF), b = static_cast<float>((a_to >> shift) & 0xFF);
				out |= static_cast<U32>(a + (b - a) * t + 0.5f) << shift;
			}
			return out;
		}

		V Add(V a, V b) { return { a.x + b.x, a.y + b.y }; }

		bool Inside(V p, V min, V max) { return p.x >= min.x && p.y >= min.y && p.x <= max.x && p.y <= max.y; }

		// --- Text and glyphs -------------------------------------------------------------

		V TextSize(float a_size, std::string_view a_text)
		{
			const std::string text(a_text);
			const auto        raw = Im::CalcTextSize(text.c_str());
			const float       ratio = a_size / std::max(Im::GetFontSize(), 1.0f);
			return { raw.x * ratio, raw.y * ratio };
		}

		void Text(Im::ImDrawList* a_draw, V a_pos, U32 a_color, float a_size, std::string_view a_text)
		{
			const std::string text(a_text);
			Draw::AddText(a_draw, Im::GetFont(), a_size, a_pos, a_color, text.c_str());
		}

		void TextCentered(Im::ImDrawList* a_draw, V a_centre, U32 a_color, float a_size, std::string_view a_text)
		{
			const auto size = TextSize(a_size, a_text);
			Text(a_draw, { a_centre.x - size.x * 0.5f, a_centre.y - size.y * 0.5f }, a_color, a_size, a_text);
		}

		// Text with a soft dark edge, for labels drawn straight onto the world.
		void TextOnWorld(Im::ImDrawList* a_draw, V a_centre, U32 a_color, float a_size, std::string_view a_text, float a_alpha)
		{
			const auto size = TextSize(a_size, a_text);
			const V    at{ a_centre.x - size.x * 0.5f, a_centre.y - size.y * 0.5f };
			const U32  shade = Alpha(kShade, a_alpha * 0.75f);
			for (const V o : { V{ 1, 1 }, V{ -1, 1 }, V{ 0, 1.5f } }) {
				Text(a_draw, Add(at, o), shade, a_size, a_text);
			}
			Text(a_draw, at, Alpha(a_color, a_alpha), a_size, a_text);
		}

		float Paragraph(Im::ImDrawList* a_draw, V a_pos, U32 a_color, float a_size, std::string_view a_text, float a_width)
		{
			const std::string text(a_text);
			Draw::AddText(a_draw, Im::GetFont(), a_size, a_pos, a_color, text.c_str(), nullptr, a_width);
			const float ratio = a_size / std::max(Im::GetFontSize(), 1.0f);
			return Im::CalcTextSize(text.c_str(), nullptr, false, a_width / ratio).y * ratio;
		}

		// Small capitals with open tracking, for section labels.
		void Label(Im::ImDrawList* a_draw, V a_pos, U32 a_color, float a_size, std::string_view a_text, float a_tracking)
		{
			float x = a_pos.x;
			for (const char c : a_text) {
				const char glyph[2]{ c, 0 };
				Text(a_draw, { x, a_pos.y }, a_color, a_size, glyph);
				x += TextSize(a_size, glyph).x + a_tracking;
			}
		}

		// Capitals for headers. Only ASCII letters change, so UTF-8 names stay whole.
		std::string Upper(std::string_view a_text)
		{
			std::string out(a_text);
			for (auto& c : out) {
				if (c >= 'a' && c <= 'z') {
					c = static_cast<char>(c - 'a' + 'A');
				}
			}
			return out;
		}

		float LabelWidth(float a_size, std::string_view a_text, float a_tracking)
		{
			float w = 0.0f;
			for (const char c : a_text) {
				const char glyph[2]{ c, 0 };
				w += TextSize(a_size, glyph).x + a_tracking;
			}
			return w - a_tracking;
		}

		void Glyph(Im::ImDrawList* a_draw, V a_centre, float a_size, std::uint32_t a_code, U32 a_color)
		{
			const auto utf8 = FontAwesome::UnicodeToUtf8(a_code);
			FontAwesome::PushSolid();
			const auto  bounds = Im::CalcTextSize(utf8.c_str());
			const float ratio = a_size / std::max(Im::GetFontSize(), 1.0f);
			Draw::AddText(a_draw, Im::GetFont(), a_size, { a_centre.x - bounds.x * ratio * 0.5f, a_centre.y - bounds.y * ratio * 0.5f }, a_color, utf8.c_str());
			FontAwesome::Pop();
		}

		// --- Shapes ------------------------------------------------------------------------

		float uiScale = 1.0f;  // this frame's scale, for decorations drawn by helpers that take no size

		// The pair of slashes that marks a header or the chosen row.
		void Slashes(Im::ImDrawList* a_draw, V a_at, float a_height, U32 a_color)
		{
			for (int i = 0; i < 2; ++i) {
				const float x = a_at.x + static_cast<float>(i) * 6.0f * uiScale;
				Draw::AddLine(a_draw, { x, a_at.y + a_height }, { x + a_height * 0.5f, a_at.y }, a_color, 1.2f * uiScale);
			}
		}

		// A small cross where frame lines would meet.
		void Cross(Im::ImDrawList* a_draw, V a_c, float a_r, U32 a_color)
		{
			Draw::AddLine(a_draw, { a_c.x - a_r, a_c.y - a_r }, { a_c.x + a_r, a_c.y + a_r }, a_color, 1.0f);
			Draw::AddLine(a_draw, { a_c.x - a_r, a_c.y + a_r }, { a_c.x + a_r, a_c.y - a_r }, a_color, 1.0f);
		}

		// A hard, near-black plate with a hairline edge: tips, notices, small menus.
		void Glass(Im::ImDrawList* a_draw, V a_min, V a_max, float a_alpha, float)
		{
			Draw::AddRectFilled(a_draw, { a_min.x + 3.0f * uiScale, a_min.y + 6.0f * uiScale }, { a_max.x + 3.0f * uiScale, a_max.y + 8.0f * uiScale },
				Alpha(kShade, a_alpha * 0.28f), 0.0f, 0);
			Draw::AddRectFilled(a_draw, a_min, a_max, Alpha(kGlass, a_alpha), 0.0f, 0);
			Draw::AddRect(a_draw, a_min, a_max, Alpha(kGlassEdge, a_alpha), 0.0f, 0, 1.0f);
		}

		void Diamond(Im::ImDrawList* a_draw, V c, float r, U32 a_fill, U32 a_edge, float a_thickness);

		// A small diamond set into a frame line.
		void Stud(Im::ImDrawList* a_draw, V a_c, float a_r, float a_alpha)
		{
			Diamond(a_draw, a_c, a_r, Alpha(kGlass, std::min(a_alpha * 1.1f, 1.0f)), Alpha(kText, a_alpha * 0.6f), 1.0f);
		}

		// A red gem set into a frame line, with short ticks running out from it.
		void Gem(Im::ImDrawList* a_draw, V a_c, float a_r, float a_alpha)
		{
			const float s = uiScale;
			for (const float side : { -1.0f, 1.0f }) {
				Draw::AddLine(a_draw, { a_c.x + side * (a_r + 4.0f * s), a_c.y }, { a_c.x + side * (a_r + 34.0f * s), a_c.y }, Alpha(kEdge, a_alpha * 0.7f), 1.0f);
				Draw::AddLine(a_draw, { a_c.x + side * (a_r + 38.0f * s), a_c.y - 3.0f * s }, { a_c.x + side * (a_r + 38.0f * s), a_c.y + 3.0f * s },
					Alpha(kText, a_alpha * 0.5f), 1.0f);
			}
			Diamond(a_draw, a_c, a_r + 2.0f * s, Alpha(kGlass, a_alpha), Alpha(kText, a_alpha * 0.55f), 1.0f);
			Diamond(a_draw, a_c, a_r - 0.5f * s, Alpha(kAccent, a_alpha), Alpha(kEdge, a_alpha), 1.0f);
		}

		// A main panel: the plate inside a double frame, heavier brackets at the corners with
		// a stud on each, a red gem in the top edge, the outer line running on past two corners,
		// a small cross off a third and a faint drafting X in the fourth.
		void Frame(Im::ImDrawList* a_draw, V a_min, V a_max, float a_alpha)
		{
			Glass(a_draw, a_min, a_max, a_alpha, 0.0f);
			const float o = 14.0f * uiScale;
			const U32   edge = Alpha(kGlassEdge, a_alpha);
			// The inner line of the double frame.
			const float in = 5.0f * uiScale;
			Draw::AddRect(a_draw, { a_min.x + in, a_min.y + in }, { a_max.x - in, a_max.y - in }, Alpha(kGlassEdge, a_alpha * 0.5f), 0.0f, 0, 1.0f);
			// Corner brackets, heavier and lighter than the frame, a stud where they meet.
			const float l = 20.0f * uiScale, t = 2.0f * uiScale;
			const U32   metal = Alpha(kText, a_alpha * 0.5f);
			const std::array<std::pair<V, V>, 4> corners{ { { a_min, { 1.0f, 1.0f } }, { { a_max.x, a_min.y }, { -1.0f, 1.0f } }, { { a_min.x, a_max.y }, { 1.0f, -1.0f } },
				{ a_max, { -1.0f, -1.0f } } } };
			for (const auto& [c, d] : corners) {
				Draw::AddLine(a_draw, c, { c.x + d.x * l, c.y }, metal, t);
				Draw::AddLine(a_draw, c, { c.x, c.y + d.y * l }, metal, t);
				Stud(a_draw, c, 3.6f * uiScale, a_alpha);
			}
			if (a_max.x - a_min.x > 160.0f * uiScale) {
				Gem(a_draw, { (a_min.x + a_max.x) * 0.5f, a_min.y }, 4.5f * uiScale, a_alpha);
				Stud(a_draw, { (a_min.x + a_max.x) * 0.5f, a_max.y }, 3.0f * uiScale, a_alpha);
			}
			Draw::AddLine(a_draw, { a_min.x - o, a_min.y }, a_min, edge, 1.0f);
			Draw::AddLine(a_draw, { a_min.x, a_min.y - o }, a_min, edge, 1.0f);
			Draw::AddLine(a_draw, a_max, { a_max.x + o, a_max.y }, edge, 1.0f);
			Draw::AddLine(a_draw, a_max, { a_max.x, a_max.y + o }, edge, 1.0f);
			Cross(a_draw, { a_min.x - o * 0.6f, a_max.y + o * 0.6f }, 4.0f * uiScale, Alpha(kGlassEdge, std::min(a_alpha * 1.6f, 1.0f)));
			const float x = std::min(64.0f * uiScale, (a_max.x - a_min.x) * 0.25f);
			const U32   faint = Alpha(kGlassEdge, a_alpha * 0.4f);
			Draw::AddLine(a_draw, { a_max.x - x, a_min.y }, { a_max.x, a_min.y + x }, faint, 1.0f);
			Draw::AddLine(a_draw, { a_max.x - x, a_min.y + x }, { a_max.x, a_min.y }, faint, 1.0f);
		}

		// An oxblood band fading to the right: where a header sits.
		void Band(Im::ImDrawList* a_draw, V a_min, V a_max, float a_alpha)
		{
			Draw::AddRectFilledMultiColor(a_draw, a_min, a_max, Alpha(kAccent, a_alpha * 0.9f), Alpha(kAccent, 0.0f), Alpha(kAccent, 0.0f),
				Alpha(kAccent, a_alpha * 0.9f));
		}

		// The chosen row: an oxblood bar fading right, a bright edge on its left, a red line
		// running on past its end, and the slashes.
		void Highlight(Im::ImDrawList* a_draw, V a_min, V a_max, float a_alpha)
		{
			if (a_alpha <= 0.01f) {
				return;
			}
			Draw::AddRectFilledMultiColor(a_draw, a_min, a_max, Alpha(kAccent, a_alpha * 0.9f), Alpha(kAccent, a_alpha * 0.16f), Alpha(kAccent, a_alpha * 0.16f),
				Alpha(kAccent, a_alpha * 0.9f));
			Draw::AddRectFilled(a_draw, a_min, { a_min.x + 2.0f * uiScale, a_max.y }, Alpha(kEdge, a_alpha), 0.0f, 0);
			Draw::AddLine(a_draw, { a_min.x, a_max.y }, { a_max.x + 12.0f * uiScale, a_max.y }, Alpha(kEdge, a_alpha * 0.6f), 1.0f);
			const float h = a_max.y - a_min.y;
			Slashes(a_draw, { a_max.x - 24.0f * uiScale, a_min.y + h * 0.28f }, h * 0.44f, Alpha(kText, a_alpha * 0.55f));
		}

		// The chosen tile: oxblood rising from its floor, a bright line along the floor.
		void Lit(Im::ImDrawList* a_draw, V a_min, V a_max, float a_alpha)
		{
			Draw::AddRectFilledMultiColor(a_draw, a_min, a_max, Alpha(kAccent, a_alpha * 0.06f), Alpha(kAccent, a_alpha * 0.06f), Alpha(kAccent, a_alpha * 0.78f),
				Alpha(kAccent, a_alpha * 0.78f));
			Draw::AddRectFilled(a_draw, { a_min.x, a_max.y - 2.0f * uiScale }, a_max, Alpha(kEdge, a_alpha), 0.0f, 0);
		}

		// A key in its own square cap; returns the cap's width.
		float Keycap(Im::ImDrawList* a_draw, V a_at, float a_height, float a_size, std::string_view a_key, float a_alpha)
		{
			const float w = std::max(a_height, TextSize(a_size, a_key).x + 12.0f * uiScale);
			Draw::AddRectFilled(a_draw, a_at, { a_at.x + w, a_at.y + a_height }, Alpha(IM_COL32(22, 22, 23, 235), a_alpha), 0.0f, 0);
			Draw::AddRect(a_draw, a_at, { a_at.x + w, a_at.y + a_height }, Alpha(kGlassEdge, a_alpha * 1.4f), 0.0f, 0, 1.0f);
			TextCentered(a_draw, { a_at.x + w * 0.5f, a_at.y + a_height * 0.5f }, Alpha(kText, a_alpha), a_size, a_key);
			return w;
		}

		void Halo(Im::ImDrawList* a_draw, V a_centre, float a_radius, U32 a_color, float a_strength)
		{
			if (a_strength <= 0.01f) {
				return;
			}
			for (int i = 4; i >= 1; --i) {
				Draw::AddCircleFilled(a_draw, a_centre, a_radius * (0.55f + 0.14f * i), Alpha(a_color, a_strength * 0.07f), 40);
			}
		}

		void Bar(Im::ImDrawList* a_draw, V a_min, float a_width, float a_height, float a_fraction, U32 a_color, float a_alpha)
		{
			Draw::AddRectFilled(a_draw, a_min, { a_min.x + a_width, a_min.y + a_height }, Alpha(IM_COL32(255, 255, 255, 20), a_alpha), 0.0f, 0);
			if (a_fraction > 0.003f) {
				Draw::AddRectFilled(a_draw, a_min, { a_min.x + a_width * Clamp01(a_fraction), a_min.y + a_height }, Alpha(a_color, a_alpha), 0.0f, 0);
			}
		}

		void Diamond(Im::ImDrawList* a_draw, V c, float r, U32 a_fill, U32 a_edge, float a_thickness)
		{
			const V top{ c.x, c.y - r }, right{ c.x + r, c.y }, bottom{ c.x, c.y + r }, left{ c.x - r, c.y };
			Draw::AddQuadFilled(a_draw, top, right, bottom, left, a_fill);
			Draw::AddQuad(a_draw, top, right, bottom, left, a_edge, a_thickness);
		}

		// Dashes that march from a to b.
		void Marching(Im::ImDrawList* a_draw, V a, V b, U32 a_color, float a_thickness, float a_dash, float a_gap, float a_offset)
		{
			const float dx = b.x - a.x, dy = b.y - a.y;
			const float length = std::sqrt(dx * dx + dy * dy);
			if (length < 1.0f) {
				return;
			}
			const float ux = dx / length, uy = dy / length;
			const float period = a_dash + a_gap;
			for (float d = std::fmod(a_offset, period) - period; d < length; d += period) {
				const float s0 = std::max(d, 0.0f), s1 = std::min(d + a_dash, length);
				if (s1 > s0) {
					Draw::AddLine(a_draw, { a.x + ux * s0, a.y + uy * s0 }, { a.x + ux * s1, a.y + uy * s1 }, a_color, a_thickness);
				}
			}
		}

		bool Hit(const char* a_id, V a_pos, V a_size, bool& a_hovered)
		{
			Im::SetCursorScreenPos(a_pos);
			const bool clicked = Im::InvisibleButton(a_id, a_size);
			a_hovered = Im::IsItemHovered();
			return clicked;
		}

		U32 DangerColour(Rules::Danger a_danger)
		{
			switch (a_danger) {
			case Rules::Danger::kTrivial:
				return IM_COL32(128, 128, 122, 255);
			case Rules::Danger::kEasy:
				return IM_COL32(156, 192, 146, 255);
			case Rules::Danger::kEven:
				return IM_COL32(236, 230, 214, 255);
			case Rules::Danger::kHard:
				return IM_COL32(236, 172, 72, 255);
			default:
				return IM_COL32(232, 74, 60, 255);
			}
		}

		std::string_view DangerWord(Rules::Danger a_danger)
		{
			switch (a_danger) {
			case Rules::Danger::kTrivial:
				return "no threat";
			case Rules::Danger::kEasy:
				return "easy";
			case Rules::Danger::kEven:
				return "an even match";
			case Rules::Danger::kHard:
				return "hard";
			default:
				return "deadly";
			}
		}

		std::uint32_t EnemyGlyph(const View::Enemy& a_enemy)
		{
			if (a_enemy.boss) {
				return 0xF521;
			}
			if (a_enemy.healer) {
				return 0xF21E;
			}
			if (a_enemy.caster) {
				return 0xE2CA;
			}
			if (a_enemy.archer) {
				return 0xF05B;
			}
			return 0xF6DE;
		}

		std::string Describe(const View::Enemy& a_enemy)
		{
			std::string kinds;
			auto        add = [&](std::string_view a_kind) {
                kinds += kinds.empty() ? "" : ", ";
                kinds += a_kind;
			};
			if (a_enemy.boss) {
				add("boss");
			}
			if (a_enemy.caster) {
				add("caster");
			}
			if (a_enemy.healer) {
				add("healer");
			}
			if (a_enemy.archer) {
				add("archer");
			}
			return kinds.empty() ? "warrior" : kinds;
		}

		U32 RoleAccent(Role a_role)
		{
			switch (a_role) {
			case Role::kTank:
				return kRed;
			case Role::kHealer:
				return kGreen;
			default:
				return kEdge;
			}
		}

		// --- State -------------------------------------------------------------------------

		enum Section : int
		{
			kRoleSection,
			kFocusSection,
			kPositionSection,
			kAbilitySection,
			kFieldSection,
			kOrdersSection,
			kSectionCount
		};

		struct State
		{
			RE::FormID selected{ 0 };
			RE::FormID shown{ 0 };  // the companion the card is showing while it changes over
			float      swap{ 1.0f };
			int        section{ kRoleSection };
			int        item{ 0 };
			bool       keyboardNav{ false };
			RE::FormID fieldEnemy{ 0 };
			float      open{ 0.0f };  // 0 closed, 1 fully open; eased into `appear`
			bool       closing{ false };
			float      clock{ 0.0f };
			Tweens     tw;

			std::string tip;
			bool        tipShown{ false };
			float       tipFade{ 0.0f };
			bool        tipAnchored{ false };  // beside an element instead of the mouse
			V           tipMin, tipMax;

			// What the pointer or pad focus is on, this frame and the last: moving onto
			// something new ticks.
			std::uint64_t hoverKey{ 0 };
			std::uint64_t hoverKeyLast{ 0 };
			float         lastTick{ 0.0f };

			// The enemy whose "send whom" menu is open.
			RE::FormID enemyMenu{ 0 };
			float      enemyMenuT{ 0.0f };
			float      enemyMenuOpenedAt{ 0.0f };
			V          enemyMin, enemyMax;
		};

		State state;

		void Tip(std::string a_text)
		{
			if (!Settings::Get().tooltips) {
				return;
			}
			state.tip = std::move(a_text);
			state.tipShown = true;
			state.tipAnchored = false;
		}

		// Points the pointer or the pad is on, so moving onto something new can tick.
		void Hovering(std::uint64_t a_key) { state.hoverKey = a_key; }

		// For keyboard and pad focus: the tip sits beside the element, not the mouse.
		void TipBeside(std::string a_text, V a_min, V a_max)
		{
			Tip(std::move(a_text));
			state.tipAnchored = true;
			state.tipMin = a_min;
			state.tipMax = a_max;
		}

		const View::Member* Selected(const View::Snapshot& a_view)
		{
			const View::Member* first = nullptr;
			for (const auto& member : a_view.party) {
				if (member.summon) {
					continue;
				}
				if (!first) {
					first = &member;
				}
				if (member.id == state.selected) {
					return &member;
				}
			}
			if (first) {
				state.selected = first->id;
			}
			return first;
		}

		void StepMember(const View::Snapshot& a_view, int a_step)
		{
			std::vector<RE::FormID> ids;
			for (const auto& member : a_view.party) {
				if (!member.summon) {
					ids.push_back(member.id);
				}
			}
			if (ids.empty()) {
				return;
			}
			const auto it = std::find(ids.begin(), ids.end(), state.selected);
			int        index = it == ids.end() ? 0 : static_cast<int>(it - ids.begin());
			index = (index + a_step + static_cast<int>(ids.size())) % static_cast<int>(ids.size());
			state.selected = ids[static_cast<std::size_t>(index)];
		}

		void SetOrders(const View::Member& a_member, const Orders& a_orders)
		{
			Battle::Command command;
			command.type = Battle::Command::Type::kSetOrders;
			command.member = a_member.id;
			command.orders = a_orders;
			Battle::Queue(command);
		}

		void Simple(Battle::Command::Type a_type, RE::FormID a_member, RE::FormID a_enemy = 0)
		{
			Battle::Command command;
			command.type = a_type;
			command.member = a_member;
			command.enemy = a_enemy;
			Battle::Queue(command);
		}

		void Preset(Battle::Preset a_preset)
		{
			Battle::Command command;
			command.type = Battle::Command::Type::kPreset;
			command.preset = a_preset;
			Battle::Queue(command);
		}

		bool FocusAllowed(Focus a_focus, const View::Member& a_member)
		{
			return a_focus != Focus::kMarked || a_member.marked != 0 || a_member.orders.focus == Focus::kMarked;
		}

		bool PositionAllowed(Position a_position, const View::Snapshot& a_view)
		{
			return a_position != Position::kFormation || a_view.walkWithMe;
		}

		// Keyboard and pad focus: four white corner brackets, breathing.
		void FocusOutline(Im::ImDrawList* a_draw, V a_min, V a_max, float, float a_alpha)
		{
			const float pulse = 0.6f + 0.4f * std::sin(state.clock * 5.0f);
			const U32   c = Alpha(kFocusRing, a_alpha * pulse);
			const V     mn{ a_min.x - 5.0f * uiScale, a_min.y - 5.0f * uiScale }, mx{ a_max.x + 5.0f * uiScale, a_max.y + 5.0f * uiScale };
			const float l = std::min(12.0f * uiScale, (mx.x - mn.x) * 0.3f);
			const float t = 1.6f * uiScale;
			Draw::AddLine(a_draw, mn, { mn.x + l, mn.y }, c, t);
			Draw::AddLine(a_draw, mn, { mn.x, mn.y + l }, c, t);
			Draw::AddLine(a_draw, { mx.x, mn.y }, { mx.x - l, mn.y }, c, t);
			Draw::AddLine(a_draw, { mx.x, mn.y }, { mx.x, mn.y + l }, c, t);
			Draw::AddLine(a_draw, { mn.x, mx.y }, { mn.x + l, mx.y }, c, t);
			Draw::AddLine(a_draw, { mn.x, mx.y }, { mn.x, mx.y - l }, c, t);
			Draw::AddLine(a_draw, mx, { mx.x - l, mx.y }, c, t);
			Draw::AddLine(a_draw, mx, { mx.x, mx.y - l }, c, t);
		}

		struct Rect
		{
			V min, max;
		};

		bool Blocked(V a_point, std::span<const Rect> a_panels)
		{
			for (const auto& r : a_panels) {
				if (Inside(a_point, r.min, r.max)) {
					return true;
				}
			}
			return false;
		}

		V ScreenOf(const View::Point& a_point, V a_display) { return { a_point.x * a_display.x, a_point.y * a_display.y }; }

		// --- The battlefield -------------------------------------------------------------------

		void Vignette(Im::ImDrawList* a_draw, V a_display, float a_alpha)
		{
			const U32   clear = IM_COL32(0, 0, 0, 0);
			const U32   dark = Alpha(IM_COL32(5, 5, 6, 200), a_alpha);
			const float edge = a_display.y * 0.26f;
			Draw::AddRectFilled(a_draw, { 0, 0 }, a_display, Alpha(IM_COL32(10, 8, 8, 80), a_alpha), 0.0f, 0);
			Draw::AddRectFilledMultiColor(a_draw, { 0, 0 }, { a_display.x, edge }, dark, dark, clear, clear);
			Draw::AddRectFilledMultiColor(a_draw, { 0, a_display.y - edge * 1.2f }, a_display, clear, clear, dark, dark);
			Draw::AddRectFilledMultiColor(a_draw, { 0, 0 }, { edge, a_display.y }, dark, clear, clear, dark);
			Draw::AddRectFilledMultiColor(a_draw, { a_display.x - edge, 0 }, a_display, clear, dark, dark, clear);
		}

		void DrawWorld(Im::ImDrawList* a_draw, const View::Snapshot& a_view, const View::Member* a_selected, V a_display, float a_s, float a_appear,
			std::span<const Rect> a_panels)
		{
			const float alpha = Stage(a_appear, 0.1f, 0.6f);
			if (alpha <= 0.01f) {
				return;
			}
			auto findEnemy = [&](RE::FormID a_id) -> const View::Enemy* {
				for (const auto& e : a_view.enemies) {
					if (e.id == a_id) {
						return &e;
					}
				}
				return nullptr;
			};
			auto findMember = [&](RE::FormID a_id) -> const View::Member* {
				for (const auto& m : a_view.party) {
					if (m.id == a_id) {
						return &m;
					}
				}
				return nullptr;
			};
			const RE::FormID chosenId = a_selected ? a_selected->id : 0;

			// Taunt reach on the ground: bright and breathing for the chosen tank, faint for others.
			for (const auto& member : a_view.party) {
				if (member.ring.size() < 2) {
					continue;
				}
				const bool  chosen = member.id == chosenId;
				const float strength = state.tw.Toward("ring", member.id, chosen ? 1.0f : 0.25f, 8.0f);
				const float breathe = 0.8f + 0.2f * std::sin(state.clock * 2.4f);
				const float reveal = Stage(a_appear, 0.25f, 0.5f);
				const std::size_t drawn = static_cast<std::size_t>(static_cast<float>(member.ring.size() - 1) * reveal);
				for (std::size_t i = 1; i <= drawn; ++i) {
					const auto& a = member.ring[i - 1];
					const auto& b = member.ring[i];
					if (!a.on || !b.on) {
						continue;
					}
					const V pa = ScreenOf(a, a_display), pb = ScreenOf(b, a_display);
					Draw::AddLine(a_draw, pa, pb, Alpha(kRed, alpha * strength * 0.16f * breathe), 7.0f * a_s);
					Draw::AddLine(a_draw, pa, pb, Alpha(kRed, alpha * strength * 0.75f * breathe), 1.6f * a_s);
				}
			}

			// Who is on whom: marching dashes for the chosen companion, quiet lines for the rest.
			for (const auto& member : a_view.party) {
				const auto* target = member.target ? findEnemy(member.target) : nullptr;
				if (!target || !member.head.on || !target->head.on) {
					continue;
				}
				const V    a = ScreenOf(member.head, a_display), b = ScreenOf(target->head, a_display);
				const bool chosen = member.id == chosenId;
				if (chosen) {
						Draw::AddLine(a_draw, a, b, Alpha(kAccent, alpha * 0.3f), 5.0f * a_s);
						Marching(a_draw, a, b, Alpha(kEdge, alpha * 0.95f), 2.0f * a_s, 9.0f * a_s, 7.0f * a_s, state.clock * 42.0f * a_s);
				} else {
					Draw::AddLine(a_draw, a, b, Alpha(kTextDim, alpha * 0.28f), 1.2f * a_s);
				}
			}
			for (const auto& enemy : a_view.enemies) {
				const auto* tank = enemy.heldBy ? findMember(enemy.heldBy) : nullptr;
				if (tank && tank->head.on && enemy.head.on) {
					Marching(a_draw, ScreenOf(enemy.head, a_display), ScreenOf(tank->head, a_display), Alpha(kRed, alpha * 0.45f), 1.2f * a_s, 4.0f * a_s,
						6.0f * a_s, state.clock * 30.0f * a_s);
				}
			}

			// Companions.
			for (const auto& member : a_view.party) {
				if (!member.head.on || member.summon) {
					continue;
				}
				const V     p = ScreenOf(member.head, a_display);
				const float chosen = state.tw.Toward("worldChosen", member.id, member.id == chosenId ? 1.0f : 0.0f, 10.0f);
				const float r = (10.0f + 3.0f * chosen) * a_s;
				Halo(a_draw, p, r * 2.4f, kAccent, chosen * (0.7f + 0.3f * std::sin(state.clock * 3.0f)) * alpha);
					Diamond(a_draw, p, r * 1.25f, Alpha(Mix(IM_COL32(16, 16, 17, 245), IM_COL32(64, 16, 15, 248), chosen), alpha), Alpha(Mix(kTextDim, kEdge, chosen), alpha),
						(1.2f + 0.6f * chosen) * a_s);
					Glyph(a_draw, p, r * 0.92f, Info(member.orders.role).glyph, Alpha(Mix(kTextDim, kText, chosen), alpha));
					TextOnWorld(a_draw, { p.x, p.y - r * 1.25f - 11.0f * a_s }, Mix(kTextDim, kText, chosen), 14.5f * a_s, member.name, alpha);
			}

			// Enemies pop in one by one; click one to mark it for the chosen companion.
			const int ourLevel = a_selected ? a_selected->level : a_view.playerLevel;
			int       index = 0;
			for (const auto& enemy : a_view.enemies) {
				const int order = index++;
				if (!enemy.head.on) {
					continue;
				}
				const V p = ScreenOf(enemy.head, a_display);
				if (Blocked(p, a_panels)) {
					continue;
				}
				const float pop = EaseOutBack((a_appear - 0.3f - 0.05f * order) / 0.35f);
				if (pop <= 0.01f) {
					continue;
				}
				const auto danger = Rules::DangerFor(ourLevel, enemy.level, enemy.boss, a_view.tuning);
				const U32  rim = DangerColour(danger);
				const bool marked = a_selected && a_selected->marked == enemy.id;
				const bool targeted = a_selected && a_selected->target == enemy.id;
				const bool focused = state.keyboardNav && state.section == kFieldSection && state.fieldEnemy == enemy.id;

				bool hovered = false;
				Im::PushID(order);
				const float hit = 22.0f * a_s;
				const bool underMenu = state.enemyMenu && Inside(Im::GetIO()->MousePos, state.enemyMin, state.enemyMax);
				if (Hit("enemy", { p.x - hit, p.y - hit }, { hit * 2, hit * 2 }, hovered) && !underMenu && !state.closing) {
					// Who to send is asked beside the enemy.
					state.enemyMenu = state.enemyMenu == enemy.id ? 0 : enemy.id;
					state.enemyMenuT = 0.0f;
					state.enemyMenuOpenedAt = state.clock;
					Sound("UIMenuFocus");
				}
				hovered = hovered && !underMenu;
				Im::PopID();
				if (hovered) {
					Hovering(0x500 + static_cast<std::uint64_t>(order));
				}
				const float lift = state.tw.Toward("enemyHover", enemy.id, hovered || focused ? 1.0f : 0.0f, 14.0f);
				const float r = (12.0f + 4.0f * lift) * a_s * pop;

				if (enemy.heldBy) {
					const float pulse = 0.5f + 0.5f * std::sin(state.clock * 6.0f + static_cast<float>(order));
					Draw::AddCircle(a_draw, p, r + (7.0f + 4.0f * pulse) * a_s, Alpha(kRed, alpha * (0.35f + 0.4f * pulse)), 40, 1.8f * a_s);
				}
				if (targeted) {
					Halo(a_draw, p, r * 2.2f, kAccent, alpha * 0.55f);
				}
				Diamond(a_draw, p, r * 1.15f, Alpha(IM_COL32(16, 17, 19, 245), alpha), Alpha(rim, alpha), (1.6f + lift) * a_s);
				Glyph(a_draw, p, r * 0.95f, EnemyGlyph(enemy), Alpha(rim, alpha));
				if (focused) {
					Draw::AddCircle(a_draw, p, r + 9.0f * a_s, Alpha(kFocusRing, alpha * (0.65f + 0.35f * std::sin(state.clock * 5.0f))), 40, 1.6f * a_s);
				}
				TextOnWorld(a_draw, { p.x, p.y - r * 1.15f - 11.0f * a_s }, rim, 14.0f * a_s, std::format("{}", enemy.level), alpha * pop);
				Bar(a_draw, { p.x - 16.0f * a_s, p.y + r * 1.15f + 6.0f * a_s }, 32.0f * a_s, 3.0f * a_s, enemy.health, kHealth, alpha * pop);
				if (marked) {
					const float drop = state.tw.Toward("flag", enemy.id, 1.0f, 9.0f);
					Glyph(a_draw, { p.x + r + 10.0f * a_s, p.y - r - (14.0f - 10.0f * drop) * a_s }, 17.0f * a_s, 0xF024, Alpha(kEdge, alpha * drop));
				} else {
					state.tw.Raw("flag", enemy.id, 0.0f) = 0.0f;
				}
				if (hovered || focused || targeted || enemy.boss) {
					const float nameAlpha = std::max(lift, targeted || enemy.boss ? 0.8f : 0.0f);
					TextOnWorld(a_draw, { p.x, p.y + r * 1.15f + 22.0f * a_s }, targeted ? kEdge : kText, 15.0f * a_s, enemy.name, alpha * nameAlpha);
				}
				if (hovered) {
					std::string tip = std::format("{}, level {}\n{}, {} for {}.", enemy.name, enemy.level, Describe(enemy), DangerWord(danger),
						a_selected ? a_selected->name : "you"s);
					if (const auto* tank = enemy.heldBy ? findMember(enemy.heldBy) : nullptr) {
						tip += std::format("\nHeld by {}'s taunt.", tank->name);
					}
					tip += "\nClick to send someone after them.";
					if (!state.enemyMenu) {
						Tip(std::move(tip));
					}
				}
			}
		}

		// --- Companion strip -------------------------------------------------------------------

		Rect DrawStrip(Im::ImDrawList* a_draw, const View::Snapshot& a_view, const View::Member* a_selected, V a_display, float a_s, float a_appear)
		{
			std::vector<const View::Member*> companions, summons;
			for (const auto& m : a_view.party) {
				(m.summon ? summons : companions).push_back(&m);
			}
			const float spacing = 118.0f * a_s;
			const float smallSpacing = 64.0f * a_s;
			const float total = spacing * std::max<float>(static_cast<float>(companions.size()) - 1.0f, 0.0f) +
			                    (summons.empty() ? 0.0f : 40.0f * a_s + smallSpacing * static_cast<float>(summons.size()));
			const float startX = a_display.x * 0.5f - total * 0.5f;
			const float baseY = a_display.y - 136.0f * a_s;

			if (companions.empty()) {
				TextCentered(a_draw, { a_display.x * 0.5f, baseY }, Alpha(kTextDim, Stage(a_appear, 0.2f, 0.5f)), 16.0f * a_s,
					"No companions in reach. Followers nearby, Walk With Me's party and summons appear here.");
				return { { 0, baseY - 40.0f * a_s }, { a_display.x, a_display.y } };
			}

			// A hairline the crests sit on, running past both ends, a cross at each.
			const float lineIn = Stage(a_appear, 0.1f, 0.5f);
			const float half = (total * 0.5f + 90.0f * a_s) * lineIn;
			const float lineY = baseY + 88.0f * a_s;  // under both lines of text, above the key hints
			Draw::AddLine(a_draw, { a_display.x * 0.5f - half, lineY }, { a_display.x * 0.5f + half, lineY }, Alpha(kGlassEdge, lineIn), 1.0f);
			Cross(a_draw, { a_display.x * 0.5f - half - 8.0f * a_s, lineY }, 4.0f * a_s, Alpha(kGlassEdge, lineIn * 1.6f));
			Cross(a_draw, { a_display.x * 0.5f + half + 8.0f * a_s, lineY }, 4.0f * a_s, Alpha(kGlassEdge, lineIn * 1.6f));
			Stud(a_draw, { a_display.x * 0.5f - half, lineY }, 3.0f * a_s, lineIn);
			Stud(a_draw, { a_display.x * 0.5f + half, lineY }, 3.0f * a_s, lineIn);

			float selectedX = startX;
			for (std::size_t i = 0; i < companions.size(); ++i) {
				const auto& m = *companions[i];
				const float enter = Stage(a_appear, 0.12f + 0.06f * static_cast<float>(i), 0.45f);
				const float x = startX + spacing * static_cast<float>(i);
				const bool  chosen = a_selected && m.id == a_selected->id;
				if (chosen) {
					selectedX = x;
				}
				const float sel = state.tw.Toward("crest", m.id, chosen ? 1.0f : 0.0f, 11.0f);
				bool        hovered = false;
				const float R = (30.0f + 5.0f * sel) * a_s;
				const V     c{ x, baseY - 8.0f * a_s * sel + (1.0f - enter) * 36.0f * a_s };
				Im::PushID(static_cast<int>(1000 + i));
				if (Hit("crest", { x - 46.0f * a_s, baseY - 52.0f * a_s }, { 92.0f * a_s, 110.0f * a_s }, hovered) && !state.closing) {
					state.selected = m.id;
				}
				Im::PopID();
				if (hovered) {
					Hovering(0x300 + i);
				}
				const float hover = state.tw.Toward("crestHover", m.id, hovered ? 1.0f : 0.0f, 14.0f);
				const float a = enter;
				const float pulse = m.down ? 0.7f + 0.3f * std::sin(state.clock * 6.0f) : 1.0f;

				Halo(a_draw, c, R * 1.8f, m.down ? kRed : kAccent, (sel * 0.9f + hover * 0.3f) * a * pulse);
				Diamond(a_draw, { c.x + 2.0f * a_s, c.y + 5.0f * a_s }, R, Alpha(kShade, a * 0.35f), Alpha(kShade, 0.0f), 1.0f);
				Diamond(a_draw, c, R, Alpha(Mix(IM_COL32(17, 17, 18, 245), IM_COL32(60, 15, 14, 248), sel), a),
					Alpha(m.down ? kRed : Mix(kGlassEdge, kEdge, std::max(sel, hover * 0.5f)), a * (m.down ? pulse : 1.0f)), (1.0f + 0.8f * sel) * a_s);
				if (sel > 0.01f) {
					Diamond(a_draw, c, R - 5.0f * a_s, Alpha(kShade, 0.0f), Alpha(kEdge, a * sel * 0.45f), 1.0f);
				}
				Glyph(a_draw, c, R * 0.62f * (1.0f + 0.06f * hover), Info(m.orders.role).glyph, Alpha(Mix(kTextDim, kText, std::max(sel, hover * 0.6f)), a));

				// Ready to act: a small red diamond at the crest's top point; charging, it fills.
				const bool tank = m.orders.role == Role::kTank, healer = m.orders.role == Role::kHealer;
				if (tank || healer) {
					const float cooldown = tank ? m.tauntCooldown : a_view.tuning.healCooldown;
					const float ready = tank ? m.tauntReady : m.healReady;
					const float charge = cooldown > 0.0f && ready > 0.0f ? 1.0f - ready / cooldown : 1.0f;
					const U32   accent = RoleAccent(m.orders.role);
					const V     pip{ c.x, c.y - R - 2.0f * a_s };
					const float glow = charge >= 1.0f ? 0.6f + 0.4f * std::sin(state.clock * 3.0f) : 0.35f + 0.65f * charge;
					Diamond(a_draw, pip, 5.0f * a_s, Alpha(accent, a * glow), Alpha(kShade, a * 0.6f), 1.0f);
				}

				// Health: a thin bar under the crest.
				const float health = state.tw.Toward("crestHealth", m.id, m.health, 6.0f);
				Bar(a_draw, { x - 26.0f * a_s, c.y + R + 8.0f * a_s }, 52.0f * a_s, 3.0f * a_s, health, health < 0.35f ? kRed : kHealth, a);

				const float ny = baseY + 52.0f * a_s + (1.0f - enter) * 36.0f * a_s;
				TextCentered(a_draw, { x, ny }, Alpha(m.down ? kRed : Mix(kTextDim, kText, std::max(sel, hover)), a), 17.0f * a_s, m.name);
				TextCentered(a_draw, { x, ny + 19.0f * a_s }, Alpha(m.down ? kRed : kTextFaint, a), 13.0f * a_s,
					m.down ? "DOWN"s : Upper(std::format("{} / {}", Info(m.orders.role).name, Info(m.resolved).name)));
			}

			// A short red rule glides along the line under the chosen name.
			const float rx = state.tw.Toward("stripRule", 0, selectedX, 12.0f);
			const float ruleAlpha = Stage(a_appear, 0.3f, 0.4f);
			Draw::AddRectFilled(a_draw, { rx - 26.0f * a_s, lineY - 1.0f * a_s }, { rx + 26.0f * a_s, lineY + 1.0f * a_s }, Alpha(kEdge, ruleAlpha), 0.0f, 0);
			Diamond(a_draw, { rx, lineY }, 4.0f * a_s, Alpha(kAccent, ruleAlpha), Alpha(kEdge, ruleAlpha), 1.0f);

			// Summons sit to the side, smaller: they are protected and healed, not commanded.
			float sx = startX + spacing * static_cast<float>(companions.size() - 1) + 40.0f * a_s + smallSpacing * 0.5f;
			for (std::size_t i = 0; i < summons.size(); ++i) {
				const auto& m = *summons[i];
				const float enter = Stage(a_appear, 0.2f + 0.06f * static_cast<float>(companions.size() + i), 0.45f);
				const V     c{ sx, baseY + (1.0f - enter) * 36.0f * a_s };
				const float R = 18.0f * a_s;
				Diamond(a_draw, c, R, Alpha(IM_COL32(17, 17, 18, 220), enter * 0.85f), Alpha(kGlassEdge, enter), 1.0f);
				Glyph(a_draw, c, R * 0.8f, 0xF0D0, Alpha(kTextFaint, enter));
				Bar(a_draw, { sx - 16.0f * a_s, c.y + R + 6.0f * a_s }, 32.0f * a_s, 2.0f * a_s, m.health, kHealth, enter * 0.7f);
				TextCentered(a_draw, { sx, c.y + 38.0f * a_s }, Alpha(kTextFaint, enter), 13.0f * a_s, m.name);
				sx += smallSpacing;
			}

			const float left = startX - 70.0f * a_s, right = std::max(sx, startX + total) + 70.0f * a_s;
			return { { left, baseY - 64.0f * a_s }, { right, baseY + 96.0f * a_s } };
		}

		// --- Small helpers ---------------------------------------------------------------------

		std::string KeyName(std::uint32_t a_key)
		{
			if (a_key == 0) {
				return "Unbound";
			}
			wchar_t    name[64]{};
			const LONG param = static_cast<LONG>((a_key & 0x7F) << 16 | (a_key & 0x80 ? 1 << 24 : 0));
			if (::GetKeyNameTextW(param, name, static_cast<int>(std::size(name))) > 0) {
				const int   size = ::WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
				std::string out(static_cast<std::size_t>(std::max(size - 1, 0)), '\0');
				::WideCharToMultiByte(CP_UTF8, 0, name, -1, out.data(), size, nullptr, nullptr);
				return out;
			}
			return std::format("Key {}", a_key);
		}

		struct Input
		{
			int  dx{ 0 };
			bool activate{ false };
			bool now{ false };
			bool flipAuto{ false };
		};

		// Party orders as the bar shows them, and as the numpad keys bind them.
		constexpr std::array<Shout, 4> orderShouts{ { Shout::kAttack, Shout::kToMe, Shout::kHoldBack, Shout::kNone } };
		constexpr std::array<std::uint32_t, 4> orderGlyphs{ { 0xF140, 0xF505, 0xF2EA, 0xF0C0 } };

		// The targets the stepper walks through, in plain words.
		constexpr std::array<Focus, 7> focusChoices{ { Focus::kEngine, Focus::kPlayerTarget, Focus::kAttackingPlayer, Focus::kAttackingParty, Focus::kBackLine,
			Focus::kStrongest, Focus::kWeakest } };

		int FocusSlot(Focus a_focus)
		{
			for (std::size_t i = 0; i < focusChoices.size(); ++i) {
				if (focusChoices[i] == a_focus) {
					return static_cast<int>(i);
				}
			}
			return -1;  // set some other way: a marked enemy, or an older save
		}

		Focus StepFocus(Focus a_focus, int a_step)
		{
			const int count = static_cast<int>(focusChoices.size());
			const int at = FocusSlot(a_focus);
			if (at < 0) {
				return focusChoices[a_step > 0 ? 0 : static_cast<std::size_t>(count - 1)];
			}
			return focusChoices[static_cast<std::size_t>((at + a_step + count) % count)];
		}

		// A section label: a small ring, then the name in spaced capitals.
		void SectionLabel(Im::ImDrawList* a_draw, V a_pos, float a_s, float a_alpha, std::string_view a_text, bool a_focused)
		{
			const U32 c = Alpha(a_focused ? kText : kTextDim, a_alpha);
			Draw::AddCircle(a_draw, { a_pos.x + 4.0f * a_s, a_pos.y + 7.5f * a_s }, 3.4f * a_s, c, 16, 1.0f * a_s);
			Label(a_draw, { a_pos.x + 15.0f * a_s, a_pos.y }, c, 13.0f * a_s, a_text, 1.8f * a_s);
		}

		// One glass panel. The body draws its content from the top-left of the padded area and
		// returns the height it used; the glass goes underneath once that is known.
		template <class F>
		Rect Panel(Im::ImDrawList* a_draw, V a_origin, float a_width, float a_pad, float a_glass, F&& a_body)
		{
			Draw::ChannelsSplit(a_draw, 2);
			Draw::ChannelsSetCurrent(a_draw, 1);
			const float height = a_body(V{ a_origin.x + a_pad, a_origin.y + a_pad }, a_width - a_pad * 2.0f);
			const V     max{ a_origin.x + a_width, a_origin.y + height + a_pad * 2.0f };
			Draw::ChannelsSetCurrent(a_draw, 0);
			Frame(a_draw, a_origin, max, a_glass);
			Draw::ChannelsMerge(a_draw);
			return { a_origin, max };
		}

		// --- The chosen companion: three panels -------------------------------------------------

		// Who they are and their role; then what they go for and where they stand; then, for
		// tanks and healers, the ability. Kept apart so each reads at a glance, and every
		// longer explanation waits for the pointer.
		Rect DrawCompanion(Im::ImDrawList* a_draw, const View::Snapshot& a_view, const View::Member& a_member, V a_origin, float a_width, float a_s,
			float a_appear, const Input& a_input)
		{
			const float swap = Smooth(state.swap);
			const bool  live = !state.closing;
			const bool  nav = state.keyboardNav && live;
			const float pad = 24.0f * a_s;
			const float gap = 14.0f * a_s;

			Orders orders = a_member.orders;
			bool   changed = false;
			float  y = a_origin.y;

			auto enter = [&](int a_index) { return Stage(a_appear, 0.05f + 0.07f * static_cast<float>(a_index), 0.5f); };
			auto origin = [&](int a_index) {
				return V{ a_origin.x + (1.0f - enter(a_index)) * 48.0f * a_s + (1.0f - swap) * 14.0f * a_s, y };
			};
			auto alphaOf = [&](int a_index) { return enter(a_index) * (0.25f + 0.75f * swap); };

			// --- Who, and their role ---
			{
				const float alpha = alphaOf(0);
				const Rect  r = Panel(a_draw, origin(0), a_width, pad, enter(0), [&](V p, float inner) {
                    // The name in capitals on an oxblood band across the panel's top.
                    const float bandTop = p.y - pad + 1.0f;
                    const float bandH = pad + 30.0f * a_s;
                    Band(a_draw, { p.x - pad + 1.0f, bandTop }, { p.x + inner + pad - 1.0f, bandTop + bandH }, alpha);
                    {
                        const V     mc{ p.x + inner - 22.0f * a_s, bandTop + bandH * 0.5f };
                        const float mr = 23.0f * a_s;
                        Diamond(a_draw, mc, mr, Alpha(IM_COL32(24, 11, 11, 250), alpha), Alpha(kEdge, alpha), 1.4f * a_s);
                        Diamond(a_draw, mc, mr - 4.0f * a_s, Alpha(kShade, 0.0f), Alpha(kText, alpha * 0.3f), 1.0f);
                        TextCentered(a_draw, mc, Alpha(kText, alpha), 19.0f * a_s, std::format("{}", a_member.level));
                        Slashes(a_draw, { mc.x - mr - 34.0f * a_s, bandTop + bandH * 0.3f }, bandH * 0.4f, Alpha(kText, alpha * 0.45f));
                    }
                    const std::string upper = Upper(a_member.name);
                    Text(a_draw, { p.x, bandTop + (bandH - TextSize(26.0f * a_s, upper).y) * 0.5f }, Alpha(kText, alpha), 26.0f * a_s, upper);
                    float yy = bandTop + bandH + 14.0f * a_s;
                    const auto  roleName = std::string(Info(orders.role).name);
                    std::string status;
                    const bool  noArrows = orders.role == Role::kArcher && a_member.bow && a_member.ammoName.empty();
                    if (a_member.down) {
                        status += "  ·  down";
                    } else if (noArrows) {
                        status += "  ·  no arrows";
                    } else if (a_member.inCombat && !a_member.targetName.empty()) {
                        status += std::format("  ·  fighting {}", a_member.targetName);
                    }
                    Text(a_draw, { p.x, yy }, Alpha(RoleAccent(orders.role), alpha), 15.5f * a_s, roleName);
                    Text(a_draw, { p.x + TextSize(15.5f * a_s, roleName).x, yy }, Alpha(a_member.down || noArrows ? kRed : kTextDim, alpha), 15.5f * a_s, status);
                    yy += 28.0f * a_s;
                    const float barW = (inner - 2 * 8.0f * a_s) / 3.0f;
                    const std::array<std::pair<float, U32>, 3> vitals{ { { a_member.health, kHealth }, { a_member.magicka, kMagicka }, { a_member.stamina, kStamina } } };
                    for (int i = 0; i < 3; ++i) {
                        const float value = state.tw.Toward("vital", static_cast<std::uint64_t>(a_member.id) * 4 + i, vitals[static_cast<std::size_t>(i)].first, 7.0f);
                        Bar(a_draw, { p.x + i * (barW + 8.0f * a_s), yy }, barW, 4.0f * a_s, value, vitals[static_cast<std::size_t>(i)].second, alpha);
                    }
                    yy += 30.0f * a_s;

                    SectionLabel(a_draw, { p.x, yy }, a_s, alpha, "ROLE", nav && state.section == kRoleSection);
                    yy += 24.0f * a_s;
                    const int   columns = 3;
                    const float tgap = 8.0f * a_s;
                    const float tileW = (inner - tgap * (columns - 1)) / columns;
                    const float tileH = 66.0f * a_s;
                    const int   chosen = static_cast<int>(orders.role);
                    const float hx = state.tw.Toward("roleX", 0, static_cast<float>(chosen % columns), 14.0f);
                    const float hy = state.tw.Toward("roleY", 0, static_cast<float>(chosen / columns), 14.0f);
                    {
                        const V hmin{ p.x + hx * (tileW + tgap), yy + hy * (tileH + tgap) };
                        const V hmax{ hmin.x + tileW, hmin.y + tileH };
                        Lit(a_draw, hmin, hmax, alpha);
                        Draw::AddRect(a_draw, hmin, hmax, Alpha(kEdge, alpha * 0.5f), 0.0f, 0, 1.0f);
                    }
                    for (int i = 0; i < static_cast<int>(Role::kCount); ++i) {
                        const auto& info = roles[static_cast<std::size_t>(i)];
                        const V     min{ p.x + (i % columns) * (tileW + tgap), yy + (i / columns) * (tileH + tgap) };
                        const V     max{ min.x + tileW, min.y + tileH };
                        bool        hovered = false;
                        Im::PushID(2000 + i);
                        const bool clicked = Hit("role", min, { tileW, tileH }, hovered) && live;
                        Im::PopID();
                        const bool  focused = nav && state.section == kRoleSection && state.item == i;
                        const auto  lack = a_member.arsenalKnown ? LackFor(static_cast<Role>(i), a_member.arsenal, a_view.lendHeal) : Lack::kNothing;
                        const bool  playable = lack == Lack::kNothing || i == chosen;
                        const float hover = state.tw.Toward("roleHover", static_cast<std::uint64_t>(i), (hovered || focused) && playable ? 1.0f : 0.0f, 16.0f);
                        float&      flash = state.tw.Raw("roleFlash", static_cast<std::uint64_t>(i), 0.0f);
                        if (hovered) {
                            Hovering(0x100 + static_cast<std::uint64_t>(i));
                        }
                        if (clicked || (focused && a_input.activate)) {
                            if (!playable) {
                                Sound("UIMenuCancel");
                            } else {
                                flash = 1.0f;
                                if (i != chosen) {
                                    orders = Retask(orders, static_cast<Role>(i));
                                    changed = true;
                                }
                                Sound("UIMenuOK");
                            }
                        }
                        flash = Follow(flash, 0.0f, state.tw.dt, 6.0f);
                        if (i != chosen) {
                            Draw::AddRectFilled(a_draw, min, max, Alpha(Mix(kTile, kTileHover, hover), alpha), 0.0f, 0);
                            Draw::AddRect(a_draw, min, max, Alpha(kGlassEdge, alpha * (0.5f + 0.5f * hover)), 0.0f, 0, 1.0f);
                        }
                        if (focused) {
                            FocusOutline(a_draw, min, max, 7.0f * a_s, alpha);
                        }
                        const float on = i == chosen ? 1.0f : hover * 0.5f;
                        const float dim = playable ? 1.0f : 0.32f;
                        Glyph(a_draw, { min.x + tileW * 0.5f, min.y + 25.0f * a_s - 2.0f * a_s * hover }, (23.0f + 5.0f * flash) * a_s, info.glyph,
                            Alpha(Mix(kTextDim, kText, on), alpha * dim));
                        TextCentered(a_draw, { min.x + tileW * 0.5f, min.y + 50.0f * a_s }, Alpha(Mix(kTextDim, kText, on), alpha * dim), 15.0f * a_s, info.name);
                        if (!playable) {
                            Glyph(a_draw, { max.x - 12.0f * a_s, min.y + 12.0f * a_s }, 10.0f * a_s, 0xF023, Alpha(kTextFaint, alpha));
                        }
                        if (hovered || focused) {
                            auto tip = playable ? std::format("{}\n{}", info.name, info.summary) : std::format("{} is not on offer.\n{}", info.name, Why(lack));
                            if (hovered) {
                                Tip(std::move(tip));
                            } else {
                                TipBeside(std::move(tip), min, max);
                            }
                        }
                    }
                    yy += tileH * 2.0f + tgap;
                    return yy - p.y;
				});
				y = r.max.y + gap;
			}

			// --- What they go for, and where they stand ---
			{
				const float alpha = alphaOf(1);
				const Rect  r = Panel(a_draw, origin(1), a_width, pad, enter(1), [&](V p, float inner) {
                    float yy = p.y;

                    // Focus: one value at a time, stepped with the arrows.
                    const bool focusRow = nav && state.section == kFocusSection;
                    SectionLabel(a_draw, { p.x, yy }, a_s, alpha, "FOCUS", focusRow);
                    yy += 24.0f * a_s;
                    {
                        const float h = 52.0f * a_s;
                        const V     min{ p.x, yy }, max{ p.x + inner, yy + h };
                        Draw::AddRectFilled(a_draw, min, max, Alpha(kTile, alpha), 0.0f, 0);
                        Draw::AddRect(a_draw, min, max, Alpha(kGlassEdge, alpha), 0.0f, 0, 1.0f);
                        Draw::AddLine(a_draw, { min.x + 48.0f * a_s, min.y + 10.0f * a_s }, { min.x + 48.0f * a_s, max.y - 10.0f * a_s }, Alpha(kGlassEdge, alpha), 1.0f);
                        Draw::AddLine(a_draw, { max.x - 48.0f * a_s, min.y + 10.0f * a_s }, { max.x - 48.0f * a_s, max.y - 10.0f * a_s }, Alpha(kGlassEdge, alpha), 1.0f);
                        if (focusRow) {
                            FocusOutline(a_draw, min, max, 0.0f, alpha);
                        }
                        int step = 0;
                        for (int side = -1; side <= 1; side += 2) {
                            const V bmin{ side < 0 ? min.x : max.x - 48.0f * a_s, min.y };
                            bool    hovered = false;
                            Im::PushID(2100 + side);
                            if (Hit("focusStep", bmin, { 48.0f * a_s, h }, hovered) && live) {
                                step = side;
                            }
                            Im::PopID();
                            if (hovered) {
                                Hovering(0x150 + static_cast<std::uint64_t>(side + 1));
                            }
                            const float hover = state.tw.Toward("focusArrow", static_cast<std::uint64_t>(side + 1), hovered ? 1.0f : 0.0f, 16.0f);
                            Glyph(a_draw, { bmin.x + 24.0f * a_s + side * 3.0f * a_s * hover, min.y + h * 0.5f }, 17.0f * a_s, side < 0 ? 0xF053 : 0xF054,
                                Alpha(Mix(kTextDim, kText, hover), alpha));
                        }
                        if (focusRow && a_input.dx) {
                            step = a_input.dx > 0 ? 1 : -1;
                        }
                        if (step) {
                            orders.focus = StepFocus(orders.focus, step);
                            changed = true;
                            state.tw.Raw("focusSlide", 0, 0.0f) = static_cast<float>(step);
                            Sound("UIMenuFocus");
                        }
                        float& slide = state.tw.Raw("focusSlide", 0, 0.0f);
                        slide = Follow(slide, 0.0f, state.tw.dt, 12.0f);
                        const auto& info = Info(orders.focus);
                        std::string name(info.name);
                        if (orders.focus == Focus::kMarked) {
                            for (const auto& e : a_view.enemies) {
                                if (e.id == a_member.marked) {
                                    name = e.name;
                                }
                            }
                        }
                        const float labelW = TextSize(18.0f * a_s, name).x;
                        const float groupW = labelW + 28.0f * a_s;
                        const float gx = min.x + (inner - groupW) * 0.5f + slide * 34.0f * a_s;
                        const float fade = alpha * (1.0f - std::min(std::fabs(slide), 1.0f) * 0.85f);
                        Glyph(a_draw, { gx + 10.0f * a_s, min.y + h * 0.5f }, 17.0f * a_s, info.glyph, Alpha(kEdge, fade));
                        Text(a_draw, { gx + 28.0f * a_s, min.y + (h - TextSize(18.0f * a_s, name).y) * 0.5f }, Alpha(kText, fade), 18.0f * a_s, name);
                        bool overValue = false;
                        Hit("focusValue", { min.x + 48.0f * a_s, min.y }, { inner - 96.0f * a_s, h }, overValue);
                        if (overValue) {
                            Tip(std::string(info.summary));
                        } else if (focusRow) {
                            TipBeside(std::string(info.summary), min, max);
                        }
                        yy += h + 11.0f * a_s;

                        // Where this value sits among the rest.
                        const int   count = static_cast<int>(focusChoices.size());
                        const float dot = 10.0f * a_s;
                        const float dx0 = p.x + (inner - dot * (count - 1)) * 0.5f;
                        const int   slot = FocusSlot(orders.focus);
                        const float at = state.tw.Toward("focusDot", 0, static_cast<float>(std::max(slot, 0)), 14.0f);
                        for (int i = 0; i < count; ++i) {
                            Diamond(a_draw, { dx0 + dot * i, yy }, 2.4f * a_s, Alpha(kTextFaint, alpha * 0.8f), Alpha(kTextFaint, 0.0f), 1.0f);
                        }
                        if (slot >= 0) {
                            Diamond(a_draw, { dx0 + dot * at, yy }, 4.6f * a_s, Alpha(kAccent, alpha), Alpha(kEdge, alpha), 1.0f);
                        }
                        yy += 26.0f * a_s;
                    }

                    // Position: a segmented bar with a sliding pill.
                    const bool positionRow = nav && state.section == kPositionSection;
                    SectionLabel(a_draw, { p.x, yy }, a_s, alpha, "POSITION", positionRow);
                    yy += 24.0f * a_s;
                    {
                        const int   count = static_cast<int>(Position::kCount);
                        const float h = 48.0f * a_s;
                        const float segW = inner / count;
                        const V     min{ p.x, yy }, max{ p.x + inner, yy + h };
                        const float dr = 18.0f * a_s;
                        Draw::AddLine(a_draw, { min.x - 6.0f * a_s, min.y + h * 0.5f }, { max.x + 6.0f * a_s, min.y + h * 0.5f }, Alpha(kGlassEdge, alpha * 0.8f), 1.0f);
                        for (int i = 0; i < count; ++i) {
                            Diamond(a_draw, { min.x + segW * (i + 0.5f), min.y + h * 0.5f }, dr, Alpha(IM_COL32(19, 19, 20, 245), alpha), Alpha(kGlassEdge, alpha * 1.3f), 1.0f);
                        }
                        if (positionRow) {
                            FocusOutline(a_draw, min, max, 0.0f, alpha);
                        }
                        if (positionRow && a_input.dx) {
                            int next = static_cast<int>(orders.position);
                            for (int tries = 0; tries < count; ++tries) {
                                next = (next + (a_input.dx > 0 ? 1 : -1) + count) % count;
                                if (PositionAllowed(static_cast<Position>(next), a_view)) {
                                    break;
                                }
                            }
                            orders.position = static_cast<Position>(next);
                            changed = true;
                            Sound("UIMenuFocus");
                        }
                        const float px = state.tw.Toward("positionPill", 0, static_cast<float>(orders.position), 14.0f);
                        const V pc{ min.x + segW * (px + 0.5f), min.y + h * 0.5f };
                        Halo(a_draw, pc, dr * 1.6f, kAccent, alpha * 0.8f);
                        Diamond(a_draw, pc, dr + 1.0f * a_s, Alpha(kAccent, alpha * 0.85f), Alpha(kEdge, alpha), 1.6f * a_s);
                        int shown = static_cast<int>(orders.position);
                        for (int i = 0; i < count; ++i) {
                            const auto position = static_cast<Position>(i);
                            const bool allowed = PositionAllowed(position, a_view);
                            const V    smin{ min.x + segW * i, min.y };
                            bool       hovered = false;
                            Im::PushID(2200 + i);
                            if (Hit("position", smin, { segW, h }, hovered) && live && allowed && orders.position != position) {
                                orders.position = position;
                                changed = true;
                                Sound("UIMenuOK");
                            }
                            Im::PopID();
                            if (hovered) {
                                shown = i;
                                Hovering(0x180 + static_cast<std::uint64_t>(i));
                                Tip(std::format("{}\n{}", Info(position).name, allowed ? Info(position).summary : "Needs Walk With Me."));
                            }
                            const float hover = state.tw.Toward("positionHover", static_cast<std::uint64_t>(i), hovered ? 1.0f : 0.0f, 16.0f);
                            const float on = orders.position == position ? 1.0f : hover * 0.6f;
                            Glyph(a_draw, { smin.x + segW * 0.5f, min.y + h * 0.5f }, 15.0f * a_s, Info(position).glyph,
                                Alpha(Mix(kTextDim, kText, on), alpha * (allowed ? 1.0f : 0.35f)));
                        }
                        yy += h + 12.0f * a_s;
                        // One short line naming the stance, so the icons never have to be guessed.
                        const auto  stance = static_cast<Position>(shown);
                        std::string line(Info(stance).name);
                        if (stance == Position::kFormation && a_view.walkWithMe) {
                            line += std::format("  ·  for now, {}", Info(Resolve(stance, a_view.formation)).name);
                        }
                        TextCentered(a_draw, { p.x + inner * 0.5f, yy + 8.0f * a_s }, Alpha(shown == static_cast<int>(orders.position) ? kText : kTextDim, alpha), 16.0f * a_s, line);
                        if (positionRow) {
                            TipBeside(std::string(Info(stance).summary), min, max);
                        }
                        yy += 20.0f * a_s;
                    }
                    return yy - p.y;
				});
				y = r.max.y + gap;
			}

			// --- The ability: one clear action and a switch ---
			const bool tank = orders.role == Role::kTank;
			const bool healer = orders.role == Role::kHealer;
			if (tank || healer) {
				const float alpha = alphaOf(2);
				const Rect  r = Panel(a_draw, origin(2), a_width, pad, enter(2), [&](V p, float inner) {
                    float     yy = p.y;
                    const int focusItem = nav && state.section == kAbilitySection ? state.item : -1;
                    SectionLabel(a_draw, { p.x, yy }, a_s, alpha, tank ? "TAUNT" : "HEAL", focusItem >= 0);
                    yy += 24.0f * a_s;

                    const float cooldown = tank ? a_member.tauntCooldown : a_view.tuning.healCooldown;
                    const float ready = tank ? a_member.tauntReady : a_member.healReady;
                    const float charge = cooldown > 0.0f && ready > 0.0f ? 1.0f - ready / cooldown : 1.0f;
                    const bool  usable = a_member.inCombat && (tank || a_member.canHeal);
                    const float h = 50.0f * a_s;
                    const float bw = inner * 0.58f;
                    const V     bmin{ p.x, yy }, bmax{ p.x + bw, yy + h };
                    bool        hovered = false;
                    const bool  clicked = Hit("abilityNow", bmin, { bw, h }, hovered) && live && usable;
                    const float hover = state.tw.Toward("abilityHover", 0, hovered ? 1.0f : 0.0f, 16.0f);
                    float&      press = state.tw.Raw("abilityPress", 0, 0.0f);
                    if (hovered) {
                        Hovering(0x400);
                        std::string stats;
                        if (tank) {
                            stats = std::format("Holds level {} and below\n{:.0f} reach  ·  {:.0f} s  ·  {:.0f} stamina", a_member.tauntCertain, a_member.tauntRadius,
                                a_member.tauntHold, a_member.tauntStamina);
                        } else if (a_member.canHeal) {
                            stats = std::format("{}{}\nAbout {:.0f} health for {:.0f} magicka", a_member.healSpell, a_member.healBorrowed ? " (borrowed)" : "",
                                a_member.healAmount, a_member.healCost);
                        } else {
                            stats = "Knows no heal for others; they will fight instead.";
                        }
                        Tip(std::move(stats));
                    }
                    if (clicked || (focusItem == 0 && a_input.activate && usable) || (a_input.now && usable)) {
                        press = 1.0f;
                        Simple(tank ? Battle::Command::Type::kTauntNow : Battle::Command::Type::kHealNow, a_member.id);
                        Sound("UIMenuOK");
                    }
                    press = Follow(press, 0.0f, state.tw.dt, 5.0f);
                    const float shown = state.tw.Toward("abilityCharge", a_member.id, charge, 8.0f);
                    Draw::AddRectFilled(a_draw, bmin, bmax, Alpha(Mix(kTile, kTileHover, hover), alpha), 0.0f, 0);
                    if (shown < 0.999f) {
                        Draw::AddRectFilledMultiColor(a_draw, bmin, { bmin.x + bw * shown, bmax.y }, Alpha(kAccent, alpha * 0.25f), Alpha(kAccent, alpha * 0.55f),
                            Alpha(kAccent, alpha * 0.55f), Alpha(kAccent, alpha * 0.25f));
                    } else if (usable) {
                        const float glow = 0.5f + 0.5f * std::sin(state.clock * 3.0f);
                        Highlight(a_draw, bmin, bmax, alpha * (0.55f + 0.2f * glow + 0.25f * press));
                    }
                    Draw::AddRect(a_draw, bmin, bmax, Alpha(usable && shown >= 0.999f ? kEdge : kGlassEdge, alpha), 0.0f, 0, 1.0f);
                    if (focusItem == 0) {
                        FocusOutline(a_draw, bmin, bmax, 0.0f, alpha);
                    }
                    std::string label;
                    if (!a_member.inCombat) {
                        label = tank ? "Taunt in battle" : "Heal in battle";
                    } else if (!usable) {
                        label = "No heal known";
                    } else if (charge < 1.0f) {
                        label = std::format("Ready in {:.0f} s", std::ceil(ready));
                    } else {
                        label = tank ? "Taunt now" : "Heal now";
                    }
                    label = Upper(label);
                    const float ls = 16.0f * a_s;
                    const float lw = TextSize(ls, label).x + 30.0f * a_s;
                    const float lx = bmin.x + (bw - lw) * 0.5f;
                    Glyph(a_draw, { lx + 10.0f * a_s, bmin.y + h * 0.5f }, (18.0f + 4.0f * press) * a_s, tank ? 0xF0A1 : 0xF4BE, Alpha(kText, alpha * (usable ? 1.0f : 0.4f)));
                    Text(a_draw, { lx + 30.0f * a_s, bmin.y + (h - TextSize(ls, label).y) * 0.5f }, Alpha(kText, alpha * (usable ? 1.0f : 0.5f)), ls, label);

                    // The switch beside it: the knob slides.
                    {
                        const float       sw = 40.0f * a_s, sh = 22.0f * a_s;
                        const std::string caption = "On their own";
                        const V           smin{ bmax.x + 18.0f * a_s, yy + (h - sh) * 0.5f };
                        bool              over = false;
                        const bool        flipped = Hit("auto", smin, { p.x + inner - smin.x, sh }, over) && live;
                        if (over) {
                            Hovering(0x401);
                            Tip(tank ? "On: they taunt by themselves when it is ready and worth it.\nOff: only when you press Taunt now."s :
                                       "On: they heal by themselves when someone drops below the line.\nOff: only when you press Heal now."s);
                        }
                        if (flipped || (focusItem == 1 && a_input.activate) || a_input.flipAuto) {
                            orders.autoAbility = !orders.autoAbility;
                            changed = true;
                            Sound("UIMenuOK");
                        }
                        const float k = state.tw.Toward("autoKnob", a_member.id, orders.autoAbility ? 1.0f : 0.0f, 14.0f);
                        Draw::AddRectFilled(a_draw, smin, { smin.x + sw, smin.y + sh }, Alpha(Mix(IM_COL32(255, 255, 255, 16), kAccent, k), alpha), 0.0f, 0);
                        Draw::AddRect(a_draw, smin, { smin.x + sw, smin.y + sh }, Alpha(Mix(kGlassEdge, kEdge, k), alpha), 0.0f, 0, 1.0f);
                        const float knob = sh - 8.0f * a_s;
                        const V     kmin{ smin.x + 4.0f * a_s + (sw - sh) * k, smin.y + 4.0f * a_s };
                        Draw::AddRectFilled(a_draw, kmin, { kmin.x + knob, kmin.y + knob }, Alpha(Mix(kTextDim, kText, k), alpha), 0.0f, 0);
                        if (focusItem == 1) {
                            FocusOutline(a_draw, smin, { smin.x + sw, smin.y + sh }, 0.0f, alpha);
                        }
                        Text(a_draw, { smin.x + sw + 10.0f * a_s, smin.y + (sh - TextSize(15.0f * a_s, caption).y) * 0.5f },
                            Alpha(Mix(kTextDim, kText, over ? 1.0f : 0.0f), alpha), 15.0f * a_s, caption);
                    }
                    yy += h;

                    // The healer's line: a slider in steps of five.
                    if (healer) {
                        yy += 22.0f * a_s;
                        int         threshold = orders.healThreshold;
                        const float labelW = 150.0f * a_s;
                        Text(a_draw, { p.x, yy - 9.0f * a_s }, Alpha(orders.autoAbility ? kTextDim : kTextFaint, alpha), 15.0f * a_s, std::format("Heal below {}%", threshold));
                        const float tw = inner - labelW, th = 4.0f * a_s;
                        const V     tmin{ p.x + labelW, yy };
                        Im::SetCursorScreenPos({ tmin.x - 8.0f * a_s, tmin.y - 12.0f * a_s });
                        Im::InvisibleButton("threshold", { tw + 16.0f * a_s, 26.0f * a_s });
                        const bool dragging = Im::IsItemActive() && live;
                        if (dragging) {
                            const float t = Clamp01((Im::GetIO()->MousePos.x - tmin.x) / tw);
                            threshold = 20 + static_cast<int>(std::round(t * 70.0f / 5.0f)) * 5;
                        }
                        if (focusItem >= 2 && a_input.dx) {
                            threshold = std::clamp(threshold + (a_input.dx > 0 ? 5 : -5), 20, 90);
                        }
                        const float t = state.tw.Toward("threshold", a_member.id, (static_cast<float>(threshold) - 20.0f) / 70.0f, 18.0f);
                        Draw::AddRectFilled(a_draw, { tmin.x, tmin.y + th * 0.5f - 0.5f }, { tmin.x + tw, tmin.y + th * 0.5f + 0.5f }, Alpha(kGlassEdge, alpha * 1.4f), 0.0f, 0);
                        Draw::AddRectFilled(a_draw, { tmin.x, tmin.y + th * 0.25f }, { tmin.x + tw * t, tmin.y + th * 0.75f }, Alpha(kEdge, alpha), 0.0f, 0);
                        Diamond(a_draw, { tmin.x + tw * t, tmin.y + th * 0.5f }, (8.0f + (dragging ? 2.0f : 0.0f)) * a_s, Alpha(kText, alpha), Alpha(kShade, alpha * 0.5f), 1.0f);
                        if (focusItem >= 2) {
                            FocusOutline(a_draw, { tmin.x - 6, tmin.y - 8 }, { tmin.x + tw + 6, tmin.y + th + 8 }, 6.0f, alpha);
                        }
                        if (threshold != orders.healThreshold) {
                            orders.healThreshold = static_cast<std::uint8_t>(threshold);
                            changed = true;
                        }
                        yy += 12.0f * a_s;
                    }
                    return yy - p.y;
				});
				y = r.max.y + gap;
			}

			if (changed && live) {
				SetOrders(a_member, orders);
			}
			return { a_origin, { a_origin.x + a_width, y - gap } };
		}

		// --- The character sheet -------------------------------------------------------------

		// With no fight on, the chosen companion's sheet sits on the left, the way a bestiary
		// page sits beside its creature: what they can take, what turns aside, what they deal,
		// what they hold, and what they are best at.
		Rect DrawSheet(Im::ImDrawList* a_draw, const View::Member& a_member, V a_origin, float a_width, float a_s, float a_appear)
		{
			const float swap = Smooth(state.swap);
			const float pad = 24.0f * a_s;
			const float gap = 14.0f * a_s;
			float       y = a_origin.y;
			auto        enter = [&](int a_index) { return Stage(a_appear, 0.08f + 0.07f * static_cast<float>(a_index), 0.5f); };
			auto        origin = [&](int a_index) { return V{ a_origin.x - (1.0f - enter(a_index)) * 48.0f * a_s - (1.0f - swap) * 14.0f * a_s, y }; };
			auto        alphaOf = [&](int a_index) { return enter(a_index) * (0.25f + 0.75f * swap); };

			// Who they are, what they can take, what turns aside.
			{
				const float alpha = alphaOf(0);
				const Rect  r = Panel(a_draw, origin(0), a_width, pad, enter(0), [&](V p, float inner) {
                    const float bandTop = p.y - pad + 1.0f;
                    const float bandH = 92.0f * a_s;
                    Band(a_draw, { p.x - pad + 1.0f, bandTop }, { p.x + inner + pad - 1.0f, bandTop + bandH }, alpha);
                    Draw::AddLine(a_draw, { p.x - pad + 1.0f, bandTop + bandH }, { p.x + inner + pad - 1.0f, bandTop + bandH }, Alpha(kEdge, alpha * 0.5f), 1.0f);
                    // Race large, class beneath it.
                    const std::string race = Upper(a_member.race.empty() ? std::string("Companion") : a_member.race);
                    Text(a_draw, { p.x, bandTop + 24.0f * a_s }, Alpha(kText, alpha), 24.0f * a_s, race);
                    if (!a_member.className.empty()) {
                        Label(a_draw, { p.x + 1.0f * a_s, bandTop + 56.0f * a_s }, Alpha(kTextDim, alpha), 13.0f * a_s, Upper(a_member.className), 1.8f * a_s);
                    }
                    // The level: large, in a medallion, a ring of studs around its points.
                    {
                        const V     mc{ p.x + inner - 40.0f * a_s, bandTop + bandH * 0.5f };
                        const float mr = 40.0f * a_s;
                        Halo(a_draw, mc, mr * 1.25f, kAccent, alpha * 0.7f);
                        Diamond(a_draw, mc, mr, Alpha(IM_COL32(24, 11, 11, 250), alpha), Alpha(kEdge, alpha), 1.6f * a_s);
                        Diamond(a_draw, mc, mr - 6.0f * a_s, Alpha(kShade, 0.0f), Alpha(kText, alpha * 0.35f), 1.0f);
                        for (const V d : { V{ 0, -1 }, V{ 1, 0 }, V{ 0, 1 }, V{ -1, 0 } }) {
                            Stud(a_draw, { mc.x + d.x * mr, mc.y + d.y * mr }, 3.4f * a_s, alpha);
                        }
                        const std::string level = std::format("{}", a_member.level);
                        TextCentered(a_draw, { mc.x, mc.y - 5.0f * a_s }, Alpha(kText, alpha), 34.0f * a_s, level);
                        const float lw = LabelWidth(11.0f * a_s, "LEVEL", 1.6f * a_s);
                        Label(a_draw, { mc.x - lw * 0.5f, mc.y + 14.0f * a_s }, Alpha(kTextDim, alpha), 11.0f * a_s, "LEVEL", 1.6f * a_s);
                    }
                    float yy = bandTop + bandH + 18.0f * a_s;

                    const std::array<std::tuple<std::string_view, float, float, U32>, 3> vitals{ {
                        { "HEALTH", a_member.healthNow, a_member.healthMax, kHealth },
                        { "MAGICKA", a_member.magickaNow, a_member.magickaMax, kMagicka },
                        { "STAMINA", a_member.staminaNow, a_member.staminaMax, kStamina },
                    } };
                    for (std::size_t i = 0; i < vitals.size(); ++i) {
                        const auto& [name, now, most, colour] = vitals[i];
                        Label(a_draw, { p.x, yy + 1.0f * a_s }, Alpha(kTextDim, alpha), 12.5f * a_s, name, 1.6f * a_s);
                        const std::string numbers = std::format("{:.0f} / {:.0f}", now, most);
                        Text(a_draw, { p.x + inner - TextSize(15.0f * a_s, numbers).x, yy }, Alpha(kText, alpha), 15.0f * a_s, numbers);
                        yy += 21.0f * a_s;
                        const float value = state.tw.Toward("sheetVital", static_cast<std::uint64_t>(a_member.id) * 4 + i, most > 0.0f ? now / most : 0.0f, 7.0f);
                        Bar(a_draw, { p.x, yy }, inner, 3.0f * a_s, value, colour, alpha);
                        yy += 17.0f * a_s;
                    }
                    yy += 8.0f * a_s;

                    // What turns aside: five diamonds, the way a bestiary shows resistances. A
                    // weakness is outlined in red.
                    SectionLabel(a_draw, { p.x, yy }, a_s, alpha, "RESISTANCE", false);
                    yy += 34.0f * a_s;
                    static constexpr std::array<std::uint32_t, 5> elements{ { 0xF06D, 0xF2DC, 0xF0E7, 0xF714, 0xF0D0 } };
                    const float cell = inner / 5.0f;
                    const float dr = 19.0f * a_s;
                    for (std::size_t i = 0; i < elements.size(); ++i) {
                        const float value = a_member.resist[i];
                        const V     c{ p.x + cell * (static_cast<float>(i) + 0.5f), yy };
                        const bool  weak = value < 0.0f, some = value > 0.0f;
                        Diamond(a_draw, c, dr, Alpha(weak ? IM_COL32(58, 15, 14, 245) : IM_COL32(19, 19, 20, 245), alpha),
                            Alpha(weak ? kEdge : (some ? kText : kGlassEdge), alpha * (some || weak ? 0.9f : 1.3f)), (weak || some ? 1.4f : 1.0f) * a_s);
                        Glyph(a_draw, c, 15.0f * a_s, elements[i], Alpha(weak ? kEdge : (some ? kText : kTextFaint), alpha));
                        const std::string number = std::format("{:.0f}%", value);
                        TextCentered(a_draw, { c.x, c.y + dr + 13.0f * a_s }, Alpha(weak ? kEdge : (some ? kText : kTextFaint), alpha), 13.5f * a_s, number);
                    }
                    yy += dr + 30.0f * a_s;

                    // Two numbers that matter most in a fight, large, each in its own frame.
                    const float half = (inner - 12.0f * a_s) * 0.5f;
                    const std::array<std::pair<std::string_view, std::string>, 2> big{ {
                        { "ARMOR", std::format("{:.0f}", a_member.armor) },
                        { "DAMAGE", a_member.damage > 0.0f ? std::format("{:.0f}", a_member.damage) : std::string("-") },
                    } };
                    for (std::size_t i = 0; i < big.size(); ++i) {
                        const V bmin{ p.x + static_cast<float>(i) * (half + 12.0f * a_s), yy };
                        const V bmax{ bmin.x + half, bmin.y + 64.0f * a_s };
                        Draw::AddRectFilled(a_draw, bmin, bmax, Alpha(kTile, alpha), 0.0f, 0);
                        Draw::AddRect(a_draw, bmin, bmax, Alpha(kGlassEdge, alpha), 0.0f, 0, 1.0f);
                        TextCentered(a_draw, { (bmin.x + bmax.x) * 0.5f, bmin.y + 25.0f * a_s }, Alpha(kText, alpha), 30.0f * a_s, big[i].second);
                        const float lw = LabelWidth(11.5f * a_s, big[i].first, 2.0f * a_s);
                        Label(a_draw, { (bmin.x + bmax.x) * 0.5f - lw * 0.5f, bmin.y + 45.0f * a_s }, Alpha(kTextDim, alpha), 11.5f * a_s, big[i].first, 2.0f * a_s);
                    }
                    yy += 64.0f * a_s;
                    return yy - p.y;
				});
				y = r.max.y + gap;
			}

			// What they hold, and what they are best at: lists, one hairline under each row.
			{
				const float alpha = alphaOf(1);
				const Rect  r = Panel(a_draw, origin(1), a_width, pad, enter(1), [&](V p, float inner) {
                    float yy = p.y;
                    SectionLabel(a_draw, { p.x, yy }, a_s, alpha, "IN HAND", false);
                    yy += 28.0f * a_s;
                    const std::array<std::pair<std::uint32_t, const std::string*>, 2> hands{ { { 0xF6DE, &a_member.rightHand }, { 0xF3ED, &a_member.leftHand } } };
                    for (const auto& [glyph, held] : hands) {
                        Glyph(a_draw, { p.x + 9.0f * a_s, yy + 9.0f * a_s }, 14.0f * a_s, glyph, Alpha(held->empty() ? kTextFaint : kText, alpha));
                        Text(a_draw, { p.x + 30.0f * a_s, yy }, Alpha(held->empty() ? kTextFaint : kText, alpha), 15.5f * a_s, held->empty() ? std::string("Empty") : *held);
                        yy += 24.0f * a_s;
                        Draw::AddLine(a_draw, { p.x, yy }, { p.x + inner, yy }, Alpha(kGlassEdge, alpha * 0.7f), 1.0f);
                        yy += 8.0f * a_s;
                    }
                    // Arrows for a bow: an archer with nothing to shoot shows before a fight does.
                    if (a_member.bow) {
                        const bool none = a_member.ammoName.empty();
                        Glyph(a_draw, { p.x + 9.0f * a_s, yy + 9.0f * a_s }, 14.0f * a_s, Info(Role::kArcher).glyph, Alpha(none ? kEdge : kText, alpha));
                        Text(a_draw, { p.x + 30.0f * a_s, yy }, Alpha(none ? kEdge : kText, alpha), 15.5f * a_s,
                            none ? std::string("No arrows") : std::format("{}  x{}", a_member.ammoName, a_member.ammoCount));
                        yy += 24.0f * a_s;
                        Draw::AddLine(a_draw, { p.x, yy }, { p.x + inner, yy }, Alpha(kGlassEdge, alpha * 0.7f), 1.0f);
                        yy += 8.0f * a_s;
                    }
                    yy += 12.0f * a_s;

                    SectionLabel(a_draw, { p.x, yy }, a_s, alpha, "BEST AT", false);
                    yy += 28.0f * a_s;
                    const float nameW = 120.0f * a_s;
                    const float numW = 34.0f * a_s;
                    for (std::size_t i = 0; i < a_member.skills.size(); ++i) {
                        const auto& [name, level] = a_member.skills[i];
                        Text(a_draw, { p.x, yy }, Alpha(i == 0 ? kText : kTextDim, alpha), 15.0f * a_s, name);
                        const float shown = state.tw.Toward("sheetSkill", static_cast<std::uint64_t>(a_member.id) * 8 + i, std::clamp(level / 100.0f, 0.0f, 1.0f), 7.0f);
                        Bar(a_draw, { p.x + nameW, yy + 8.0f * a_s }, inner - nameW - numW, 2.0f * a_s, shown, i == 0 ? kEdge : kTextFaint, alpha);
                        const std::string number = std::format("{}", level);
                        Text(a_draw, { p.x + inner - TextSize(15.0f * a_s, number).x, yy }, Alpha(kText, alpha), 15.0f * a_s, number);
                        yy += 25.0f * a_s;
                    }
                    return yy - p.y;
				});
				y = r.max.y;
			}
			return { { a_origin.x - 16.0f * a_s, a_origin.y - 16.0f * a_s }, { a_origin.x + a_width + 16.0f * a_s, y + 16.0f * a_s } };
		}

				// --- Party orders --------------------------------------------------------------------------

		// The four orders for everyone on one dark band, each a large silhouette over its name
		// in capitals. Each replaces every companion's own orders; the one still holding is
		// lit from below and underlined in red.
		Rect DrawOrders(Im::ImDrawList* a_draw, const View::Snapshot& a_view, V a_centre, float a_s, float a_appear, const Input& a_input)
		{
			const bool  live = !state.closing;
			const bool  rowFocused = state.keyboardNav && live && state.section == kOrdersSection;
			const float alpha = Stage(a_appear, 0.18f, 0.5f);
			const float h = 70.0f * a_s;
			const float segW = 136.0f * a_s;
			const float width = segW * static_cast<float>(orderShouts.size());
			const V     min{ a_centre.x - width * 0.5f, a_centre.y + (1.0f - alpha) * -10.0f * a_s };
			const V     max{ min.x + width, min.y + h };
			const auto& settings = Settings::Get();

			Draw::AddRectFilledMultiColor(a_draw, { min.x - 60.0f * a_s, min.y }, { min.x, max.y }, Alpha(kGlass, 0.0f), Alpha(kGlass, alpha * 0.9f), Alpha(kGlass, alpha * 0.9f),
				Alpha(kGlass, 0.0f));
			Draw::AddRectFilled(a_draw, min, max, Alpha(kGlass, alpha * 0.9f), 0.0f, 0);
			Draw::AddRectFilledMultiColor(a_draw, { max.x, min.y }, { max.x + 60.0f * a_s, max.y }, Alpha(kGlass, alpha * 0.9f), Alpha(kGlass, 0.0f), Alpha(kGlass, 0.0f),
				Alpha(kGlass, alpha * 0.9f));
			const float run = 90.0f * a_s;
			for (const float ly : { min.y, max.y }) {
				Draw::AddLine(a_draw, { min.x - run, ly }, { max.x + run, ly }, Alpha(kGlassEdge, alpha), 1.0f);
			}
			Cross(a_draw, { min.x - run - 8.0f * a_s, (min.y + max.y) * 0.5f }, 4.0f * a_s, Alpha(kGlassEdge, alpha * 1.6f));
			Cross(a_draw, { max.x + run + 8.0f * a_s, (min.y + max.y) * 0.5f }, 4.0f * a_s, Alpha(kGlassEdge, alpha * 1.6f));
			for (const float ly : { min.y, max.y }) {
				Stud(a_draw, { min.x - run, ly }, 3.0f * a_s, alpha);
				Stud(a_draw, { max.x + run, ly }, 3.0f * a_s, alpha);
			}

			int active = -1;
			for (std::size_t i = 0; i < orderShouts.size(); ++i) {
				if (a_view.shout != Shout::kNone && orderShouts[i] == a_view.shout) {
					active = static_cast<int>(i);
				}
			}
			const float shown = state.tw.Toward("orderPillShown", 0, active >= 0 ? 1.0f : 0.0f, 10.0f);
			if (active >= 0) {
				state.tw.Raw("orderPillAt", 0, static_cast<float>(active)) = static_cast<float>(active);
			}
			const float px = state.tw.Toward("orderPill", 0, state.tw.Raw("orderPillAt", 0, 0.0f), 14.0f);
			if (shown > 0.01f) {
				const V pmin{ min.x + px * segW, min.y + 1.0f };
				const V pmax{ pmin.x + segW, max.y - 1.0f };
				Lit(a_draw, pmin, pmax, alpha * shown * 0.8f);
			}

			for (std::size_t i = 0; i < orderShouts.size(); ++i) {
				const auto  shout = orderShouts[i];
				const auto& info = Info(shout);
				const V     smin{ min.x + segW * static_cast<float>(i), min.y };
				const V     smax{ smin.x + segW, max.y };
				bool        pointed = false;
				Im::PushID(5000 + static_cast<int>(i));
				const bool clicked = Hit("order", smin, { segW, h }, pointed) && live;
				Im::PopID();
				const bool  focused = rowFocused && state.item == static_cast<int>(i);
				const auto  key = settings.orderKeys[i] ? std::format("  ({})", KeyName(settings.orderKeys[i])) : ""s;
				const auto  tip = std::format("{}{}\nReplaces every companion's own orders.", info.summary, key);
				if (pointed) {
					Hovering(0x200 + i);
					Tip(tip);
				} else if (focused) {
					TipBeside(tip, smin, smax);
				}
				float& flash = state.tw.Raw("orderFlash", i, 0.0f);
				if (clicked || (focused && a_input.activate)) {
					Battle::Command command;
					command.type = Battle::Command::Type::kShout;
					command.shout = shout;
					Battle::Queue(command);
					flash = 1.0f;
					Sound("UIMenuOK");
				}
				flash = Follow(flash, 0.0f, state.tw.dt, 3.0f);
				const float hover = state.tw.Toward("orderHover", i, pointed || focused ? 1.0f : 0.0f, 16.0f);
				if (focused) {
					FocusOutline(a_draw, { smin.x + 6.0f * a_s, smin.y + 6.0f * a_s }, { smax.x - 6.0f * a_s, smax.y - 6.0f * a_s }, 0.0f, alpha);
				}
				if (i > 0) {
					Draw::AddLine(a_draw, { smin.x, smin.y + 14.0f * a_s }, { smin.x, smax.y - 14.0f * a_s }, Alpha(kGlassEdge, alpha * 0.7f), 1.0f);
					Stud(a_draw, { smin.x, smin.y }, 3.0f * a_s, alpha);
					Stud(a_draw, { smin.x, smax.y }, 3.0f * a_s, alpha);
				}
				const bool  on = static_cast<int>(i) == active;
				const float lit = std::max({ on ? 1.0f : 0.0f, hover * 0.7f, flash });
				const float cx = smin.x + segW * 0.5f;
				Glyph(a_draw, { cx, smin.y + 27.0f * a_s - 2.0f * a_s * hover }, (24.0f + 3.0f * flash) * a_s, orderGlyphs[i], Alpha(Mix(kTextDim, kText, lit), alpha));
				const std::string name = Upper(info.name);
				const float       lw = LabelWidth(13.0f * a_s, name, 1.6f * a_s);
				Label(a_draw, { cx - lw * 0.5f, smin.y + 46.0f * a_s }, Alpha(Mix(kTextDim, kText, lit), alpha), 13.0f * a_s, name, 1.6f * a_s);
			}
			return { { min.x - 70.0f * a_s, min.y - 10.0f * a_s }, { max.x + 70.0f * a_s, max.y + 10.0f * a_s } };
		}

				// --- Sending someone at an enemy -----------------------------------------------------------

		// Clicking an enemy offers who to send, in a small glass list beside them.
		void DrawEnemyMenu(Im::ImDrawList* a_draw, const View::Snapshot& a_view, const View::Member* a_selected, V a_display, float a_s)
		{
			if (!state.enemyMenu) {
				return;
			}
			const View::Enemy* enemy = nullptr;
			for (const auto& e : a_view.enemies) {
				if (e.id == state.enemyMenu) {
					enemy = &e;
				}
			}
			if (!enemy || !enemy->head.on || state.closing) {
				state.enemyMenu = 0;
				return;
			}
			struct Item
			{
				std::string label;
				int         action;  // 0 send the chosen one, 1 send everyone, 2 call everyone off
			};
			std::vector<Item> items;
			bool              anyOn = false;
			for (const auto& m : a_view.party) {
				anyOn = anyOn || (!m.summon && m.marked == enemy->id);
			}
			if (a_selected) {
				items.push_back({ a_selected->marked == enemy->id ? std::format("Call {} off", a_selected->name) : std::format("Send {}", a_selected->name), 0 });
			}
			items.push_back({ "Send everyone", 1 });
			if (anyOn) {
				items.push_back({ "Call everyone off", 2 });
			}

			state.enemyMenuT = std::min(state.enemyMenuT + state.tw.dt / 0.16f, 1.0f);
			const float a = EaseOut(state.enemyMenuT);
			const float size = 16.5f * a_s, rowH = 32.0f * a_s, inset = 16.0f * a_s;
			float       width = TextSize(13.5f * a_s, enemy->name).x + inset * 2.0f + 40.0f * a_s;
			for (const auto& item : items) {
				width = std::max(width, TextSize(size, item.label).x + inset * 2.0f);
			}
			const V anchor{ enemy->head.x * a_display.x, enemy->head.y * a_display.y };
			V       min{ anchor.x + 28.0f * a_s, anchor.y - 20.0f * a_s + (1.0f - a) * 6.0f * a_s };
			if (min.x + width > a_display.x - 490.0f * a_s) {
				min.x = anchor.x - 28.0f * a_s - width;  // the companion's panels own the right side
			}
			const float height = 34.0f * a_s + rowH * static_cast<float>(items.size()) + 8.0f * a_s;
			const V     max{ min.x + width, min.y + height };
			state.enemyMin = min;
			state.enemyMax = max;
			Glass(a_draw, min, max, a, 0.0f);
			Band(a_draw, { min.x + 1.0f, min.y + 1.0f }, { max.x - 1.0f, min.y + 30.0f * a_s }, a);
			Text(a_draw, { min.x + inset, min.y + 8.0f * a_s }, Alpha(kText, a), 13.5f * a_s, Upper(std::format("{}  /  level {}", enemy->name, enemy->level)));

			const V    mouse = Im::GetIO()->MousePos;
			const bool press = Im::GetIO()->MouseClicked[0];
			for (std::size_t i = 0; i < items.size(); ++i) {
				const V     rmin{ min.x + 5.0f * a_s, min.y + 34.0f * a_s + rowH * static_cast<float>(i) };
				const V     rmax{ max.x - 5.0f * a_s, rmin.y + rowH };
				const bool  pointed = Inside(mouse, rmin, rmax);
				const float hover = state.tw.Toward("enemyItem", i, pointed ? 1.0f : 0.0f, 18.0f);
				if (pointed) {
					Hovering(0x900 + i);
				}
				Highlight(a_draw, rmin, rmax, a * hover);
				Text(a_draw, { rmin.x + inset - 5.0f * a_s, rmin.y + (rowH - TextSize(size, items[i].label).y) * 0.5f }, Alpha(pointed ? kText : kTextDim, a), size,
					items[i].label);
				if (pointed && press) {
					const auto& item = items[i];
					for (const auto& m : a_view.party) {
						if (m.summon) {
							continue;
						}
						if (item.action == 0 && a_selected && m.id == a_selected->id) {
							Simple(Battle::Command::Type::kMark, m.id, m.marked == enemy->id ? 0 : enemy->id);
						} else if (item.action == 1) {
							Simple(Battle::Command::Type::kMark, m.id, enemy->id);
						} else if (item.action == 2 && m.marked == enemy->id) {
							Simple(Battle::Command::Type::kMark, m.id, 0);
						}
					}
					Sound("UIMenuOK");
					state.enemyMenu = 0;
				}
			}
			if (press && state.enemyMenu && !Inside(mouse, min, max) && state.clock - state.enemyMenuOpenedAt > 0.05f) {
				state.enemyMenu = 0;
			}
		}

		// --- Key hints -------------------------------------------------------------------------

		struct Hint
		{
			std::string_view key;
			std::string_view what;
		};

		constexpr std::array<Hint, 5> padHints{ { { "LB / RB", "Companion" }, { "D-pad", "Move" }, { "A", "Choose" }, { "X", "Ability now" }, { "B", "Close" } } };
		constexpr std::array<Hint, 5> keyHints{ { { "Click", "Send at an enemy" }, { "Tab", "Companion" }, { "Arrows", "Move" }, { "Enter", "Choose" }, { "Esc", "Close" } } };

		// Each key in its own small cap, what it does beside it.
		void DrawHints(Im::ImDrawList* a_draw, V a_display, float a_s, float a_alpha)
		{
			const auto& hints = padUsed.load() ? padHints : keyHints;
			const float keySize = 14.0f * a_s, textSize = 15.0f * a_s;
			const float capH = 24.0f * a_s, between = 26.0f * a_s;
			float       total = 0.0f;
			for (const auto& h : hints) {
				total += std::max(capH, TextSize(keySize, h.key).x + 12.0f * a_s) + 8.0f * a_s + TextSize(textSize, h.what).x + between;
			}
			total -= between;
			float       x = a_display.x * 0.5f - total * 0.5f;
			const float y = a_display.y - 34.0f * a_s;
			for (const auto& h : hints) {
				const float w = Keycap(a_draw, { x, y }, capH, keySize, h.key, a_alpha);
				x += w + 8.0f * a_s;
				Text(a_draw, { x, y + (capH - TextSize(textSize, h.what).y) * 0.5f }, Alpha(kTextDim, a_alpha), textSize, h.what);
				x += TextSize(textSize, h.what).x + between;
			}
		}

		// --- The window ------------------------------------------------------------------------

		int ItemCount(int a_section, const View::Member* a_member, std::size_t a_enemies)
		{
			switch (a_section) {
			case kRoleSection:
				return static_cast<int>(Role::kCount);
			case kFocusSection:
			case kPositionSection:
				return a_member ? 1 : 0;  // one value, stepped with left and right
			case kAbilitySection:
				if (!a_member) {
					return 0;
				}
				return a_member->orders.role == Role::kHealer ? 3 : (a_member->orders.role == Role::kTank ? 2 : 0);
			case kFieldSection:
				return static_cast<int>(a_enemies);
			case kOrdersSection:
				return static_cast<int>(orderShouts.size());
			default:
				return 0;
			}
		}

		void DrawTip(V a_display, float a_s)
		{
			state.tipFade = Follow(state.tipFade, state.tipShown ? 1.0f : 0.0f, state.tw.dt, 18.0f);
			if (state.tipFade < 0.02f || state.tip.empty()) {
				return;
			}
			auto*       draw = Im::GetForegroundDrawList();
			const float size = 15.0f * a_s;
			const float width = 300.0f * a_s;
			const float ratio = size / std::max(Im::GetFontSize(), 1.0f);
			const auto  measured = Im::CalcTextSize(state.tip.c_str(), nullptr, false, width / ratio);
			const V     box{ std::min(measured.x * ratio, width) + 28.0f * a_s, measured.y * ratio + 22.0f * a_s };
			const V     mouse = Im::GetIO()->MousePos;
			V           at{ mouse.x + 20.0f * a_s, mouse.y + 18.0f * a_s };
			if (state.tipAnchored) {
				// Beside the focused element: to its left if there is room, else to its right.
				at = { state.tipMin.x - box.x - 14.0f * a_s, state.tipMin.y };
				if (at.x < 12.0f) {
					at.x = state.tipMax.x + 14.0f * a_s;
				}
			}
			at.x = std::min(at.x, a_display.x - box.x - 12.0f);
			at.y = std::min(at.y, a_display.y - box.y - 12.0f);
			at.y += (1.0f - state.tipFade) * 6.0f * a_s;
			Glass(draw, at, Add(at, box), state.tipFade, 0.0f);
			Draw::AddRectFilled(draw, at, { at.x + 2.0f * a_s, at.y + box.y }, Alpha(kEdge, state.tipFade), 0.0f, 0);
			Paragraph(draw, { at.x + 14.0f * a_s, at.y + 11.0f * a_s }, Alpha(kText, state.tipFade), size, state.tip, width);
		}

		void __stdcall RenderPlan()
		{
			if (!plan || !plan->IsOpen) {
				return;
			}
			auto*       io = Im::GetIO();
			const float dt = std::min(io->DeltaTime, 0.05f);
			state.tw.dt = dt;
			state.clock += dt;

			if (justOpened.exchange(false)) {
				state.open = 0.0f;
				state.closing = false;
				state.section = kRoleSection;
				state.item = 0;
				padDx = padDy = padShoulder = 0;
				padA = padB = padX = padY = false;
			}
			if (closeRequested.exchange(false) && !state.closing) {
				state.closing = true;
				Battle::RequestClose();  // time eases back while the plan fades out
			}
			// A load or a new game closed the plan underneath us.
			if (!Battle::PlanOpen() && !state.closing) {
				plan->IsOpen = false;
				return;
			}
			state.open = std::clamp(state.open + (state.closing ? -dt / 0.24f : dt / 0.5f), 0.0f, 1.0f);
			if (state.closing && state.open <= 0.0f) {
				plan->IsOpen = false;
				state.closing = false;
				return;
			}
			const float appear = state.closing ? Smooth(state.open) : state.open;

			Battle::RefreshView();
			const auto  view = Battle::ReadView();
			const V     display = io->DisplaySize;
			const float s = std::clamp(display.y / 1080.0f, 0.65f, 2.5f) * Settings::Get().uiScale;
			uiScale = s;
			state.tipShown = false;

			Im::SetNextWindowPos({ 0, 0 }, Im::ImGuiCond_Always, { 0, 0 });
			Im::SetNextWindowSize(display, Im::ImGuiCond_Always);
			Im::PushStyleVar(Im::ImGuiStyleVar_WindowBorderSize, 0.0f);
			Im::PushStyleVar(Im::ImGuiStyleVar_WindowPadding, V{ 0, 0 });
			Im::PushStyleColor(Im::ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
			Im::PushStyleColor(Im::ImGuiCol_Text, kText);
			bool open = true;
			if (Im::Begin("FightWithMeBattlePlan", &open,
					Im::ImGuiWindowFlags_NoTitleBar | Im::ImGuiWindowFlags_NoResize | Im::ImGuiWindowFlags_NoMove | Im::ImGuiWindowFlags_NoScrollbar |
						Im::ImGuiWindowFlags_NoScrollWithMouse | Im::ImGuiWindowFlags_NoSavedSettings | Im::ImGuiWindowFlags_NoNav |
						Im::ImGuiWindowFlags_NoBringToFrontOnFocus)) {
				auto* draw = Im::GetWindowDrawList();
				auto* back = Im::GetBackgroundDrawList();
				Vignette(back, display, Stage(appear, 0.0f, 0.5f));

				if (!view || !view->inGame) {
					TextCentered(draw, { display.x * 0.5f, display.y * 0.5f }, Alpha(kTextDim, appear), 18.0f * s, "Gathering the party...");
				} else {
					// Input from the pad, the keyboard and the mouse.
					Input input;
					int   dy = padDy.exchange(0), shoulder = padShoulder.exchange(0);
					input.dx = padDx.exchange(0);
					input.activate = padA.exchange(false);
					const bool cancel = padB.exchange(false);
					input.now = padX.exchange(false);
					input.flipAuto = padY.exchange(false);
					input.dx += Im::IsKeyPressed(Im::ImGuiKey_RightArrow) - Im::IsKeyPressed(Im::ImGuiKey_LeftArrow);
					dy += Im::IsKeyPressed(Im::ImGuiKey_DownArrow) - Im::IsKeyPressed(Im::ImGuiKey_UpArrow);
					if (Im::IsKeyPressed(Im::ImGuiKey_Tab, false)) {
						shoulder += io->KeyShift ? -1 : 1;
					}
					input.activate = input.activate || Im::IsKeyPressed(Im::ImGuiKey_Enter, false);
					if (state.closing) {
						input = {};
						dy = shoulder = 0;
					}
					if (input.dx || dy || input.activate || shoulder || input.now || input.flipAuto) {
						state.keyboardNav = true;
					}
					if (std::abs(io->MouseDelta.x) + std::abs(io->MouseDelta.y) > 2.0f) {
						state.keyboardNav = false;
					}
					if (cancel && !state.closing) {
						open = false;
					}
					if (shoulder) {
						StepMember(*view, shoulder);
					}
					const auto* member = Selected(*view);
					// No enemies about: the party view, the camera face to face with the companion.
					const bool partyView = view->enemies.empty() && !view->inCombat;
					Camera::Focus(member && !state.closing ? member->id : 0, partyView);

					// Changing companion fades the card over.
					if (member && member->id != state.shown) {
						if (state.shown != 0) {
							Sound("UIMenuPrevNext");
						}
						state.swap = state.shown == 0 ? 1.0f : 0.0f;
						state.shown = member->id;
					}
					state.swap = std::min(state.swap + dt / 0.24f, 1.0f);

					if (dy && state.section == kRoleSection && member) {
						const int row = state.item / 3 + (dy > 0 ? 1 : -1);
						if (row >= 0 && row < 2) {
							state.item = row * 3 + state.item % 3;
							dy = 0;
						}
					}
					if (dy) {
						int next = state.section;
						for (int tries = 0; tries < kSectionCount; ++tries) {
							next = std::clamp(next + (dy > 0 ? 1 : -1), 0, kSectionCount - 1);
							if (ItemCount(next, member, view->enemies.size()) > 0) {
								break;
							}
						}
						state.section = next;
						// Entering the role tiles starts on the role they have; from below, on the
						// bottom row.
						state.item = next == kRoleSection && member ? static_cast<int>(member->orders.role) : 0;
						if (next == kRoleSection && dy < 0 && state.item < 3) {
							state.item += 3;
						}
					}
					const int count = ItemCount(state.section, member, view->enemies.size());
					// Left and right walk the items of a row; on the focus, the position and the
					// healer's slider they change the value instead.
					const bool cycles = state.section == kFieldSection || state.section == kOrdersSection || (state.section == kAbilitySection && state.item < 2);
					bool       moved = cycles && input.dx != 0 && count > 0;
					if (moved) {
						state.item = (state.item + input.dx + count) % count;
					}
					// Across the role tiles, left and right stay on their row.
					if (state.section == kRoleSection && input.dx != 0) {
						state.item = (state.item / 3) * 3 + std::clamp(state.item % 3 + input.dx, 0, 2);
						moved = true;
					}
					state.item = count > 0 ? std::clamp(state.item, 0, count - 1) : 0;
					Input cardInput = input;
					if (moved) {
						cardInput.dx = 0;
					}

					// The battlefield's keyboard focus walks the markers left to right.
					std::vector<const View::Enemy*> field;
					for (const auto& e : view->enemies) {
						if (e.head.on) {
							field.push_back(&e);
						}
					}
					std::sort(field.begin(), field.end(), [](const View::Enemy* a, const View::Enemy* b) { return a->head.x < b->head.x; });
					state.fieldEnemy = state.section == kFieldSection && state.item < static_cast<int>(field.size()) ? field[static_cast<std::size_t>(state.item)]->id : 0;
					if (state.section == kFieldSection && input.activate && member && state.fieldEnemy) {
						Simple(Battle::Command::Type::kMark, member->id, member->marked == state.fieldEnemy ? 0 : state.fieldEnemy);
					}

					// Title in the game's own capitals, with a rule that draws outward from the centre.
					const float titleIn = Stage(appear, 0.0f, 0.55f);
					const float titleY = 44.0f * s - (1.0f - titleIn) * 10.0f * s;
					const float ruleY = titleY + 24.0f * s;
					{
						const float            size = 24.0f * s;
						const float            tracking = 6.0f * s;
						const std::string_view title = partyView ? "YOUR PARTY" : "BATTLE PLAN";
						const float            w = LabelWidth(size, title, tracking);
						const float            cx = display.x * 0.5f;
						const float            bandW = w * 0.5f + 70.0f * s;
						const float            top = titleY - 22.0f * s, bottom = titleY + 18.0f * s;
						// The band is darkest under the title and fades out to both sides.
						Draw::AddRectFilledMultiColor(draw, { cx - bandW, top }, { cx, bottom }, Alpha(kAccent, 0.0f), Alpha(kAccent, titleIn * 0.85f), Alpha(kAccent, titleIn * 0.85f),
							Alpha(kAccent, 0.0f));
						Draw::AddRectFilledMultiColor(draw, { cx, top }, { cx + bandW, bottom }, Alpha(kAccent, titleIn * 0.85f), Alpha(kAccent, 0.0f), Alpha(kAccent, 0.0f),
							Alpha(kAccent, titleIn * 0.85f));
						Label(draw, { cx - w * 0.5f, titleY - size * 0.55f }, Alpha(kText, titleIn), size, title, tracking);
						Slashes(draw, { cx + w * 0.5f + 18.0f * s, top + 12.0f * s }, 16.0f * s, Alpha(kText, titleIn * 0.5f));
						// Hairlines along the band's edges, drawing outward, ending in crosses.
						const float run = bandW + 140.0f * s * Stage(appear, 0.12f, 0.6f);
						for (const float ly : { top, bottom }) {
							Draw::AddLine(draw, { cx - run, ly }, { cx + run, ly }, Alpha(kGlassEdge, titleIn), 1.0f);
						}
						Cross(draw, { cx - run - 8.0f * s, (top + bottom) * 0.5f }, 4.0f * s, Alpha(kGlassEdge, titleIn * 1.6f));
						Cross(draw, { cx + run + 8.0f * s, (top + bottom) * 0.5f }, 4.0f * s, Alpha(kGlassEdge, titleIn * 1.6f));
						for (const float ly : { top, bottom }) {
							Stud(draw, { cx - run, ly }, 3.0f * s, titleIn);
							Stud(draw, { cx + run, ly }, 3.0f * s, titleIn);
						}
						Gem(draw, { cx, bottom }, 5.0f * s, titleIn);
					}
					int companions = 0;
					for (const auto& m : view->party) {
						companions += m.summon ? 0 : 1;
					}
					const auto timeLine = std::string(Settings::Get().stopTime ? "Time stopped" : "Time slowed");
					const auto subtitle = partyView ? std::format("{}   ·   {} {}", timeLine, companions, companions == 1 ? "companion" : "companions") :
					                                  std::format("{}   ·   {} {}   ·   {} {}", timeLine, companions, companions == 1 ? "companion" : "companions",
														  view->enemies.size(), view->enemies.size() == 1 ? "enemy" : "enemies");
					const float subW = TextSize(15.0f * s, subtitle).x;
					Glyph(draw, { display.x * 0.5f - subW * 0.5f - 14.0f * s, ruleY + 22.0f * s }, 13.0f * s, 0xF252, Alpha(kEdge, Stage(appear, 0.2f, 0.5f)));
					TextCentered(draw, { display.x * 0.5f + 4.0f * s, ruleY + 22.0f * s }, Alpha(kTextDim, Stage(appear, 0.2f, 0.5f)), 15.0f * s, subtitle);

					std::vector<Rect> panels;
					panels.push_back(DrawOrders(draw, *view, { display.x * 0.5f, ruleY + 46.0f * s }, s, appear, input));
					panels.push_back(DrawStrip(draw, *view, member, display, s, appear));
					if (member) {
						const float width = 440.0f * s;
						panels.push_back(DrawCompanion(draw, *view, *member, { display.x - 40.0f * s - width, 112.0f * s }, width, s, appear, cardInput));
						const float sheetIn = state.tw.Toward("partyView", 0, partyView ? 1.0f : 0.0f, 8.0f);
						if (sheetIn > 0.01f) {
							panels.push_back(DrawSheet(draw, *member, { 40.0f * s, 112.0f * s }, 380.0f * s, s, appear * sheetIn));
						}
					}
					DrawHints(draw, display, s, Stage(appear, 0.35f, 0.5f));
					if (state.keyboardNav) {
						Hovering(0x1000 + static_cast<std::uint64_t>(state.section) * 64 + static_cast<std::uint64_t>(state.item));
					}

					// Face to face, the markers over heads would only be in the way.
					if (!partyView) {
						DrawWorld(back, *view, member, display, s, appear, panels);
						DrawEnemyMenu(draw, *view, member, display, s);
					}
				}
			}
			Im::End();
			Im::PopStyleColor(2);
			Im::PopStyleVar(2);
			DrawTip(display, s);

			// A soft tick whenever the pointer or the pad moves onto something new.
			if (state.hoverKey != 0 && state.hoverKey != state.hoverKeyLast && !state.closing && state.clock - state.lastTick > 0.045f) {
				Sound("UIMenuFocus");
				state.lastTick = state.clock;
			}
			state.hoverKeyLast = state.hoverKey;
			state.hoverKey = 0;

			if (!open && !state.closing) {
				state.closing = true;
				Battle::RequestClose();
			}
		}

		// --- HUD notices --------------------------------------------------------------------------

		void __stdcall RenderHUD()
		{
			// Menu Framework may close the window on its own (its toggle key, a load).
			if (plan && !plan->IsOpen && Battle::PlanOpen()) {
				Battle::RequestClose();
			}
			if (plan && plan->IsOpen) {
				return;
			}
			const auto notices = Notices::Recent(4800);
			if (notices.empty()) {
				return;
			}
			auto*       draw = Im::GetForegroundDrawList();
			const V     display = Im::GetIO()->DisplaySize;
			const float s = std::clamp(display.y / 1080.0f, 0.65f, 2.5f) * Settings::Get().uiScale;
			uiScale = s;
			const auto  now = ::GetTickCount64();
			float       y = display.y * 0.64f;
			for (auto it = notices.rbegin(); it != notices.rend(); ++it) {
				const float age = static_cast<float>(now - it->at) / 1000.0f;
				const float in = EaseOut(age / 0.35f);
				const float alpha = std::min(in, Clamp01((4.8f - age) / 0.9f));
				if (alpha <= 0.01f) {
					continue;
				}
				U32           accent = kEdge;
				std::uint32_t glyph = 0xF0C0;
				if (it->kind == Notices::Kind::kTaunt) {
					accent = kRed;
					glyph = 0xF0A1;
				} else if (it->kind == Notices::Kind::kHeal) {
					accent = kGreen;
					glyph = 0xF4BE;
				} else if (it->kind == Notices::Kind::kOrder) {
					accent = kEdge;
					glyph = 0xF0C0;
				} else if (it->kind == Notices::Kind::kWarning) {
					accent = IM_COL32(236, 172, 72, 255);
					glyph = 0xF071;
				}
				const float size = 17.0f * s;
				const auto  ts = TextSize(size, it->text);
				const float h = ts.y + 18.0f * s;
				const float slide = (1.0f - in) * -40.0f * s;
				const V     min{ 44.0f * s + slide, y - h };
				const V     max{ min.x + ts.x + 58.0f * s, y };
				Glass(draw, min, max, alpha, 0.0f);
				Draw::AddRectFilledMultiColor(draw, min, { min.x + h * 2.0f, max.y }, Alpha(accent, alpha * 0.35f), Alpha(accent, 0.0f), Alpha(accent, 0.0f),
					Alpha(accent, alpha * 0.35f));
				Draw::AddRectFilled(draw, min, { min.x + 3.0f * s, max.y }, Alpha(accent, alpha), 0.0f, 0);
				Glyph(draw, { min.x + h * 0.5f + 4.0f * s, (min.y + max.y) * 0.5f }, 15.0f * s, glyph, Alpha(accent, alpha));
				Text(draw, { min.x + h + 10.0f * s, min.y + 9.0f * s }, Alpha(kText, alpha), size, it->text);
				y = min.y - 8.0f * s;
			}
		}

		// BATTLE_PLAN_END

		// SETTINGS_BEGIN: the settings pages, kept in one block for the same reason.

		// Menu Framework pages, laid out like Walk With Me's: a header line, collapsible
		// groups with an icon, two-column rows for values and grids of switches, each with
		// hover help. Changes apply at once and are written back to FightWithMe.ini.

		constexpr U32 kInk = IM_COL32(17, 17, 18, 242);
		constexpr U32 kPanel = IM_COL32(30, 30, 31, 225);

		float E(float a_value) { return Im::GetFontSize() * a_value; }

		void Space(float a_value = 0.16f) { Im::Dummy({ 0, E(a_value) }); }

		void Note(const char* a_text)
		{
			Im::PushStyleColor(Im::ImGuiCol_Text, kTextDim);
			Im::TextWrapped("%s", a_text);
			Im::PopStyleColor();
		}

		void Help(const char* a_text)
		{
			if (Settings::Get().tooltips && Im::IsItemHovered(Im::ImGuiHoveredFlags_AllowWhenDisabled)) {
				Im::BeginTooltip();
				Im::PushTextWrapPos(E(30));
				Im::TextUnformatted(a_text);
				Im::PopTextWrapPos();
				Im::EndTooltip();
			}
		}

		struct Theme
		{
			Theme()
			{
				Im::PushStyleColor(Im::ImGuiCol_Text, kText);
				Im::PushStyleColor(Im::ImGuiCol_TextDisabled, kTextFaint);
				Im::PushStyleColor(Im::ImGuiCol_WindowBg, kInk);
				Im::PushStyleColor(Im::ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
				Im::PushStyleColor(Im::ImGuiCol_FrameBg, kPanel);
				Im::PushStyleColor(Im::ImGuiCol_FrameBgHovered, IM_COL32(72, 24, 22, 232));
				Im::PushStyleColor(Im::ImGuiCol_FrameBgActive, IM_COL32(98, 28, 26, 240));
				Im::PushStyleColor(Im::ImGuiCol_Button, kPanel);
				Im::PushStyleColor(Im::ImGuiCol_ButtonHovered, IM_COL32(86, 24, 22, 236));
				Im::PushStyleColor(Im::ImGuiCol_ButtonActive, IM_COL32(122, 32, 30, 240));
				Im::PushStyleColor(Im::ImGuiCol_CheckMark, kEdge);
				Im::PushStyleColor(Im::ImGuiCol_SliderGrab, kEdge);
				Im::PushStyleColor(Im::ImGuiCol_SliderGrabActive, IM_COL32(238, 100, 88, 255));
				Im::PushStyleColor(Im::ImGuiCol_Header, IM_COL32(76, 22, 20, 230));
				Im::PushStyleColor(Im::ImGuiCol_HeaderHovered, IM_COL32(100, 28, 26, 236));
				Im::PushStyleColor(Im::ImGuiCol_HeaderActive, IM_COL32(122, 32, 30, 240));
				Im::PushStyleColor(Im::ImGuiCol_Separator, IM_COL32(255, 255, 255, 54));
				Im::PushStyleColor(Im::ImGuiCol_PopupBg, kInk);
				Im::PushStyleVar(Im::ImGuiStyleVar_FramePadding, V{ E(0.2f), E(0.09f) });
				Im::PushStyleVar(Im::ImGuiStyleVar_ItemSpacing, V{ E(0.25f), E(0.12f) });
				Im::PushStyleVar(Im::ImGuiStyleVar_FrameRounding, 0.0f);
				Im::PushStyleVar(Im::ImGuiStyleVar_WindowPadding, V{ E(0.35f), E(0.25f) });
			}
			~Theme()
			{
				Im::PopStyleVar(4);
				Im::PopStyleColor(18);
			}
		};

		// Compact density inside the page; the child window keeps every style change to itself.
		struct SettingsPage
		{
			Theme theme;
			explicit SettingsPage(const char* a_id)
			{
				Im::BeginChild(a_id, { 0, 0 });
				Im::SetWindowFontScale(0.85f);
				Im::PushStyleVar(Im::ImGuiStyleVar_ItemSpacing, V{ 6, 3 });
				Im::PushStyleVar(Im::ImGuiStyleVar_FramePadding, V{ 4, 2 });
				Im::PushStyleVar(Im::ImGuiStyleVar_CellPadding, V{ 6, 2 });
			}
			~SettingsPage()
			{
				Im::PopStyleVar(3);
				Im::SetWindowFontScale(1);
				Im::EndChild();
			}
			SettingsPage(const SettingsPage&) = delete;
			SettingsPage& operator=(const SettingsPage&) = delete;
		};

		bool Group(const char* a_title, std::uint32_t a_glyph, bool a_open = true)
		{
			Space();
			const auto p = Im::GetCursorScreenPos();
			Glyph(Im::GetWindowDrawList(), { p.x + E(0.45f), p.y + E(0.6f) }, E(0.72f), a_glyph, kEdge);
			Im::Dummy({ E(1.35f), E(1.2f) });
			Im::SameLine(0, 0);
			Im::PushStyleColor(Im::ImGuiCol_Text, kText);
			const bool expanded = Im::CollapsingHeader(a_title, a_open ? Im::ImGuiTreeNodeFlags_DefaultOpen : 0);
			Im::PopStyleColor();
			return expanded;
		}

		void Heading(const char* a_text, std::uint32_t a_glyph)
		{
			Space(0.35f);
			const auto p = Im::GetCursorScreenPos();
			Glyph(Im::GetWindowDrawList(), { p.x + E(0.4f), p.y + E(0.5f) }, E(0.66f), a_glyph, kEdge);
			Im::Dummy({ E(1.05f), E(1) });
			Im::SameLine(0, 0);
			Im::PushStyleColor(Im::ImGuiCol_Text, kText);
			Im::TextUnformatted(a_text);
			Im::PopStyleColor();
			Im::Separator();
			Space(0.12f);
		}

		bool Rows(const char* a_id)
		{
			if (!Im::BeginTable(a_id, 2, Im::ImGuiTableFlags_SizingStretchProp | Im::ImGuiTableFlags_NoSavedSettings | Im::ImGuiTableFlags_PadOuterX)) {
				return false;
			}
			Im::TableSetupColumn("Setting", Im::ImGuiTableColumnFlags_WidthStretch, 0.40f);
			Im::TableSetupColumn("Value", Im::ImGuiTableColumnFlags_WidthStretch, 0.60f);
			return true;
		}

		bool Checks(const char* a_id, int a_maximum = 2)
		{
			// Checkbox and label share a cell; collapse to one column on narrow panels.
			const int columns = std::clamp(static_cast<int>(Im::GetContentRegionAvail().x / E(13)), 1, a_maximum);
			return Im::BeginTable(a_id, columns, Im::ImGuiTableFlags_SizingStretchSame | Im::ImGuiTableFlags_NoSavedSettings | Im::ImGuiTableFlags_PadOuterX);
		}

		// The page's working copy of the settings. Edits land here and are submitted at
		// the end of the frame; the main thread applies and saves them.
		Settings::Data draft;
		bool           haveDraft{ false };
		bool           dirty{ false };
		std::string    feedback;

		std::atomic<bool>          capturing{ false };
		std::atomic<int>           captured{ -1 };  // -1 nothing yet, -2 cancelled, else the scan code (0 clears)
		int                        captureSlot{ 0 };  // 0 the battle plan, 1 to 4 the party orders
		std::atomic<bool>          restoreMenuHotkey{ false }, previousMenuHotkey{ false };
		std::atomic<std::uint64_t> captureStarted{ 0 };

		void Slider(const char* a_name, float& a_value, float a_low, float a_high, const char* a_format, const char* a_hint)
		{
			Im::TableNextRow();
			Im::TableNextColumn();
			Im::AlignTextToFramePadding();
			Im::TextWrapped("%s", a_name);
			Help(a_hint);
			Im::TableNextColumn();
			Im::SetNextItemWidth(-1);
			Im::PushID(a_name);
			dirty |= Im::SliderFloat("##value", &a_value, a_low, a_high, a_format, Im::ImGuiSliderFlags_AlwaysClamp);
			Help(a_hint);
			Im::PopID();
		}

		// A 0..1 value shown as a percentage.
		void Percent(const char* a_name, float& a_value, int a_low, int a_high, const char* a_hint)
		{
			int percent = static_cast<int>(std::round(a_value * 100.0f));
			Im::TableNextRow();
			Im::TableNextColumn();
			Im::AlignTextToFramePadding();
			Im::TextWrapped("%s", a_name);
			Help(a_hint);
			Im::TableNextColumn();
			Im::SetNextItemWidth(-1);
			Im::PushID(a_name);
			if (Im::SliderInt("##value", &percent, a_low, a_high, "%d%%", Im::ImGuiSliderFlags_AlwaysClamp)) {
				a_value = static_cast<float>(percent) / 100.0f;
				dirty = true;
			}
			Help(a_hint);
			Im::PopID();
		}

		void IntSlider(const char* a_name, int& a_value, int a_low, int a_high, const char* a_format, const char* a_hint)
		{
			Im::TableNextRow();
			Im::TableNextColumn();
			Im::AlignTextToFramePadding();
			Im::TextWrapped("%s", a_name);
			Help(a_hint);
			Im::TableNextColumn();
			Im::SetNextItemWidth(-1);
			Im::PushID(a_name);
			dirty |= Im::SliderInt("##value", &a_value, a_low, a_high, a_format, Im::ImGuiSliderFlags_AlwaysClamp);
			Help(a_hint);
			Im::PopID();
		}

		void Toggle(const char* a_name, bool& a_value, const char* a_hint)
		{
			Im::TableNextRow();
			Im::TableNextColumn();
			Im::AlignTextToFramePadding();
			Im::TextWrapped("%s", a_name);
			Help(a_hint);
			Im::TableNextColumn();
			Im::PushID(a_name);
			dirty |= Im::Checkbox("##value", &a_value);
			Help(a_hint);
			Im::PopID();
		}

		void Check(const char* a_name, bool& a_value, const char* a_hint)
		{
			Im::TableNextColumn();
			dirty |= Im::Checkbox(a_name, &a_value);
			Help(a_hint);
		}

		void CancelCapture()
		{
			capturing = false;
			if (restoreMenuHotkey.exchange(false)) {
				MF::SetHotkeyEnabled(previousMenuHotkey.load());
			}
		}

		void StartCapture(int a_slot)
		{
			captureSlot = a_slot;
			// Menu Framework's own toggle key would otherwise close the menu mid-capture.
			if (!restoreMenuHotkey.exchange(true)) {
				previousMenuHotkey = MF::IsHotkeyEnabled();
			}
			MF::SetHotkeyEnabled(false);
			captured = -1;
			captureStarted = ::GetTickCount64();
			capturing = true;
		}

		void Prepare()
		{
			if (!haveDraft) {
				draft = Settings::Get();
				haveDraft = true;
			}
			// Nothing pending: follow the live settings, which a save or an INI edit may have moved.
			if (!dirty && Settings::AppliedRevision() == Settings::RequestedRevision() && !capturing.load()) {
				draft = Settings::Get();
				feedback = Settings::LastSaveFailed() ? "Could not save FightWithMe.ini. Check file access, then change the setting again." : "";
			}
			const int key = captured.exchange(-1);
			if (capturing.load() && key != -1) {
				if (key >= 0) {
					auto& target = captureSlot == 0 ? draft.planKey : draft.orderKeys[static_cast<std::size_t>(captureSlot - 1)];
					target = static_cast<std::uint32_t>(key);
					dirty = true;
				}
				CancelCapture();
			}
			if (capturing.load() && ::GetTickCount64() - captureStarted.load() > 15000) {
				CancelCapture();
			}
		}

		std::size_t CompanionCount(const View::Snapshot* a_view)
		{
			if (!a_view) {
				return 0;
			}
			return static_cast<std::size_t>(std::count_if(a_view->party.begin(), a_view->party.end(), [](const View::Member& m) { return !m.summon; }));
		}

		void Header(const char* a_title)
		{
			Battle::RefreshView();
			const auto view = Battle::ReadView();
			Im::PushStyleColor(Im::ImGuiCol_Text, kEdge);
			Im::Text("FIGHT WITH ME / %s", a_title);
			Im::PopStyleColor();
			Im::SameLine(0, E(0.65f));
			const auto count = CompanionCount(view.get());
			Im::TextDisabled("%zu companion%s in reach", count, count == 1 ? "" : "s");
			Im::PushStyleColor(Im::ImGuiCol_Separator, kEdge);
			Im::Separator();
			Im::PopStyleColor();
			Space();
		}

		void Footer()
		{
			if (dirty) {
				Settings::Submit(draft);
				dirty = false;
				feedback.clear();
			}
			if (!feedback.empty()) {
				Note(feedback.c_str());
			}
		}

		// A small button that puts this page's values back; the caller copies the fields.
		bool ResetButton(const char* a_hint)
		{
			Space(0.5f);
			const bool clicked = Im::SmallButton("Reset this page");
			Help(a_hint);
			return clicked;
		}

		// --- Party ---------------------------------------------------------------------

		// a_why: an empty reason means the choice is on offer.
		void RosterCombo(const char* a_id, int& a_value, int a_count, const char* (*a_name)(int), const std::function<std::string_view(int)>& a_why,
			bool& a_changed)
		{
			Im::SetNextItemWidth(-1);
			if (Im::BeginCombo(a_id, a_name(a_value))) {
				for (int i = 0; i < a_count; ++i) {
					const auto why = a_why ? a_why(i) : std::string_view{};
					Im::BeginDisabled(!why.empty() && i != a_value);
					if (Im::Selectable(a_name(i), i == a_value) && i != a_value) {
						a_value = i;
						a_changed = true;
					}
					Im::EndDisabled();
					if (!why.empty()) {
						const std::string text(why);
						Help(text.c_str());
					}
				}
				Im::EndCombo();
			}
		}

		const char* RoleName(int a_index) { return roles[static_cast<std::size_t>(a_index)].name.data(); }
		const char* FocusName(int a_index) { return focuses[static_cast<std::size_t>(a_index)].name.data(); }
		const char* PositionName(int a_index) { return positions[static_cast<std::size_t>(a_index)].name.data(); }

		void Roster(const View::Snapshot& a_view)
		{
			if (CompanionCount(&a_view) == 0) {
				Note("No companions in reach. Followers nearby, Walk With Me's party and summons appear here once loaded.");
				return;
			}
			if (!Im::BeginTable("roster", 5, Im::ImGuiTableFlags_SizingStretchProp | Im::ImGuiTableFlags_NoSavedSettings | Im::ImGuiTableFlags_PadOuterX |
												 Im::ImGuiTableFlags_RowBg)) {
				return;
			}
			Im::TableSetupColumn("Companion", Im::ImGuiTableColumnFlags_WidthStretch, 1.1f);
			Im::TableSetupColumn("Role", Im::ImGuiTableColumnFlags_WidthStretch, 0.9f);
			Im::TableSetupColumn("Focus", Im::ImGuiTableColumnFlags_WidthStretch, 1.2f);
			Im::TableSetupColumn("Position", Im::ImGuiTableColumnFlags_WidthStretch, 1.0f);
			Im::TableSetupColumn("Auto", Im::ImGuiTableColumnFlags_WidthFixed, E(2.4f));
			Im::TableHeadersRow();
			std::string summons;
			for (const auto& m : a_view.party) {
				if (m.summon) {
					summons += summons.empty() ? "" : ", ";
					summons += m.name;
					continue;
				}
				Im::PushID(static_cast<int>(m.id));
				Im::TableNextRow();
				Im::TableNextColumn();
				Im::AlignTextToFramePadding();
				Im::TextUnformatted(m.name.c_str());
				Im::SameLine();
				Im::TextDisabled("Lv %d", m.level);
				Orders orders = m.orders;
				bool   changed = false;
				int    role = static_cast<int>(orders.role), focus = static_cast<int>(orders.focus), position = static_cast<int>(orders.position);
				Im::TableNextColumn();
				const int before = role;
				RosterCombo("##role", role, static_cast<int>(Role::kCount), RoleName,
					[&](int i) { return m.arsenalKnown ? Why(LackFor(static_cast<Role>(i), m.arsenal, a_view.lendHeal)) : std::string_view{}; }, changed);
				Help(roles[static_cast<std::size_t>(role)].summary.data());
				Im::TableNextColumn();
				RosterCombo("##focus", focus, static_cast<int>(Focus::kCount), FocusName, nullptr, changed);
				Help(focuses[static_cast<std::size_t>(focus)].summary.data());
				Im::TableNextColumn();
				RosterCombo("##position", position, static_cast<int>(Position::kCount), PositionName,
					[&](int i) { return PositionAllowed(static_cast<Position>(i), a_view) ? std::string_view{} : std::string_view("Needs Walk With Me."); }, changed);
				Help(positions[static_cast<std::size_t>(position)].summary.data());
				Im::TableNextColumn();
				const bool ability = orders.role == Role::kTank || orders.role == Role::kHealer;
				Im::BeginDisabled(!ability);
				changed |= Im::Checkbox("##auto", &orders.autoAbility);
				Im::EndDisabled();
				Help(ability ? (orders.role == Role::kTank ? "Taunt without being told." : "Heal without being told.") :
							   "Only tanks and healers have an ability of their own.");
				if (changed) {
					if (role != before) {
						orders = ForRole(static_cast<Role>(role), orders);  // a new role brings its focus and position
					} else {
						orders.focus = static_cast<Focus>(focus);
						orders.position = static_cast<Position>(position);
					}
					SetOrders(m, orders);
				}
				Im::PopID();
			}
			Im::EndTable();
			if (!summons.empty()) {
				Im::TextDisabled("Also with you: %s. Summons are protected and healed, not commanded.", summons.c_str());
			}
		}

		void __stdcall RenderParty()
		{
			Prepare();
			SettingsPage page("FightWithMeParty");
			Header("PARTY");
			if (Rows("enabled")) {
				Toggle("Enable Fight With Me", draft.enabled, "Let Fight With Me direct companions in battle. Off hands every fight back to their own judgement at once.");
				Im::EndTable();
			}
			Heading("COMPANIONS", 0xF0C0);
			Battle::RefreshView();
			const auto view = Battle::ReadView();
			if (view) {
				Im::BeginDisabled(!draft.enabled);
				Roster(*view);
				Im::EndDisabled();
			}
			Note("Orders change at once and are kept in your save. The battle plan shows the same choices with the fight in view.");
			Space();
			if (Im::Button("Open the battle plan")) {
				Open();
			}
			Help("Slows the world and shows every companion, their target and the enemies around you.");
			if (Group("Whole party", 0xF0C0)) {
				Im::BeginDisabled(!draft.enabled);
				if (Im::Button("Everyone back to their role")) {
					Preset(Battle::Preset::kRoles);
				}
				Help("Sets every companion's stance and targets back to what their role suggests.");
				Im::EndDisabled();
				Note("Party orders (Attack!, To me!, Hold back, As you were) replace everyone's own orders at once. They are on the numpad by default; see Controls.");
			}
			if (Group("Roles", 0xF3ED)) {
				if (Rows("roles")) {
					Percent("Role strength", draft.styleWeight, 0, 100,
						"How far a role reshapes a companion's own way of fighting. 0% changes only whom they fight; 100% replaces their style with the role's.");
					Im::EndTable();
				}
				if (Checks("roleOptions")) {
					Check("Guess roles for new companions", draft.guessRoles,
						"The first time a companion joins: a shield makes a Tank, a two-hander a Bruiser, a bow an Archer, a staff or spells a Mage or Healer. "
						"An NFF tank or healer role is kept. Off: everyone starts as a Fighter.");
					Im::EndTable();
				}
			}
			if (Group("When the fight is over", 0xF252)) {
				if (Checks("emptyFights")) {
					Check("Stand down when no enemy is left", draft.leaveEmptyFights,
						"With weapons out and nobody left to fight, companions leave combat and fall back into Walk With Me's formation instead of searching.");
					Im::EndTable();
				}
				Im::BeginDisabled(!draft.leaveEmptyFights);
				if (Rows("emptyFightDelay")) {
					Slider("After", draft.leaveAfter, 0.5f, 10.0f, "%.1f s", "How long the fight must have nobody in it.");
					Im::EndTable();
				}
				Im::EndDisabled();
			}
			if (ResetButton("Role strength, role guessing and standing down back to their defaults. Companions' orders are not touched.")) {
				const auto d = Settings::Defaults();
				draft.styleWeight = d.styleWeight;
				draft.guessRoles = d.guessRoles;
				draft.leaveEmptyFights = d.leaveEmptyFights;
				draft.leaveAfter = d.leaveAfter;
				dirty = true;
			}
			Footer();
		}

		// --- Tanks -----------------------------------------------------------------------

		int exampleLevel = 30;

		void __stdcall RenderTanks()
		{
			Prepare();
			SettingsPage page("FightWithMeTanks");
			Header("TANKS");
			auto& t = draft.tuning;
			if (Group("Taunt", 0xF0A1)) {
				if (Checks("tauntOptions")) {
					Check("Allow taunts", draft.taunt, "Tanks challenge nearby enemies with a shield bang, a weapon flourish or a war cry.");
					Im::BeginDisabled(!draft.taunt);
					Check("Opening taunt", draft.tauntOpener, "Taunt once as a fight starts, before anyone is in trouble.");
					Im::EndDisabled();
					Check("Shield wall", draft.shieldWall,
						"A tank with a shield raises it against archers and casters shooting at them, turned to the nearest, and lowers it to fight once anything comes within reach.");
					Im::EndTable();
				}
				Im::BeginDisabled(!draft.taunt);
				if (Rows("tauntCost")) {
					Percent("Chance when ready", draft.tauntChance, 5, 100,
						"Once a taunt is ready and someone needs it, the tank weighs it up at every check and taunts with this chance. Lower values space taunts out.");
					Slider("Check every", draft.tauntCheckSeconds, 0.5f, 8.0f, "%.1f s", "How often a ready tank considers taunting.");
					Slider("Stamina cost", t.tauntStamina, 0.0f, 150.0f, "%.0f", "Stamina spent per taunt. A tank short of stamina waits.");
					Slider("Cooldown", t.cooldownBase, 2.0f, 60.0f, "%.1f s", "Seconds between taunts before the tank's level shortens it.");
					Slider("Shorter per level", t.cooldownPerLevel, 0.0f, 0.5f, "%.2f s", "Seconds taken off the cooldown for each level of the tank.");
					Slider("Shortest cooldown", t.cooldownMin, 0.5f, 30.0f, "%.1f s", "The cooldown never drops below this.");
					Slider("Glow", draft.glowSeconds, 0.0f, 4.0f, "%.1f s", "How long the red glow shows on a taunting tank. 0 turns it off.");
					Im::EndTable();
				}
				Im::EndDisabled();
			}
			if (Group("Who a taunt holds", 0xF3ED)) {
				Im::BeginDisabled(!draft.taunt);
				if (Rows("tauntReach")) {
					Slider("Level reach", t.tauntReachDivisor, 1.0f, 12.0f, "level / %.1f",
						"A tank holds enemies up to their own level plus this share of it for certain. Lower numbers reach further.");
					Percent("Chance lost per level", t.tauntFalloffPerLevel, 0, 50, "Beyond the certain range, each level costs this much of the chance to hold.");
					IntSlider("No chance after", t.tauntFalloffLevels, 0, 20, "%d levels", "Enemies this many levels past the certain range cannot be held at all.");
					IntSlider("Boss and dragon handicap", t.bossLevelBonus, 0, 20, "+%d levels", "Bosses and dragons count as this many levels higher.");
					IntSlider("Cowed below", t.cowedLevelGap, 1, 30, "%d levels", "Enemies this far below the tank stay held longer.");
					Slider("Cowed hold", t.cowedHoldMult, 1.0f, 3.0f, "%.2fx", "How much longer a cowed enemy stays held.");
					Slider("Hold time", t.holdBase, 1.0f, 20.0f, "%.1f s", "Seconds an enemy stays on the tank, before level.");
					Slider("Longer per level", t.holdPerLevel, 0.0f, 0.3f, "%.2f s", "Seconds added to the hold for each level of the tank.");
					Im::EndTable();
				}
				// The rule, worked through for one tank.
				Space(0.3f);
				if (Rows("tauntExample")) {
					IntSlider("Example tank level", exampleLevel, 1, 81, "%d", "Only for the example below; nothing is saved.");
					Im::EndTable();
				}
				const int   certain = exampleLevel + Rules::TauntReach(exampleLevel, t);
				const int   never = certain + t.tauntFalloffLevels + 1;
				const float sample = Rules::TauntChance(exampleLevel, certain + 3, false, t);
				const auto  line = std::format(
                    "A level {} tank holds anything up to level {} for certain, {:.0f}% of level {}, and nothing from level {}. "
                    "Bosses and dragons count {} higher. An enemy stays on them for {:.1f} s; the next taunt is ready after {:.1f} s.",
                    exampleLevel, certain, sample * 100.0f, certain + 3, never, t.bossLevelBonus, Rules::HoldSeconds(exampleLevel, exampleLevel, t),
                    Rules::TauntCooldown(exampleLevel, t));
				Note(line.c_str());
				Im::EndDisabled();
			}
			if (Group("Reach", 0xF140, false)) {
				Im::BeginDisabled(!draft.taunt);
				if (Rows("tauntRadius")) {
					Slider("Base reach", t.tauntRadiusBase, 200.0f, 3000.0f, "%.0f units", "How far a taunt carries before skill.");
					Slider("Per skill point", t.tauntRadiusPerSkill, 0.0f, 20.0f, "%.1f units", "Added per point of the better of Block and Heavy Armor.");
					Slider("Farthest reach", t.tauntRadiusMax, 200.0f, 4000.0f, "%.0f units", "A taunt never carries past this.");
					Slider("Hold breaks at", t.holdBreakDistance, 500.0f, 8000.0f, "%.0f units", "A held enemy is let go when the tank gets this far away.");
					Slider("Draw fire from", t.tauntRangedReach, 500.0f, 6000.0f, "%.0f units",
						"An archer or caster shooting at you can be taunted from this far, so their arrows and spells go to the tank instead of you.");
					Im::EndTable();
				}
				const auto line = std::format("With 50 Block: {:.0f} units. With 100: {:.0f}.", Rules::TauntRadius(50, 0, t), Rules::TauntRadius(100, 0, t));
				Note(line.c_str());
				Im::EndDisabled();
			}
			Note("The glow, sound and animations are forms named in FightWithMe.ini.");
			if (ResetButton("Every taunt setting on this page back to its default.")) {
				const auto d = Settings::Defaults();
				const auto& dt = d.tuning;
				draft.taunt = d.taunt;
				draft.tauntOpener = d.tauntOpener;
				draft.shieldWall = d.shieldWall;
				draft.tauntChance = d.tauntChance;
				draft.tauntCheckSeconds = d.tauntCheckSeconds;
				draft.glowSeconds = d.glowSeconds;
				t.tauntStamina = dt.tauntStamina;
				t.cooldownBase = dt.cooldownBase;
				t.cooldownPerLevel = dt.cooldownPerLevel;
				t.cooldownMin = dt.cooldownMin;
				t.tauntReachDivisor = dt.tauntReachDivisor;
				t.tauntFalloffPerLevel = dt.tauntFalloffPerLevel;
				t.tauntFalloffLevels = dt.tauntFalloffLevels;
				t.bossLevelBonus = dt.bossLevelBonus;
				t.cowedLevelGap = dt.cowedLevelGap;
				t.cowedHoldMult = dt.cowedHoldMult;
				t.holdBase = dt.holdBase;
				t.holdPerLevel = dt.holdPerLevel;
				t.tauntRadiusBase = dt.tauntRadiusBase;
				t.tauntRadiusPerSkill = dt.tauntRadiusPerSkill;
				t.tauntRadiusMax = dt.tauntRadiusMax;
				t.holdBreakDistance = dt.holdBreakDistance;
				t.tauntRangedReach = dt.tauntRangedReach;
				dirty = true;
			}
			Footer();
		}

		// --- Healers ---------------------------------------------------------------------

		void __stdcall RenderHealers()
		{
			Prepare();
			SettingsPage page("FightWithMeHealers");
			Header("HEALERS");
			auto& t = draft.tuning;
			if (Group("Healing", 0xF4BE)) {
				if (Checks("healOptions")) {
					Check("Allow heals", draft.heal, "Healers watch the party and heal whoever is in the most trouble.");
					Im::BeginDisabled(!draft.heal);
					Check("Lend Healing Hands", draft.healFallback,
						"Let a companion with no heal of their own be a Healer, borrowing Healing Hands scaled by their Restoration skill. Off: Healer is only offered to companions who know a heal for others.");
					Im::EndDisabled();
					Im::EndTable();
				}
				Im::BeginDisabled(!draft.heal);
				if (Rows("healValues")) {
					Percent("Heal themselves under", t.healSelfThreshold, 5, 95,
						"A healer tends to their own wounds only below this, whatever the line set for allies.");
					Slider("Between heals", t.healCooldown, 10.0f, 60.0f, "%.0f s", "Seconds a healer waits after each heal. At least 10, so heals are an event, not a stream.");
					Slider("Heal from", t.healRange, 200.0f, 3000.0f, "%.0f units", "Heals are cast from where the healer stands on allies up to this far.");
					Slider("Go to the downed from", t.reviveReach, 300.0f, 5000.0f, "%.0f units",
						"A companion who is down is walked to from up to this far and healed up close. You always come first.");
					Slider("Channel time", draft.concentrationSeconds, 0.5f, 6.0f, "%.1f s",
						"A concentration spell such as Healing Hands counts as this many seconds of channelling: more healing, more magicka.");
					Im::EndTable();
				}
				Im::EndDisabled();
			}
			Note("Each healer's line for allies is set per companion in the battle plan.");
			if (ResetButton("Every healing setting on this page back to its default.")) {
				const auto d = Settings::Defaults();
				draft.heal = d.heal;
				draft.healFallback = d.healFallback;
				draft.concentrationSeconds = d.concentrationSeconds;
				t.healSelfThreshold = d.tuning.healSelfThreshold;
				t.healCooldown = d.tuning.healCooldown;
				t.healRange = d.tuning.healRange;
				t.reviveReach = d.tuning.reviveReach;
				dirty = true;
			}
			Footer();
		}

		// --- Targeting -------------------------------------------------------------------

		void __stdcall RenderTargeting()
		{
			Prepare();
			SettingsPage page("FightWithMeTargeting");
			Header("TARGETING");
			auto& t = draft.tuning;
			if (Group("Choosing a target", 0xF140)) {
				if (Rows("targetChoice")) {
					Slider("Nearer counts up to", t.engageRange, 500.0f, 6000.0f, "%.0f units",
						"Between two enemies that fit the focus, the nearer is preferred up to this distance. Distance never rules anyone out: an ordered target is chased wherever it is.");
					Slider("Stick to a target", t.stickiness, 0.0f, 100.0f, "%.0f", "How much a companion prefers the enemy they are already fighting.");
					Slider("Spread out", t.spreadPenalty, 0.0f, 60.0f, "%.0f", "How much they avoid an enemy others are already on, per ally.");
					Slider("Distance weight", t.distanceWeight, 0.0f, 0.15f, "%.3f", "How much nearer enemies are preferred.");
					Slider("Rethink every", draft.planInterval, 0.1f, 1.5f, "%.2f s", "How often targets are reconsidered.");
					Im::EndTable();
				}
			}
			if (Group("Positions", 0xF505)) {
				if (Rows("targetPositions")) {
					Slider("Guard you within", t.guardRadius, 200.0f, 2000.0f, "%.0f units", "Guard you: companions fight enemies this close to you, or attacking you.");
					Slider("Hold back within", t.holdBackRadius, 150.0f, 1500.0f, "%.0f units",
						"Hold back: melee companions only meet enemies this close to themselves. Ranged ones keep shooting from where they stand.");
					Slider("With me within", t.withMeRadius, 200.0f, 1000.0f, "%.0f units",
						"With me (what To me! gives everyone): they keep this close to you and fight there, against anything that comes near you.");
					Slider("Fight back within", t.defendRange, 100.0f, 1500.0f, "%.0f units",
						"An enemy attacking a companion from this close is dealt with first, whatever their focus. Your target and a marked enemy are orders and still come first.");
					Toggle("Keep them near you", draft.guardLeash,
						"Companions guarding you, holding back or with you keep inside their distance, the way a guard keeps to their post: they break off a chase and come back.");
					Im::BeginDisabled(!draft.guardLeash);
					Slider("Hold back leash", draft.holdBackLeash, 300.0f, 3000.0f, "%.0f units", "How far from you a companion holding back may go.");
					Im::EndDisabled();
					Im::EndTable();
				}
			}
			if (ResetButton("Every targeting setting on this page back to its default.")) {
				const auto d = Settings::Defaults();
				t.engageRange = d.tuning.engageRange;
				t.stickiness = d.tuning.stickiness;
				t.spreadPenalty = d.tuning.spreadPenalty;
				t.distanceWeight = d.tuning.distanceWeight;
				t.guardRadius = d.tuning.guardRadius;
				t.holdBackRadius = d.tuning.holdBackRadius;
				t.withMeRadius = d.tuning.withMeRadius;
				t.defendRange = d.tuning.defendRange;
				draft.planInterval = d.planInterval;
				draft.guardLeash = d.guardLeash;
				draft.holdBackLeash = d.holdBackLeash;
				dirty = true;
			}
			Footer();
		}

		// --- Battle plan ---------------------------------------------------------------------

		void __stdcall RenderBattlePlan()
		{
			Prepare();
			SettingsPage page("FightWithMeBattlePlan");
			Header("BATTLE PLAN");
			if (Group("Time", 0xF252)) {
				if (Rows("time")) {
					Toggle("Stop time", draft.stopTime, "Pause the game while planning instead of slowing it.");
					Im::BeginDisabled(draft.stopTime);
					Slider("Slow motion", draft.timeScale, 0.02f, 1.0f, "%.2fx", "How fast the world runs while the plan is open.");
					Slider("Ease in and out", draft.easeSeconds, 0.0f, 1.0f, "%.2f s", "Seconds for time to slow down and speed back up.");
					Im::EndDisabled();
					Im::EndTable();
				}
				if (Checks("timeOptions")) {
					Im::BeginDisabled(draft.stopTime);
					Check("Slow Time screen effect", draft.timeEffect, "The Slow Time shout's look while planning. It washes the picture out and blurs it; off keeps the battlefield clear.");
					Im::EndDisabled();
					Check("Hide the HUD", draft.hideHud, "Hide the HUD and other interface while the plan is open; everything comes back when it closes.");
					Im::EndTable();
				}
			}
			if (Group("Camera", 0xF030)) {
				if (Rows("camera")) {
					Toggle("Glide to the companion", draft.cameraGlide,
						"While the plan is open the camera eases over to the companion you are commanding, follows as you switch between them, and eases back when you close it.");
					Im::BeginDisabled(!draft.cameraGlide);
					Slider("Distance", draft.cameraDistance, 120.0f, 700.0f, "%.0f units", "How far behind the companion the camera settles.");
					Slider("Height", draft.cameraHeight, 0.0f, 300.0f, "%.0f units", "How far above their eyes.");
					Im::EndDisabled();
					Im::EndTable();
				}
			}
			if (Group("Interface", 0xF06E)) {
				if (Rows("planLook")) {
					Slider("Menu scale", draft.uiScale, 0.6f, 2.0f, "%.2fx", "Size of the battle plan on top of the screen-height scaling.");
					Im::EndTable();
				}
				if (Checks("notices")) {
					Check("Taunt and heal notices", draft.notices, "Short lines on the left of the screen when a tank taunts or a healer heals.");
					Check("Menu sounds", draft.sounds, "The game's own menu sounds when you point at and choose orders in the battle plan.");
					Check("Tooltips", draft.tooltips, "Describe each control when you point at it or move to it with the pad. Turn off once you know them.");
					Im::EndTable();
				}
			}
			if (Group("Troubleshooting", 0xF188, false)) {
				if (Checks("debug")) {
					Check("Log target decisions", draft.logDecisions, "Write every target change and role style to FightWithMe.log.");
					Im::EndTable();
				}
			}
			Space();
			if (Im::Button("Open the battle plan")) {
				Open();
			}
			if (ResetButton("Every setting on this page back to its default.")) {
				const auto d = Settings::Defaults();
				draft.stopTime = d.stopTime;
				draft.timeScale = d.timeScale;
				draft.easeSeconds = d.easeSeconds;
				draft.timeEffect = d.timeEffect;
				draft.hideHud = d.hideHud;
				draft.cameraGlide = d.cameraGlide;
				draft.cameraDistance = d.cameraDistance;
				draft.cameraHeight = d.cameraHeight;
				draft.uiScale = d.uiScale;
				draft.notices = d.notices;
				draft.sounds = d.sounds;
				draft.tooltips = d.tooltips;
				draft.logDecisions = d.logDecisions;
				dirty = true;
			}
			Footer();
		}

		// --- Controls --------------------------------------------------------------------------

		struct PadChoice
		{
			std::uint32_t button;
			const char*   name;
		};

		constexpr std::array<PadChoice, 5> padChoices{ {
			{ 0, "Unbound" },
			{ kPadDown, "LB + D-pad down" },
			{ kPadUp, "LB + D-pad up" },
			{ kPadY, "LB + Y" },
			{ 0x80, "LB + Right-stick click" },
		} };

		void __stdcall RenderControls()
		{
			Prepare();
			SettingsPage page("FightWithMeControls");
			Header("CONTROLS");
			Heading("KEY BINDINGS", 0xF11C);
			Note("Select the binding, then press a key. Esc cancels; Delete clears.");
			Space();
			if (Im::BeginTable("bindings", 3, Im::ImGuiTableFlags_SizingStretchProp)) {
				Im::TableSetupColumn("Action", Im::ImGuiTableColumnFlags_WidthStretch, 1.4f);
				Im::TableSetupColumn("Key", Im::ImGuiTableColumnFlags_WidthStretch, 1.0f);
				Im::TableSetupColumn("", Im::ImGuiTableColumnFlags_WidthFixed, E(3.5f));
				// One row per binding: the battle plan, then the four party orders.
				auto binding = [&](int a_slot, const char* a_label, std::uint32_t& a_key) {
					Im::PushID(a_slot);
					Im::TableNextRow();
					Im::TableNextColumn();
					Im::AlignTextToFramePadding();
					Im::TextUnformatted(a_label);
					Im::TableNextColumn();
					const bool waiting = capturing.load() && captureSlot == a_slot;
					const auto name = waiting ? std::string("Press a key...") : KeyName(a_key);
					if (Im::Button(name.c_str(), { -1, 0 })) {
						if (waiting) {
							CancelCapture();
						} else {
							StartCapture(a_slot);
						}
					}
					Im::TableNextColumn();
					if (Im::Button("Clear", { -1, 0 })) {
						a_key = 0;
						dirty = true;
						CancelCapture();
					}
					Im::PopID();
				};
				binding(0, "Open the battle plan", draft.planKey);
				binding(1, "Attack! (everyone on your target)", draft.orderKeys[0]);
				binding(2, "To me! (everyone guards you)", draft.orderKeys[1]);
				binding(3, "Hold back (everyone stays back)", draft.orderKeys[2]);
				binding(4, "As you were", draft.orderKeys[3]);
				Im::EndTable();
			}
			if (Group("Gamepad", 0xF11B)) {
				if (Rows("gamepad")) {
					Im::TableNextRow();
					Im::TableNextColumn();
					Im::AlignTextToFramePadding();
					Im::TextUnformatted("Open the battle plan");
					Im::TableNextColumn();
					int current = 0;
					for (int i = 0; i < static_cast<int>(padChoices.size()); ++i) {
						if (padChoices[static_cast<std::size_t>(i)].button == draft.planGamepadKey) {
							current = i;
						}
					}
					Im::SetNextItemWidth(-1);
					if (Im::BeginCombo("##pad", padChoices[static_cast<std::size_t>(current)].name)) {
						for (int i = 0; i < static_cast<int>(padChoices.size()); ++i) {
							if (Im::Selectable(padChoices[static_cast<std::size_t>(i)].name, i == current)) {
								draft.planGamepadKey = padChoices[static_cast<std::size_t>(i)].button;
								dirty = true;
							}
						}
						Im::EndCombo();
					}
					Help("Hold LB and press the chosen button. Walk With Me's wheel uses LB with the right stick, Y or D-pad up; pick something it does not.");
					Im::EndTable();
				}
			}
			if (Group("In the battle plan", 0xF05A)) {
				if (Im::BeginTable("planControls", 3, Im::ImGuiTableFlags_SizingStretchProp | Im::ImGuiTableFlags_RowBg)) {
					Im::TableSetupColumn("What", Im::ImGuiTableColumnFlags_WidthStretch, 1.2f);
					Im::TableSetupColumn("Keyboard and mouse", Im::ImGuiTableColumnFlags_WidthStretch, 1.0f);
					Im::TableSetupColumn("Gamepad", Im::ImGuiTableColumnFlags_WidthStretch, 1.0f);
					Im::TableHeadersRow();
					static constexpr std::array<std::array<const char*, 3>, 7> rows{ {
						{ "Choose a companion", "Click a crest, Tab", "LB / RB" },
						{ "Move between rows", "Up / Down", "D-pad up / down" },
						{ "Pick or step a value", "Click, Left / Right", "D-pad left / right" },
						{ "Give a party order", "Click it, or its key", "Party orders row, A" },
						{ "Send someone after an enemy", "Click the enemy", "Battlefield row, A" },
						{ "Ability now / switch", "Click", "X / Y" },
						{ "Close", "Esc or the hotkey", "B" },
					} };
					for (const auto& row : rows) {
						Im::TableNextRow();
						for (const char* cell : row) {
							Im::TableNextColumn();
							Im::TextUnformatted(cell);
						}
					}
					Im::EndTable();
				}
			}
			Space();
			if (Im::Button("Open the battle plan")) {
				Open();
			}
			Help("Commands pause in game menus. Avoid keys used by other mods or by quicksave and quickload.");
			if (ResetButton("The hotkey and the gamepad button back to their defaults.")) {
				const auto d = Settings::Defaults();
				draft.planKey = d.planKey;
				draft.planGamepadKey = d.planGamepadKey;
				draft.orderKeys = d.orderKeys;
				CancelCapture();
				dirty = true;
			}
			Footer();
		}

		// SETTINGS_END

		// --- Input -------------------------------------------------------------------

		bool GameplayAllowsPlan()
		{
			auto* ui = RE::UI::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!ui || !player || !player->Is3DLoaded() || ui->GameIsPaused()) {
				return false;
			}
			static constexpr std::array<std::string_view, 8> blockers{ RE::Console::MENU_NAME, RE::DialogueMenu::MENU_NAME, RE::MapMenu::MENU_NAME,
				RE::LoadingMenu::MENU_NAME, RE::MainMenu::MENU_NAME, RE::RaceSexMenu::MENU_NAME, RE::FaderMenu::MENU_NAME, RE::LockpickingMenu::MENU_NAME };
			for (const auto& name : blockers) {
				if (ui->IsMenuOpen(name)) {
					return false;
				}
			}
			return !MF::IsAnyBlockingWindowOpened();
		}

		bool __stdcall OnInput(RE::InputEvent* a_event)
		{
			if (!a_event) {
				return false;
			}
			// Binding a key on the Controls page: the next key press is the answer.
			if (capturing.load()) {
				if (a_event->GetDevice() != RE::INPUT_DEVICE::kKeyboard || a_event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
					return false;
				}
				auto* button = a_event->AsButtonEvent();
				if (!button || !button->IsDown()) {
					return true;
				}
				const auto key = button->GetIDCode();
				captured = key == kEscape ? -2 : (key == 0xD3 ? 0 : static_cast<int>(key));  // 0xD3 is Delete
				return true;
			}
			const bool open = plan && plan->IsOpen;
			const auto& settings = Settings::Get();

			if (a_event->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
				if (a_event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
					return open;
				}
				auto* button = a_event->AsButtonEvent();
				if (!button) {
					return open;
				}
				const auto key = button->GetIDCode();
				const bool fresh = button->IsDown() && (padHeld & key) == 0;
				if (button->IsPressed()) {
					padHeld |= key;
				} else {
					padHeld &= ~key;
				}
				if (open) {
					padUsed = true;
					if (fresh) {
						switch (key) {
						case kPadA:
							padA = true;
							break;
						case kPadB:
							padB = true;
							break;
						case kPadX:
							padX = true;
							break;
						case kPadY:
							padY = true;
							break;
						case kPadLB:
							padShoulder -= 1;
							break;
						case kPadRB:
							padShoulder += 1;
							break;
						case kPadUp:
							padDy -= 1;
							break;
						case kPadDown:
							padDy += 1;
							break;
						case kPadLeft:
							padDx -= 1;
							break;
						case kPadRight:
							padDx += 1;
							break;
						default:
							break;
						}
					}
					return true;
				}
				// LB held plus the configured button opens the plan.
				if (fresh && settings.planGamepadKey && key == settings.planGamepadKey && (padHeld & kPadLB) && GameplayAllowsPlan()) {
					Open();
					return true;
				}
				return false;
			}

			if (a_event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton || a_event->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
				return false;
			}
			auto* button = a_event->AsButtonEvent();
			if (!button || !button->IsDown()) {
				return false;
			}
			const auto key = button->GetIDCode();
			for (std::size_t i = 0; i < settings.orderKeys.size(); ++i) {
				if (settings.orderKeys[i] && key == settings.orderKeys[i] && (open || GameplayAllowsPlan())) {
					Battle::Command command;
					command.type = Battle::Command::Type::kShout;
					command.shout = orderShouts[i];
					Battle::Queue(command);
					if (open) {
						Sound("UIMenuOK");
					}
					return true;
				}
			}
			if (open) {
				if (key == kEscape || key == settings.planKey) {
					Close();
					return true;
				}
				return false;
			}
			if (settings.planKey && key == settings.planKey && GameplayAllowsPlan()) {
				Open();
				return true;
			}
			return false;
		}
	}

	void Register()
	{
		// Ask the loaded DLL directly. The header's GetMenuFrameworkVersion first checks a path
		// relative to the current directory, which can fail while the DLL is loaded.
		using VersionFunction = float (*)();
		const auto version = GetMenuFrameworkFunction<VersionFunction>("GetMenuFrameworkVersion");
		if (!::GetModuleHandleW(L"SKSEMenuFramework.dll") || !version || version() < 3.0f) {
			Log::Warn("[Menu] SKSE Menu Framework 3 not found ({}): no battle plan. Orders still apply from the save and the INI."sv,
				!::GetModuleHandleW(L"SKSEMenuFramework.dll") ? "DLL not loaded"sv : (!version ? "no version export"sv : "older than 3"sv));
			return;
		}
		MF::SetSection("Fight With Me");
		MF::AddSectionItem("Party", RenderParty);
		MF::AddSectionItem("Tanks", RenderTanks);
		MF::AddSectionItem("Healers", RenderHealers);
		MF::AddSectionItem("Targeting", RenderTargeting);
		MF::AddSectionItem("Battle plan", RenderBattlePlan);
		MF::AddSectionItem("Controls", RenderControls);
		plan = MF::AddWindow(RenderPlan, true);
		inputRegistration.reset(MF::AddInputEvent(OnInput));
		hudRegistration.reset(MF::AddHudElement(RenderHUD));
		Log::Info("[Menu] Battle plan and six settings pages registered (Menu Framework {:.2f})."sv, version());
	}

	bool Available() noexcept
	{
		return plan != nullptr;
	}

	void Open()
	{
		if (!plan) {
			RE::SendHUDMessage::ShowHUDMessage("Fight With Me: the battle plan needs SKSE Menu Framework 3.");
			return;
		}
		if (plan->IsOpen) {
			return;
		}
		closeRequested = false;
		justOpened = true;
		plan->IsOpen = true;
		Battle::RequestOpen();
	}

	void Close()
	{
		if (plan && plan->IsOpen) {
			closeRequested = true;
		}
	}

	bool IsOpen() noexcept
	{
		return plan && plan->IsOpen;
	}
}

#include "FWM/Menu.h"

#include "FWM/Battle.h"
#include "FWM/Camera.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Rules.h"
#include "FWM/Settings.h"
#include "FWM/UITheme.h"

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

		// All three render entry points select one palette for the whole frame. These
		// aliases retain the same storage when the theme changes; no game state is touched.
		UITheme::Palette palette = UITheme::Oxblood;
		const U32& kGlass = palette.glass;
		const U32& kGlassEdge = palette.glassEdge;
		const U32& kTile = palette.tile;
		const U32& kTileHover = palette.tileHover;
		const U32& kAccent = palette.accent;
		const U32& kAccentDim = palette.accentDim;
		const U32& kEdge = palette.edge;
		const U32& kText = palette.text;
		const U32& kTextDim = palette.textDim;
		const U32& kTextFaint = palette.textFaint;
		constexpr U32 kRed = IM_COL32(214, 58, 52, 255);
		constexpr U32 kGreen = IM_COL32(132, 200, 140, 255);
		constexpr U32 kAmber = IM_COL32(236, 172, 72, 255);
		constexpr U32 kHealth = IM_COL32(178, 44, 40, 255);
		constexpr U32 kMagicka = IM_COL32(84, 130, 204, 255);
		constexpr U32 kStamina = IM_COL32(118, 174, 94, 255);
		constexpr U32 kRejuvenated = IM_COL32(120, 210, 255, 255);  // magicka quickened by Rejuvenate
		const U32& kFocusRing = palette.focusRing;
		constexpr U32 kShade = IM_COL32(0, 0, 0, 255);

		void SelectTheme() { palette = UITheme::For(Settings::Get().uiTheme); }

		constexpr std::uint32_t kPadUp = 0x1, kPadDown = 0x2, kPadLeft = 0x4, kPadRight = 0x8;
		constexpr std::uint32_t kPadLB = 0x100, kPadRB = 0x200, kPadA = 0x1000, kPadB = 0x2000, kPadX = 0x4000, kPadY = 0x8000;
		constexpr std::uint32_t kPadLT = 0x9, kPadRT = 0xA;
		constexpr std::uint32_t kPadView = 0x20;
		constexpr std::uint32_t kEscape = 0x01;

		MF::Model::WindowInterface*            plan{ nullptr };
		std::unique_ptr<MF::Model::InputEvent> inputRegistration;
		std::unique_ptr<MF::Model::HudElement> hudRegistration;

		std::atomic<int>  padDx{ 0 }, padDy{ 0 }, padShoulder{ 0 };
		std::atomic<bool> padA{ false }, padB{ false }, padX{ false }, padY{ false };
		std::atomic<bool> padUsed{ false };
		std::uint32_t     padHeld{ 0 };
		std::atomic<float> stickX{ 0.0f }, stickY{ 0.0f };      // the right stick, as last reported
		std::atomic<float> leftX{ 0.0f }, leftY{ 0.0f };        // the left stick: free move overhead
		std::atomic<float> triggerL{ 0.0f }, triggerR{ 0.0f };
		std::atomic<bool>  overheadToggle{ false };
		std::atomic<bool> closeRequested{ false };
		std::atomic<bool> justOpened{ false };
		std::atomic<bool> spellListOpen{ false };  // Esc closes the spell list, not the plan
		std::atomic<bool> spellListBack{ false };
		std::atomic<bool> pathPageOpen{ false };  // and the path page, likewise
		std::atomic<bool> pathPageBack{ false };
		std::atomic<bool> padView{ false };  // View: the guide, on the path page

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
			if (palette.Engraved()) {
				// An inset shadow changes the material, never the glyph's size or position.
				Draw::AddText(a_draw, Im::GetFont(), a_size,
					{ a_centre.x - bounds.x * ratio * 0.5f, a_centre.y - bounds.y * ratio * 0.5f + 1.0f },
					Alpha(kShade, static_cast<float>(a_color >> 24) / 255.0f * 0.8f), utf8.c_str());
			}
			Draw::AddText(a_draw, Im::GetFont(), a_size, { a_centre.x - bounds.x * ratio * 0.5f, a_centre.y - bounds.y * ratio * 0.5f }, a_color, utf8.c_str());
			FontAwesome::Pop();
		}

		// --- Shapes ------------------------------------------------------------------------

		float uiScale = 1.0f;  // this frame's scale, for decorations drawn by helpers that take no size
		bool  hitsBlocked = false;  // the guide is over the page: nothing beneath it answers the pointer

		// The pair of slashes that marks a header or the chosen row.
		void Slashes(Im::ImDrawList* a_draw, V a_at, float a_height, U32 a_color)
		{
			if (palette.Engraved()) {
				const float x = a_at.x + 3.0f * uiScale, y = a_at.y + a_height * 0.5f;
				const float r = std::min(a_height * 0.35f, 4.0f * uiScale);
				Draw::AddLine(a_draw, { x, y - r }, { x + r, y }, a_color, 1.0f);
				Draw::AddLine(a_draw, { x + r, y }, { x, y + r }, a_color, 1.0f);
				Draw::AddLine(a_draw, { x - 2.0f * uiScale, y }, { x + r, y }, a_color, 1.0f);
				return;
			}
			if (palette.Subdued()) {
				// A small open lozenge takes the place of the modern slash motif.
				const V c{ a_at.x + 6.0f * uiScale, a_at.y + a_height * 0.5f };
				const float r = std::min(a_height * 0.38f, 4.0f * uiScale);
				Draw::AddQuad(a_draw, { c.x, c.y - r }, { c.x + r, c.y }, { c.x, c.y + r }, { c.x - r, c.y }, a_color, 1.0f);
				return;
			}
			for (int i = 0; i < 2; ++i) {
				const float x = a_at.x + static_cast<float>(i) * 6.0f * uiScale;
				Draw::AddLine(a_draw, { x, a_at.y + a_height }, { x + a_height * 0.5f, a_at.y }, a_color, 1.2f * uiScale);
			}
		}

		// A small cross where frame lines would meet.
		void Cross(Im::ImDrawList* a_draw, V a_c, float a_r, U32 a_color)
		{
			if (palette.Engraved()) {
				Draw::AddQuad(a_draw, { a_c.x, a_c.y - a_r }, { a_c.x + a_r * 0.6f, a_c.y },
					{ a_c.x, a_c.y + a_r }, { a_c.x - a_r * 0.6f, a_c.y }, a_color, 1.0f);
				return;
			}
			Draw::AddLine(a_draw, { a_c.x - a_r, a_c.y - a_r }, { a_c.x + a_r, a_c.y + a_r }, a_color, 1.0f);
			Draw::AddLine(a_draw, { a_c.x - a_r, a_c.y + a_r }, { a_c.x + a_r, a_c.y - a_r }, a_color, 1.0f);
		}

		// The Nordic ornament stays inside the supplied bounds, away from text and hit targets.
		void EtchedCorners(Im::ImDrawList* a_draw, V a_min, V a_max, float a_alpha)
		{
			const float s = uiScale;
			const float arm = std::min(22.0f * s, std::min(a_max.x - a_min.x, a_max.y - a_min.y) * 0.24f);
			const float inset = 4.0f * s;
			if (arm < inset * 2.0f) { return; }
			for (const float dx : { -1.0f, 1.0f }) {
				for (const float dy : { -1.0f, 1.0f }) {
					const V c{ dx > 0 ? a_min.x : a_max.x, dy > 0 ? a_min.y : a_max.y };
					const V p{ c.x + dx * inset, c.y + dy * inset };
					const U32 metal = Alpha(kEdge, a_alpha * 0.7f);
					Draw::AddLine(a_draw, { c.x + dx * arm, p.y }, p, metal, 1.0f);
					Draw::AddLine(a_draw, p, { p.x, c.y + dy * arm }, metal, 1.0f);
					Draw::AddLine(a_draw, { c.x + dx * arm, p.y }, { c.x + dx * (arm - inset), c.y + dy * inset * 2.0f }, metal, 1.0f);
					Draw::AddLine(a_draw, { p.x, c.y + dy * arm }, { c.x + dx * inset * 2.0f, c.y + dy * (arm - inset) }, metal, 1.0f);
					Draw::AddLine(a_draw, { p.x + dx * inset, p.y }, { p.x, p.y + dy * inset }, Alpha(kTextDim, a_alpha * 0.4f), 1.0f);
				}
			}
		}

		// A hard, near-black plate with a hairline edge: tips, notices, small menus.
		void Glass(Im::ImDrawList* a_draw, V a_min, V a_max, float a_alpha, float)
		{
			if (palette.Engraved()) {
				Draw::AddRectFilledMultiColor(a_draw, a_min, a_max,
					Alpha(IM_COL32(28, 37, 37, 246), a_alpha), Alpha(kGlass, a_alpha),
					Alpha(IM_COL32(9, 14, 15, 247), a_alpha), Alpha(IM_COL32(14, 21, 22, 247), a_alpha));
				// Sparse, stable grain: a few low-contrast strokes in the stone, with no texture asset.
				const float w = a_max.x - a_min.x, h = a_max.y - a_min.y, s = uiScale;
				const int rows = std::min(32, static_cast<int>(h / (18.0f * s)));
				for (int i = 1; i < rows; ++i) {
					const float y = a_min.y + h * static_cast<float>(i) / static_cast<float>(rows);
					const float x = a_min.x + w * (0.08f + 0.72f * static_cast<float>((i * 17) % 31) / 31.0f);
					Draw::AddLine(a_draw, { x, y }, { std::min(x + w * 0.14f, a_max.x - 8.0f * s), y - s },
						Alpha(kText, a_alpha * 0.018f), 1.0f);
				}
				Draw::AddRect(a_draw, a_min, a_max, Alpha(kGlassEdge, a_alpha), 0.0f, 0, 1.0f);
				Draw::AddLine(a_draw, { a_min.x + s, a_min.y + s }, { a_max.x - s, a_min.y + s }, Alpha(kText, a_alpha * 0.18f), 1.0f);
				Draw::AddLine(a_draw, { a_min.x + s, a_min.y + s }, { a_min.x + s, a_max.y - s }, Alpha(kText, a_alpha * 0.10f), 1.0f);
				Draw::AddLine(a_draw, { a_min.x + s, a_max.y - s }, { a_max.x - s, a_max.y - s }, Alpha(kShade, a_alpha * 0.75f), 1.0f);
				return;
			}
			if (palette.Subdued()) {
				Draw::AddRectFilledMultiColor(a_draw, a_min, a_max, Alpha(kGlass, a_alpha), Alpha(kGlass, a_alpha),
					Alpha(IM_COL32(5, 7, 9, 232), a_alpha), Alpha(IM_COL32(5, 7, 9, 232), a_alpha));
				Draw::AddRect(a_draw, a_min, a_max, Alpha(kGlassEdge, a_alpha), 0.0f, 0, 1.0f);
				return;
			}
			Draw::AddRectFilled(a_draw, { a_min.x + 3.0f * uiScale, a_min.y + 6.0f * uiScale }, { a_max.x + 3.0f * uiScale, a_max.y + 8.0f * uiScale },
				Alpha(kShade, a_alpha * 0.28f), 0.0f, 0);
			Draw::AddRectFilled(a_draw, a_min, a_max, Alpha(kGlass, a_alpha), 0.0f, 0);
			Draw::AddRect(a_draw, a_min, a_max, Alpha(kGlassEdge, a_alpha), 0.0f, 0, 1.0f);
		}

		void Diamond(Im::ImDrawList* a_draw, V c, float r, U32 a_fill, U32 a_edge, float a_thickness);

		// A small diamond set into a frame line.
		void Stud(Im::ImDrawList* a_draw, V a_c, float a_r, float a_alpha)
		{
			if (palette.Engraved()) {
				Draw::AddCircleFilled(a_draw, a_c, a_r, Alpha(kGlass, a_alpha), 8);
				Draw::AddCircle(a_draw, a_c, a_r, Alpha(kTextDim, a_alpha * 0.65f), 8, 1.0f);
				return;
			}
			Diamond(a_draw, a_c, a_r, Alpha(kGlass, std::min(a_alpha * 1.1f, 1.0f)), Alpha(kText, a_alpha * 0.6f), 1.0f);
		}

		// A red gem set into a frame line, with short ticks running out from it.
		void Gem(Im::ImDrawList* a_draw, V a_c, float a_r, float a_alpha)
		{
			const float s = uiScale;
			if (palette.Engraved()) {
				for (const float side : { -1.0f, 1.0f }) {
					const float x = a_c.x + side * (a_r + 4.0f * s);
					Draw::AddLine(a_draw, { x, a_c.y }, { x + side * 23.0f * s, a_c.y }, Alpha(kTextDim, a_alpha * 0.65f), 1.0f);
					Draw::AddLine(a_draw, { x, a_c.y }, { x + side * 7.0f * s, a_c.y + 3.0f * s }, Alpha(kTextDim, a_alpha * 0.65f), 1.0f);
				}
				Diamond(a_draw, a_c, a_r, Alpha(kGlass, a_alpha), Alpha(kEdge, a_alpha * 0.85f), 1.0f);
				return;
			}
			if (palette.Subdued()) {
				for (const float side : { -1.0f, 1.0f }) {
					Draw::AddLine(a_draw, { a_c.x + side * (a_r + 4.0f * s), a_c.y }, { a_c.x + side * (a_r + 25.0f * s), a_c.y },
						Alpha(kTextDim, a_alpha * 0.55f), 1.0f);
				}
				Diamond(a_draw, a_c, a_r, Alpha(kGlass, a_alpha), Alpha(kText, a_alpha * 0.65f), 1.0f);
				return;
			}
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
			if (palette.Engraved()) {
				EtchedCorners(a_draw, a_min, a_max, a_alpha);
				const float s = uiScale;
				if (a_max.x - a_min.x > 160.0f * s) {
					const float cx = (a_min.x + a_max.x) * 0.5f;
					Gem(a_draw, { cx, a_min.y + 4.0f * s }, 3.0f * s, a_alpha);
				}
				return;
			}
			if (palette.Subdued()) {
				// Quiet silver rules, inward corner notches and an open central diamond.
				// Keep ornaments inside the existing frame so every hit target stays put.
				const float s = uiScale, inset = 4.0f * s, arm = 15.0f * s;
				const U32 metal = Alpha(kText, a_alpha * 0.60f);
				Draw::AddRect(a_draw, { a_min.x + inset, a_min.y + inset }, { a_max.x - inset, a_max.y - inset },
					Alpha(kGlassEdge, a_alpha * 0.35f), 0.0f, 0, 1.0f);
				for (const float dx : { -1.0f, 1.0f }) {
					for (const float dy : { -1.0f, 1.0f }) {
						const V c{ dx > 0 ? a_min.x : a_max.x, dy > 0 ? a_min.y : a_max.y };
						const V p{ c.x + dx * inset, c.y + dy * inset };
						Draw::AddLine(a_draw, { c.x + dx * arm, c.y }, { p.x, c.y }, metal, 1.0f);
						Draw::AddLine(a_draw, { p.x, c.y }, { c.x, p.y }, metal, 1.0f);
						Draw::AddLine(a_draw, { c.x, p.y }, { c.x, c.y + dy * arm }, metal, 1.0f);
					}
				}
				if (a_max.x - a_min.x > 160.0f * s) {
					Gem(a_draw, { (a_min.x + a_max.x) * 0.5f, a_min.y }, 3.5f * s, a_alpha);
				}
				return;
			}
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
			if (palette.Engraved()) {
				Draw::AddRectFilledMultiColor(a_draw, a_min, a_max, Alpha(kText, a_alpha * 0.06f), Alpha(kText, a_alpha * 0.02f),
					Alpha(kShade, a_alpha * 0.12f), Alpha(kShade, a_alpha * 0.12f));
				Draw::AddLine(a_draw, { a_min.x, a_max.y - uiScale }, { a_max.x, a_max.y - uiScale }, Alpha(kShade, a_alpha * 0.7f), 1.0f);
				Draw::AddLine(a_draw, { a_min.x, a_max.y }, a_max, Alpha(kTextDim, a_alpha * 0.35f), 1.0f);
				return;
			}
			if (palette.Subdued()) {
				Draw::AddRectFilledMultiColor(a_draw, a_min, a_max, Alpha(kAccent, a_alpha * 0.26f), Alpha(kAccent, 0.0f),
					Alpha(kAccent, 0.0f), Alpha(kAccent, a_alpha * 0.26f));
				Draw::AddLine(a_draw, { a_min.x, a_max.y }, a_max, Alpha(kGlassEdge, a_alpha * 0.8f), 1.0f);
				return;
			}
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
			if (palette.Engraved()) {
				Draw::AddRectFilledMultiColor(a_draw, a_min, a_max, Alpha(kAccent, a_alpha * 0.7f), Alpha(kAccent, a_alpha * 0.22f),
					Alpha(kAccent, a_alpha * 0.22f), Alpha(kAccent, a_alpha * 0.7f));
				Draw::AddLine(a_draw, a_min, { a_min.x, a_max.y }, Alpha(kEdge, a_alpha), 1.0f);
				Draw::AddLine(a_draw, { a_min.x, a_max.y }, a_max, Alpha(kTextDim, a_alpha * 0.5f), 1.0f);
				EtchedCorners(a_draw, a_min, a_max, a_alpha);
				return;
			}
			if (palette.Subdued()) {
				Draw::AddRectFilledMultiColor(a_draw, a_min, a_max, Alpha(kText, a_alpha * 0.13f), Alpha(kText, 0.0f),
					Alpha(kText, 0.0f), Alpha(kText, a_alpha * 0.13f));
				Draw::AddLine(a_draw, a_min, { a_min.x, a_max.y }, Alpha(kText, a_alpha * 0.8f), 1.0f);
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
			if (palette.Engraved()) {
				Draw::AddRectFilledMultiColor(a_draw, a_min, a_max, Alpha(kEdge, a_alpha * 0.17f), Alpha(kEdge, a_alpha * 0.17f),
					Alpha(kAccent, a_alpha * 0.65f), Alpha(kAccent, a_alpha * 0.65f));
				Draw::AddLine(a_draw, a_min, { a_max.x, a_min.y }, Alpha(kEdge, a_alpha * 0.8f), 1.0f);
				EtchedCorners(a_draw, a_min, a_max, a_alpha);
				return;
			}
			if (palette.Subdued()) {
				Draw::AddRectFilledMultiColor(a_draw, a_min, a_max, Alpha(kText, a_alpha * 0.02f), Alpha(kText, a_alpha * 0.02f),
					Alpha(kText, a_alpha * 0.14f), Alpha(kText, a_alpha * 0.14f));
				Draw::AddLine(a_draw, { a_min.x, a_max.y - 1.0f }, { a_max.x, a_max.y - 1.0f }, Alpha(kText, a_alpha * 0.8f), 1.0f);
				return;
			}
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
			if (palette.Subdued()) {
				a_strength *= 0.38f;
			}
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
			if (palette.Engraved() && r >= 10.0f * uiScale && (a_fill >> 24) > 0 && (a_edge >> 24) > 0) {
				const float inset = 3.0f * uiScale;
				const float opacity = static_cast<float>(a_edge >> 24) / 255.0f;
				Draw::AddLine(a_draw, { left.x + inset, left.y }, { top.x, top.y + inset }, Alpha(kText, opacity * 0.38f), 1.0f);
				Draw::AddLine(a_draw, { top.x, top.y + inset }, { right.x - inset, right.y }, Alpha(kText, opacity * 0.22f), 1.0f);
				Draw::AddLine(a_draw, { left.x + inset, left.y }, { bottom.x, bottom.y - inset }, Alpha(kShade, opacity * 0.8f), 1.0f);
				Draw::AddLine(a_draw, { bottom.x, bottom.y - inset }, { right.x - inset, right.y }, Alpha(kShade, opacity * 0.8f), 1.0f);
			}
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
			if (hitsBlocked) {
				a_hovered = false;
				return false;
			}
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

		std::uint32_t SchoolGlyph(Rules::School a_school)
		{
			switch (a_school) {
			case Rules::School::kDestruction:
				return 0xF06D;
			case Rules::School::kRestoration:
				return 0xF4BE;
			case Rules::School::kAlteration:
				return 0xF3ED;
			case Rules::School::kConjuration:
				return 0xF6E2;
			case Rules::School::kIllusion:
				return 0xF06E;
			default:
				return 0xF0D0;
			}
		}

		// Muted, so they sit with the oxblood rather than against it.
		U32 SchoolColour(Rules::School a_school)
		{
			switch (a_school) {
			case Rules::School::kDestruction:
				return IM_COL32(226, 104, 70, 255);
			case Rules::School::kRestoration:
				return IM_COL32(226, 198, 124, 255);
			case Rules::School::kAlteration:
				return IM_COL32(124, 170, 226, 255);
			case Rules::School::kConjuration:
				return IM_COL32(170, 132, 226, 255);
			case Rules::School::kIllusion:
				return IM_COL32(210, 136, 198, 255);
			default:
				return kTextDim;
			}
		}

		std::string_view HandLabel(std::size_t a_hand) { return a_hand == kRightHand ? "RIGHT HAND"sv : "LEFT HAND"sv; }

		bool Fits(const View::Spell& a_spell, std::size_t a_hand) { return a_spell.twoHanded || (a_hand == kRightHand ? a_spell.right : a_spell.left); }

		// What a role's spells may be, in one line.
		std::string SpellRule(const View::Member& a_member)
		{
			if (a_member.orders.role == Role::kMage) {
				return "A Mage holds any spell but a heal.";
			}
			if (Settings::Get().healersAnySpell) {
				return "A Healer holds any spell (Healers settings).";
			}
			const auto cap = Rules::HealerTierCap(a_member.level);
			std::string line = std::format("A Healer holds any spell, with attack spells up to {}", Rules::tierNames[static_cast<std::size_t>(cap)]);
			if (cap == Rules::SpellTier::kNovice) {
				line += " (Apprentice from level 10)";
			} else if (cap == Rules::SpellTier::kApprentice) {
				line += " (Adept from level 25)";
			}
			return line + ".";
		}

		// --- State -------------------------------------------------------------------------

		enum Section : int
		{
			kRoleSection,
			kFocusSection,
			kPositionSection,
			kAbilitySection,
			kSpellSection,  // the sheet's two hands, out of a fight
			kFieldSection,
			kOrdersSection,
			kSectionCount
		};

		// The parts of the path page the guide lights, one a step.
		enum GuideArea : int
		{
			kGuideNone = -1,
			kGuidePoints,
			kGuideTabs,
			kGuideTree,
			kGuideCard,
			kGuideAction,
			kGuideSockets,
			kGuideButton,
			kGuideAreas
		};

		struct State
		{
			RE::FormID selected{ 0 };
			RE::FormID shown{ 0 };  // the companion the card is showing while it changes over
			float      swap{ 1.0f };
			int        section{ kRoleSection };
			int        item{ 0 };
			int        abilityColumn{ 0 };  // the badge the pad was last on, to come back up to
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

			bool  overhead{ false };   // the whole fight from above; kept between openings
			float panDragged{ 0.0f };  // pixels the middle button has dragged since it went down

			// Auto battle as last switched here, shown until the settings catch up with it.
			std::optional<bool> autoAsked;
			float               autoAskedAt{ 0.0f };

			// The spell list for one hand, open beside the sheet.
			struct SpellList
			{
				RE::FormID  member{ 0 };  // 0: closed
				std::size_t hand{ kLeftHand };
				int         school{ -1 };  // -1: every school
				int         cursor{ 0 };   // the pad's row among those that can be picked
				float       scroll{ 0.0f };
				float       scrollTo{ 0.0f };
				float       openT{ 0.0f };
				float       openedAt{ 0.0f };
				bool        placed{ false };  // the cursor has been put on what the hand holds
			};
			SpellList spells;

			// A companion's path, open over the party view: the tree of one role, the node the
			// cursor is on, and what is needed to animate a new rank.
			struct PathPage
			{
				RE::FormID member{ 0 };  // 0: closed
				Role       path{ Role::kFighter };
				int        cursor{ 0 };
				float      openedAt{ 0.0f };
				float      tabAt{ 0.0f };  // the path last changed: its nodes come in again
				bool       resetArmed{ false };
				float      resetArmedAt{ 0.0f };
				RE::FormID seenMember{ 0 };
				Role       seenPath{ Role::kCount };
				std::array<std::uint8_t, Paths::kNodes> seen{};
				std::array<float, Paths::kNodes>        rankedAt{};
				int        lastUnspent{ -1 };
				float      pointsAt{ -99.0f };
				bool       triggerDown{ false };
				int        previewNode{ -1 };  // the ladder row the pointer is on, for the card's text
				int        previewRank{ 0 };
			};
			PathPage page;

			// The guide to paths, over the page: the step it is on, the part of the page each step
			// lights (set as the page draws), and the lit part as it glides from one to the next.
			struct Guide
			{
				struct Area
				{
					V min{}, max{};
				};
				bool                          open{ false };
				int                           step{ 0 };
				float                         openedAt{ 0.0f };
				float                         stepAt{ 0.0f };
				std::array<Area, kGuideAreas> areas{};
				Area                          from{};
				Area                          shown{};
				bool                          placed{ false };  // from has been set since it opened
			};
			Guide guide;

			// Orders to go after an enemy, and companions taking them up, for the lock-on that
			// answers each one.
			struct Beat
			{
				RE::FormID enemy{ 0 };
				RE::FormID member{ 0 };
				float      at{ 0.0f };
			};
			std::vector<Beat>                          sent;
			std::vector<Beat>                          engaged;
			std::unordered_map<RE::FormID, RE::FormID> wasOn;  // companion -> the mark they were fighting last frame
		};

		State state;

		// Information, not a description of a control: shown whatever the tooltip setting says.
		void Readout(std::string a_text)
		{
			state.tip = std::move(a_text);
			state.tipShown = true;
			state.tipAnchored = false;
		}

		void Tip(std::string a_text)
		{
			if (Settings::Get().tooltips) {
				Readout(std::move(a_text));
			}
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

		// Sends a companion after an enemy (0 calls them off), and starts the lock-on that
		// shows the order was heard.
		void SendAfter(RE::FormID a_member, RE::FormID a_enemy)
		{
			Simple(Battle::Command::Type::kMark, a_member, a_enemy);
			if (a_enemy) {
				state.sent.push_back({ a_enemy, a_member, state.clock });
			}
		}

		void Preset(Battle::Preset a_preset)
		{
			Battle::Command command;
			command.type = Battle::Command::Type::kPreset;
			command.preset = a_preset;
			Battle::Queue(command);
		}

		void ChooseSpell(RE::FormID a_member, std::size_t a_hand, RE::FormID a_spell)
		{
			Battle::Command command;
			command.type = Battle::Command::Type::kSetSpell;
			command.member = a_member;
			command.hand = a_hand;
			command.spell = a_spell;
			Battle::Queue(command);
		}

		void CloseSpellList() { state.spells.member = 0; }

		// Opened on what the hand holds now, in every school.
		void OpenSpellList(const View::Member& a_member, std::size_t a_hand)
		{
			state.spells = {};
			state.spells.member = a_member.id;
			state.spells.hand = a_hand;
			state.spells.openedAt = state.clock;
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
			Draw::AddRectFilled(a_draw, { 0, 0 }, a_display, Alpha(palette.vignette, a_alpha), 0.0f, 0);
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
			// Sent after someone and not on them yet: a bright line from them to the mark.
			for (const auto& member : a_view.party) {
				const auto* mark = member.marked ? findEnemy(member.marked) : nullptr;
				if (!mark || member.target == member.marked || !member.head.on || !mark->head.on) {
					continue;
				}
				Marching(a_draw, ScreenOf(member.head, a_display), ScreenOf(mark->head, a_display), Alpha(kText, alpha * 0.85f), 1.6f * a_s, 5.0f * a_s,
					6.0f * a_s, state.clock * 60.0f * a_s);
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
					Diamond(a_draw, p, r * 1.25f, Alpha(Mix(IM_COL32(16, 16, 17, 245), (palette.Subdued() ? palette.points : IM_COL32(64, 16, 15, 248)), chosen), alpha), Alpha(Mix(kTextDim, kEdge, chosen), alpha),
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
				// Health, and beneath it magicka and stamina side by side: all three as they are now.
				const float barsAt = p.y + r * 1.15f + 6.0f * a_s;
				Bar(a_draw, { p.x - 16.0f * a_s, barsAt }, 32.0f * a_s, 3.0f * a_s, state.tw.Toward("enemyHealth", enemy.id, enemy.health, 8.0f), kHealth, alpha * pop);
				Bar(a_draw, { p.x - 16.0f * a_s, barsAt + 5.0f * a_s }, 15.0f * a_s, 2.0f * a_s, state.tw.Toward("enemyMagicka", enemy.id, enemy.magicka, 8.0f), kMagicka,
					alpha * pop * 0.9f);
				Bar(a_draw, { p.x + 1.0f * a_s, barsAt + 5.0f * a_s }, 15.0f * a_s, 2.0f * a_s, state.tw.Toward("enemyStamina", enemy.id, enemy.stamina, 8.0f), kStamina,
					alpha * pop * 0.9f);
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
					if (enemy.healthMax > 0.0f) {
						tip += std::format("\nHealth {:.0f} / {:.0f}  ·  Magicka {:.0f} / {:.0f}  ·  Stamina {:.0f} / {:.0f}", std::max(enemy.healthNow, 0.0f),
							enemy.healthMax, std::max(enemy.magickaNow, 0.0f), enemy.magickaMax, std::max(enemy.staminaNow, 0.0f), enemy.staminaMax);
					}
					if (const auto* tank = enemy.heldBy ? findMember(enemy.heldBy) : nullptr) {
						tip += std::format("\nHeld by {}'s taunt.", tank->name);
					}
					// Who they are and how they stand is shown whatever the tooltip setting; only the
					// hint on what a click does goes with it.
					if (Settings::Get().tooltips) {
						tip += "\nClick to send someone after them.";
					}
					if (!state.enemyMenu) {
						Readout(std::move(tip));
					}
				}
			}
		}

		// --- Buffs and debuffs -------------------------------------------------------------------

		U32 TintColour(View::Status::Tint a_tint)
		{
			switch (a_tint) {
			case View::Status::Tint::kDebuff:
				return kRed;
			case View::Status::Tint::kArmor:
				return kAmber;
			case View::Status::Tint::kMagicka:
				return kRejuvenated;
			default:
				return kGreen;
			}
		}

		std::string TimeLeft(float a_seconds)
		{
			const int s = static_cast<int>(std::ceil(std::max(a_seconds, 0.0f)));
			return s >= 60 ? std::format("{}:{:02} left", s / 60, s % 60) : std::format("{} s left", s);
		}

		std::string StatusText(const View::Status& a_status)
		{
			std::string text = std::format("{}\n{}", a_status.name, a_status.detail);
			if (a_status.left >= 0.0f) {
				text += "\n" + TimeLeft(a_status.left);
			} else if (a_status.progress >= 0.0f) {
				text += std::format("\n{:.0f}% of the way back", a_status.progress * 100.0f);
			}
			return text;
		}

		// A row of small icons over a companion's crest, one for each buff or debuff the party's
		// abilities have put on them. Each ring is what is left of it, running down from noon, and
		// blinks in its last seconds; pointed at, it says what it is and for how long.
		void StatusRow(Im::ImDrawList* a_draw, const View::Member& a_member, V a_centre, float a_s, float a_alpha, int a_crest)
		{
			const auto& all = a_member.statuses;
			if (all.empty() || a_alpha <= 0.01f) {
				return;
			}
			constexpr std::size_t kShown = 4;
			const std::size_t     shown = std::min(all.size(), kShown);
			const bool            more = all.size() > kShown;
			const float           r = 10.0f * a_s;
			const float           step = 25.0f * a_s;
			float                 x = a_centre.x - step * (static_cast<float>(shown + (more ? 1 : 0)) - 1.0f) * 0.5f;
			auto hit = [&](V a_c, int a_index) {
				bool hovered = false;
				Im::PushID(5000 + a_crest * 64 + a_index);
				Hit("status", { a_c.x - r - 2.0f * a_s, a_c.y - r - 2.0f * a_s }, { 2.0f * r + 4.0f * a_s, 2.0f * r + 4.0f * a_s }, hovered);
				Im::PopID();
				if (hovered) {
					Hovering(0x600 + static_cast<std::uint64_t>(a_crest) * 16 + static_cast<std::uint64_t>(a_index));
				}
				return hovered;
			};
			for (std::size_t k = 0; k < shown; ++k, x += step) {
				const auto& status = all[k];
				const V     c{ x, a_centre.y };
				const U32   colour = TintColour(status.tint);
				const float frac = status.left >= 0.0f && status.length > 0.0f ? Clamp01(status.left / status.length) :
				                                                                   (status.progress >= 0.0f ? Clamp01(status.progress) : 1.0f);
				const float blink = status.left >= 0.0f && status.left < 3.0f ? 0.55f + 0.45f * std::sin(state.clock * 9.0f) : 1.0f;
				const bool  hovered = hit(c, static_cast<int>(k));
				Draw::AddCircleFilled(a_draw, c, r + 1.5f * a_s, Alpha(IM_COL32(12, 12, 14, 235), a_alpha), 28);
				Draw::AddCircle(a_draw, c, r, Alpha(kGlassEdge, a_alpha), 28, 1.0f);
				if (frac > 0.01f) {
					Draw::PathClear(a_draw);
					Draw::PathArcTo(a_draw, c, r, -1.5707963f, -1.5707963f + 6.2831853f * frac, std::max(6, static_cast<int>(32.0f * frac)));
					Draw::PathStroke(a_draw, Alpha(colour, a_alpha * blink), 0, 2.2f * a_s);
				}
				Glyph(a_draw, c, r * 1.05f, status.glyph, Alpha(colour, a_alpha * (hovered ? 1.0f : 0.85f)));
				if (hovered) {
					Draw::AddCircle(a_draw, c, r + 3.5f * a_s, Alpha(colour, a_alpha * 0.6f), 28, 1.2f * a_s);
					Readout(StatusText(status));
				}
			}
			if (more) {
				// The rest behind a count; pointed at, all of them.
				const V c{ x, a_centre.y };
				Draw::AddCircleFilled(a_draw, c, r + 1.5f * a_s, Alpha(IM_COL32(12, 12, 14, 235), a_alpha), 28);
				Draw::AddCircle(a_draw, c, r, Alpha(kGlassEdge, a_alpha * 1.5f), 28, 1.0f);
				TextCentered(a_draw, c, Alpha(kTextDim, a_alpha), 12.0f * a_s, std::format("+{}", all.size() - shown));
				if (hit(c, static_cast<int>(kShown))) {
					std::string text;
					for (std::size_t k = shown; k < all.size(); ++k) {
						text += (text.empty() ? "" : "\n\n") + StatusText(all[k]);
					}
					Readout(std::move(text));
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
				Diamond(a_draw, c, R, Alpha(Mix(IM_COL32(17, 17, 18, 245), (palette.Subdued() ? palette.points : IM_COL32(60, 15, 14, 248)), sel), a),
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
				StatusRow(a_draw, m, { x, c.y - R - 34.0f * a_s }, a_s, a, static_cast<int>(i));

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
			return { { left, baseY - 90.0f * a_s }, { right, baseY + 96.0f * a_s } };
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

		// Shift, Ctrl or Alt, either side.
		const char* ModifierName(std::uint32_t a_modifier)
		{
			switch (ModifierOf(a_modifier)) {
			case kShift:
				return "Shift";
			case kCtrl:
				return "Ctrl";
			case kAlt:
				return "Alt";
			default:
				return "None";
			}
		}

		std::string HotkeyName(const Hotkey& a_hotkey)
		{
			if (a_hotkey.key == 0 || a_hotkey.modifier == 0) {
				return KeyName(a_hotkey.key);
			}
			return std::format("{} + {}", ModifierName(a_hotkey.modifier), KeyName(a_hotkey.key));
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

		// Fire, frost, shock, poison, magic: the order of View::Member::resist.
		constexpr std::array<std::uint32_t, 5> kElements{ { 0xF06D, 0xF2DC, 0xF0E7, 0xF714, 0xF0D0 } };

		// What a resistance looks like: a weakness in red, any resistance bright, none faint.
		U32 ResistTone(float a_value) { return a_value < 0.0f ? kEdge : (a_value > 0.0f ? kText : kTextFaint); }

		// The role box in a fight: the role they fight as, lit, and beside it what they bring to
		// it: armor and damage, then what turns aside.
		void RoleInFight(Im::ImDrawList* a_draw, const View::Member& a_member, Role a_role, V a_min, float a_width, float a_tileW, float a_height, float a_s,
			float a_alpha)
		{
			const auto& info = Info(a_role);
			{
				const V max{ a_min.x + a_tileW, a_min.y + a_height };
				Lit(a_draw, a_min, max, a_alpha);
				Draw::AddRect(a_draw, a_min, max, Alpha(kEdge, a_alpha * 0.5f), 0.0f, 0, 1.0f);
				const float cx = a_min.x + a_tileW * 0.5f;
				Glyph(a_draw, { cx, a_min.y + a_height * 0.38f }, 34.0f * a_s, info.glyph, Alpha(kText, a_alpha));
				TextCentered(a_draw, { cx, a_min.y + a_height * 0.72f }, Alpha(kText, a_alpha), 16.0f * a_s, info.name);
				bool hovered = false;
				Hit("roleInFight", a_min, { a_tileW, a_height }, hovered);
				if (hovered) {
					Hovering(0x180);
					Tip(std::format("{}\n{}\nRoles are set out of a fight: open the plan with nobody fighting to change it.", info.name, info.summary));
				}
			}

			const float x0 = a_min.x + a_tileW + 8.0f * a_s;
			const float w = a_min.x + a_width - x0;
			const float frameH = 62.0f * a_s;
			const float half = (w - 8.0f * a_s) * 0.5f;
			const std::array<std::pair<std::string_view, std::string>, 2> big{ {
				{ "ARMOR", std::format("{:.0f}", a_member.armor) },
				{ "DAMAGE", a_member.damage > 0.0f ? std::format("{:.0f}", a_member.damage) : std::string("-") },
			} };
			for (std::size_t i = 0; i < big.size(); ++i) {
				const V bmin{ x0 + static_cast<float>(i) * (half + 8.0f * a_s), a_min.y };
				const V bmax{ bmin.x + half, bmin.y + frameH };
				Draw::AddRectFilled(a_draw, bmin, bmax, Alpha(kTile, a_alpha), 0.0f, 0);
				Draw::AddRect(a_draw, bmin, bmax, Alpha(kGlassEdge, a_alpha), 0.0f, 0, 1.0f);
				TextCentered(a_draw, { (bmin.x + bmax.x) * 0.5f, bmin.y + 24.0f * a_s }, Alpha(kText, a_alpha), 25.0f * a_s, big[i].second);
				const float lw = LabelWidth(10.5f * a_s, big[i].first, 1.8f * a_s);
				Label(a_draw, { (bmin.x + bmax.x) * 0.5f - lw * 0.5f, bmin.y + 42.0f * a_s }, Alpha(kTextDim, a_alpha), 10.5f * a_s, big[i].first, 1.8f * a_s);
			}

			const float cell = w / static_cast<float>(kElements.size());
			const float dr = 13.0f * a_s;
			const float cy = a_min.y + frameH + 16.0f * a_s + dr;
			for (std::size_t i = 0; i < kElements.size(); ++i) {
				const float value = a_member.resist[i];
				const V     c{ x0 + cell * (static_cast<float>(i) + 0.5f), cy };
				const bool  weak = value < 0.0f, some = value > 0.0f;
				Diamond(a_draw, c, dr, Alpha(weak ? IM_COL32(58, 15, 14, 245) : IM_COL32(19, 19, 20, 245), a_alpha),
					Alpha(weak ? kEdge : (some ? kText : kGlassEdge), a_alpha * (some || weak ? 0.9f : 1.3f)), (weak || some ? 1.3f : 1.0f) * a_s);
				Glyph(a_draw, c, 11.0f * a_s, kElements[i], Alpha(ResistTone(value), a_alpha));
				TextCentered(a_draw, { c.x, c.y + dr + 11.0f * a_s }, Alpha(ResistTone(value), a_alpha), 12.5f * a_s, std::format("{:.0f}%", value));
			}
		}

		// --- Paths: helpers ---------------------------------------------------------------------

		std::uint32_t AbilityBit(Paths::Ability a_ability) { return 1u << static_cast<std::uint32_t>(a_ability); }

		std::string_view RankWord(int a_rank)
		{
			static constexpr std::array<std::string_view, 4> words{ "-", "I", "II", "III" };
			return words[static_cast<std::size_t>(std::clamp(a_rank, 0, 3))];
		}

		bool CarriesNow(const View::Member& a_member, Paths::Ability a_ability)
		{
			return std::any_of(a_member.carried.begin(), a_member.carried.end(), [&](const auto& a_carried) { return a_carried.ability == a_ability; });
		}

		// The healer's line under the badges: for whoever heals, by role or by carrying Heal.
		bool HasSlider(const View::Member& a_member)
		{
			return a_member.paths ? CarriesNow(a_member, Paths::Ability::kHeal) : a_member.orders.role == Role::kHealer;
		}

		// The ABILITIES panel as the pad walks it: a row of badges, each with its AUTO switch under
		// it (items 2k and 2k + 1, in the order the panel draws them), then the path tile and the
		// healer's line, a row each.
		struct AbilityRows
		{
			int columns{ 0 };
			int path{ -1 };
			int slider{ -1 };

			[[nodiscard]] int  Count() const { return columns * 2 + (path >= 0 ? 1 : 0) + (slider >= 0 ? 1 : 0); }
			[[nodiscard]] bool Badge(int a_item) const { return a_item >= 0 && a_item < columns * 2 && a_item % 2 == 0; }
			[[nodiscard]] bool Auto(int a_item) const { return a_item >= 0 && a_item < columns * 2 && a_item % 2 == 1; }
		};

		AbilityRows AbilityRowsOf(const View::Member& a_member)
		{
			AbilityRows rows;
			if (a_member.paths) {
				rows.columns = static_cast<int>(Paths::kSlots);
				rows.path = rows.columns * 2;
			} else {
				const auto role = a_member.orders.role;
				rows.columns = (role == Role::kTank || role == Role::kHealer ? 1 : 0) + (role == Role::kHealer ? 1 : 0) + (SpecialOf(role) != Special::kNone ? 1 : 0);
			}
			if (HasSlider(a_member)) {
				rows.slider = rows.columns * 2 + (rows.path >= 0 ? 1 : 0);
			}
			return rows;
		}

		int Unspent(const View::Member& a_member) { return std::max(a_member.pointsEarned - a_member.pointsSpent, 0); }

		// The first node worth looking at: something that can be learned now, or the first ability.
		int FirstWorthy(const View::Member& a_member, Role a_path)
		{
			for (std::size_t i = 0; i < Paths::kNodes; ++i) {
				if (Paths::CanLearn(a_member.record, a_path, i, a_member.pointsEarned) == Paths::Block::kNone) {
					return static_cast<int>(i);
				}
			}
			return 0;
		}

		void CloseSpellList();
		void DrawOrb(Im::ImDrawList* a_draw, V c, float r, bool a_lit, float a_s, float a_alpha);

		// The guide opens by itself the first time a path does, a moment after the page has come in.
		// That first time is saved at once, so closing the plan midway does not bring it back.
		void OpenGuide(float a_delay = 0.0f)
		{
			auto& guide = state.guide;
			guide.open = true;
			guide.step = 0;
			guide.openedAt = state.clock + a_delay;
			guide.stepAt = guide.openedAt;
			guide.placed = false;
			if (!Settings::Get().pathsGuideSeen) {
				auto data = Settings::Chosen();
				data.pathsGuideSeen = true;
				Settings::Submit(data);
			}
		}

		void CloseGuide() { state.guide.open = false; }

		void OpenPathPage(const View::Member& a_member, Role a_path, int a_node)
		{
			auto& page = state.page;
			page.member = a_member.id;
			page.path = a_path;
			page.cursor = a_node >= 0 ? a_node : FirstWorthy(a_member, a_path);
			page.openedAt = state.clock;
			page.tabAt = state.clock;
			page.resetArmed = false;
			page.seenMember = 0;
			CloseSpellList();
			if (!Settings::Get().pathsGuideSeen) {
				OpenGuide(0.45f);
			}
		}

		void ClosePathPage() { state.page.member = 0; }

		void PathCommand(Battle::Command::Type a_type, RE::FormID a_member, Role a_path, std::size_t a_node = 0, std::size_t a_slot = 0,
			Paths::Ability a_ability = Paths::Ability::kNone)
		{
			Battle::Command command;
			command.type = a_type;
			command.member = a_member;
			command.path = a_path;
			command.node = a_node;
			command.slot = a_slot;
			command.ability = a_ability;
			Battle::Queue(command);
		}

		// A flat-topped hexagon: how a trait is drawn (abilities are diamonds, branches rings).
		void Hexagon(Im::ImDrawList* a_draw, V c, float r, U32 a_fill, U32 a_edge, float a_thickness)
		{
			std::array<V, 6> points{};
			for (int i = 0; i < 6; ++i) {
				const float a = 1.0471976f * static_cast<float>(i);
				points[static_cast<std::size_t>(i)] = { c.x + std::cos(a) * r, c.y + std::sin(a) * r };
			}
			Draw::AddConvexPolyFilled(a_draw, points.data(), 6, a_fill);
			for (std::size_t i = 0; i < 6; ++i) {
				Draw::AddLine(a_draw, points[i], points[(i + 1) % 6], a_edge, a_thickness);
			}
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
			const bool  nav = state.keyboardNav && live && !state.spells.member;  // the spell list has the pad while open
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
                        Diamond(a_draw, mc, mr, Alpha((palette.Subdued() ? palette.inset : IM_COL32(24, 11, 11, 250)), alpha), Alpha(kEdge, alpha), 1.4f * a_s);
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
                    } else if (a_member.drained) {
                        status += "  ·  out of magicka, falling back";
                    } else if (a_member.inCombat && !a_member.targetName.empty()) {
                        status += std::format("  ·  fighting {}", a_member.targetName);
                    }
                    Text(a_draw, { p.x, yy }, Alpha(RoleAccent(orders.role), alpha), 15.5f * a_s, roleName);
                    Text(a_draw, { p.x + TextSize(15.5f * a_s, roleName).x, yy }, Alpha(a_member.down || noArrows || a_member.drained ? kRed : kTextDim, alpha), 15.5f * a_s, status);
                    yy += 28.0f * a_s;
                    const float barW = (inner - 2 * 8.0f * a_s) / 3.0f;
                    const std::array<std::pair<float, U32>, 3> vitals{ { { a_member.health, kHealth }, { a_member.magicka, kMagicka }, { a_member.stamina, kStamina } } };
                    // The numbers beneath each bar, as they stand right now.
                    const std::array<std::pair<float, float>, 3> numbers{ { { a_member.healthNow, a_member.healthMax }, { a_member.magickaNow, a_member.magickaMax },
                        { a_member.staminaNow, a_member.staminaMax } } };
                    for (int i = 0; i < 3; ++i) {
                        const float value = state.tw.Toward("vital", static_cast<std::uint64_t>(a_member.id) * 4 + i, vitals[static_cast<std::size_t>(i)].first, 7.0f);
                        Bar(a_draw, { p.x + i * (barW + 8.0f * a_s), yy }, barW, 4.0f * a_s, value, vitals[static_cast<std::size_t>(i)].second, alpha);
                        const auto& [now, most] = numbers[static_cast<std::size_t>(i)];
                        if (most > 0.0f) {
                            // Magicka quickened by a healer's Rejuvenate: its numbers in the mystic blue.
                            const bool quick = i == 1 && a_member.rejuvenated > 0.0f;
                            const auto text = quick ? std::format("{:.0f} / {:.0f}  +{:.0f}%", std::max(now, 0.0f), most, a_view.tuning.rejuvenateBonus) :
                                                      std::format("{:.0f} / {:.0f}", std::max(now, 0.0f), most);
                            Text(a_draw, { p.x + i * (barW + 8.0f * a_s), yy + 8.0f * a_s }, Alpha(quick ? kRejuvenated : kTextDim, alpha), 13.0f * a_s, text);
                        }
                    }
                    yy += 38.0f * a_s;

                    // Roles are chosen out of a fight. In one, the box holds the role they fight as
                    // and, beside it, what they bring to it.
                    const bool  locked = a_view.inCombat || !a_view.enemies.empty();
                    const float lockIn = state.tw.Toward("roleLocked", 0, locked ? 1.0f : 0.0f, 8.0f);
                    SectionLabel(a_draw, { p.x, yy }, a_s, alpha, "ROLE", nav && state.section == kRoleSection && !locked);
                    if (lockIn > 0.01f) {
                        constexpr std::string_view note = "SET OUT OF A FIGHT";
                        const float                nw = LabelWidth(11.0f * a_s, note, 1.6f * a_s);
                        Label(a_draw, { p.x + inner - nw, yy + 1.5f * a_s }, Alpha(kTextFaint, alpha * lockIn), 11.0f * a_s, note, 1.6f * a_s);
                        Glyph(a_draw, { p.x + inner - nw - 11.0f * a_s, yy + 7.5f * a_s }, 10.0f * a_s, 0xF023, Alpha(kTextFaint, alpha * lockIn));
                    }
                    yy += 24.0f * a_s;
                    const int   columns = 3;
                    const float tgap = 8.0f * a_s;
                    const float tileW = (inner - tgap * (columns - 1)) / columns;
                    const float tileH = 66.0f * a_s;
                    const float boxH = tileH * 2.0f + tgap;
                    if (lockIn > 0.01f) {
                        RoleInFight(a_draw, a_member, orders.role, { p.x, yy }, inner, tileW, boxH, a_s, alpha * lockIn);
                    }
                    const float gridAlpha = alpha * (1.0f - lockIn);
                    const int   chosen = static_cast<int>(orders.role);
                    const float hx = state.tw.Toward("roleX", 0, static_cast<float>(chosen % columns), 14.0f);
                    const float hy = state.tw.Toward("roleY", 0, static_cast<float>(chosen / columns), 14.0f);
                    if (gridAlpha > 0.01f) {
                        const V hmin{ p.x + hx * (tileW + tgap), yy + hy * (tileH + tgap) };
                        const V hmax{ hmin.x + tileW, hmin.y + tileH };
                        Lit(a_draw, hmin, hmax, gridAlpha);
                        Draw::AddRect(a_draw, hmin, hmax, Alpha(kEdge, gridAlpha * 0.5f), 0.0f, 0, 1.0f);
                    }
                    for (int i = 0; gridAlpha > 0.01f && i < static_cast<int>(Role::kCount); ++i) {
                        const auto& info = roles[static_cast<std::size_t>(i)];
                        const V     min{ p.x + (i % columns) * (tileW + tgap), yy + (i / columns) * (tileH + tgap) };
                        const V     max{ min.x + tileW, min.y + tileH };
                        bool        hovered = false;
                        bool        clicked = false;
                        if (!locked) {
                            Im::PushID(2000 + i);
                            clicked = Hit("role", min, { tileW, tileH }, hovered) && live;
                            Im::PopID();
                        }
                        const bool  focused = nav && !locked && state.section == kRoleSection && state.item == i;
                        const auto  lack = a_member.arsenalKnown ? LackFor(static_cast<Role>(i), a_member.arsenal) : Lack::kNothing;
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
                            Draw::AddRectFilled(a_draw, min, max, Alpha(Mix(kTile, kTileHover, hover), gridAlpha), 0.0f, 0);
                            Draw::AddRect(a_draw, min, max, Alpha(kGlassEdge, gridAlpha * (0.5f + 0.5f * hover)), 0.0f, 0, 1.0f);
                        }
                        if (focused) {
                            FocusOutline(a_draw, min, max, 7.0f * a_s, gridAlpha);
                        }
                        const float on = i == chosen ? 1.0f : hover * 0.5f;
                        const float dim = playable ? 1.0f : 0.32f;
                        Glyph(a_draw, { min.x + tileW * 0.5f, min.y + 25.0f * a_s - 2.0f * a_s * hover }, (23.0f + 5.0f * flash) * a_s, info.glyph,
                            Alpha(Mix(kTextDim, kText, on), gridAlpha * dim));
                        TextCentered(a_draw, { min.x + tileW * 0.5f, min.y + 50.0f * a_s }, Alpha(Mix(kTextDim, kText, on), gridAlpha * dim), 15.0f * a_s, info.name);
                        if (!playable) {
                            Glyph(a_draw, { max.x - 12.0f * a_s, min.y + 12.0f * a_s }, 10.0f * a_s, 0xF023, Alpha(kTextFaint, gridAlpha));
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
                    yy += boxH;
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

			// --- Abilities: round badges, each with its cooldown sweeping round inside ---
			// Tanks and healers have their taunt or heal; every role its special.
			// With the Paths add-on: the two abilities their path carries, and the path.
			const bool    tree = a_member.paths;
			const bool    tank = !tree && orders.role == Role::kTank;
			const bool    healer = !tree && orders.role == Role::kHealer;
			const bool    primary = tank || healer;
			const Special special = tree ? Special::kNone : SpecialOf(orders.role);
			const bool    hasSlider = HasSlider(a_member);
			{
				const float alpha = alphaOf(2);
				const Rect  r = Panel(a_draw, origin(2), a_width, pad, enter(2), [&](V p, float inner) {
					float     yy = p.y;
					const int focusItem = nav && state.section == kAbilitySection ? state.item : -1;
					int       next = 0;
					const int primaryBadge = primary ? next++ : -1;
					const int primaryAuto = primary ? next++ : -1;
					const int rejuvenateBadge = healer ? next++ : -1;
					const int rejuvenateAuto = healer ? next++ : -1;
					const int specialBadge = special != Special::kNone ? next++ : -1;
					const int specialAuto = special != Special::kNone ? next++ : -1;
					std::array<int, Paths::kSlots> carriedBadge{ -1, -1 }, carriedAuto{ -1, -1 };
					for (std::size_t s = 0; tree && s < Paths::kSlots; ++s) {
						carriedBadge[s] = next++;
						carriedAuto[s] = next++;
					}
					const int pathButton = tree ? next++ : -1;
					const int slider = hasSlider ? next++ : -1;
					SectionLabel(a_draw, { p.x, yy }, a_s, alpha, "ABILITIES", focusItem >= 0);
					yy += 30.0f * a_s;

					const int   count = tree ? static_cast<int>(Paths::kSlots) : (primary ? 1 : 0) + (healer ? 1 : 0) + (special != Special::kNone ? 1 : 0);
					const float slot = inner / static_cast<float>(std::max(count, 1));
					const float radius = std::min(30.0f * a_s, slot * 0.3f);
					const V     top{ p.x, yy };
					int         placed = 0;

					// A badge: the icon in a ringed disc. Cooling down, a dark wedge covers what is
					// left of the wait and shrinks clockwise from noon, with a bright hand at its edge,
					// the ring filling behind it and the seconds in the middle. Ready, it lights and
					// breathes. Under it, the name and its AUTO switch.
					struct Badge
					{
						int           index{ 0 };
						std::uint32_t glyph{ 0 };
						std::string   name;
						float         ready{ 0.0f };
						float         cooldown{ 0.0f };
						bool          usable{ false };    // lit and bright: it can be used now
						bool          clickable{ true };  // pressing it does something (a switch always does)
						bool*         autoOn{ nullptr };
						int           badgeItem{ -1 };
						int           autoItem{ -1 };
						std::string   tip;
						std::string   autoTip;
						bool          padNow{ false };
						bool          padFlip{ false };
						std::function<void()> use;
						bool          orb{ false };       // Magnus: its ball of light instead of an icon
					};
					auto badge = [&](Badge a_badge) {
						const V     c{ top.x + slot * (static_cast<float>(placed) + 0.5f), top.y + radius + 4.0f * a_s };
						const float id = static_cast<float>(a_badge.index);
						++placed;
						const float charge = a_badge.cooldown > 0.0f && a_badge.ready > 0.0f ? Clamp01(1.0f - a_badge.ready / a_badge.cooldown) : 1.0f;
						const bool  cooling = charge < 1.0f;  // cooldowns run on after a fight, so the wait shows then too
						const bool  lit = a_badge.usable && !cooling;

						bool hovered = false;
						Im::PushID(a_badge.index);
						const bool clicked = Hit("badge", { c.x - radius, c.y - radius }, { radius * 2.0f, radius * 2.0f }, hovered) && live;
						Im::PopID();
						const float hover = state.tw.Toward("badgeHover", static_cast<std::uint64_t>(a_badge.index), hovered ? 1.0f : 0.0f, 16.0f);
						float&      press = state.tw.Raw("badgePress", static_cast<std::uint64_t>(a_badge.index), 0.0f);
						if (hovered) {
							Hovering(0x400 + static_cast<std::uint64_t>(a_badge.index) * 2);
							Tip(a_badge.tip);
						}
						if ((clicked || (focusItem == a_badge.badgeItem && a_input.activate) || a_badge.padNow) && a_badge.use) {
							if (a_badge.clickable) {
								press = 1.0f;
								a_badge.use();
								Sound("UIMenuOK");
							} else {
								Sound("UIMenuCancel");
							}
						}
						press = Follow(press, 0.0f, state.tw.dt, 6.0f);
						const float rr = radius * (1.0f + 0.06f * press + 0.03f * hover);

						// The glow behind a ready badge.
						const float breathe = 0.5f + 0.5f * std::sin(state.clock * 2.6f + id);
						if (lit) {
							Halo(a_draw, c, rr * 1.9f, kAccent, alpha * (0.45f + 0.25f * breathe + 0.4f * press));
						}
						// Disc and frame: a dark plate, an oxblood well when ready, a metal ring, a hairline outside.
						Draw::AddCircleFilled(a_draw, c, rr, Alpha(IM_COL32(16, 16, 18, 245), alpha), 48);
						if (lit) {
							Draw::AddCircleFilled(a_draw, c, rr * 0.92f, Alpha(kAccent, alpha * (0.55f + 0.2f * breathe)), 48);
						}
						Draw::AddCircle(a_draw, c, rr, Alpha(Mix(kTextDim, kText, std::max(hover, lit ? 0.6f : 0.0f)), alpha * (a_badge.usable ? 0.9f : 0.4f)), 48,
							2.0f * a_s);
						Draw::AddCircle(a_draw, c, rr + 5.0f * a_s, Alpha(kGlassEdge, alpha), 48, 1.0f);
						for (int k = 0; k < 4; ++k) {  // studs at the quarters, as on the panels
							const float a = 1.5707963f * static_cast<float>(k) - 1.5707963f;
							Stud(a_draw, { c.x + std::cos(a) * (rr + 5.0f * a_s), c.y + std::sin(a) * (rr + 5.0f * a_s) }, 2.4f * a_s, alpha);
						}
						if (a_badge.orb) {
							DrawOrb(a_draw, c, rr * 0.5f, true, a_s, alpha * (cooling ? 0.35f : 1.0f));
						} else {
							Glyph(a_draw, c, (22.0f + 3.0f * press) * a_s, a_badge.glyph,
								Alpha(a_badge.usable ? kText : kTextDim, alpha * (a_badge.usable ? (cooling ? 0.3f : 1.0f) : 0.45f)));
						}

						// The cooldown: the wait still to come, as a wedge from the hand round to noon.
						if (cooling) {
							constexpr float kNoon = -1.5707963f;
							const float     from = kNoon + 6.2831853f * charge, to = kNoon + 6.2831853f;
							const int       steps = std::max(2, static_cast<int>(64.0f * (1.0f - charge)));
							V               last{ c.x + std::cos(from) * rr * 0.97f, c.y + std::sin(from) * rr * 0.97f };
							for (int k = 1; k <= steps; ++k) {
								const float a = from + (to - from) * static_cast<float>(k) / static_cast<float>(steps);
								const V     pt{ c.x + std::cos(a) * rr * 0.97f, c.y + std::sin(a) * rr * 0.97f };
								Draw::AddTriangleFilled(a_draw, c, last, pt, Alpha(kShade, alpha * 0.62f));
								last = pt;
							}
							// The ring fills behind the hand.
							Draw::PathClear(a_draw);
							Draw::PathArcTo(a_draw, c, rr, kNoon, from, std::max(4, static_cast<int>(48.0f * charge)));
							Draw::PathStroke(a_draw, Alpha(kEdge, alpha), 0, 3.0f * a_s);
							Draw::AddLine(a_draw, c, { c.x + std::cos(from) * rr, c.y + std::sin(from) * rr }, Alpha(kEdge, alpha * 0.9f), 1.6f * a_s);
							const auto seconds = std::format("{:.0f}", std::ceil(a_badge.ready));
							TextCentered(a_draw, { c.x + 1.0f * a_s, c.y + 1.5f * a_s }, Alpha(kShade, alpha * 0.9f), 21.0f * a_s, seconds);
							TextCentered(a_draw, c, Alpha(kText, alpha), 21.0f * a_s, seconds);
						}
						if (focusItem == a_badge.badgeItem) {
							Draw::AddCircle(a_draw, c, rr + 10.0f * a_s, Alpha(kFocusRing, alpha * (0.65f + 0.35f * std::sin(state.clock * 5.0f))), 48, 1.6f * a_s);
						}

						// The name, and the AUTO switch under it.
						const float ny = c.y + radius + 14.0f * a_s;
						TextCentered(a_draw, { c.x, ny }, Alpha(a_badge.usable ? kText : kTextDim, alpha), 13.5f * a_s, Upper(a_badge.name));
						if (a_badge.autoOn) {
							const float pw = 52.0f * a_s, ph = 18.0f * a_s;
							const V     pmin{ c.x - pw * 0.5f, ny + 12.0f * a_s };
							bool        over = false;
							Im::PushID(a_badge.index);
							const bool flipped = Hit("auto", pmin, { pw, ph }, over) && live;
							Im::PopID();
							if (over) {
								Hovering(0x401 + static_cast<std::uint64_t>(a_badge.index) * 2);
								Tip(a_badge.autoTip);
							}
							if (flipped || (focusItem == a_badge.autoItem && a_input.activate) || a_badge.padFlip) {
								*a_badge.autoOn = !*a_badge.autoOn;
								changed = true;
								Sound("UIMenuOK");
							}
							const float k = state.tw.Toward("autoPill", static_cast<std::uint64_t>(a_member.id) * 8 + static_cast<std::uint64_t>(a_badge.index),
								*a_badge.autoOn ? 1.0f : 0.0f, 14.0f);
							Draw::AddRectFilled(a_draw, pmin, { pmin.x + pw, pmin.y + ph }, Alpha(Mix(IM_COL32(255, 255, 255, 10), kAccent, k), alpha), 0.0f, 0);
							Draw::AddRect(a_draw, pmin, { pmin.x + pw, pmin.y + ph }, Alpha(Mix(kGlassEdge, kEdge, std::max(k, over ? 0.7f : 0.0f)), alpha), 0.0f, 0,
								1.0f);
							TextCentered(a_draw, { c.x, pmin.y + ph * 0.5f }, Alpha(Mix(kTextFaint, kText, k), alpha), 11.5f * a_s, "AUTO");
							if (focusItem == a_badge.autoItem) {
								FocusOutline(a_draw, pmin, { pmin.x + pw, pmin.y + ph }, 0.0f, alpha);
							}
						}
					};

					// Pad X: the first ability now; pad Y: its switch.
					bool padNow = a_input.now, padFlip = a_input.flipAuto;
					if (primary) {
						Badge b;
						b.index = 0;
						b.glyph = tank ? 0xF0A1 : 0xF4BE;
						b.name = tank ? "Taunt" : "Heal";
						b.ready = tank ? a_member.tauntReady : a_member.healReady;
						b.cooldown = tank ? a_member.tauntCooldown : a_view.tuning.healCooldown;
						b.usable = a_member.inCombat;
						b.clickable = b.usable;
						b.autoOn = &orders.autoAbility;
						b.badgeItem = primaryBadge;
						b.autoItem = primaryAuto;
						if (tank) {
							b.tip = std::format("Taunt now\nHolds level {} and below  ·  {:.0f} reach  ·  {:.0f} s  ·  {:.0f} stamina", a_member.tauntCertain,
								a_member.tauntRadius, a_member.tauntHold, a_member.tauntStamina);
							if (a_member.tauntArmor > 0.0f) {
								b.tip += std::format("\nBraces them: +{:.0f} armor for as long as it holds, more as they rise in level.", a_member.tauntArmor);
							}
						} else {
							b.tip = std::format("Heal now\nYou and the whole party: +{:.0f} health each, more as they rise in level. No magicka needed.\nReady again {:.0f} s after use.",
								a_member.healAmount, a_view.tuning.healCooldown);
						}
						if (!a_member.inCombat) {
							b.tip += "\nIn a fight only.";
						}
						b.autoTip = tank ? "AUTO on: they taunt by themselves when it is ready and worth it.\nOff: only when you press Taunt."s :
						                   "AUTO on: they heal by themselves when anyone is down or drops below the line.\nOff: only when you press Heal."s;
						b.padNow = padNow;
						b.padFlip = padFlip;
						const auto command = tank ? Battle::Command::Type::kTauntNow : Battle::Command::Type::kHealNow;
						b.use = [&, command] { Simple(command, a_member.id); };
						badge(std::move(b));
						padNow = padFlip = false;
					}
					if (healer) {
						Badge b;
						b.index = 3;
						b.glyph = 0xF5BB;
						b.name = "Rejuvenate";
						b.ready = a_member.rejuvenateReady;
						b.cooldown = a_member.rejuvenateCooldown;
						b.usable = a_member.inCombat;
						b.clickable = b.usable;
						b.autoOn = &orders.autoRejuvenate;
						b.badgeItem = rejuvenateBadge;
						b.autoItem = rejuvenateAuto;
						b.tip = std::format("Rejuvenate now\nYour magicka and every companion's comes back {:.0f}% faster for {:.0f} s, longer as they rise in level.\nReady again {:.0f} s after use.{}",
							a_view.tuning.rejuvenateBonus, a_member.rejuvenateSeconds, 2.0f * a_member.rejuvenateSeconds, a_member.inCombat ? "" : "\nIn a fight only.");
						b.autoTip = "AUTO on: they rejuvenate by themselves when you, a mage or a healer runs out of magicka or nearly does.\nOff: only when you press Rejuvenate.";
						b.use = [&] { Simple(Battle::Command::Type::kRejuvenateNow, a_member.id); };
						badge(std::move(b));
					}
					if (special != Special::kNone) {
						const auto& info = Info(special);
						Badge       b;
						b.index = 1;
						b.glyph = info.glyph;
						b.name = std::string(info.name);
						b.ready = a_member.specialReady;
						b.cooldown = a_member.specialCooldown;
						b.usable = a_member.inCombat;
						b.clickable = b.usable;
						b.autoOn = &orders.autoSpecial;
						b.badgeItem = specialBadge;
						b.autoItem = specialAuto;
						b.tip = std::format("{} now\n{}\nReady again {:.0f} s after use.{}", info.name, info.summary, SpecialCooldown(special),
							a_member.inCombat ? "" : "\nIn a fight only.");
						b.autoTip = std::format("AUTO on: {}\nOff: only when you press {}.", info.own, info.name);
						b.padNow = padNow;
						b.padFlip = padFlip;
						b.use = [&] { Simple(Battle::Command::Type::kSpecialNow, a_member.id); };
						badge(std::move(b));
					}
					// The two abilities a path carries: used now in a fight; out of one, their path
					// opens on the ability, to rank it or carry something else.
					std::array<bool, Paths::kSlots> carriedAutoOn{};
					for (std::size_t s = 0; tree && s < Paths::kSlots; ++s) {
						const auto& carried = a_member.carried[s];
						const auto  ability = carried.ability;
						const auto& info = Paths::Info(ability);
						const int   node = Paths::IndexOf(orders.role, ability);
						const int   rank = std::max(Paths::Rank(a_member.record, orders.role, ability), 1);
						const auto  bit = AbilityBit(ability);
						carriedAutoOn[s] = (orders.manual & bit) == 0;
						Badge b;
						b.index = 10 + static_cast<int>(s);
						b.glyph = info.glyph ? info.glyph : 0xF128;
						b.name = ability == Paths::Ability::kNone ? std::string("Empty") : std::string(info.name);
						b.ready = carried.ready;
						b.cooldown = carried.cooldown;
						b.usable = ability != Paths::Ability::kNone;
						b.clickable = b.usable;
						b.autoOn = b.usable ? &carriedAutoOn[s] : nullptr;
						b.orb = ability == Paths::Ability::kMagnus;
						b.badgeItem = carriedBadge[s];
						b.autoItem = carriedAuto[s];
						auto words = node >= 0 ? Paths::Describe(Paths::PathOf(orders.role)[static_cast<std::size_t>(node)].perk, rank) : std::string();
						if (ability == Paths::Ability::kTaunt) {
							words += std::format("\nHolds level {} and below  ·  {:.0f} reach  ·  {:.0f} stamina", a_member.tauntCertain, a_member.tauntRadius,
								a_member.tauntStamina);
						}
						b.tip = a_member.inCombat ? std::format("{} now  ·  rank {}\n{}\nReady again {:.0f} s after use.", info.name, RankWord(rank), words, carried.cooldown) :
						                            std::format("{}  ·  rank {}\n{}\nChoose to open their path: rank it up, or carry something else.", info.name,
														RankWord(rank), words);
						b.autoTip = std::format("AUTO on: {}\nOff: only when you press {}.", info.own, info.name);
						b.padNow = s == 0 && padNow;
						b.padFlip = s == 0 && padFlip;
						b.use = [&, ability, node] {
							if (a_member.inCombat) {
								Battle::Command command;
								command.type = Battle::Command::Type::kAbilityNow;
								command.member = a_member.id;
								command.ability = ability;
								Battle::Queue(command);
							} else {
								OpenPathPage(a_member, orders.role, std::max(node, 0));
							}
						};
						badge(std::move(b));
						if (carriedAutoOn[s] != ((orders.manual & bit) == 0)) {
							orders.manual = carriedAutoOn[s] ? (orders.manual & ~bit) : (orders.manual | bit);
							changed = true;
						}
					}
					if (tree) {
						padNow = padFlip = false;
					}
					yy = top.y + radius * 2.0f + 4.0f * a_s + 64.0f * a_s;

					// Their path: a wide tile that opens it, the points still to spend lit on the right.
					if (tree) {
						const float h = 46.0f * a_s;
						const V     min{ p.x, yy }, max{ p.x + inner, yy + h };
						bool        hovered = false;
						const bool  clicked = Hit("pathTile", min, { inner, h }, hovered) && live;
						const bool  focused = focusItem == pathButton;
						const int   unspent = std::max(a_member.pointsEarned - a_member.pointsSpent, 0);
						const float hover = state.tw.Toward("pathTile", a_member.id, hovered || focused ? 1.0f : 0.0f, 16.0f);
						Draw::AddRectFilled(a_draw, min, max, Alpha(Mix(kTile, kTileHover, hover), alpha), 0.0f, 0);
						Lit(a_draw, min, max, alpha * (0.35f + 0.4f * hover));
						Draw::AddRect(a_draw, min, max, Alpha(Mix(kGlassEdge, kEdge, hover * 0.8f), alpha), 0.0f, 0, 1.0f);
						const V dc{ min.x + 24.0f * a_s, min.y + h * 0.5f };
						Diamond(a_draw, dc, 15.0f * a_s, Alpha((palette.Subdued() ? palette.inset : IM_COL32(24, 11, 11, 250)), alpha), Alpha(kEdge, alpha), 1.2f * a_s);
						Glyph(a_draw, dc, 12.0f * a_s, Info(orders.role).glyph, Alpha(kText, alpha));
						Label(a_draw, { min.x + 48.0f * a_s, min.y + 9.0f * a_s }, Alpha(kText, alpha), 13.5f * a_s, Upper(Paths::Info(orders.role).title), 1.6f * a_s);
						Text(a_draw, { min.x + 48.0f * a_s, min.y + 25.0f * a_s }, Alpha(kTextFaint, alpha), 12.5f * a_s,
							std::format("{} of {} point{} spent", a_member.pointsSpent, a_member.pointsEarned, a_member.pointsEarned == 1 ? "" : "s"));
						if (unspent > 0) {
							// The points waiting, in a breathing red chip.
							const auto  chip = std::format("{} TO SPEND", unspent);
							const float cw = LabelWidth(11.5f * a_s, chip, 1.4f * a_s) + 18.0f * a_s;
							const V     cmin{ max.x - cw - 30.0f * a_s, min.y + h * 0.5f - 10.0f * a_s };
							const float breathe = 0.5f + 0.5f * std::sin(state.clock * 3.0f);
							Halo(a_draw, { cmin.x + cw * 0.5f, cmin.y + 10.0f * a_s }, cw * 0.7f, kAccent, alpha * (0.4f + 0.3f * breathe));
							Draw::AddRectFilled(a_draw, cmin, { cmin.x + cw, cmin.y + 20.0f * a_s }, Alpha(kAccent, alpha), 0.0f, 0);
							Draw::AddRect(a_draw, cmin, { cmin.x + cw, cmin.y + 20.0f * a_s }, Alpha(kEdge, alpha), 0.0f, 0, 1.0f);
							Label(a_draw, { cmin.x + 9.0f * a_s, cmin.y + 4.0f * a_s }, Alpha(kText, alpha), 11.5f * a_s, chip, 1.4f * a_s);
						}
						Glyph(a_draw, { max.x - 16.0f * a_s + 3.0f * a_s * hover, min.y + h * 0.5f }, 13.0f * a_s, 0xF054, Alpha(Mix(kTextDim, kText, hover), alpha));
						if (focused) {
							FocusOutline(a_draw, min, max, 0.0f, alpha);
						}
						if (hovered) {
							Hovering(0x480);
							Tip("Their path: a perk tree for this role. A point for every level, spent on new abilities, ranks and traits, and the two abilities "
								"they carry into a fight.");
						}
						if (clicked || (focused && a_input.activate)) {
							OpenPathPage(a_member, orders.role, -1);
							Sound("UIMenuOK");
						}
						yy += h + 6.0f * a_s;
					}

					// The healer's line: a slider in steps of five.
					if (hasSlider) {
						yy += 10.0f * a_s;
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
						if (focusItem == slider && a_input.dx) {
							threshold = std::clamp(threshold + (a_input.dx > 0 ? 5 : -5), 20, 90);
						}
						const float t = state.tw.Toward("threshold", a_member.id, (static_cast<float>(threshold) - 20.0f) / 70.0f, 18.0f);
						Draw::AddRectFilled(a_draw, { tmin.x, tmin.y + th * 0.5f - 0.5f }, { tmin.x + tw, tmin.y + th * 0.5f + 0.5f }, Alpha(kGlassEdge, alpha * 1.4f), 0.0f, 0);
						Draw::AddRectFilled(a_draw, { tmin.x, tmin.y + th * 0.25f }, { tmin.x + tw * t, tmin.y + th * 0.75f }, Alpha(kEdge, alpha), 0.0f, 0);
						Diamond(a_draw, { tmin.x + tw * t, tmin.y + th * 0.5f }, (8.0f + (dragging ? 2.0f : 0.0f)) * a_s, Alpha(kText, alpha), Alpha(kShade, alpha * 0.5f), 1.0f);
						if (focusItem == slider) {
							FocusOutline(a_draw, { tmin.x - 6, tmin.y - 8 }, { tmin.x + tw + 6, tmin.y + th + 8 }, 6.0f, alpha);
						}
						if (threshold != orders.healThreshold) {
							orders.healThreshold = static_cast<std::uint8_t>(threshold);
							changed = true;
						}
						yy += 18.0f * a_s;
					}

					// The answer to the last press: what they did, or why not.
					{
						const bool  waiting = a_member.asked;
						const bool  fresh = !a_member.answer.empty() && a_member.answerAge < 6.0f;
						const float show = waiting ? 1.0f : (fresh ? 1.0f - Clamp01((a_member.answerAge - 4.5f) / 1.5f) : 0.0f);
						const float in = state.tw.Toward("answerIn", a_member.id, show, 10.0f);
						if (in > 0.01f) {
							yy += 8.0f * a_s;
							const std::string line = waiting ? std::format("{} heard you...", a_member.name) : a_member.answer;
							const bool        good = !waiting && a_member.answerDone;
							const U32         color = waiting ? kTextDim : (good ? kText : kAmber);
							Glyph(a_draw, { p.x + 7.0f * a_s, yy + 9.0f * a_s }, 12.0f * a_s, waiting ? 0xF141 : (good ? 0xF00C : 0xF071), Alpha(color, alpha * in));
							Text(a_draw, { p.x + 22.0f * a_s, yy }, Alpha(color, alpha * in), 15.0f * a_s, line);
							yy += 18.0f * a_s * in;
						}
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

		// A mage's or healer's two hands, each a tile naming the spell held there. Pointing at
		// one and choosing opens the list beside the sheet.
		float HandTiles(Im::ImDrawList* a_draw, const View::Member& a_member, V a_at, float a_inner, float a_s, float a_alpha, const Input& a_input)
		{
			const bool live = !state.closing;
			const bool row = state.keyboardNav && live && !state.spells.member && state.section == kSpellSection;
			float      yy = a_at.y;
			SectionLabel(a_draw, { a_at.x, yy }, a_s, a_alpha, "SPELLS", row);
			yy += 28.0f * a_s;
			for (const auto hand : { kLeftHand, kRightHand }) {
				const auto& held = a_member.hands[hand];
				const bool  chosen = held.id != 0;
				const float h = 58.0f * a_s;
				const V     min{ a_at.x, yy }, max{ a_at.x + a_inner, yy + h };
				bool        hovered = false;
				Im::PushID(6000 + static_cast<int>(hand));
				const bool clicked = Hit("hand", min, { a_inner, h }, hovered) && live;
				Im::PopID();
				const bool  focused = row && state.item == static_cast<int>(hand);
				const bool  open = state.spells.member == a_member.id && state.spells.hand == hand;
				const float hover = state.tw.Toward("handHover", hand, hovered || focused ? 1.0f : 0.0f, 16.0f);
				const float lit = state.tw.Toward("handOpen", hand, open ? 1.0f : 0.0f, 14.0f);
				Draw::AddRectFilled(a_draw, min, max, Alpha(Mix(kTile, kTileHover, hover), a_alpha), 0.0f, 0);
				Lit(a_draw, min, max, a_alpha * lit);
				Draw::AddRect(a_draw, min, max, Alpha(Mix(kGlassEdge, kEdge, lit * 0.7f), a_alpha), 0.0f, 0, 1.0f);
				if (hovered) {
					Hovering(0x600 + static_cast<std::uint64_t>(hand));
				}

				// The school's mark in a diamond; the hand above the spell's name; what it is beneath.
				const U32 tone = chosen ? SchoolColour(held.school) : kTextFaint;
				const V   dc{ min.x + 27.0f * a_s, min.y + h * 0.5f };
				Diamond(a_draw, dc, 16.0f * a_s, Alpha(IM_COL32(19, 19, 20, 245), a_alpha), Alpha(chosen ? tone : kGlassEdge, a_alpha), 1.2f * a_s);
				Glyph(a_draw, dc, 13.0f * a_s, chosen ? SchoolGlyph(held.school) : 0xF074, Alpha(tone, a_alpha));
				const float tx = min.x + 54.0f * a_s;
				Label(a_draw, { tx, min.y + 8.0f * a_s }, Alpha(open ? kText : kTextDim, a_alpha), 11.0f * a_s, HandLabel(hand), 1.6f * a_s);
				Text(a_draw, { tx, min.y + 22.0f * a_s }, Alpha(chosen ? kText : kTextDim, a_alpha), 16.5f * a_s, chosen ? held.name : std::string("Their own choice"));
				const auto& now = hand == kRightHand ? a_member.rightHand : a_member.leftHand;
				std::string line;
				if (chosen) {
					line = std::format("{}  ·  {}{}", Rules::tierNames[static_cast<std::size_t>(held.tier)], Rules::schoolNames[static_cast<std::size_t>(held.school)],
						held.twoHanded ? "  ·  both hands" : "");
				} else {
					line = now.empty() ? "Empty-handed now" : std::format("Holding {} now", now);
				}
				Text(a_draw, { tx, min.y + 41.0f * a_s }, Alpha(kTextFaint, a_alpha), 12.5f * a_s, line);
				Glyph(a_draw, { max.x - 18.0f * a_s + 3.0f * a_s * (hover + lit), min.y + h * 0.5f }, 13.0f * a_s, 0xF054, Alpha(Mix(kTextDim, kText, std::max(hover, lit)), a_alpha));
				if (focused) {
					FocusOutline(a_draw, min, max, 0.0f, a_alpha);
				}
				if (hovered || focused) {
					auto tip = std::format("{}\n{}\nChoose to see every spell for this hand.", hand == kRightHand ? "Right hand" : "Left hand",
						chosen ? std::format("They hold {} here in a fight, and no other spell.", held.name) : "Left to them: they use whatever they think best."s);
					if (hovered) {
						Tip(std::move(tip));
					} else {
						TipBeside(std::move(tip), min, max);
					}
				}
				if (clicked || (focused && a_input.activate)) {
					if (open) {
						CloseSpellList();
						Sound("UIMenuCancel");
					} else {
						OpenSpellList(a_member, hand);
						Sound("UIMenuOK");
					}
				}
				yy += h + 8.0f * a_s;
			}
			yy += 2.0f * a_s;
			yy += Paragraph(a_draw, { a_at.x, yy }, Alpha(kTextFaint, a_alpha), 12.5f * a_s, SpellRule(a_member), a_inner);
			return yy + 20.0f * a_s;
		}

		// With no fight on, the chosen companion's sheet sits on the left, the way a bestiary
		// page sits beside its creature: what they can take, what turns aside, what they deal,
		// what they hold, and what they are best at.
		Rect DrawSheet(Im::ImDrawList* a_draw, const View::Member& a_member, V a_origin, float a_width, float a_s, float a_appear, const Input& a_input)
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
                        Diamond(a_draw, mc, mr, Alpha((palette.Subdued() ? palette.inset : IM_COL32(24, 11, 11, 250)), alpha), Alpha(kEdge, alpha), 1.6f * a_s);
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
                    const float cell = inner / 5.0f;
                    const float dr = 19.0f * a_s;
                    for (std::size_t i = 0; i < kElements.size(); ++i) {
                        const float value = a_member.resist[i];
                        const V     c{ p.x + cell * (static_cast<float>(i) + 0.5f), yy };
                        const bool  weak = value < 0.0f, some = value > 0.0f;
                        Diamond(a_draw, c, dr, Alpha(weak ? IM_COL32(58, 15, 14, 245) : IM_COL32(19, 19, 20, 245), alpha),
                            Alpha(weak ? kEdge : (some ? kText : kGlassEdge), alpha * (some || weak ? 0.9f : 1.3f)), (weak || some ? 1.4f : 1.0f) * a_s);
                        Glyph(a_draw, c, 15.0f * a_s, kElements[i], Alpha(weak ? kEdge : (some ? kText : kTextFaint), alpha));
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
                    // A mage or healer: the two hands as tiles, each naming its chosen spell.
                    const bool casts = ChoosesSpells(a_member.orders.role);
                    if (casts) {
                        yy = HandTiles(a_draw, a_member, { p.x, yy }, inner, a_s, alpha, a_input);
                    } else {
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
                    const auto  rows = casts ? std::min<std::size_t>(a_member.skills.size(), 4) : a_member.skills.size();  // room for the hands
                    for (std::size_t i = 0; i < rows; ++i) {
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

		// --- The spell list ---------------------------------------------------------------------

		// Beside the sheet, for one hand: their own choice, then their spells, then yours they
		// could be taught, a tab per school. Choosing a row sends it and closes the list.
		Rect DrawSpellList(Im::ImDrawList* a_draw, const View::Member& a_member, V a_origin, float a_width, float a_maxHeight, float a_s, float a_appear,
			const Input& a_input, int a_dy, bool a_back, const Rect& a_sheet)
		{
			auto&            list = state.spells;
			const auto       hand = list.hand;
			const RE::FormID current = a_member.hands[hand].id;
			list.openT = std::min(list.openT + state.tw.dt / 0.2f, 1.0f);
			const float a = EaseOut(list.openT) * a_appear;

			// A tab for every school with something for this hand, after "All".
			std::array<int, static_cast<std::size_t>(Rules::School::kCount)> perSchool{};
			for (const auto& spell : a_member.spellOptions) {
				if (Fits(spell, hand)) {
					++perSchool[static_cast<std::size_t>(spell.school)];
				}
			}
			std::vector<int> tabs{ -1 };
			for (std::size_t i = 0; i < perSchool.size(); ++i) {
				if (perSchool[i] > 0) {
					tabs.push_back(static_cast<int>(i));
				}
			}
			if (std::find(tabs.begin(), tabs.end(), list.school) == tabs.end()) {
				list.school = -1;
			}
			const int tabAt = static_cast<int>(std::find(tabs.begin(), tabs.end(), list.school) - tabs.begin());
			auto      setTab = [&](int a_school) {
                if (a_school != list.school) {
                    list.school = a_school;
                    list.cursor = 0;
                    list.scrollTo = 0.0f;
                    Sound("UIMenuFocus");
                }
			};
			if (a_input.dx && tabs.size() > 1) {
				const int n = static_cast<int>(tabs.size());
				setTab(tabs[static_cast<std::size_t>((tabAt + (a_input.dx > 0 ? 1 : -1) + n) % n)]);
			}

			// The rows: their own choice, then what they know, then what you could teach them.
			enum : int
			{
				kOwn,
				kKnowHeading,
				kTeachHeading,
				kSpellRow
			};
			struct Row
			{
				int                kind;
				const View::Spell* spell;
			};
			std::vector<Row> rows{ { kOwn, nullptr } };
			for (const bool known : { true, false }) {
				bool headed = false;
				for (const auto& spell : a_member.spellOptions) {
					if (spell.known != known || !Fits(spell, hand) || (list.school >= 0 && static_cast<int>(spell.school) != list.school)) {
						continue;
					}
					if (!headed) {
						rows.push_back({ known ? kKnowHeading : kTeachHeading, nullptr });
						headed = true;
					}
					rows.push_back({ kSpellRow, &spell });
				}
			}
			std::vector<std::size_t> picks;
			for (std::size_t i = 0; i < rows.size(); ++i) {
				if (rows[i].kind == kOwn || rows[i].kind == kSpellRow) {
					picks.push_back(i);
				}
			}

			const float pad = 18.0f * a_s;
			const float rowH = 40.0f * a_s, headH = 30.0f * a_s;
			const float headerH = 46.0f * a_s, tabsH = 70.0f * a_s;
			std::vector<float> top(rows.size());
			float              content = 0.0f;
			for (std::size_t i = 0; i < rows.size(); ++i) {
				top[i] = content;
				content += rows[i].kind == kKnowHeading || rows[i].kind == kTeachHeading ? headH : rowH;
			}
			const float inner = a_width - pad * 2.0f;
			const auto  rule = SpellRule(a_member);
			const float ruleH = TextSize(12.5f * a_s, "X").y * (TextSize(12.5f * a_s, rule).x > inner ? 2.0f : 1.0f);
			const float footH = ruleH + 18.0f * a_s;
			const float roomForList = a_maxHeight - pad * 2.0f - headerH - tabsH - footH;
			const float listH = std::max(std::min(content, roomForList), rowH * 3.0f);

			// The pad's row: up and down walk it, and it is put on what the hand holds first.
			bool cursorMoved = !list.placed;
			if (!list.placed) {
				list.placed = true;
				for (std::size_t k = 0; k < picks.size(); ++k) {
					const auto* spell = rows[picks[k]].spell;
					if ((spell ? spell->id : 0) == current) {
						list.cursor = static_cast<int>(k);
					}
				}
			}
			if (a_dy && !picks.empty()) {
				list.cursor = std::clamp(list.cursor + (a_dy > 0 ? 1 : -1), 0, static_cast<int>(picks.size()) - 1);
				cursorMoved = true;
				Sound("UIMenuFocus");
			}
			list.cursor = picks.empty() ? 0 : std::clamp(list.cursor, 0, static_cast<int>(picks.size()) - 1);
			if (cursorMoved) {
				const float y = top[picks[static_cast<std::size_t>(list.cursor)]];
				if (y < list.scrollTo) {
					list.scrollTo = y;
				} else if (y + rowH > list.scrollTo + listH) {
					list.scrollTo = y + rowH - listH;
				}
			}

			const V    min{ a_origin.x - (1.0f - a) * 24.0f * a_s, a_origin.y };
			const V    max{ min.x + a_width, min.y + pad * 2.0f + headerH + tabsH + listH + footH };
			const V    mouse = Im::GetIO()->MousePos;
			const bool press = Im::IsMouseClicked(0) && !state.closing;
			Frame(a_draw, min, max, a);

			// The header: which hand, whose.
			Band(a_draw, { min.x + 1.0f, min.y + 1.0f }, { max.x - 1.0f, min.y + pad + headerH - 8.0f * a_s }, a);
			const V p{ min.x + pad, min.y + pad };
			Label(a_draw, { p.x, p.y + 2.0f * a_s }, Alpha(kText, a), 15.0f * a_s, HandLabel(hand), 2.2f * a_s);
			const auto whose = Upper(a_member.name);
			Text(a_draw, { p.x + inner - TextSize(13.0f * a_s, whose).x - 30.0f * a_s, p.y + 3.0f * a_s }, Alpha(kTextDim, a), 13.0f * a_s, whose);
			{
				const V    xc{ p.x + inner - 8.0f * a_s, p.y + 11.0f * a_s };
				const bool overX = Inside(mouse, { xc.x - 12.0f * a_s, xc.y - 12.0f * a_s }, { xc.x + 12.0f * a_s, xc.y + 12.0f * a_s });
				Glyph(a_draw, xc, 13.0f * a_s, 0xF00D, Alpha(overX ? kText : kTextDim, a));
				if (overX && press) {
					a_back = true;
				}
			}

			// Schools: a glyph each on a rail, a lit diamond sliding under the one shown, its name beneath.
			{
				const float ty = p.y + headerH;
				const int   n = static_cast<int>(tabs.size());
				const float segW = inner / static_cast<float>(n);
				const float cy = ty + 18.0f * a_s;
				const float dr = 15.0f * a_s;
				Draw::AddLine(a_draw, { p.x - 4.0f * a_s, cy }, { p.x + inner + 4.0f * a_s, cy }, Alpha(kGlassEdge, a * 0.8f), 1.0f);
				const float slide = state.tw.Toward("schoolPill", 0, static_cast<float>(tabAt), 14.0f);
				int         shown = tabAt;
				for (int i = 0; i < n; ++i) {
					const V c{ p.x + segW * (static_cast<float>(i) + 0.5f), cy };
					Diamond(a_draw, c, dr, Alpha(IM_COL32(19, 19, 20, 245), a), Alpha(kGlassEdge, a * 1.3f), 1.0f);
				}
				const V pc{ p.x + segW * (slide + 0.5f), cy };
				Halo(a_draw, pc, dr * 1.6f, kAccent, a * 0.7f);
				Diamond(a_draw, pc, dr + 1.0f * a_s, Alpha(kAccent, a * 0.85f), Alpha(kEdge, a), 1.4f * a_s);
				for (int i = 0; i < n; ++i) {
					const int  school = tabs[static_cast<std::size_t>(i)];
					const V    c{ p.x + segW * (static_cast<float>(i) + 0.5f), cy };
					const bool over = Inside(mouse, { c.x - segW * 0.5f, ty }, { c.x + segW * 0.5f, ty + 36.0f * a_s });
					if (over) {
						shown = i;
						Hovering(0x780 + static_cast<std::uint64_t>(i));
						if (press) {
							setTab(school);
						}
					}
					const float on = i == tabAt ? 1.0f : (over ? 0.6f : 0.0f);
					const U32   tone = school < 0 ? kText : SchoolColour(static_cast<Rules::School>(school));
					Glyph(a_draw, c, 13.0f * a_s, school < 0 ? 0xF0CA : SchoolGlyph(static_cast<Rules::School>(school)), Alpha(Mix(kTextDim, tone, on), a));
				}
				const int   school = tabs[static_cast<std::size_t>(shown)];
				const int   count = school < 0 ? static_cast<int>(picks.size()) - 1 : perSchool[static_cast<std::size_t>(school)];
				const auto  name = school < 0 ? "Every school"s : std::string(Rules::schoolNames[static_cast<std::size_t>(school)]);
				const auto  line = std::format("{}  ·  {} {}", name, count, count == 1 ? "spell" : "spells");
				TextCentered(a_draw, { p.x + inner * 0.5f, ty + 50.0f * a_s }, Alpha(shown == tabAt ? kText : kTextDim, a), 14.5f * a_s, line);
			}

			// The rows, scrolled within their own window.
			const V lmin{ p.x - 6.0f * a_s, p.y + headerH + tabsH }, lmax{ p.x + inner + 6.0f * a_s, lmin.y + listH };
			const float most = std::max(content - listH, 0.0f);
			if (Inside(mouse, lmin, lmax) && Im::GetIO()->MouseWheel != 0.0f) {
				list.scrollTo -= Im::GetIO()->MouseWheel * rowH * 2.0f;
			}
			list.scrollTo = std::clamp(list.scrollTo, 0.0f, most);
			list.scroll = Follow(list.scroll, list.scrollTo, state.tw.dt, 18.0f);
			Draw::PushClipRect(a_draw, lmin, lmax, true);
			const RE::FormID none = 0;
			for (std::size_t i = 0; i < rows.size(); ++i) {
				const auto& row = rows[i];
				const float y = lmin.y + top[i] - list.scroll;
				const float h = row.kind == kKnowHeading || row.kind == kTeachHeading ? headH : rowH;
				if (y + h < lmin.y || y > lmax.y) {
					continue;
				}
				if (row.kind == kKnowHeading || row.kind == kTeachHeading) {
					const std::string_view words = row.kind == kKnowHeading ? "THEY KNOW"sv : "YOU CAN TEACH THEM"sv;
					Label(a_draw, { p.x, y + 11.0f * a_s }, Alpha(kTextDim, a), 11.5f * a_s, words, 1.8f * a_s);
					const float lw = LabelWidth(11.5f * a_s, words, 1.8f * a_s);
					Draw::AddLine(a_draw, { p.x + lw + 10.0f * a_s, y + 18.0f * a_s }, { p.x + inner, y + 18.0f * a_s }, Alpha(kGlassEdge, a * 0.8f), 1.0f);
					continue;
				}
				const auto* spell = row.spell;
				const auto  id = spell ? spell->id : none;
				const int   pick = static_cast<int>(std::find(picks.begin(), picks.end(), i) - picks.begin());
				const V     rmin{ p.x, y + 2.0f * a_s }, rmax{ p.x + inner, y + h - 2.0f * a_s };
				const bool  pointed = Inside(mouse, rmin, rmax) && Inside(mouse, lmin, lmax);
				const bool  focused = state.keyboardNav && pick == list.cursor;
				const bool  held = id == current;
				const float hover = state.tw.Toward("spellRow", (static_cast<std::uint64_t>(id) << 1) | (spell ? 1 : 0), pointed || focused ? 1.0f : 0.0f, 18.0f);
				if (pointed) {
					Hovering(0x800 + i);
				}
				if (held) {
					Highlight(a_draw, rmin, rmax, a);
				} else {
					Draw::AddRectFilled(a_draw, rmin, rmax, Alpha(IM_COL32(255, 255, 255, 14), a * hover), 0.0f, 0);
				}
				const U32 tone = spell ? SchoolColour(spell->school) : kTextFaint;
				const V   dc{ rmin.x + 18.0f * a_s, (rmin.y + rmax.y) * 0.5f };
				Diamond(a_draw, dc, 12.0f * a_s, Alpha(IM_COL32(19, 19, 20, 245), a), Alpha(Mix(kGlassEdge, tone, spell ? 0.9f : 0.0f), a), 1.0f);
				Glyph(a_draw, dc, 11.0f * a_s, spell ? SchoolGlyph(spell->school) : 0xF074, Alpha(tone, a));
				const std::string name = spell ? spell->name : std::string("Their own choice");
				Text(a_draw, { rmin.x + 40.0f * a_s, (rmin.y + rmax.y) * 0.5f - TextSize(16.0f * a_s, name).y * 0.5f }, Alpha(held || pointed || focused ? kText : kTextDim, a),
					16.0f * a_s, name);
				float right = rmax.x - 8.0f * a_s;
				if (spell) {
					const auto tier = std::string(Rules::tierNames[static_cast<std::size_t>(spell->tier)]);
					right -= TextSize(13.0f * a_s, tier).x;
					Text(a_draw, { right, (rmin.y + rmax.y) * 0.5f - TextSize(13.0f * a_s, tier).y * 0.5f }, Alpha(kTextFaint, a), 13.0f * a_s, tier);
					if (!spell->known || spell->twoHanded) {
						const std::string_view tag = !spell->known ? "TEACH"sv : "BOTH HANDS"sv;
						const float            tw = LabelWidth(10.0f * a_s, tag, 1.2f * a_s) + 12.0f * a_s;
						const V                tmin{ right - tw - 10.0f * a_s, (rmin.y + rmax.y) * 0.5f - 9.0f * a_s };
						Draw::AddRect(a_draw, tmin, { tmin.x + tw, tmin.y + 18.0f * a_s }, Alpha(!spell->known ? kEdge : kGlassEdge, a), 0.0f, 0, 1.0f);
						Label(a_draw, { tmin.x + 6.0f * a_s, tmin.y + 3.0f * a_s }, Alpha(!spell->known ? kText : kTextDim, a), 10.0f * a_s, tag, 1.2f * a_s);
					}
				}
				if (focused) {
					FocusOutline(a_draw, rmin, rmax, 0.0f, a);
				}
				if (pointed || focused) {
					std::string tip;
					if (!spell) {
						tip = "Their own choice\nThey use whatever they think best in this hand.";
					} else {
						tip = std::format("{}\n{} {}  ·  {:.0f} magicka{}{}", spell->name, Rules::tierNames[static_cast<std::size_t>(spell->tier)],
							Rules::schoolNames[static_cast<std::size_t>(spell->school)], spell->cost, spell->twoHanded ? "  ·  takes both hands" : "",
							spell->known ? "" : "\nOne of yours: choosing it teaches them, and they keep it.");
					}
					if (pointed) {
						Tip(std::move(tip));
					} else {
						TipBeside(std::move(tip), rmin, rmax);
					}
				}
				if ((pointed && press) || (focused && a_input.activate)) {
					if (id != current) {
						ChooseSpell(a_member.id, hand, id);
						Sound(spell && !spell->known ? "UIItemMeleeUp" : "UIMenuOK");
					} else {
						Sound("UIMenuCancel");
					}
					CloseSpellList();
				}
			}
			if (rows.size() == 1) {
				TextCentered(a_draw, { p.x + inner * 0.5f, lmin.y + rowH * 1.8f }, Alpha(kTextFaint, a), 14.0f * a_s, "Nothing else for this hand.");
			}
			Draw::PopClipRect(a_draw);
			// A thin bar when there is more than fits.
			if (most > 0.0f) {
				const float barH = listH * listH / content;
				const float barY = lmin.y + (listH - barH) * (list.scroll / most);
				Draw::AddRectFilled(a_draw, { lmax.x + 2.0f * a_s, lmin.y }, { lmax.x + 4.0f * a_s, lmax.y }, Alpha(kGlassEdge, a), 0.0f, 0);
				Draw::AddRectFilled(a_draw, { lmax.x + 2.0f * a_s, barY }, { lmax.x + 4.0f * a_s, barY + barH }, Alpha(kEdge, a), 0.0f, 0);
			}

			// The rule, and how to leave.
			Paragraph(a_draw, { p.x, lmax.y + 12.0f * a_s }, Alpha(kTextFaint, a), 12.5f * a_s, rule, inner);

			const bool outside = press && !Inside(mouse, min, max) && !Inside(mouse, a_sheet.min, a_sheet.max) && state.clock - list.openedAt > 0.05f;
			if ((a_back || outside) && list.member) {
				CloseSpellList();
				Sound("UIMenuCancel");
			}
			return { { min.x - 16.0f * a_s, min.y - 16.0f * a_s }, { max.x + 16.0f * a_s, max.y + 16.0f * a_s } };
		}

				// --- Party orders --------------------------------------------------------------------------

		// The four orders for everyone on one dark band, each a large silhouette over its name
		// in capitals. Each replaces every companion's own orders; the one still holding is
		// lit from below and underlined in red.
		Rect DrawOrders(Im::ImDrawList* a_draw, const View::Snapshot& a_view, V a_centre, float a_s, float a_appear, const Input& a_input)
		{
			const bool  live = !state.closing;
			const bool  rowFocused = state.keyboardNav && live && !state.spells.member && state.section == kOrdersSection;
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
				const auto  key = settings.orderKeys[i].key ? std::format("  ({})", HotkeyName(settings.orderKeys[i])) : ""s;
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

		// --- A companion's path -----------------------------------------------------------------

		// Magnus: a ball of light that breathes, its corona turning and motes circling it. Until it
		// is learned, a dark sphere with only a faint stir of light inside.
		void DrawOrb(Im::ImDrawList* a_draw, V c, float r, bool a_lit, float a_s, float a_alpha)
		{
			const float t = state.clock;
			const float pulse = 0.5f + 0.5f * std::sin(t * 2.1f);
			if (!a_lit) {
				Draw::AddCircleFilled(a_draw, c, r, Alpha(palette.orbShadow, a_alpha), 48);
				for (int i = 1; i <= 4; ++i) {
					const float k = static_cast<float>(i) / 4.0f;
					Draw::AddCircleFilled(a_draw, { c.x - r * 0.12f * k, c.y - r * 0.12f * k }, r * (1.0f - 0.22f * static_cast<float>(i)),
						Alpha(palette.orbDormant, a_alpha * 0.18f), 40);
				}
				Draw::PathClear(a_draw);
				Draw::PathArcTo(a_draw, c, r * 0.62f, t * 0.4f, t * 0.4f + 2.2f, 24);
				Draw::PathStroke(a_draw, Alpha(palette.orbRim, a_alpha * 0.18f), 0, 1.4f * a_s);
				Draw::AddCircle(a_draw, c, r, Alpha(kGlassEdge, a_alpha * 1.2f), 48, 1.4f * a_s);
				return;
			}
			// The corona, breathing.
			Halo(a_draw, c, r * (2.9f + 0.25f * pulse), kAccent, a_alpha);
			Halo(a_draw, c, r * (2.0f + 0.12f * pulse), palette.orbHalo, a_alpha * 0.75f);
			// The body: deep ember at the edge to white gold at the heart.
			for (int i = 0; i <= 12; ++i) {
				const float k = static_cast<float>(i) / 12.0f;
				const U32   tone = Mix(palette.orbEdge, palette.orbHeart, std::pow(k, 1.6f));
				Draw::AddCircleFilled(a_draw, c, r * (1.0f - 0.86f * k) * (1.0f + 0.03f * pulse), Alpha(tone, a_alpha * (0.35f + 0.65f * k)), 40);
			}
			// Bands of light turning round it at different speeds.
			struct Band
			{
				float radius, speed, length, width;
			};
			for (const auto& band : { Band{ 1.12f, 1.3f, 2.4f, 1.8f }, Band{ 1.26f, -0.8f, 1.6f, 1.3f }, Band{ 1.40f, 0.5f, 3.4f, 1.0f } }) {
				for (int k = 0; k < 2; ++k) {
					const float a = t * band.speed + 3.1415927f * static_cast<float>(k);
					Draw::PathClear(a_draw);
					Draw::PathArcTo(a_draw, c, r * band.radius, a, a + band.length, 28);
					Draw::PathStroke(a_draw, Alpha(palette.orbBand, a_alpha * (0.45f + 0.3f * pulse)), 0, band.width * a_s);
				}
			}
			// Motes circling on tilted paths.
			for (int i = 0; i < 6; ++i) {
				const float a = t * (0.9f + 0.15f * static_cast<float>(i)) + static_cast<float>(i) * 1.0472f;
				const float tilt = 0.45f + 0.1f * static_cast<float>(i % 3);
				const float spin = static_cast<float>(i) * 0.9f;
				const float ex = std::cos(a) * r * 1.55f, ey = std::sin(a) * r * 1.55f * tilt;
				const V     p{ c.x + ex * std::cos(spin) - ey * std::sin(spin), c.y + ex * std::sin(spin) + ey * std::cos(spin) };
				Halo(a_draw, p, 7.0f * a_s, palette.orbMote, a_alpha * 0.8f);
				Draw::AddCircleFilled(a_draw, p, 1.8f * a_s, Alpha(palette.orbSpark, a_alpha), 8);
			}
			// A highlight on its upper side.
			Draw::AddCircleFilled(a_draw, { c.x - r * 0.28f, c.y - r * 0.3f }, r * 0.22f, Alpha(IM_COL32(255, 255, 255, 255), a_alpha * 0.35f), 20);
		}

		// One node: an ability is a diamond, a branch a ring, a trait a hexagon, the capstone an orb.
		// Learned ones hold an oxblood well; those that can be bought now breathe, the pip their next
		// point would fill lit and pulsing; locked ones keep their mark, dim, with a small lock.
		struct NodeLook
		{
			Paths::Kind   kind{ Paths::Kind::kAbility };
			std::uint32_t glyph{ 0 };
			int           rank{ 0 };
			int           most{ 3 };
			bool          locked{ false };   // its tier has not opened
			bool          buyable{ false };  // a point would go in now
			bool          selected{ false };
			bool          capstone{ false };
			float         hover{ 0.0f };
			float         appear{ 1.0f };
			float         rankedAge{ 99.0f };  // seconds since it last gained a rank
		};

		void DrawNode(Im::ImDrawList* a_draw, V c, float r, const NodeLook& a_node, float a_s, float a_alpha)
		{
			const float pop = EaseOutBack(a_node.appear);
			const float alpha = a_alpha * Clamp01(a_node.appear * 1.4f);
			const float rr = r * (0.6f + 0.4f * pop) * (1.0f + 0.07f * a_node.hover + (a_node.selected ? 0.04f : 0.0f));
			const bool  learned = a_node.rank > 0;
			const bool  maxed = a_node.rank >= a_node.most;
			const float breathe = 0.5f + 0.5f * std::sin(state.clock * 3.2f + c.x * 0.01f);

			if (a_node.capstone) {
				if (!learned && a_node.buyable) {
					Halo(a_draw, c, rr * 2.2f, kAccent, alpha * (0.3f + 0.3f * breathe));
				}
				DrawOrb(a_draw, c, rr, learned, a_s, alpha * (a_node.locked ? 0.75f : 1.0f));
				if (a_node.selected || a_node.hover > 0.01f) {
					Draw::AddCircle(a_draw, c, rr + 7.0f * a_s, Alpha(kText, alpha * std::max(a_node.hover, a_node.selected ? 0.8f : 0.0f)), 48, 1.2f * a_s);
				}
			} else {
				// What has been learned glows; what can be bought breathes a fainter glow.
				if (learned) {
					Halo(a_draw, c, rr * 2.0f, kAccent, alpha * (maxed ? 0.75f : 0.5f));
				} else if (a_node.buyable) {
					Halo(a_draw, c, rr * 1.9f, kAccent, alpha * (0.2f + 0.25f * breathe));
				}
				const U32 plate = Alpha(a_node.locked ? IM_COL32(14, 14, 15, 220) : IM_COL32(19, 19, 20, 245), alpha);
				const U32 well = Alpha(kAccent, alpha * (maxed ? 0.85f : 0.62f));
				const U32 edge = Alpha(a_node.selected ? kText : (learned ? kEdge : (a_node.buyable ? Mix(kTextDim, kText, breathe) : kGlassEdge)),
					alpha * (a_node.locked ? 0.7f : 1.0f));
				const float t = (a_node.selected ? 2.0f : 1.4f) * a_s;
				switch (a_node.kind) {
				case Paths::Kind::kAbility:
					Diamond(a_draw, c, rr, plate, edge, t);
					if (learned) {
						Diamond(a_draw, c, rr * 0.84f, well, Alpha(kShade, 0.0f), 1.0f);
					}
					Diamond(a_draw, c, rr + 6.0f * a_s, Alpha(kShade, 0.0f), Alpha(kGlassEdge, alpha * (a_node.locked ? 0.5f : 1.0f)), 1.0f);
					for (const V d : { V{ 0, -1 }, V{ 1, 0 }, V{ 0, 1 }, V{ -1, 0 } }) {
						Stud(a_draw, { c.x + d.x * (rr + 6.0f * a_s), c.y + d.y * (rr + 6.0f * a_s) }, 2.6f * a_s, alpha * (learned ? 1.0f : 0.6f));
					}
					break;
				case Paths::Kind::kBranch:
					Draw::AddCircleFilled(a_draw, c, rr, plate, 36);
					if (learned) {
						Draw::AddCircleFilled(a_draw, c, rr * 0.84f, well, 36);
					}
					Draw::AddCircle(a_draw, c, rr, edge, 36, t);
					Draw::AddCircle(a_draw, c, rr + 4.0f * a_s, Alpha(kGlassEdge, alpha * (a_node.locked ? 0.4f : 0.8f)), 36, 1.0f);
					if (palette.Engraved()) {
						for (int k = 0; k < 8; ++k) {
							const float angle = static_cast<float>(k) * 0.7853982f;
							const V d{ std::cos(angle), std::sin(angle) };
							Draw::AddLine(a_draw, { c.x + d.x * (rr + a_s), c.y + d.y * (rr + a_s) },
								{ c.x + d.x * (rr + 3.0f * a_s), c.y + d.y * (rr + 3.0f * a_s) }, Alpha(edge, 0.6f), 1.0f);
						}
					}
					break;
				case Paths::Kind::kTrait:
					Hexagon(a_draw, c, rr, plate, edge, t);
					if (learned) {
						Hexagon(a_draw, c, rr * 0.82f, well, Alpha(kShade, 0.0f), 1.0f);
					}
					Hexagon(a_draw, c, rr + 5.0f * a_s, Alpha(kShade, 0.0f), Alpha(kGlassEdge, alpha * (a_node.locked ? 0.4f : 0.8f)), 1.0f);
					break;
				}
				const float glyphSize = (a_node.kind == Paths::Kind::kAbility ? 0.62f : 0.66f) * rr;
				Glyph(a_draw, c, glyphSize, a_node.glyph, Alpha(learned ? kText : (a_node.locked ? kTextFaint : kTextDim), alpha * (a_node.locked ? 0.6f : 1.0f)));
				// The cursor: two arcs turning round the node.
				if (a_node.selected) {
					const float a = state.clock * 1.6f;
					const float ring = rr + (a_node.kind == Paths::Kind::kAbility ? 13.0f : 10.0f) * a_s;
					for (int k = 0; k < 2; ++k) {
						Draw::PathClear(a_draw);
						Draw::PathArcTo(a_draw, c, ring, a + 3.1415927f * static_cast<float>(k), a + 3.1415927f * static_cast<float>(k) + 1.9f, 20);
						Draw::PathStroke(a_draw, Alpha(kText, alpha * (0.7f + 0.3f * std::sin(state.clock * 5.0f))), 0, 1.6f * a_s);
					}
				}
			}
			// Locked: a small lock tucked under its lower right.
			if (a_node.locked) {
				const V lc{ c.x + rr * 0.72f, c.y + rr * 0.72f };
				Draw::AddCircleFilled(a_draw, lc, 8.0f * a_s, Alpha(IM_COL32(16, 16, 18, 250), alpha), 16);
				Draw::AddCircle(a_draw, lc, 8.0f * a_s, Alpha(kGlassEdge, alpha * 1.4f), 16, 1.0f);
				Glyph(a_draw, lc, 8.5f * a_s, 0xF023, Alpha(kTextDim, alpha));
			}
			// A new rank: a ring thrown out from the node, and a flash of white.
			if (a_node.rankedAge < 0.7f) {
				const float k = a_node.rankedAge / 0.7f;
				Draw::AddCircle(a_draw, c, rr * (1.0f + 1.5f * EaseOut(k)), Alpha(kText, alpha * (1.0f - k)), 40, (3.0f - 2.0f * k) * a_s);
				Draw::AddCircle(a_draw, c, rr * (1.0f + 0.8f * EaseOut(k)), Alpha(kEdge, alpha * (1.0f - k) * 0.8f), 40, 2.0f * a_s);
				Halo(a_draw, c, rr * 2.4f, (palette.Subdued() ? palette.orbSpark : IM_COL32(255, 230, 210, 255)), alpha * (1.0f - k));
			}
			// A pip per rank, filled red as they are bought; the newest pops, and the one a point would
			// fill next glows while it can be bought.
			const float pip = 4.6f * a_s, gap = 13.0f * a_s;
			const float y = c.y + (a_node.capstone ? rr * 1.5f : rr) + (a_node.kind == Paths::Kind::kAbility ? 15.0f : 12.0f) * a_s;
			const float x0 = c.x - gap * static_cast<float>(a_node.most - 1) * 0.5f;
			for (int k = 0; k < a_node.most; ++k) {
				const bool  on = k < a_node.rank;
				const bool  next = !on && k == a_node.rank && a_node.buyable;
				const bool  newest = on && k == a_node.rank - 1 && a_node.rankedAge < 0.5f;
				const float size = pip * (newest ? 1.0f + 0.8f * (1.0f - a_node.rankedAge / 0.5f) : 1.0f);
				const V     pc{ x0 + gap * static_cast<float>(k), y };
				if (next) {
					Halo(a_draw, pc, 11.0f * a_s, kEdge, alpha * (0.35f + 0.4f * breathe));
				}
				Diamond(a_draw, pc, size, Alpha(on ? kEdge : IM_COL32(26, 26, 28, 255), alpha),
					Alpha(on ? kText : (next ? Mix(kEdge, kText, breathe) : kGlassEdge), alpha * (on ? 0.6f : 1.0f)), next ? 1.4f * a_s : 1.0f);
			}
		}

		// Prose with its numbers picked out: a word holding a digit, and the "s" after one, is drawn
		// in the strong colour and a touch heavier. Returns the height; with no draw list it only measures.
		float RichText(Im::ImDrawList* a_draw, V a_pos, float a_size, std::string_view a_text, float a_width, U32 a_base, U32 a_strong)
		{
			const float space = TextSize(a_size, " ").x;
			const float line = a_size * 1.34f;
			float       x = 0.0f, y = 0.0f;
			bool        strong = false;
			for (std::size_t at = 0; at < a_text.size();) {
				const auto end = std::min(a_text.find(' ', at), a_text.size());
				const auto word = a_text.substr(at, end - at);
				at = end + 1;
				if (word.empty()) {
					continue;
				}
				auto bare = word;
				while (!bare.empty() && (bare.back() == ',' || bare.back() == '.' || bare.back() == ';' || bare.back() == ':')) {
					bare.remove_suffix(1);
				}
				const bool digits = std::ranges::any_of(word, [](char c) { return c >= '0' && c <= '9'; });
				strong = digits || (strong && bare == "s");
				const float w = TextSize(a_size, word).x;
				if (x > 0.0f && x + w > a_width) {
					x = 0.0f;
					y += line;
				}
				if (a_draw) {
					const V p{ a_pos.x + x, a_pos.y + y };
					if (strong) {
						Text(a_draw, { p.x + 0.6f * uiScale, p.y }, a_strong, a_size, word);
					}
					Text(a_draw, p, strong ? a_strong : a_base, a_size, word);
				}
				x += w + space;
			}
			return y + line;
		}

		// A quiet button for what is rarely pressed: a hairline, an icon and small capitals. It fills
		// on hover, burns red while what it switches is on, and turns amber while it waits for a
		// second press.
		bool GhostButton(Im::ImDrawList* a_draw, const char* a_id, V a_min, V a_size, std::uint32_t a_glyph, std::string_view a_label, bool a_lit, bool a_warn,
			float a_s, float a_alpha, std::uint64_t a_hoverKey)
		{
			bool        hovered = false;
			const bool  clicked = Hit(a_id, a_min, a_size, hovered) && !state.closing;
			const V     max = Add(a_min, a_size);
			const float hover = state.tw.Toward("ghostButton", a_hoverKey, hovered ? 1.0f : 0.0f, 16.0f);
			if (hovered) {
				Hovering(a_hoverKey);
			}
			const U32 tone = a_warn ? kAmber : (a_lit ? kEdge : kTextDim);
			if (a_lit) {
				Draw::AddRectFilledMultiColor(a_draw, a_min, max, Alpha(kAccent, a_alpha * (0.55f + 0.2f * hover)), Alpha(kAccent, a_alpha * 0.12f),
					Alpha(kAccent, a_alpha * 0.12f), Alpha(kAccent, a_alpha * (0.55f + 0.2f * hover)));
			} else if (hover > 0.01f) {
				Draw::AddRectFilled(a_draw, a_min, max, Alpha(kTileHover, a_alpha * hover), 0.0f, 0);
			}
			Draw::AddRect(a_draw, a_min, max, Alpha(a_lit || a_warn ? tone : kGlassEdge, a_alpha * (0.6f + 0.4f * hover)), 0.0f, 0, 1.0f);
			const U32   ink = Alpha(a_warn ? kAmber : Mix(a_lit ? kText : kTextDim, kText, hover), a_alpha);
			const float cy = a_min.y + a_size.y * 0.5f;
			if (a_label.empty()) {
				Glyph(a_draw, { a_min.x + a_size.x * 0.5f, cy }, 14.0f * a_s, a_glyph, ink);
				return clicked;
			}
			const float size = 11.5f * a_s, track = 1.6f * a_s, gw = 20.0f * a_s;
			const float x = a_min.x + (a_size.x - LabelWidth(size, a_label, track) - gw) * 0.5f;
			Glyph(a_draw, { x + 6.0f * a_s, cy }, 11.0f * a_s, a_glyph, ink);
			Label(a_draw, { x + gw, cy - size * 0.58f }, ink, size, a_label, track);
			return clicked;
		}

		// The one thing to press on the page: learn, or rank up. Oxblood and breathing, with its
		// price on the right as a diamond and a number, the way the header counts points; with no
		// price (below zero), a glyph there instead.
		bool ActionButton(Im::ImDrawList* a_draw, const char* a_id, V a_min, V a_size, std::string_view a_verb, int a_cost, float a_s, float a_alpha,
			std::uint64_t a_hoverKey, std::uint32_t a_glyph = 0)
		{
			bool        hovered = false;
			const bool  clicked = Hit(a_id, a_min, a_size, hovered) && !state.closing;
			const V     max = Add(a_min, a_size);
			const float hover = state.tw.Toward("actionButton", a_hoverKey, hovered ? 1.0f : 0.0f, 14.0f);
			const float breathe = 0.5f + 0.5f * std::sin(state.clock * 2.6f);
			if (hovered) {
				Hovering(a_hoverKey);
			}
			if (palette.Engraved()) {
				Glass(a_draw, a_min, max, a_alpha, 0.0f);
				Lit(a_draw, a_min, max, a_alpha * (0.75f + 0.25f * hover));
			} else {
				for (int i = 1; i <= 3; ++i) {
					const float g = static_cast<float>(i) * 3.0f * a_s;
					Draw::AddRect(a_draw, { a_min.x - g, a_min.y - g }, { max.x + g, max.y + g },
						Alpha(kAccent, a_alpha * (0.16f + 0.08f * breathe + 0.12f * hover) * static_cast<float>(4 - i) / 3.0f), 0.0f, 0, 3.0f * a_s);
				}
				const U32 left = Mix(kAccent, kEdge, 0.15f + 0.45f * hover);
				Draw::AddRectFilledMultiColor(a_draw, a_min, max, Alpha(left, a_alpha), Alpha(kAccent, a_alpha * 0.6f), Alpha(kAccent, a_alpha * 0.6f), Alpha(left, a_alpha));
			}
			Draw::AddRect(a_draw, a_min, max, Alpha(Mix(kEdge, kText, 0.25f + 0.5f * hover), a_alpha), 0.0f, 0, 1.2f * a_s);
			const float cy = a_min.y + a_size.y * 0.5f;
			const float size = 16.0f * a_s;
			Label(a_draw, { a_min.x + 22.0f * a_s, cy - size * 0.58f }, Alpha(kText, a_alpha), size, a_verb, 3.2f * a_s);
			if (a_cost < 0) {
				Glyph(a_draw, { max.x - 24.0f * a_s + 3.0f * a_s * hover, cy }, 14.0f * a_s, a_glyph, Alpha(kText, a_alpha));
				return clicked;
			}
			const auto  price = std::to_string(a_cost);
			const float ps = 18.0f * a_s;
			const auto  pd = TextSize(ps, price);
			const float px = max.x - 20.0f * a_s - pd.x;
			Text(a_draw, { px, cy - pd.y * 0.5f }, Alpha(kText, a_alpha), ps, price);
			Diamond(a_draw, { px - 13.0f * a_s, cy }, 6.0f * a_s, Alpha(kText, a_alpha * 0.85f), Alpha(kText, a_alpha), 1.0f);
			return clicked;
		}

		// A wide button on the path page: red and slashed when it is the thing to do, an outline
		// otherwise, dim when it cannot be pressed.
		bool PageButton(Im::ImDrawList* a_draw, const char* a_id, V a_min, V a_size, std::string_view a_label, bool a_enabled, bool a_primary, bool a_lit,
			float a_s, float a_alpha, std::uint64_t a_hoverKey)
		{
			bool       hovered = false;
			const bool clicked = Hit(a_id, a_min, a_size, hovered) && a_enabled && !state.closing;
			const V    max = Add(a_min, a_size);
			const float hover = state.tw.Toward("pageButton", a_hoverKey, hovered && a_enabled ? 1.0f : 0.0f, 16.0f);
			if (hovered) {
				Hovering(a_hoverKey);
			}
			if (a_primary && a_enabled) {
				Highlight(a_draw, a_min, max, a_alpha * (0.75f + 0.25f * hover));
			} else {
				Draw::AddRectFilled(a_draw, a_min, max, Alpha(Mix(kTile, kTileHover, hover), a_alpha), 0.0f, 0);
				if (a_lit) {
					Lit(a_draw, a_min, max, a_alpha);
				}
			}
			Draw::AddRect(a_draw, a_min, max, Alpha(a_enabled ? Mix(kGlassEdge, kEdge, std::max(hover, a_primary ? 0.6f : 0.0f)) : kGlassEdge, a_alpha * (a_enabled ? 1.0f : 0.6f)),
				0.0f, 0, 1.0f);
			const float lw = LabelWidth(13.0f * a_s, a_label, 1.6f * a_s);
			Label(a_draw, { a_min.x + (a_size.x - lw) * 0.5f, a_min.y + a_size.y * 0.5f - 7.5f * a_s }, Alpha(a_enabled ? kText : kTextFaint, a_alpha), 13.0f * a_s,
				a_label, 1.6f * a_s);
			return clicked;
		}

		// The whole path page: the role's tree on the left, what the chosen node does on the
		// right, the two abilities carried beneath it. Opens over the party view (the sheet and
		// the companion card step aside), follows LB and RB to the next companion.
		Rect DrawPathPage(Im::ImDrawList* a_draw, const View::Member& a_member, const Rules::Tuning& a_tuning, V a_min, V a_max, float a_s, float a_appear,
			const Input& a_input, int a_dy, int a_tab)
		{
			auto&       page = state.page;
			const auto  path = page.path;
			const auto& tree = Paths::PathOf(path);
			const auto& record = a_member.record;
			const int   unspent = Unspent(a_member);
			const bool  live = !state.closing && !state.guide.open;
			const float since = state.clock - page.openedAt;
			const float open = EaseOut(since / 0.35f) * a_appear;
			const float alpha = open;
			const V     min{ a_min.x, a_min.y + (1.0f - open) * 22.0f * a_s };
			const V     max{ a_max.x, a_max.y + (1.0f - open) * 22.0f * a_s };

			// New ranks since the last frame, for the ring they throw out.
			if (page.seenMember != a_member.id || page.seenPath != path) {
				page.seenMember = a_member.id;
				page.seenPath = path;
				page.seen = record.ranks[static_cast<std::size_t>(path)];
				page.rankedAt.fill(-99.0f);
				page.lastUnspent = unspent;
			}
			for (std::size_t i = 0; i < Paths::kNodes; ++i) {
				const auto now = record.ranks[static_cast<std::size_t>(path)][i];
				if (now > page.seen[i]) {
					page.rankedAt[i] = state.clock;
					Sound("UIMenuOK");
				}
				page.seen[i] = now;
			}
			if (unspent != page.lastUnspent) {
				page.pointsAt = state.clock;
				page.lastUnspent = unspent;
			}

			// Switching paths: the triggers, Q and E, or a tab.
			int tabStep = a_tab;
			if (tabStep != 0) {
				page.path = static_cast<Role>((static_cast<int>(path) + tabStep + static_cast<int>(Role::kCount)) % static_cast<int>(Role::kCount));
				page.tabAt = state.clock;
				page.cursor = std::min(page.cursor, static_cast<int>(Paths::kNodes) - 1);
				page.resetArmed = false;
				Sound("UIMenuPrevNext");
			}

			Frame(a_draw, min, max, alpha);
			const float pad = 26.0f * a_s;

			// --- Header: the path's mark, its name, and the points ---
			const float bandH = 92.0f * a_s;
			const int   onPath = Paths::Spent(record, path);  // ranks and bonus points: what the bonus counts, and Start over gives back
			const int   onTree = Paths::OnTree(record, path);
			const int   invested = Paths::Invested(record, path);
			const bool  mastered = Paths::Mastered(record, path);
			const int   bonusRoom = Rules::PathBonusRoom(path, onPath, a_tuning);
			const int   mastery = std::max(Paths::Mastery(path), 1);
			Band(a_draw, { min.x + 1.0f, min.y + 1.0f }, { max.x - 1.0f, min.y + bandH }, alpha);
			{
				// The band's floor is the path's mastery: lit as far as the points spent on it reach.
				const float filled = state.tw.Toward("pathMastery", static_cast<std::uint64_t>(path), static_cast<float>(onTree) / static_cast<float>(mastery), 6.0f);
				const float x0 = min.x + 1.0f, x1 = max.x - 1.0f, yb = min.y + bandH;
				Draw::AddLine(a_draw, { x0, yb }, { x1, yb }, Alpha(kEdge, alpha * 0.35f), 1.0f);
				if (filled > 0.002f) {
					const float xf = x0 + (x1 - x0) * Clamp01(filled);
					Draw::AddRectFilled(a_draw, { x0, yb - 1.5f * a_s }, { xf, yb + 1.5f * a_s }, Alpha(kEdge, alpha), 0.0f, 0);
					Halo(a_draw, { xf, yb }, 10.0f * a_s, kEdge, alpha);
					Diamond(a_draw, { xf, yb }, 4.0f * a_s, Alpha(kText, alpha), Alpha(kEdge, alpha), 1.0f);
				}
			}
			{
				const V mc{ min.x + pad + 34.0f * a_s, min.y + bandH * 0.5f };
				Halo(a_draw, mc, 52.0f * a_s, kAccent, alpha * 0.8f);
				Diamond(a_draw, mc, 32.0f * a_s, Alpha((palette.Subdued() ? palette.inset : IM_COL32(24, 11, 11, 250)), alpha), Alpha(kEdge, alpha), 1.6f * a_s);
				Diamond(a_draw, mc, 26.0f * a_s, Alpha(kShade, 0.0f), Alpha(kText, alpha * 0.35f), 1.0f);
				Glyph(a_draw, mc, 22.0f * a_s, Info(path).glyph, Alpha(kText, alpha));
				const float tx = mc.x + 52.0f * a_s;
				Label(a_draw, { tx, min.y + 16.0f * a_s }, Alpha(kTextDim, alpha), 12.0f * a_s, Upper(std::format("{}  /  level {}", a_member.name, a_member.level)), 1.8f * a_s);
				const auto title = Upper(Paths::Info(path).title);
				Label(a_draw, { tx, min.y + 34.0f * a_s }, Alpha(kText, alpha), 24.0f * a_s, title, 4.0f * a_s);
				Text(a_draw, { tx, min.y + 64.0f * a_s }, Alpha(kTextDim, alpha), 14.5f * a_s, Paths::Info(path).lead);

				// The path bonus, beside the name: what every point spent here is worth to someone of
				// this role, counting up as ranks are bought. Faint until it applies to them.
				const float per = Rules::PathBonusPerPoint(path, a_tuning);
				const float bonus = Rules::PathBonus(path, onPath, a_tuning);
				const float shown = state.tw.Toward("pathBonus", static_cast<std::uint64_t>(path), bonus, 7.0f);
				const bool  whole = std::abs(per - std::round(per)) < 0.01f;
				const auto  number = Rules::PathBonusNumber(path, whole ? std::round(shown) : std::round(shown * 10.0f) / 10.0f);
				const auto  word = Upper(Rules::PathBonusName(path));
				const bool  theirs = path == a_member.orders.role;
				const bool  working = theirs && bonus > 0.0f;
				float       lastRank = -99.0f;
				for (const float at : page.rankedAt) {
					lastRank = std::max(lastRank, at);
				}
				const float flash = 1.0f - Clamp01((state.clock - lastRank) / 0.7f);
				const float bump = 1.0f + 0.16f * flash;
				const float numberSize = 19.0f * a_s, wordSize = 10.5f * a_s, wordTracking = 1.6f * a_s;
				const float numberW = TextSize(numberSize, number).x, wordW = LabelWidth(wordSize, word, wordTracking);
				// Points put straight into it, as a small tally at the end.
				const auto  tally = std::format("{}", invested);
				const float tallyW = invested > 0 ? 14.0f * a_s + TextSize(12.0f * a_s, tally).x + 10.0f * a_s : 0.0f;
				const float h = 30.0f * a_s;
				const V     bmin{ tx + LabelWidth(24.0f * a_s, title, 4.0f * a_s) + 22.0f * a_s, min.y + 46.0f * a_s - h * 0.5f };
				const V     bmax{ bmin.x + 12.0f * a_s + 16.0f * a_s + 9.0f * a_s + numberW + 9.0f * a_s + wordW + 14.0f * a_s + tallyW, bmin.y + h };
				const float dim = working ? 1.0f : 0.55f;
				bool        hovered = false;
				Hit("pathBonus", bmin, { bmax.x - bmin.x, h }, hovered);
				const float hover = state.tw.Toward("pathBonusHover", 0, hovered ? 1.0f : 0.0f, 16.0f);
				Halo(a_draw, { bmin.x + 28.0f * a_s + numberW * 0.5f, (bmin.y + bmax.y) * 0.5f }, 46.0f * a_s, kAccent, alpha * flash);
				Draw::AddRectFilled(a_draw, bmin, bmax, Alpha(Mix((palette.Subdued() ? palette.inset : IM_COL32(26, 12, 11, 235)), kAccent, 0.25f * hover + 0.35f * flash), alpha * (0.6f + 0.4f * dim)), 0.0f, 0);
				Draw::AddRect(a_draw, bmin, bmax, Alpha(Mix(kGlassEdge, kEdge, working ? 0.6f + 0.4f * hover : 0.25f * hover), alpha), 0.0f, 0, 1.0f);
				Draw::AddRectFilled(a_draw, bmin, { bmin.x + 2.0f * a_s, bmax.y }, Alpha(working ? kEdge : kTextFaint, alpha), 0.0f, 0);
				constexpr std::array<std::uint32_t, 6> glyphs{ 0xF0E7, 0xF3ED, 0xF21E, 0xF6E3, 0xF2EA, 0xF0D0 };  // bolt, shield, heartbeat, hammer, arrow back, wand
				const float cy = (bmin.y + bmax.y) * 0.5f;
				float       x = bmin.x + 12.0f * a_s;
				Glyph(a_draw, { x + 8.0f * a_s, cy }, 13.0f * a_s, glyphs[static_cast<std::size_t>(path) % glyphs.size()], Alpha(working ? kEdge : kTextDim, alpha));
				x += 16.0f * a_s + 9.0f * a_s;
				const float big = numberSize * bump;
				Text(a_draw, { x + (numberW - TextSize(big, number).x) * 0.5f, cy - TextSize(big, number).y * 0.5f }, Alpha(Mix(working ? kText : kTextDim, kEdge, flash * 0.5f), alpha), big,
					number);
				x += numberW + 9.0f * a_s;
				Label(a_draw, { x, cy - TextSize(wordSize, word).y * 0.5f }, Alpha(working ? kTextDim : kTextFaint, alpha), wordSize, word, wordTracking);
				if (invested > 0) {
					const float lx = bmax.x - tallyW;
					Draw::AddLine(a_draw, { lx, bmin.y + 6.0f * a_s }, { lx, bmax.y - 6.0f * a_s }, Alpha(kGlassEdge, alpha), 1.0f);
					Diamond(a_draw, { lx + 11.0f * a_s, cy }, 4.0f * a_s, Alpha(kAccent, alpha), Alpha(kEdge, alpha), 1.0f);
					Text(a_draw, { lx + 19.0f * a_s, cy - TextSize(12.0f * a_s, tally).y * 0.5f }, Alpha(working ? kText : kTextDim, alpha), 12.0f * a_s, tally);
				}
				// Points straight into the bonus and back out, beside the chip. Always there, so the way in
				// is seen; dim when shut, and saying why.
				{
					const auto investBlock = Paths::CanInvest(record, path, a_member.pointsEarned, bonusRoom);
					const bool canPut = investBlock == Paths::Block::kNone;
					const bool fighting = a_member.inCombat;
					const bool canTake = invested > 0 && a_member.respec && !fighting;
					const V    minus{ bmax.x + 6.0f * a_s, bmin.y };
					const V    plus{ minus.x + h + 4.0f * a_s, bmin.y };
					if (GhostButton(a_draw, "pathDivest", minus, { h, h }, 0xF068, "", false, false, a_s, alpha * (canTake ? 1.0f : 0.4f), 0x7260) && live && canTake) {
						PathCommand(Battle::Command::Type::kDivest, a_member.id, path);
						Sound("UIMenuCancel");
					}
					if (Im::IsItemHovered()) {
						if (canTake) {
							Tip("Take a point back out of the bonus."s);
						} else if (invested == 0) {
							Readout("No points in the bonus to take back."s);
						} else {
							Readout(a_member.respec ? "Points come back out of the bonus once the fight is over."s : std::string(Paths::Why(Paths::Block::kNoRespec)));
						}
					}
					if (GhostButton(a_draw, "pathInvestChip", plus, { h, h }, 0xF067, "", canPut, false, a_s, alpha * (canPut ? 1.0f : 0.4f), 0x7262) && live && canPut) {
						PathCommand(Battle::Command::Type::kInvest, a_member.id, path);
						Sound("UIMenuOK");
					}
					if (Im::IsItemHovered()) {
						if (canPut) {
							Tip(std::format("Put a leftover point straight into the bonus: {} now, {} with it.", Rules::PathBonusText(path, bonus),
								Rules::PathBonusText(path, Rules::PathBonus(path, onPath + 1, a_tuning))));
						} else if (investBlock == Paths::Block::kNotMastered) {
							int left = 0;
							for (std::size_t i = 0; i < Paths::kNodes; ++i) {
								left += tree[i].Empty() ? 0 : std::max(tree[i].most - record.ranks[static_cast<std::size_t>(path)][i], 0);
							}
							Readout(std::format("Points go straight into the bonus once every node on this path is mastered: {} more rank{} to go.", left, left == 1 ? "" : "s"));
						} else if (investBlock == Paths::Block::kBonusFull) {
							Readout(std::format("The bonus is at its highest: {}.", Rules::PathBonusText(path, bonus)));
						} else if (investBlock == Paths::Block::kNoPoints) {
							Readout("No points left. One more comes with every level."s);
						} else {
							Readout(std::string(Paths::Why(investBlock)));
						}
					}
				}
				if (hovered) {
					Hovering(0x7250);
					const auto rate = Rules::Percent(per);
					std::string what;
					switch (path) {
					case Role::kFighter:
						what = std::format("{} faster with their weapons in a fight", rate);
						break;
					case Role::kTank:
						what = std::format("{} less damage from every blow, arrow and harmful spell ({} at most)", rate, Rules::Percent(Rules::kMostDamageCut));
						break;
					case Role::kHealer:
						what = std::format("{} more to all the healing they do: their abilities, the spells they cast and the potions they drink", rate);
						break;
					case Role::kBruiser:
						what = std::format("{} more damage with their weapons in a fight", rate);
						break;
					case Role::kArcher:
						what = std::format("{} more chance that an arrow they shoot is not used up ({} at most)", rate, Rules::Percent(Rules::kMostArrowsKept));
						break;
					case Role::kMage:
						what = std::format("{} less magicka for every spell they cast ({} off at most)", rate, Rules::Percent(Rules::kMostCostCut));
						break;
					default:
						break;
					}
					auto text = std::format("Path bonus. Every point spent on the {}: {}.\n{} spent here: {}.", Paths::Info(path).title, what, onPath,
						Rules::PathBonusText(path, bonus));
					if (invested > 0) {
						text += std::format("\n{} of them put straight into the bonus.", invested);
					}
					if (bonusRoom <= 0) {
						text += "\nAt its highest: more points change nothing.";
					} else if (mastered) {
						text += "\nEvery node is mastered: leftover points can go straight into it.";
					} else {
						text += "\nOnce every node here is mastered, leftover points can go straight into it.";
					}
					if (!theirs) {
						text += std::format("\nOnly while they are a {}; they are a {} now.", Info(path).name, Info(a_member.orders.role).name);
					}
					Readout(std::move(text));
				}
			}
			{
				// The points: a large medallion that breathes while any are left, and bounces when
				// the count changes.
				const V     pc{ max.x - pad - 40.0f * a_s, min.y + bandH * 0.5f };
				const float bump = 1.0f + 0.18f * (1.0f - Clamp01((state.clock - page.pointsAt) / 0.4f));
				const float breathe = 0.5f + 0.5f * std::sin(state.clock * 2.6f);
				if (unspent > 0) {
					Halo(a_draw, pc, 58.0f * a_s * bump, kAccent, alpha * (0.6f + 0.35f * breathe));
				}
				Diamond(a_draw, pc, 36.0f * a_s * bump, Alpha(unspent > 0 ? palette.points : IM_COL32(20, 20, 21, 250), alpha), Alpha(unspent > 0 ? kEdge : kGlassEdge, alpha),
					1.6f * a_s);
				for (const V d : { V{ 0, -1 }, V{ 1, 0 }, V{ 0, 1 }, V{ -1, 0 } }) {
					Stud(a_draw, { pc.x + d.x * 36.0f * a_s * bump, pc.y + d.y * 36.0f * a_s * bump }, 3.0f * a_s, alpha);
				}
				if (a_member.unlimited) {
					Glyph(a_draw, { pc.x, pc.y - 5.0f * a_s }, 24.0f * a_s * bump, 0xF534, Alpha(kText, alpha));  // infinity
				} else {
					TextCentered(a_draw, { pc.x, pc.y - 5.0f * a_s }, Alpha(unspent > 0 ? kText : kTextDim, alpha), 30.0f * a_s * bump, std::format("{}", unspent));
				}
				const float lw = LabelWidth(10.0f * a_s, "POINTS", 1.4f * a_s);
				Label(a_draw, { pc.x - lw * 0.5f, pc.y + 13.0f * a_s }, Alpha(kTextDim, alpha), 10.0f * a_s, "POINTS", 1.4f * a_s);
				const auto  line1 = a_member.unlimited ? std::format("{} spent, unlimited", a_member.pointsSpent) : std::format("{} of {} spent", a_member.pointsSpent, a_member.pointsEarned);
				const auto  line2 = invested > 0 ? std::format("Mastery {} / {}, {} in the bonus", onTree, mastery, invested) : std::format("Mastery {} / {}", onTree, mastery);
				const auto  line3 = a_member.unlimited ? std::string("Cheater") : a_member.level <= a_member.pointsAbove ? std::format("The first comes at level {}", a_member.pointsAbove + 1) :
				                                                             std::string("One more every level");
				const float rx = pc.x - 58.0f * a_s;
				Text(a_draw, { rx - TextSize(14.0f * a_s, line1).x, min.y + 20.0f * a_s }, Alpha(kText, alpha), 14.0f * a_s, line1);
				Text(a_draw, { rx - TextSize(13.0f * a_s, line2).x, min.y + 39.0f * a_s }, Alpha(kTextDim, alpha), 13.0f * a_s, line2);
				Text(a_draw, { rx - TextSize(12.5f * a_s, line3).x, min.y + 57.0f * a_s }, Alpha(kTextFaint, alpha), 12.5f * a_s, line3);
				state.guide.areas[kGuidePoints] = { { rx - 172.0f * a_s, min.y + 10.0f * a_s }, { max.x - pad + 4.0f * a_s, min.y + bandH - 4.0f * a_s } };

				// The guide: a ? in the header, as well as F1 and View on a pad.
				const V     gc{ rx - 210.0f * a_s, min.y + bandH * 0.5f - 7.0f * a_s };
				const float gr = 16.0f * a_s;
				bool        hovered = false;
				const bool  clicked = Hit("pathGuide", { gc.x - gr - 6.0f * a_s, gc.y - gr - 6.0f * a_s }, { gr * 2.0f + 12.0f * a_s, gr * 2.0f + 30.0f * a_s }, hovered) && live;
				const float hover = state.tw.Toward("pathGuide", 0, hovered ? 1.0f : 0.0f, 16.0f);
				if (hovered) {
					Hovering(0x7240);
					Tip("How paths work, step by step. F1 opens it too, and View on a pad."s);
				}
				Halo(a_draw, gc, gr * 2.2f, kAccent, alpha * 0.6f * hover);
				Draw::AddCircleFilled(a_draw, gc, gr, Alpha(Mix(palette.inset, kAccent, hover * 0.7f), alpha), 32);
				Draw::AddCircle(a_draw, gc, gr, Alpha(Mix(kTextFaint, kText, hover), alpha), 32, 1.2f * a_s);
				Glyph(a_draw, gc, 14.0f * a_s, 0xF128, Alpha(Mix(kTextDim, kText, hover), alpha));
				const float gw = LabelWidth(9.5f * a_s, "GUIDE", 1.8f * a_s);
				Label(a_draw, { gc.x - gw * 0.5f, gc.y + gr + 6.0f * a_s }, Alpha(Mix(kTextDim, kText, hover), alpha), 9.5f * a_s, "GUIDE", 1.8f * a_s);
				state.guide.areas[kGuideButton] = { { gc.x - gr - 14.0f * a_s, gc.y - gr - 10.0f * a_s }, { gc.x + gr + 14.0f * a_s, gc.y + gr + 24.0f * a_s } };
				if (clicked) {
					OpenGuide();
					Sound("UIMenuOK");
				}

				// Unlimited points, beside the guide: it is about the companion, not the path, so it sits up
				// here. Burning red while they are on; pressed again, they go back to what they had.
				if (a_member.unlimitedAllowed || a_member.unlimited) {
					const bool  on = a_member.unlimited;
					const V     uc{ gc.x - 84.0f * a_s, gc.y };
					bool        over = false;
					const bool  pressed = Hit("pathCheat", { uc.x - gr - 14.0f * a_s, uc.y - gr - 6.0f * a_s }, { gr * 2.0f + 28.0f * a_s, gr * 2.0f + 30.0f * a_s }, over) && live;
					const float lift = state.tw.Toward("pathCheat", 0, over ? 1.0f : 0.0f, 16.0f);
					if (over) {
						Hovering(0x7230);
						Tip(on ? "Unlimited points are on. Press again to put them back exactly as they were before: every rank bought since is given back, "
						         "and anything they carry that they no longer know is swapped for a default."s :
						         "Give unlimited points: every rank on every path for free. Pressing it again puts them back exactly as they were."s);
					}
					Halo(a_draw, uc, gr * 2.4f, kAccent, alpha * (on ? 0.6f + 0.3f * breathe : 0.6f * lift));
					const U32 rest = palette.inset;
					Draw::AddCircleFilled(a_draw, uc, gr, Alpha(on ? Mix(kAccent, kEdge, 0.2f + 0.4f * lift) : Mix(rest, kAccent, lift * 0.7f), alpha), 32);
					Draw::AddCircle(a_draw, uc, gr, Alpha(on ? Mix(kEdge, kText, 0.4f + 0.4f * breathe) : Mix(kTextFaint, kText, lift), alpha), 32, (on ? 1.6f : 1.2f) * a_s);
					Glyph(a_draw, uc, 15.0f * a_s, 0xF534, Alpha(on ? kText : Mix(kTextDim, kText, lift), alpha));
					const auto  word = on ? std::string_view("UNLIMITED: ON") : std::string_view("UNLIMITED");
					const float uw = LabelWidth(9.5f * a_s, word, 1.8f * a_s);
					Label(a_draw, { uc.x - uw * 0.5f, uc.y + gr + 6.0f * a_s }, Alpha(on ? kEdge : Mix(kTextDim, kText, lift), alpha), 9.5f * a_s, word, 1.8f * a_s);
					if (pressed) {
						Battle::Command command;
						command.type = Battle::Command::Type::kUnlimited;
						command.member = a_member.id;
						command.on = !on;
						Battle::Queue(command);
						Sound(on ? "UIMenuCancel" : "UIMenuOK");
					}
				}
			}

			// --- The six paths, as tabs ---
			const float detailW = 400.0f * a_s;
			const float treeL = min.x + pad, treeR = max.x - pad - detailW - 24.0f * a_s;
			const float treeW = treeR - treeL;
			const float tabsY = min.y + bandH + 14.0f * a_s, tabH = 38.0f * a_s;
			state.guide.areas[kGuideTabs] = { { treeL - 4.0f * a_s, tabsY - 4.0f * a_s }, { treeR + 4.0f * a_s, tabsY + tabH + 4.0f * a_s } };
			state.guide.areas[kGuideTree] = { { treeL - 4.0f * a_s, tabsY + tabH + 10.0f * a_s }, { treeR + 4.0f * a_s, max.y - pad - 24.0f * a_s } };
			{
				const float tabW = treeW / static_cast<float>(Role::kCount);
				const float slide = state.tw.Toward("pathTab", 0, static_cast<float>(page.path), 14.0f);
				Draw::AddLine(a_draw, { treeL, tabsY + tabH }, { treeR, tabsY + tabH }, Alpha(kGlassEdge, alpha), 1.0f);
				// The lit floor glides to the open path.
				Lit(a_draw, { treeL + tabW * slide + 2.0f * a_s, tabsY }, { treeL + tabW * (slide + 1.0f) - 2.0f * a_s, tabsY + tabH }, alpha);
				for (int r = 0; r < static_cast<int>(Role::kCount); ++r) {
					const auto role = static_cast<Role>(r);
					const V    tmin{ treeL + tabW * static_cast<float>(r), tabsY };
					bool       hovered = false;
					Im::PushID(7000 + r);
					const bool clicked = Hit("pathTab", tmin, { tabW, tabH }, hovered) && live;
					Im::PopID();
					const bool  on = role == page.path;
					const bool  theirs = role == a_member.orders.role;
					const float hover = state.tw.Toward("pathTabHover", static_cast<std::uint64_t>(r), hovered ? 1.0f : 0.0f, 16.0f);
					const U32   tone = Mix(kTextDim, kText, std::max(on ? 1.0f : 0.0f, hover));
					const auto  name = std::string(Info(role).name);
					const int   spent = Paths::Spent(record, role);
					const auto  count = spent > 0 ? std::format("{}", spent) : std::string();
					// Glyph, name and the points spent there, centred together; their own role marked by a
					// small red diamond before the glyph.
					const float nameW = TextSize(14.0f * a_s, name).x;
					const float countW = count.empty() ? 0.0f : TextSize(11.5f * a_s, count).x + 12.0f * a_s;
					const float markW = theirs ? 14.0f * a_s : 0.0f;
					const float total = markW + 22.0f * a_s + nameW + (count.empty() ? 0.0f : 8.0f * a_s + countW);
					float       x = tmin.x + (tabW - total) * 0.5f;
					const float cy = tmin.y + tabH * 0.5f;
					if (theirs) {
						Diamond(a_draw, { x + 4.0f * a_s, cy }, 4.0f * a_s, Alpha(kEdge, alpha), Alpha(kText, alpha * 0.6f), 1.0f);
						x += markW;
					}
					Glyph(a_draw, { x + 8.0f * a_s, cy }, 13.0f * a_s, Info(role).glyph, Alpha(tone, alpha));
					x += 22.0f * a_s;
					Text(a_draw, { x, cy - 8.5f * a_s }, Alpha(tone, alpha), 14.0f * a_s, name);
					x += nameW + 8.0f * a_s;
					if (!count.empty()) {
						const V cmin{ x, cy - 8.0f * a_s }, cmax{ x + countW, cy + 8.0f * a_s };
						Draw::AddRectFilled(a_draw, cmin, cmax, Alpha(on ? kAccent : IM_COL32(32, 32, 34, 255), alpha), 0.0f, 0);
						TextCentered(a_draw, { (cmin.x + cmax.x) * 0.5f, cy }, Alpha(on ? kText : kTextDim, alpha), 11.5f * a_s, count);
					}
					if (hovered) {
						Hovering(0x7000 + static_cast<std::uint64_t>(r));
						Tip(theirs ? std::format("{}: their role now. What they learn here they use in a fight.", Paths::Info(role).title) :
						             std::format("{}: points spent here wait until they take the {} role.", Paths::Info(role).title, Info(role).name));
					}
					if (clicked && !on) {
						page.path = role;
						page.tabAt = state.clock;
						page.resetArmed = false;
						Sound("UIMenuPrevNext");
					}
				}
			}

			// --- The tree: four columns, an ability over its two branches in each (or the capstone
			// alone), then the traits along the foot, under the lines between the columns ---
			const float treeTop = tabsY + tabH + 24.0f * a_s;
			const float colW = treeW / static_cast<float>(Paths::kAbilitiesPerPath);
			const float rows = std::clamp(max.y - pad - 34.0f * a_s - treeTop, 440.0f * a_s, 620.0f * a_s);
			const float abilityY = treeTop + rows * 0.15f;
			const float branchY = treeTop + rows * 0.45f;
			const float columnFoot = branchY + 60.0f * a_s;
			const float traitsLabelY = treeTop + rows * 0.64f;
			const float traitY = treeTop + rows * 0.83f;
			const float abilityR = 36.0f * a_s, branchR = 24.0f * a_s, traitR = 27.0f * a_s, capstoneR = 46.0f * a_s;
			const float entrance = state.clock - std::max(page.tabAt, page.openedAt);

			// Whether a node can be reached yet: its tier open and, for a capstone, every rank of
			// everything else on the path held.
			int othersHeld = 0, others = 0;
			for (std::size_t i = 0; i < Paths::kNodes; ++i) {
				if (!tree[i].Empty() && !tree[i].capstone) {
					others += tree[i].most;
					othersHeld += std::min<int>(record.ranks[static_cast<std::size_t>(path)][i], tree[i].most);
				}
			}
			auto shut = [&](std::size_t a_index) {
				const auto& n = tree[a_index];
				return !Paths::TierOpen(record, path, n.tier) || (n.capstone && othersHeld < others);
			};

			std::array<V, Paths::kNodes>     centres{};
			std::array<float, Paths::kNodes> radii{};
			for (std::size_t a = 0, trait = 0; a < Paths::kNodes; ++a) {
				const auto& node = tree[a];
				if (node.Empty()) {
					continue;
				}
				if (node.kind == Paths::Kind::kAbility) {
					const std::size_t column = a / 3;
					const float       cx = treeL + colW * (static_cast<float>(column) + 0.5f);
					if (node.capstone) {
						centres[a] = { cx, (abilityY + branchY) * 0.5f - 8.0f * a_s };
						radii[a] = capstoneR;
						continue;
					}
					centres[a] = { cx, abilityY };
					radii[a] = abilityR;
					centres[a + 1] = { cx - colW * 0.25f, branchY };
					centres[a + 2] = { cx + colW * 0.25f, branchY };
					radii[a + 1] = radii[a + 2] = branchR;
				} else if (node.kind == Paths::Kind::kTrait) {
					centres[a] = { treeL + treeW * static_cast<float>(trait + 1) / static_cast<float>(Paths::kTraitsPerPath + 1), traitY };
					radii[a] = traitR;
					++trait;
				}
			}

			// Pad and keys walk the nodes: left and right along a row, up and down to the nearest
			// node of the next row.
			auto rowOf = [&](std::size_t a_index) { return tree[a_index].kind == Paths::Kind::kAbility ? 0 : (tree[a_index].kind == Paths::Kind::kBranch ? 1 : 2); };
			if (live && (a_input.dx != 0 || a_dy != 0)) {
				const auto here = static_cast<std::size_t>(std::clamp(page.cursor, 0, static_cast<int>(Paths::kNodes) - 1));
				const int  row = rowOf(here);
				int        best = -1;
				float      bestScore = 1e9f;
				for (std::size_t i = 0; i < Paths::kNodes; ++i) {
					if (i == here || tree[i].Empty()) {
						continue;
					}
					const float dx = centres[i].x - centres[here].x;
					if (a_input.dx != 0) {
						if (rowOf(i) != row || dx * static_cast<float>(a_input.dx) <= 0.0f) {
							continue;
						}
						if (std::fabs(dx) < bestScore) {
							bestScore = std::fabs(dx);
							best = static_cast<int>(i);
						}
					} else if (rowOf(i) == row + (a_dy > 0 ? 1 : -1)) {
						if (std::fabs(dx) < bestScore) {
							bestScore = std::fabs(dx);
							best = static_cast<int>(i);
						}
					}
				}
				if (best >= 0) {
					page.cursor = best;
					page.resetArmed = false;
				}
			}
			page.cursor = std::clamp(page.cursor, 0, static_cast<int>(Paths::kNodes) - 1);
			if (tree[static_cast<std::size_t>(page.cursor)].Empty()) {
				page.cursor = 0;
			}

			// Unlimited points: the picture faint in the tree's bottom right corner, and CHEATER, large and
			// faint, along the bottom. Both behind everything else on the page.
			if (a_member.unlimited) {
				static const auto picture = MF::LoadTexture("Data/Interface/FightWithMe/cheater.png");
				const float bottom = max.y - pad;
				const float side = std::min(300.0f * a_s, bottom - (traitsLabelY - 40.0f * a_s));
				const V     pmin{ treeR - side, bottom - side };
				if (picture) {
					Draw::AddImage(a_draw, picture, pmin, { treeR, bottom }, { 0, 0 }, { 1, 1 }, Alpha(IM_COL32(255, 255, 255, 255), alpha * 0.24f));
				}
				const float size = 116.0f * a_s;
				const auto  word = std::string_view("CHEATER");
				const float w = LabelWidth(size, word, 10.0f * a_s);
				const V     at{ pmin.x - w - 10.0f * a_s, bottom - size * 0.95f };
				// Bold, as the menu font has no bold: the word laid down a few times, a pixel or two apart.
				for (const V d : { V{ 0, 0 }, V{ 1.5f, 0 }, V{ 0, 1.5f }, V{ 1.5f, 1.5f }, V{ 3.0f, 0.75f } }) {
					Label(a_draw, { at.x + d.x * a_s, at.y + d.y * a_s }, Alpha(kEdge, alpha * 0.07f), size, word, 10.0f * a_s);
				}
			}

			// The columns: hairlines between them, a tier heading over each, and over a column still
			// shut, a darker floor and what opens it.
			for (std::size_t column = 1; column < Paths::kAbilitiesPerPath; ++column) {
				const float x = treeL + colW * static_cast<float>(column);
				const float grow = Stage(entrance, 0.05f * static_cast<float>(column), 0.5f);
				Draw::AddLine(a_draw, { x, treeTop - 4.0f * a_s }, { x, treeTop - 4.0f * a_s + (columnFoot - treeTop) * grow }, Alpha(kGlassEdge, alpha * 0.8f), 1.0f);
				Stud(a_draw, { x, treeTop - 4.0f * a_s }, 2.2f * a_s, alpha * 0.8f);
			}
			for (std::size_t column = 0; column < Paths::kAbilitiesPerPath; ++column) {
				const auto  index = column * 3;
				const auto& node = tree[index];
				const bool  closed = shut(index);
				const float dim = state.tw.Toward("pathShut", static_cast<std::uint64_t>(path) * 8 + column, closed ? 1.0f : 0.0f, 8.0f);
				const float cx = treeL + colW * (static_cast<float>(column) + 0.5f);
				if (dim > 0.01f) {
					Draw::AddRectFilledMultiColor(a_draw, { cx - colW * 0.5f + 2.0f * a_s, treeTop + 16.0f * a_s }, { cx + colW * 0.5f - 2.0f * a_s, columnFoot },
						Alpha(kShade, alpha * 0.32f * dim), Alpha(kShade, alpha * 0.32f * dim), Alpha(kShade, 0.0f), Alpha(kShade, 0.0f));
				}
				std::string tag;
				if (node.capstone) {
					tag = othersHeld < others ? std::format("{} / {} RANKS", othersHeld, others) : std::string("CAPSTONE");
				} else if (!Paths::TierOpen(record, path, node.tier)) {
					tag = std::format("{} / {} POINTS", std::min(Paths::SpentBelow(record, path, node.tier), Paths::kTierPoints[node.tier]), Paths::kTierPoints[node.tier]);
				} else {
					tag = std::format("TIER {}", RankWord(node.tier + 1));
				}
				const float tw = LabelWidth(11.0f * a_s, tag, 1.6f * a_s);
				const float lx = cx - tw * 0.5f + (closed ? 8.0f * a_s : 0.0f);
				if (closed) {
					Glyph(a_draw, { lx - 12.0f * a_s, treeTop + 6.0f * a_s }, 10.0f * a_s, 0xF023, Alpha(kEdge, alpha));
				}
				Label(a_draw, { lx, treeTop }, Alpha(closed ? kEdge : (node.capstone ? kText : kTextFaint), alpha), 11.0f * a_s, tag, 1.6f * a_s);
			}

			// Connectors, ability to branch: an elbow from the diamond's corner. Lit once the
			// branch has a rank, with a spark running down it.
			for (std::size_t a = 0; a < Paths::kNodes; a += 3) {
				if (tree[a].kind != Paths::Kind::kAbility || tree[a].capstone) {
					continue;
				}
				const int  abilityRank = record.ranks[static_cast<std::size_t>(path)][a];
				const bool openTier = Paths::TierOpen(record, path, tree[a].tier);
				for (std::size_t b = a + 1; b <= a + 2; ++b) {
					if (tree[b].Empty()) {
						continue;
					}
					const float appear = Stage(entrance, 0.12f + 0.025f * static_cast<float>(b), 0.35f);
					const int   branchRank = record.ranks[static_cast<std::size_t>(path)][b];
					const float side = centres[b].x < centres[a].x ? -1.0f : 1.0f;
					const V     from{ centres[a].x + side * (abilityR + 6.0f * a_s), centres[a].y };
					const V     elbow{ centres[b].x, centres[a].y };
					const V     to{ centres[b].x, centres[b].y - branchR - 4.0f * a_s };
					const float k = appear;
					const V     mid{ from.x + (elbow.x - from.x) * Clamp01(k * 2.0f), from.y };
					const V     end{ elbow.x, elbow.y + (to.y - elbow.y) * Clamp01(k * 2.0f - 1.0f) };
					U32         color = Alpha(kGlassEdge, alpha);
					float       thick = 1.0f;
					if (branchRank > 0) {
						color = Alpha(kEdge, alpha);
						thick = 2.0f * a_s;
					} else if (abilityRank > 0 && openTier) {
						color = Alpha(kTextDim, alpha * 0.55f);
					}
					if (abilityRank == 0 || !openTier) {
						Marching(a_draw, from, mid, color, 1.0f, 5.0f * a_s, 5.0f * a_s, 0.0f);
						if (k > 0.5f) {
							Marching(a_draw, elbow, end, color, 1.0f, 5.0f * a_s, 5.0f * a_s, 0.0f);
						}
					} else {
						Draw::AddLine(a_draw, from, mid, color, thick);
						if (k > 0.5f) {
							Draw::AddLine(a_draw, elbow, end, color, thick);
						}
					}
					if (branchRank > 0 && k >= 1.0f) {
						const float run = std::fabs(elbow.x - from.x) + std::fabs(to.y - elbow.y);
						const float at = std::fmod(state.clock * 90.0f * a_s + static_cast<float>(b) * 37.0f, run);
						const V     spark = at < std::fabs(elbow.x - from.x) ? V{ from.x + side * at, from.y } : V{ elbow.x, elbow.y + (at - std::fabs(elbow.x - from.x)) };
						Halo(a_draw, spark, 9.0f * a_s, (palette.Subdued() ? palette.orbMote : IM_COL32(255, 200, 170, 255)), alpha);
						Draw::AddCircleFilled(a_draw, spark, 1.8f * a_s, Alpha(kText, alpha), 8);
					}
				}
			}

			// The traits' rule and label, and what traits are, centred under it.
			{
				const float appear = Stage(entrance, 0.3f, 0.4f);
				const float run = treeW * appear;
				const float cx = treeL + treeW * 0.5f;
				Draw::AddLine(a_draw, { cx - run * 0.5f, traitsLabelY + 7.0f * a_s }, { cx + run * 0.5f, traitsLabelY + 7.0f * a_s }, Alpha(kGlassEdge, alpha), 1.0f);
				const float lw = LabelWidth(12.0f * a_s, "TRAITS", 2.0f * a_s);
				Draw::AddRectFilled(a_draw, { cx - lw * 0.5f - 12.0f * a_s, traitsLabelY - 3.0f * a_s }, { cx + lw * 0.5f + 12.0f * a_s, traitsLabelY + 17.0f * a_s },
					Alpha(IM_COL32(15, 15, 16, 255), alpha), 0.0f, 0);
				Label(a_draw, { cx - lw * 0.5f, traitsLabelY }, Alpha(kTextDim, alpha * appear), 12.0f * a_s, "TRAITS", 2.0f * a_s);
				TextCentered(a_draw, { cx, traitsLabelY + 30.0f * a_s }, Alpha(kTextFaint, alpha * appear), 12.0f * a_s, "Always on, in every fight");
			}

			// The nodes.
			for (std::size_t i = 0; i < Paths::kNodes; ++i) {
				const auto& node = tree[i];
				if (node.Empty()) {
					continue;
				}
				const int  rank = record.ranks[static_cast<std::size_t>(path)][i];
				const auto block = Paths::CanLearn(record, path, i, a_member.pointsEarned);
				NodeLook   look;
				look.kind = node.kind;
				look.glyph = node.glyph;
				look.rank = rank;
				look.most = node.most;
				look.locked = shut(i);
				look.capstone = node.capstone;
				look.buyable = block == Paths::Block::kNone;
				look.selected = page.cursor == static_cast<int>(i);
				look.appear = Stage(entrance, 0.06f + 0.03f * static_cast<float>(i), 0.45f);
				look.rankedAge = state.clock - page.rankedAt[i];
				const float r = radii[i];
				bool        hovered = false;
				Im::PushID(7100 + static_cast<int>(i));
				const bool clicked = Hit("pathNode", { centres[i].x - r - 4.0f * a_s, centres[i].y - r - 4.0f * a_s }, { (r + 4.0f * a_s) * 2.0f, (r + 4.0f * a_s) * 2.0f },
					hovered) && live;
				Im::PopID();
				look.hover = state.tw.Toward("pathNodeHover", static_cast<std::uint64_t>(path) * 32 + i, hovered ? 1.0f : 0.0f, 16.0f);
				DrawNode(a_draw, centres[i], r, look, a_s, alpha);
				// The name under the pips; an ability carried into a fight says which slot.
				const float shown = alpha * Clamp01(look.appear * 1.4f);
				const float ny = centres[i].y + (node.capstone ? r * 1.5f : r) + (node.kind == Paths::Kind::kAbility ? 26.0f : 22.0f) * a_s;
				const U32   tone = rank > 0 ? kText : (look.locked ? kTextFaint : kTextDim);
				const bool  big = node.kind == Paths::Kind::kAbility;
				TextCentered(a_draw, { centres[i].x, ny + 7.0f * a_s }, Alpha(tone, shown), (node.capstone ? 17.0f : (big ? 15.0f : 13.0f)) * a_s,
					big ? Upper(node.name) : std::string(node.name));
				if (big) {
					const auto& slots = record.slots[static_cast<std::size_t>(path)];
					const int   slot = slots[0] == node.ability ? 1 : (slots[1] == node.ability ? 2 : 0);
					if (slot > 0) {
						const auto  tag = std::format("SLOT {}", slot);
						const float tw = LabelWidth(9.5f * a_s, tag, 1.4f * a_s) + 12.0f * a_s;
						const V     tmin{ centres[i].x - tw * 0.5f, ny + 19.0f * a_s };
						Draw::AddRectFilled(a_draw, tmin, { tmin.x + tw, tmin.y + 15.0f * a_s }, Alpha(kAccent, shown), 0.0f, 0);
						Draw::AddRect(a_draw, tmin, { tmin.x + tw, tmin.y + 15.0f * a_s }, Alpha(kEdge, shown), 0.0f, 0, 1.0f);
						Label(a_draw, { tmin.x + 6.0f * a_s, tmin.y + 3.0f * a_s }, Alpha(kText, shown), 9.5f * a_s, tag, 1.4f * a_s);
					}
				}
				if (hovered) {
					Hovering(0x7100 + i);
				}
				if (clicked) {
					if (page.cursor == static_cast<int>(i) && block == Paths::Block::kNone) {
						PathCommand(Battle::Command::Type::kLearn, a_member.id, path, i);  // a second click on the chosen node buys it
					} else {
						Sound("UIMenuFocus");
					}
					page.cursor = static_cast<int>(i);
					page.resetArmed = false;
				}
				if (hovered && Im::IsMouseClicked(1) && live) {
					page.cursor = static_cast<int>(i);
					if (Paths::CanForget(record, path, i) == Paths::Block::kNone && a_member.respec) {
						PathCommand(Battle::Command::Type::kForget, a_member.id, path, i);
						Sound("UIMenuCancel");
					}
				}
			}

			// --- The card for the chosen node ---
			// Its mark and name, what it does with the numbers picked out, a ladder of its ranks, and
			// one thing to press. At the foot, the two abilities carried into a fight as sockets to
			// drop it in, and under them the rarely used switches.
			const auto  chosen = static_cast<std::size_t>(page.cursor);
			const auto& node = tree[chosen];
			const int   rank = record.ranks[static_cast<std::size_t>(path)][chosen];
			const auto  learnBlock = Paths::CanLearn(record, path, chosen, a_member.pointsEarned);
			const auto  forgetBlock = Paths::CanForget(record, path, chosen);
			const float dx = max.x - pad - detailW;
			const float dTop = min.y + bandH + 14.0f * a_s;
			const float dBottom = max.y - pad;
			const float swap = Smooth((state.clock - page.tabAt) / 0.25f);
			float       y = dTop;
			Draw::AddLine(a_draw, { dx - 12.0f * a_s, dTop }, { dx - 12.0f * a_s, dBottom }, Alpha(kGlassEdge, alpha), 1.0f);
			Stud(a_draw, { dx - 12.0f * a_s, dTop }, 2.6f * a_s, alpha);
			Stud(a_draw, { dx - 12.0f * a_s, dBottom }, 2.6f * a_s, alpha);
			const float da = alpha * (0.3f + 0.7f * swap);
			const float breathe = 0.5f + 0.5f * std::sin(state.clock * 3.0f);
			const bool  isAbility = node.kind == Paths::Kind::kAbility;
			const bool  known = isAbility && rank > 0;
			const auto& slots = record.slots[static_cast<std::size_t>(path)];
			const bool  canLearn = learnBlock == Paths::Block::kNone;
			const bool  canForget = forgetBlock == Paths::Block::kNone && a_member.respec;
			const int   cost = Paths::CostOf(path, chosen, rank + 1);
			auto        textMid = [&](V a_at, U32 a_color, float a_size, std::string_view a_text) {
                Text(a_draw, { a_at.x, a_at.y - TextSize(a_size, a_text).y * 0.5f }, a_color, a_size, a_text);
			};

			// The foot is laid out first, from the bottom up, so the card knows where to stop.
			const bool  canReset = onPath > 0 && a_member.respec;
			const float footH = canReset ? 30.0f * a_s : 0.0f;
			const float footY = dBottom - footH;
			const float loadH = 128.0f * a_s;
			const float loadY = footY - (footH > 0.0f ? 16.0f * a_s : 0.0f) - loadH;
			const float actionH = 46.0f * a_s;
			const float actionY = loadY - 20.0f * a_s - actionH;

			// The mark and the name.
			{
				const V  c{ dx + 36.0f * a_s, y + 40.0f * a_s };
				NodeLook look;
				look.kind = node.kind;
				look.glyph = node.glyph;
				look.rank = rank;
				look.most = node.most;
				look.locked = shut(chosen);
				look.buyable = canLearn;
				look.capstone = node.capstone;
				look.rankedAge = state.clock - page.rankedAt[chosen];
				DrawNode(a_draw, c, 24.0f * a_s, look, a_s, da);
				std::string kind = "TRAIT  /  ALWAYS ON";
				if (isAbility) {
					kind = node.capstone ? "CAPSTONE" : (node.base > 0 ? "ABILITY  /  EVERYONE STARTS WITH IT" : "ABILITY");
				} else if (node.kind == Paths::Kind::kBranch) {
					kind = Upper(std::format("Branch of {}", Paths::Info(node.ability).name));
				}
				const float tx = dx + 88.0f * a_s;
				const float room = dx + detailW - tx;
				Label(a_draw, { tx, y + 8.0f * a_s }, Alpha(kEdge, da), 10.5f * a_s, kind, 2.0f * a_s);
				const auto  title = Upper(node.name);
				const float size = std::min(25.0f * a_s, 25.0f * a_s * room / std::max(LabelWidth(25.0f * a_s, title, 2.8f * a_s), 1.0f));
				Label(a_draw, { tx, y + 26.0f * a_s }, Alpha(kText, da), size, title, size * 0.11f);
				const auto status = rank >= node.most ? std::string("Mastered") :
				                    (rank > 0 ? std::format("Rank {} of {}", RankWord(rank), RankWord(node.most)) : std::string("Not learned yet"));
				Text(a_draw, { tx, y + 58.0f * a_s }, Alpha(rank > 0 ? kEdge : kTextFaint, da), 14.0f * a_s, status);
				y += (node.capstone ? 104.0f : 96.0f) * a_s;
			}
			Draw::AddLine(a_draw, { dx, y }, { dx + detailW, y }, Alpha(kGlassEdge, da), 1.0f);
			Stud(a_draw, { dx + detailW * 0.5f, y }, 3.0f * a_s, da);
			y += 16.0f * a_s;

			// A line of facts, each an icon and a few words.
			{
				std::vector<std::pair<std::uint32_t, std::string>> facts;
				if (isAbility) {
					Paths::Record next = record;
					if (rank == 0) {
						next.ranks[static_cast<std::size_t>(path)][chosen] = 1;
					}
					facts.emplace_back(0xF254, std::format("Ready again in {:.0f} s", Paths::Cooldown(next, path, node.ability)));
					if (!node.capstone) {
						int bheld = 0, bmost = 0;
						for (std::size_t b = chosen + 1; b <= chosen + 2 && b < Paths::kNodes; ++b) {
							bheld += record.ranks[static_cast<std::size_t>(path)][b];
							bmost += tree[b].most;
						}
						facts.emplace_back(0xF126, std::format("Branches {} / {}", bheld, bmost));
					}
				} else if (node.kind == Paths::Kind::kBranch) {
					const int parent = Paths::ParentOf(path, chosen);
					const int parentRank = parent >= 0 ? record.ranks[static_cast<std::size_t>(path)][static_cast<std::size_t>(parent)] : 0;
					facts.emplace_back(0xF0C1, parentRank > 0 ? std::format("{} at rank {}", Paths::Info(node.ability).name, RankWord(parentRank)) :
					                                            std::format("Needs {} first", Paths::Info(node.ability).name));
				} else {
					facts.emplace_back(0xF534, "In every fight, never pressed");
				}
				float fx = dx;
				for (const auto& [glyph, words] : facts) {
					Glyph(a_draw, { fx + 7.0f * a_s, y + 9.0f * a_s }, 12.0f * a_s, glyph, Alpha(kEdge, da));
					textMid({ fx + 20.0f * a_s, y + 9.0f * a_s }, Alpha(kTextDim, da), 14.0f * a_s, words);
					fx += 20.0f * a_s + TextSize(14.0f * a_s, words).x + 22.0f * a_s;
				}
				y += 30.0f * a_s;
			}

			// What it does: at the rank held, or at the rank the pointer is on in the ladder. The space
			// is the longest rank's, so running along the ladder never moves anything.
			const int heldRank = std::max(rank, 1);
			const int shown = page.previewNode == static_cast<int>(chosen) && page.previewRank > 0 ? page.previewRank : heldRank;
			{
				float tallest = 0.0f;
				for (int r = 1; r <= node.most; ++r) {
					tallest = std::max(tallest, RichText(nullptr, { dx, y }, 16.0f * a_s, Paths::Describe(node.perk, r), detailW, 0, 0));
				}
				RichText(a_draw, { dx, y }, 16.0f * a_s, Paths::Describe(node.perk, shown), detailW, Alpha(kTextDim, da), Alpha(kText, da));
				y += tallest + 12.0f * a_s;
			}

			// The ladder: a row a rank, held ones lit and ticked, the next one breathing with its price.
			{
				const float rowH = 30.0f * a_s;
				int         hoveredRank = 0;
				for (int r = 1; r <= node.most; ++r) {
					const bool held = r <= rank;
					const bool next = r == rank + 1;
					const V    rmin{ dx, y + rowH * static_cast<float>(r - 1) };
					const V    rmax{ dx + detailW, rmin.y + rowH };
					bool       hovered = false;
					Im::PushID(7500 + r);
					Hit("pathRank", rmin, { detailW, rowH }, hovered);
					Im::PopID();
					if (hovered) {
						hoveredRank = r;
					}
					const float hover = state.tw.Toward("pathRank", static_cast<std::uint64_t>(r), hovered ? 1.0f : 0.0f, 14.0f);
					if (next) {
						const U32 band = Alpha(kAccent, da * (0.34f + 0.14f * breathe));
						Draw::AddRectFilledMultiColor(a_draw, rmin, rmax, band, Alpha(kAccent, 0.0f), Alpha(kAccent, 0.0f), band);
					}
					if (hover > 0.01f) {
						Draw::AddRectFilled(a_draw, rmin, rmax, Alpha(kTileHover, da * hover), 0.0f, 0);
					}
					const V mc{ rmin.x + 15.0f * a_s, (rmin.y + rmax.y) * 0.5f };
					if (r < node.most) {
						Draw::AddLine(a_draw, { mc.x, mc.y + 11.0f * a_s }, { mc.x, mc.y + rowH - 11.0f * a_s }, Alpha(r < rank ? kEdge : kGlassEdge, da), 1.4f * a_s);
					}
					Diamond(a_draw, mc, 11.0f * a_s, Alpha(held ? kAccent : IM_COL32(19, 19, 20, 245), da),
						Alpha(held ? kEdge : (next ? Mix(kEdge, kText, breathe) : kGlassEdge), da), next ? 1.5f * a_s : 1.0f);
					TextCentered(a_draw, mc, Alpha(held || next ? kText : kTextFaint, da), 10.5f * a_s, RankWord(r));
					textMid({ rmin.x + 38.0f * a_s, mc.y }, Alpha(held || next ? kText : Mix(kTextFaint, kTextDim, hover), da), 15.0f * a_s, Paths::Measure(node.perk, r));
					if (held) {
						Glyph(a_draw, { rmax.x - 12.0f * a_s, mc.y }, 11.0f * a_s, 0xF00C, Alpha(kEdge, da));
					} else {
						const auto  price = std::to_string(Paths::CostOf(path, chosen, r));
						const float pw = TextSize(14.0f * a_s, price).x;
						const U32   ink = Alpha(next ? kText : kTextFaint, da);
						textMid({ rmax.x - 6.0f * a_s - pw, mc.y }, ink, 14.0f * a_s, price);
						Diamond(a_draw, { rmax.x - 17.0f * a_s - pw, mc.y }, 4.5f * a_s, ink, ink, 1.0f);
					}
				}
				page.previewNode = static_cast<int>(chosen);
				page.previewRank = hoveredRank;
				y += rowH * static_cast<float>(node.most) + 12.0f * a_s;
			}
			state.guide.areas[kGuideCard] = { { dx - 10.0f * a_s, dTop - 6.0f * a_s }, { dx + detailW + 10.0f * a_s, y } };

			// The one thing to press, always in the same place: learn or rank up; when it cannot be
			// bought, what stands in the way; once every rank is held, a mark that says so. Giving a
			// rank back is the small square beside it.
			{
				const bool  canGiveBack = rank > node.base;
				const float backW = canGiveBack ? actionH : 0.0f;
				const float mainW = detailW - (canGiveBack ? backW + 8.0f * a_s : 0.0f);
				std::string why;
				if (!canLearn && learnBlock != Paths::Block::kMaxed) {
					why = std::string(Paths::Why(learnBlock));
					if (learnBlock == Paths::Block::kTier) {
						why = std::format("Opens with {} points spent on this path, below it. {} so far.", Paths::kTierPoints[node.tier],
							Paths::SpentBelow(record, path, node.tier));
					} else if (learnBlock == Paths::Block::kCapstone) {
						why = std::format("Opens once every other node on this path is mastered: {} more rank{} to go.", others - othersHeld, others - othersHeld == 1 ? "" : "s");
					} else if (learnBlock == Paths::Block::kParent) {
						why = std::format("A branch never outranks its ability: {} rank {} first.", Paths::Info(node.ability).name, RankWord(rank + 1));
					} else if (learnBlock == Paths::Block::kNoPoints && cost > 1) {
						why = std::format("Needs {} points, and {} are left to spend.", cost, unspent);
					} else if (learnBlock == Paths::Block::kNoPoints) {
						why = a_member.level <= a_member.pointsAbove ? std::format("No points yet: the first comes at level {}.", a_member.pointsAbove + 1) :
						                                               std::string("No points left. One more comes with every level.");
					}
				}
				// What stands in the way may need two lines; the plate grows upwards from the button's floor.
				const float whyH = why.empty() ? 0.0f : RichText(nullptr, {}, 13.5f * a_s, why, mainW - 52.0f * a_s, 0, 0);
				const float plateH = why.empty() ? actionH : std::max(actionH, whyH + 18.0f * a_s);
				const float top = actionY + actionH - plateH;
				state.guide.areas[kGuideAction] = { { dx - 10.0f * a_s, top - 10.0f * a_s }, { dx + detailW + 10.0f * a_s, actionY + actionH + 10.0f * a_s } };

				// With room above the button, an ability says when they use it on their own.
				if (isAbility) {
					const auto& info = Paths::Info(node.ability);
					std::string own = info.own.empty() ? std::string("When it is needed.") : std::string(info.own);
					if (!own.empty() && own[0] >= 'A' && own[0] <= 'Z') {
						own[0] = static_cast<char>(own[0] - 'A' + 'a');
					}
					own = "On their own, " + own;
					const float ownH = RichText(nullptr, {}, 13.5f * a_s, own, detailW - 24.0f * a_s, 0, 0);
					if (y + ownH <= top - 10.0f * a_s) {
						Glyph(a_draw, { dx + 7.0f * a_s, y + 9.0f * a_s }, 12.0f * a_s, 0xF5DC, Alpha(kTextFaint, da));
						RichText(a_draw, { dx + 24.0f * a_s, y }, 13.5f * a_s, own, detailW - 24.0f * a_s, Alpha(kTextFaint, da), Alpha(kTextDim, da));
					}
				}

				// With every node on the path mastered, points left over go straight into its bonus.
				const bool canInvest = rank >= node.most && mastered && Paths::CanInvest(record, path, a_member.pointsEarned, bonusRoom) == Paths::Block::kNone;
				if (canInvest) {
					if (ActionButton(a_draw, "pathInvest", { dx, actionY }, { mainW, actionH }, "INTO THE BONUS", 1, a_s, da, 0x7270) || (live && a_input.activate)) {
						PathCommand(Battle::Command::Type::kInvest, a_member.id, path);
						Sound("UIMenuOK");
					}
					if (Im::IsItemHovered()) {
						Tip(std::format("Every node here is mastered: put a leftover point straight into the path bonus, {} now, {} with it.",
							Rules::PathBonusText(path, Rules::PathBonus(path, onPath, a_tuning)), Rules::PathBonusText(path, Rules::PathBonus(path, onPath + 1, a_tuning))));
					}
				} else if (rank >= node.most) {
					const float cy = actionY + actionH * 0.5f;
					const auto  word = std::string("MASTERED");
					const float size = 15.0f * a_s, track = 4.0f * a_s;
					const float lw = LabelWidth(size, word, track);
					const float mid = dx + mainW * 0.5f;
					Halo(a_draw, { mid, cy }, 60.0f * a_s, kAccent, da * 0.6f);
					for (const float side : { -1.0f, 1.0f }) {
						const float inner = mid + side * (lw * 0.5f + 16.0f * a_s);
						const float outer = side < 0.0f ? dx + 6.0f * a_s : dx + mainW - 6.0f * a_s;
						Draw::AddLine(a_draw, { inner, cy }, { outer, cy }, Alpha(kEdge, da * 0.7f), 1.0f);
						Diamond(a_draw, { inner - side * 2.0f * a_s, cy }, 3.5f * a_s, Alpha(kEdge, da), Alpha(kEdge, da), 1.0f);
						Stud(a_draw, { outer, cy }, 2.6f * a_s, da);
					}
					Label(a_draw, { mid - lw * 0.5f, cy - size * 0.58f }, Alpha(kEdge, da), size, word, track);
				} else if (canLearn) {
					if (ActionButton(a_draw, "pathLearn", { dx, actionY }, { mainW, actionH }, rank == 0 ? "LEARN" : "RANK UP", cost, a_s, da, 0x7200) ||
						(live && a_input.activate)) {
						PathCommand(Battle::Command::Type::kLearn, a_member.id, path, chosen);
						Sound("UIMenuOK");
					}
				} else {
					const V pmin{ dx, top }, pmax{ dx + mainW, actionY + actionH };
					Draw::AddRectFilled(a_draw, pmin, pmax, Alpha(kAmber, da * 0.06f), 0.0f, 0);
					Draw::AddRect(a_draw, pmin, pmax, Alpha(kAmber, da * 0.35f), 0.0f, 0, 1.0f);
					Glyph(a_draw, { pmin.x + 22.0f * a_s, (pmin.y + pmax.y) * 0.5f }, 14.0f * a_s, 0xF023, Alpha(kAmber, da));
					RichText(a_draw, { pmin.x + 44.0f * a_s, (pmin.y + pmax.y) * 0.5f - whyH * 0.5f }, 13.5f * a_s, why, mainW - 52.0f * a_s, Alpha(kAmber, da * 0.85f),
						Alpha(kAmber, da));
				}
				if (canGiveBack) {
					const V at{ dx + detailW - backW, actionY };
					if (GhostButton(a_draw, "pathForget", at, { backW, actionH }, 0xF2EA, "", false, false, a_s, da * (canForget ? 1.0f : 0.5f), 0x7210) && canForget) {
						PathCommand(Battle::Command::Type::kForget, a_member.id, path, chosen);
						Sound("UIMenuCancel");
					}
					if (Im::IsItemHovered()) {
						if (canForget) {
							Tip("Give back a rank."s);
						} else {
							Readout(std::string(a_member.respec ? Paths::Why(forgetBlock) : Paths::Why(Paths::Block::kNoRespec)));
						}
					}
				}
				if (live && canForget && (a_input.flipAuto || Im::IsKeyPressed(Im::ImGuiKey_Backspace, false) || Im::IsKeyPressed(Im::ImGuiKey_Delete, false))) {
					PathCommand(Battle::Command::Type::kForget, a_member.id, path, chosen);
					Sound("UIMenuCancel");
				}
			}

			// --- The two carried into a fight, as sockets ---
			// With an ability chosen that they know, the sockets it is not in turn to take it: hover
			// one and it fades in, press and it is carried there. Otherwise pressing a socket goes to
			// the ability in it.
			{
				const bool carried = known && (slots[0] == node.ability || slots[1] == node.ability);
				state.guide.areas[kGuideSockets] = { { dx - 10.0f * a_s, loadY - 10.0f * a_s }, { dx + detailW + 10.0f * a_s, loadY + loadH + 6.0f * a_s } };
				SectionLabel(a_draw, { dx, loadY }, a_s, da, path == a_member.orders.role ? "INTO A FIGHT" : "CARRIED ON THIS PATH", false);
				std::string hint;
				U32         tone = kTextFaint;
				if (known && a_member.inCombat) {
					hint = "Carrying waits for the fight to end";
					tone = kAmber;
				} else if (known && !carried) {
					hint = std::format("Choose a slot for {}", node.name);
					tone = Mix(kTextDim, kText, breathe);
				}
				if (!hint.empty()) {
					const float hw = TextSize(12.5f * a_s, hint).x;
					Text(a_draw, { dx + detailW - hw, loadY + 1.0f * a_s }, Alpha(tone, da), 12.5f * a_s, hint);
				}
				const float socketW = (detailW - 12.0f * a_s) * 0.5f;
				for (std::size_t s = 0; s < Paths::kSlots; ++s) {
					const V    cmin{ dx + (socketW + 12.0f * a_s) * static_cast<float>(s), loadY + 24.0f * a_s };
					const V    cmax{ cmin.x + socketW, loadY + loadH };
					const bool here = known && slots[s] == node.ability;
					const bool offer = known && !carried && !a_member.inCombat;
					bool       hovered = false;
					Im::PushID(7300 + static_cast<int>(s));
					const bool clicked = Hit("pathSocket", cmin, { socketW, cmax.y - cmin.y }, hovered) && live;
					Im::PopID();
					const float hover = state.tw.Toward("pathSocket", s, hovered && offer ? 1.0f : 0.0f, 14.0f);
					const float lift = state.tw.Toward("pathSocketAny", s, hovered ? 1.0f : 0.0f, 14.0f);
					if (hovered) {
						Hovering(0x7300 + s);
					}
					if (lift > 0.01f) {
						Draw::AddRectFilled(a_draw, cmin, cmax, Alpha(kTile, da * lift), 0.0f, 0);
					}
					const V     sc{ (cmin.x + cmax.x) * 0.5f, cmin.y + 32.0f * a_s };
					const float sr = 26.0f * a_s;
					if (here) {
						Halo(a_draw, sc, sr * 2.1f, kAccent, da * 0.85f);
					} else if (offer) {
						Halo(a_draw, sc, sr * 1.9f, kAccent, da * (0.12f + 0.16f * breathe + 0.4f * hover));
					}
					Draw::AddCircleFilled(a_draw, sc, sr, Alpha(IM_COL32(13, 13, 14, 248), da), 40);
					Draw::AddCircle(a_draw, sc, sr, Alpha(here ? kEdge : Mix(kGlassEdge, kText, lift * 0.4f), da), 40, here ? 1.8f * a_s : 1.0f);
					Draw::AddCircle(a_draw, sc, sr + 5.0f * a_s, Alpha(kGlassEdge, da * 0.6f), 40, 1.0f);
					if (offer) {
						const float a = state.clock * 1.4f;
						for (int k = 0; k < 2; ++k) {
							Draw::PathClear(a_draw);
							Draw::PathArcTo(a_draw, sc, sr + 5.0f * a_s, a + 3.1415927f * static_cast<float>(k), a + 3.1415927f * static_cast<float>(k) + 1.4f, 18);
							Draw::PathStroke(a_draw, Alpha(Mix(kEdge, kText, hover), da * (0.55f + 0.45f * breathe)), 0, 1.6f * a_s);
						}
					}
					auto inside = [&](Paths::Ability a_ability, float a_alpha) {
						if (a_alpha <= 0.01f || a_ability == Paths::Ability::kNone) {
							return;
						}
						const auto& info = Paths::Info(a_ability);
						if (a_ability == Paths::Ability::kMagnus) {
							DrawOrb(a_draw, sc, 13.0f * a_s, true, a_s, a_alpha);
							return;
						}
						Diamond(a_draw, sc, 17.0f * a_s, Alpha(kAccent, a_alpha), Alpha(kEdge, a_alpha), 1.2f * a_s);
						Glyph(a_draw, sc, 13.0f * a_s, info.glyph ? info.glyph : 0xF128, Alpha(kText, a_alpha));
					};
					inside(slots[s], da * (1.0f - hover));
					inside(node.ability, da * hover);
					// Its key, on the rim, as a hotkey sits on an ability.
					Keycap(a_draw, { sc.x - sr * 0.92f - 9.0f * a_s, sc.y + sr * 0.38f - 9.0f * a_s }, 18.0f * a_s, 11.0f * a_s, s == 0 ? "1" : "2", da);
					const auto  ability = hover > 0.5f ? node.ability : slots[s];
					const auto& info = Paths::Info(ability);
					TextCentered(a_draw, { sc.x, sc.y + sr + 18.0f * a_s }, Alpha(hover > 0.5f ? kText : (here ? kText : kTextDim), da), 15.0f * a_s,
						info.name.empty() ? std::string_view("Empty") : info.name);
					if (hover > 0.5f) {
						const auto  word = std::string("CARRY HERE");
						const float lw = LabelWidth(10.0f * a_s, word, 1.8f * a_s);
						Label(a_draw, { sc.x - lw * 0.5f, sc.y + sr + 32.0f * a_s }, Alpha(kEdge, da), 10.0f * a_s, word, 1.8f * a_s);
					} else {
						const int   r = Paths::Rank(record, path, slots[s]);
						const float gap = 11.0f * a_s;
						for (int k = 0; k < 3; ++k) {
							const bool on = k < r;
							Diamond(a_draw, { sc.x + gap * static_cast<float>(k - 1), sc.y + sr + 37.0f * a_s }, 3.6f * a_s, Alpha(on ? kEdge : IM_COL32(26, 26, 28, 255), da),
								Alpha(on ? kText : kGlassEdge, da * (on ? 0.6f : 1.0f)), 1.0f);
						}
					}
					const bool key = live && known && !here && ((s == 0 && Im::IsKeyPressed(Im::ImGuiKey_1, false)) || (s == 1 && Im::IsKeyPressed(Im::ImGuiKey_2, false)));
					if ((clicked && known && !here) || key) {
						PathCommand(Battle::Command::Type::kEquip, a_member.id, path, chosen, s, node.ability);
						Sound("UIMenuOK");
					} else if (clicked && !known && slots[s] != Paths::Ability::kNone) {
						for (std::size_t i = 0; i < Paths::kNodes; ++i) {
							if (tree[i].kind == Paths::Kind::kAbility && tree[i].ability == slots[s]) {
								page.cursor = static_cast<int>(i);
								page.resetArmed = false;
								Sound("UIMenuFocus");
							}
						}
					}
				}
				// Pad X: carry it, in the first slot it is not already in.
				if (live && known && a_input.now) {
					const std::size_t s = slots[0] == node.ability ? 1 : 0;
					if (slots[s] != node.ability) {
						PathCommand(Battle::Command::Type::kEquip, a_member.id, path, chosen, s, node.ability);
						Sound("UIMenuOK");
					}
				}
			}

			// --- Starting over, at the foot ---
			// One press arms it, a second within a few seconds gives every point on the path back.
			if (footH > 0.0f) {
				const bool armed = page.resetArmed && state.clock - page.resetArmedAt < 4.0f;
				const auto label = armed ? std::format("AGAIN: ALL {} BACK", onPath) : std::string("START OVER");
				if (GhostButton(a_draw, "pathReset", { dx, footY }, { detailW, footH }, 0xF2F9, label, false, armed, a_s, da, 0x7220)) {
					if (armed) {
						PathCommand(Battle::Command::Type::kResetPath, a_member.id, path);
						page.resetArmed = false;
						Sound("UIMenuCancel");
					} else {
						page.resetArmed = true;
						page.resetArmedAt = state.clock;
						Sound("UIMenuFocus");
					}
				}
			}

			// What came of the last press.
			{
				const bool  fresh = !a_member.answer.empty() && a_member.answerAge < 5.0f;
				const float show = fresh ? 1.0f - Clamp01((a_member.answerAge - 3.5f) / 1.5f) : 0.0f;
				if (show > 0.01f) {
					const bool good = a_member.answerDone;
					const U32  color = good ? kText : kAmber;
					const V    at{ treeL, max.y - pad - 16.0f * a_s };
					Glyph(a_draw, { at.x + 7.0f * a_s, at.y + 9.0f * a_s }, 12.0f * a_s, good ? 0xF00C : 0xF071, Alpha(color, alpha * show));
					Text(a_draw, { at.x + 22.0f * a_s, at.y }, Alpha(color, alpha * show), 15.0f * a_s, a_member.answer);
				}
			}
			return { min, max };
		}

		// --- The guide to paths ---------------------------------------------------------------
		// A few steps over the path page: the first time a path opens, and whenever the ? is
		// pressed. Each step lights one part of the page and darkens the rest, and a card beside
		// the light says what that part is for.

		struct GuideStep
		{
			int              area;  // kGuideNone: nothing lit, the card in the middle
			std::string_view title;
		};

		constexpr std::array<GuideStep, 8> guideSteps{ {
			{ kGuideNone, "PATHS" },
			{ kGuidePoints, "POINTS" },
			{ kGuideTabs, "SIX PATHS" },
			{ kGuideTree, "THE TREE" },
			{ kGuideCard, "THE CARD" },
			{ kGuideAction, "LEARNING" },
			{ kGuideSockets, "INTO A FIGHT" },
			{ kGuideButton, "THAT'S ALL" },
		} };

		std::string GuideText(int a_step, const View::Member& a_member)
		{
			switch (a_step) {
			case 0:
				return "Every companion has a path for each of the six roles: abilities to learn, ranks that make them stronger, and traits that are "
				       "always on. A few short steps show how it works.";
			case 1:
				return std::format("{} earns a point for every level past {}, counted from the highest level they have reached, so someone met late "
								   "arrives with points to spend. The same points serve all six paths.",
					a_member.name, a_member.pointsAbove);
			case 2:
				return std::format("One for each role. Points can go into any of them, but only the path of the role {} has now counts in a fight. Q "
								   "and E, the triggers or the tabs turn between them.",
					a_member.name);
			case 3:
				return "Deeper tiers open as points go into the ones above them. Every node holds up to three ranks, and a capstone asks for every "
				       "other node mastered first.";
			case 4:
				return "Whatever the cursor is on is read here, its numbers picked out. The ladder shows each rank: ticked once held, the next with "
				       "its price. Point at a rank to read what it would do.";
			case 5:
				return a_member.respec ? "Spend points here, or with Enter (A on a pad). Out of a fight a rank comes back for free: the small button "
				                         "beside this one, or a right-click on the node." :
				                         "Spend points here, or with Enter (A on a pad). Giving points back is switched off in this game, so choose "
				                         "with care.";
			case 6:
				return "Two abilities go into every fight. Choose one they know, then a slot, or press 1 or 2 (X on a pad). They use them on their "
				       "own when the moment comes, or when you call for them from their card.";
			default:
				return "Start over, at the foot of the card, gives back every point on a path. This guide is under the ?, or F1 (View on a pad), "
				       "whenever you want it again.";
			}
		}

		// a_move: +1 on, -1 back, from the keys and the pad; a_close: Esc or B.
		void DrawGuide(Im::ImDrawList* a_draw, const View::Member& a_member, V a_display, float a_s, float a_appear, int a_move, bool a_close)
		{
			using Area = State::Guide::Area;
			auto&     guide = state.guide;
			const int last = static_cast<int>(guideSteps.size()) - 1;
			hitsBlocked = false;
			auto go = [&](int a_step) {
				if (a_step > last) {
					CloseGuide();
					Sound("UIMenuOK");
					return;
				}
				a_step = std::max(a_step, 0);
				if (a_step != guide.step) {
					guide.from = guide.shown;
					guide.step = a_step;
					guide.stepAt = state.clock;
					Sound("UIMenuPrevNext");
				}
			};
			if (a_close) {
				CloseGuide();
				Sound("UIMenuCancel");
			} else if (a_move != 0) {
				go(guide.step + (a_move > 0 ? 1 : -1));
			}
			if (!guide.open) {
				hitsBlocked = true;
				return;
			}

			const float in = a_appear * EaseOut((state.clock - guide.openedAt) / 0.35f);
			const auto& step = guideSteps[static_cast<std::size_t>(guide.step)];
			const V     centre{ a_display.x * 0.5f, a_display.y * 0.5f };
			const Area  target = step.area >= 0 ? guide.areas[static_cast<std::size_t>(step.area)] : Area{ centre, centre };
			if (!guide.placed) {
				guide.from = target;
				guide.placed = true;
			}
			// The light glides from the last part to this one.
			const float k = EaseOut((state.clock - guide.stepAt) / 0.45f);
			auto        lerp = [&](V a, V b) { return V{ a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k }; };
			const Area  lit{ lerp(guide.from.min, target.min), lerp(guide.from.max, target.max) };
			guide.shown = lit;

			const U32 dark = Alpha(IM_COL32(4, 4, 5, 255), in * 0.76f);
			Draw::AddRectFilled(a_draw, { 0, 0 }, { a_display.x, lit.min.y }, dark, 0.0f, 0);
			Draw::AddRectFilled(a_draw, { 0, lit.max.y }, a_display, dark, 0.0f, 0);
			Draw::AddRectFilled(a_draw, { 0, lit.min.y }, { lit.min.x, lit.max.y }, dark, 0.0f, 0);
			Draw::AddRectFilled(a_draw, { lit.max.x, lit.min.y }, { a_display.x, lit.max.y }, dark, 0.0f, 0);
			const float breathe = 0.5f + 0.5f * std::sin(state.clock * 2.8f);
			if (lit.max.x - lit.min.x > 6.0f * a_s) {
				for (int i = 1; i <= 3; ++i) {
					const float g = static_cast<float>(i) * 3.0f * a_s;
					Draw::AddRect(a_draw, { lit.min.x - g, lit.min.y - g }, { lit.max.x + g, lit.max.y + g },
						Alpha(kAccent, in * (0.18f + 0.1f * breathe) * static_cast<float>(4 - i) / 3.0f), 0.0f, 0, 3.0f * a_s);
				}
				Draw::AddRect(a_draw, lit.min, lit.max, Alpha(kEdge, in * (0.7f + 0.3f * breathe)), 0.0f, 0, 1.4f * a_s);
				// Bright corners, as on a sight.
				const float c = std::min(16.0f * a_s, (lit.max.x - lit.min.x) * 0.3f);
				const U32   tick = Alpha(kText, in);
				for (const V corner : { lit.min, V{ lit.max.x, lit.min.y }, lit.max, V{ lit.min.x, lit.max.y } }) {
					const float sx = corner.x == lit.min.x ? 1.0f : -1.0f, sy = corner.y == lit.min.y ? 1.0f : -1.0f;
					Draw::AddLine(a_draw, corner, { corner.x + sx * c, corner.y }, tick, 2.0f * a_s);
					Draw::AddLine(a_draw, corner, { corner.x, corner.y + sy * c }, tick, 2.0f * a_s);
				}
			}

			// The card: what this part is for, sized to its words, beside the light where it fits.
			const float cardW = 430.0f * a_s, padC = 24.0f * a_s, bodySize = 15.5f * a_s;
			const auto  body = GuideText(guide.step, a_member);
			const float bodyW = cardW - padC * 2.0f;
			const float bodyH = RichText(nullptr, {}, bodySize, body, bodyW, 0, 0);
			const bool  welcome = step.area < 0;
			const bool  legend = step.area == kGuideTree;
			const float artH = welcome ? 104.0f * a_s : (legend ? 100.0f * a_s : 0.0f);
			const float cardH = padC + 20.0f * a_s + 38.0f * a_s + artH + bodyH + 22.0f * a_s + 42.0f * a_s + padC;
			V           at{ centre.x - cardW * 0.5f, centre.y - cardH * 0.5f };
			if (!welcome) {
				const float gap = 30.0f * a_s, edge = 24.0f * a_s;
				if (target.max.x + gap + cardW <= a_display.x - edge) {
					at = { target.max.x + gap, target.min.y };
				} else if (target.min.x - gap - cardW >= edge) {
					at = { target.min.x - gap - cardW, target.min.y };
				} else if (target.max.y + gap + cardH <= a_display.y - edge) {
					at = { (target.min.x + target.max.x - cardW) * 0.5f, target.max.y + gap };
				} else {
					at = { (target.min.x + target.max.x - cardW) * 0.5f, target.min.y - gap - cardH };
				}
				at.x = std::clamp(at.x, edge, a_display.x - edge - cardW);
				at.y = std::clamp(at.y, edge, a_display.y - 60.0f * a_s - cardH);
			}
			const float sk = EaseOut((state.clock - guide.stepAt) / 0.3f);
			const float ca = in * sk;
			const V     cmin{ at.x, at.y + (1.0f - sk) * 12.0f * a_s };
			const V     cmax{ cmin.x + cardW, cmin.y + cardH };

			// A thread from the card to the light, ending in a stud on its edge.
			if (!welcome) {
				const V mid{ (cmin.x + cmax.x) * 0.5f, (cmin.y + cmax.y) * 0.5f };
				const V onLit{ std::clamp(mid.x, target.min.x, target.max.x), std::clamp(mid.y, target.min.y, target.max.y) };
				const V onCard{ std::clamp(onLit.x, cmin.x, cmax.x), std::clamp(onLit.y, cmin.y, cmax.y) };
				Draw::AddLine(a_draw, onCard, onLit, Alpha(kEdge, ca * 0.7f), 1.2f * a_s);
				Diamond(a_draw, onLit, 4.5f * a_s, Alpha(kEdge, ca), Alpha(kText, ca), 1.0f);
			}

			Glass(a_draw, cmin, cmax, ca, 0.0f);
			Band(a_draw, { cmin.x + 1.0f, cmin.y + 1.0f }, { cmax.x - 1.0f, cmin.y + 74.0f * a_s }, ca * 0.55f);
			Draw::AddRectFilled(a_draw, cmin, { cmax.x, cmin.y + 2.0f * a_s }, Alpha(kEdge, ca), 0.0f, 0);
			float y = cmin.y + padC;
			const auto counter = welcome ? std::string("GUIDE") : std::format("GUIDE  /  {} OF {}", guide.step, last);
			Label(a_draw, { cmin.x + padC, y }, Alpha(kEdge, ca), 10.5f * a_s, counter, 2.2f * a_s);
			y += 20.0f * a_s;
			Label(a_draw, { cmin.x + padC, y }, Alpha(kText, ca), 23.0f * a_s, step.title, 3.6f * a_s);
			y += 38.0f * a_s;

			if (welcome) {
				// The path's own mark, large.
				const V mc{ (cmin.x + cmax.x) * 0.5f, y + 50.0f * a_s };
				Halo(a_draw, mc, 70.0f * a_s, kAccent, ca * (0.7f + 0.2f * breathe));
				Diamond(a_draw, mc, 38.0f * a_s, Alpha((palette.Subdued() ? palette.inset : IM_COL32(24, 11, 11, 250)), ca), Alpha(kEdge, ca), 1.8f * a_s);
				Diamond(a_draw, mc, 31.0f * a_s, Alpha(kShade, 0.0f), Alpha(kText, ca * 0.35f), 1.0f);
				for (const V d : { V{ 0, -1 }, V{ 1, 0 }, V{ 0, 1 }, V{ -1, 0 } }) {
					Stud(a_draw, { mc.x + d.x * 38.0f * a_s, mc.y + d.y * 38.0f * a_s }, 3.0f * a_s, ca);
				}
				Glyph(a_draw, mc, 26.0f * a_s, Info(state.page.path).glyph, Alpha(kText, ca));
			} else if (legend) {
				// The three shapes on the tree, each with a word on what it is.
				struct Shape
				{
					Paths::Kind      kind;
					std::uint32_t    glyph;
					std::string_view name, what;
				};
				constexpr std::array<Shape, 3> shapes{ { { Paths::Kind::kAbility, 0xF6DE, "ABILITY", "used in a fight" },
					{ Paths::Kind::kBranch, 0xF126, "BRANCH", "adds to its ability" }, { Paths::Kind::kTrait, 0xF3ED, "TRAIT", "always on" } } };
				const float third = bodyW / 3.0f;
				for (std::size_t i = 0; i < shapes.size(); ++i) {
					const V  c{ cmin.x + padC + third * (static_cast<float>(i) + 0.5f), y + 28.0f * a_s };
					NodeLook look;
					look.kind = shapes[i].kind;
					look.glyph = shapes[i].glyph;
					look.rank = 1;
					look.most = 0;
					DrawNode(a_draw, c, (shapes[i].kind == Paths::Kind::kAbility ? 17.0f : 15.0f) * a_s, look, a_s, ca);
					const float nw = LabelWidth(10.5f * a_s, shapes[i].name, 1.8f * a_s);
					Label(a_draw, { c.x - nw * 0.5f, y + 58.0f * a_s }, Alpha(kText, ca), 10.5f * a_s, shapes[i].name, 1.8f * a_s);
					TextCentered(a_draw, { c.x, y + 82.0f * a_s }, Alpha(kTextFaint, ca), 12.5f * a_s, shapes[i].what);
				}
			}
			y += artH;
			RichText(a_draw, { cmin.x + padC, y }, bodySize, body, bodyW, Alpha(kTextDim, ca), Alpha(kText, ca));
			y += bodyH + 22.0f * a_s;

			// The foot: a diamond a step, back, and on.
			const float bh = 42.0f * a_s;
			const float gap = 12.0f * a_s;
			for (int i = 0; i <= last; ++i) {
				const bool now = i == guide.step, done = i < guide.step;
				Diamond(a_draw, { cmin.x + padC + 4.0f * a_s + gap * static_cast<float>(i), y + bh * 0.5f }, (now ? 4.6f : 3.4f) * a_s,
					Alpha(now ? kEdge : (done ? kTextDim : IM_COL32(30, 30, 32, 255)), ca), Alpha(now ? kText : kGlassEdge, ca), 1.0f);
			}
			const float nextW = 150.0f * a_s;
			const V     nextAt{ cmax.x - padC - nextW, y };
			const auto  verb = welcome ? std::string_view("BEGIN") : (guide.step == last ? std::string_view("DONE") : std::string_view("NEXT"));
			const bool  next = ActionButton(a_draw, "guideNext", nextAt, { nextW, bh }, verb, -1, a_s, ca, 0x7600, guide.step == last ? 0xF00C : 0xF061);
			bool        back = false;
			if (guide.step > 0) {
				back = GhostButton(a_draw, "guideBack", { nextAt.x - 10.0f * a_s - bh, y }, { bh, bh }, 0xF060, "", false, false, a_s, ca, 0x7601);
			}
			const float xs = 30.0f * a_s;
			const bool  close = GhostButton(a_draw, "guideClose", { cmax.x - 12.0f * a_s - xs, cmin.y + 12.0f * a_s }, { xs, xs }, 0xF00D, "", false, false, a_s, ca, 0x7602);
			if (Im::IsItemHovered()) {
				Tip("Close the guide. It is under the ? whenever you want it."s);
			}
			if (close) {
				CloseGuide();
				Sound("UIMenuCancel");
			} else if (next) {
				go(guide.step + 1);
			} else if (back) {
				go(guide.step - 1);
			}
			hitsBlocked = true;
		}

		// --- Overhead ------------------------------------------------------------------------

		constexpr U32 kAlly = IM_COL32(118, 172, 228, 255);
		constexpr U32 kFoe = IM_COL32(222, 62, 52, 255);

		// A ring on the ground: a faint fill, a soft glow under a bright edge.
		void GroundMark(Im::ImDrawList* a_draw, std::span<const View::Point> a_ring, V a_display, U32 a_color, float a_alpha, float a_fill, float a_thickness)
		{
			if (a_ring.size() < 4 || a_alpha <= 0.01f) {
				return;
			}
			std::vector<V> points;
			points.reserve(a_ring.size());
			bool whole = true;
			for (std::size_t i = 0; i + 1 < a_ring.size(); ++i) {  // the last point closes the ring
				if (!a_ring[i].on) {
					whole = false;
					continue;
				}
				points.push_back(ScreenOf(a_ring[i], a_display));
			}
			if (whole && a_fill > 0.0f && points.size() >= 3) {
				Draw::AddConvexPolyFilled(a_draw, points.data(), static_cast<int>(points.size()), Alpha(a_color, a_alpha * a_fill));
			}
			for (std::size_t i = 1; i < a_ring.size(); ++i) {
				if (!a_ring[i - 1].on || !a_ring[i].on) {
					continue;
				}
				const V a = ScreenOf(a_ring[i - 1], a_display), b = ScreenOf(a_ring[i], a_display);
				Draw::AddLine(a_draw, a, b, Alpha(a_color, a_alpha * 0.2f), a_thickness * 3.2f);
				Draw::AddLine(a_draw, a, b, Alpha(a_color, a_alpha), a_thickness);
			}
		}

		// Seen from above, everyone stands in a ring: blue for the party, red for whoever they
		// fight, white for you, under a veil that pushes the world back.
		void DrawOverhead(Im::ImDrawList* a_draw, const View::Snapshot& a_view, const View::Member* a_selected, V a_display, float a_s, float a_appear)
		{
			const float alpha = Stage(a_appear, 0.1f, 0.6f) * state.tw.Toward("overheadIn", 0, a_view.overhead ? 1.0f : 0.0f, 6.0f);
			if (alpha <= 0.01f) {
				return;
			}
			Draw::AddRectFilled(a_draw, { 0.0f, 0.0f }, a_display, Alpha(kShade, alpha * 0.3f), 0.0f, 0);
			const float pulse = 0.5f + 0.5f * std::sin(state.clock * 3.2f);
			for (const auto& enemy : a_view.enemies) {
				const bool marked = a_selected && a_selected->marked == enemy.id;
				GroundMark(a_draw, enemy.footRing, a_display, marked ? kText : kFoe, alpha, 0.24f, (enemy.boss ? 3.0f : 2.2f) * a_s + (marked ? 1.2f * pulse * a_s : 0.0f));
			}
			for (const auto& member : a_view.party) {
				const bool chosen = a_selected && member.id == a_selected->id;
				GroundMark(a_draw, member.footRing, a_display, kAlly, alpha * (member.summon ? 0.7f : 1.0f), chosen ? 0.36f : 0.2f,
					(chosen ? 2.6f + 0.9f * pulse : 2.0f) * a_s);
				if (member.down && member.foot.on) {
					Draw::AddCircle(a_draw, ScreenOf(member.foot, a_display), (16.0f + 10.0f * pulse) * a_s, Alpha(kAmber, alpha), 32, 2.0f * a_s);
				}
			}
			GroundMark(a_draw, a_view.playerRing, a_display, kText, alpha, 0.26f, 2.4f * a_s);
			if (a_view.playerHead.on) {
				TextOnWorld(a_draw, ScreenOf(a_view.playerHead, a_display), kText, 14.0f * a_s, "YOU", alpha);
			}
		}

		std::string PadButtonName(std::uint32_t a_button)
		{
			switch (a_button) {
			case 0x80:
				return "R3";
			case 0x40:
				return "L3";
			case 0x20:
				return "Back";
			default:
				return {};
			}
		}

		// Top left: the companion, or the whole fight from above. Two segments, the one shown lit.
		Rect DrawViewSwitch(Im::ImDrawList* a_draw, const View::Snapshot& a_view, float a_s, float a_appear, bool& a_clicked)
		{
			a_clicked = false;
			const float in = Stage(a_appear, 0.15f, 0.5f) * (a_view.driving ? 1.0f : 0.45f);
			const bool  overhead = state.overhead && a_view.driving;
			const float h = 38.0f * a_s, size = 13.5f * a_s, tracking = 2.0f * a_s, pad = 14.0f * a_s, icon = 15.0f * a_s;
			static constexpr std::array<std::pair<std::string_view, std::uint32_t>, 2> items{ { { "COMPANION", 0xF007 }, { "OVERHEAD", 0xF279 } } };
			std::array<float, 2> widths{};
			float                total = 0.0f;
			for (std::size_t i = 0; i < items.size(); ++i) {
				widths[i] = pad + icon + 9.0f * a_s + LabelWidth(size, items[i].first, tracking) + pad;
				total += widths[i];
			}
			const V at{ 40.0f * a_s, 30.0f * a_s - (1.0f - Stage(a_appear, 0.15f, 0.5f)) * 8.0f * a_s };
			Glass(a_draw, at, { at.x + total, at.y + h }, in, 0.0f);
			float x = at.x;
			for (std::size_t i = 0; i < items.size(); ++i) {
				const V    min{ x, at.y }, max{ x + widths[i], at.y + h };
				const bool shown = (i == 1) == overhead;
				bool       hovered = false;
				Im::PushID(static_cast<int>(i));
				if (Hit("view", min, { widths[i], h }, hovered) && a_view.driving && !state.closing && !shown) {
					a_clicked = true;
				}
				Im::PopID();
				const float lit = state.tw.Toward("viewSeg", i, shown ? 1.0f : 0.0f, 12.0f);
				Lit(a_draw, min, max, in * lit);
				if (hovered && !shown) {
					Draw::AddRectFilled(a_draw, min, max, Alpha(kTileHover, in), 0.0f, 0);
				}
				const U32 color = Alpha(Mix(kTextDim, kText, std::max(lit, hovered ? 0.6f : 0.0f)), in);
				Glyph(a_draw, { x + pad + icon * 0.5f, at.y + h * 0.5f }, icon, items[i].second, color);
				Label(a_draw, { x + pad + icon + 9.0f * a_s, at.y + (h - size) * 0.5f }, color, size, items[i].first, tracking);
				if (i == 1) {
					Draw::AddLine(a_draw, { x, at.y + 7.0f * a_s }, { x, at.y + h - 7.0f * a_s }, Alpha(kGlassEdge, in), 1.0f);
				}
				if (hovered) {
					Hovering(0x700 + i);
					if (!a_view.driving) {
						Tip("The overhead view moves the camera, so it needs Glide to the companion on (Battle plan settings, Camera).");
					} else if (i == 1) {
						Tip("The whole fight from above: blue rings for the party, red for enemies, white for you. Indoors the ceiling is cut away "
							"above everyone's heads. Click an enemy to send someone after them.");
					} else {
						Tip("The camera on the companion you are commanding.");
					}
				}
				x += widths[i];
			}
			// The key that switches, under the switch.
			const auto& settings = Settings::Get();
			const auto  key = padUsed.load() ? PadButtonName(settings.overheadGamepadKey) : (settings.overheadKey.key ? HotkeyName(settings.overheadKey) : ""s);
			float       bottom = at.y + h;
			if (!key.empty() && a_view.driving) {
				const float capH = 22.0f * a_s;
				const float w = Keycap(a_draw, { at.x, at.y + h + 8.0f * a_s }, capH, 12.5f * a_s, key, in);
				Text(a_draw, { at.x + w + 8.0f * a_s, at.y + h + 8.0f * a_s + (capH - TextSize(13.5f * a_s, "switches").y) * 0.5f }, Alpha(kTextDim, in), 13.5f * a_s,
					"switches");
				bottom += 8.0f * a_s + capH;
			}
			return { at, { at.x + total, bottom } };
		}

		// Top right, across from the view switch: auto battle. On, a companion who sees an enemy
		// starts the fight; off, they wait for it to come to them. Last of the party orders row
		// for the pad.
		Rect DrawAutoBattle(Im::ImDrawList* a_draw, const View::Snapshot& a_view, V a_display, float a_s, float a_appear, const Input& a_input)
		{
			const bool live = !state.closing;
			const bool focused = state.keyboardNav && live && !state.spells.member && state.section == kOrdersSection &&
			                     state.item == static_cast<int>(orderShouts.size());
			if (state.autoAsked && (*state.autoAsked == a_view.autoBattle || state.clock - state.autoAskedAt > 1.5f)) {
				state.autoAsked.reset();
			}
			const bool  on = state.autoAsked.value_or(a_view.autoBattle);
			const float in = Stage(a_appear, 0.15f, 0.5f);
			const float h = 38.0f * a_s, size = 13.5f * a_s, tracking = 2.0f * a_s, pad = 14.0f * a_s, icon = 15.0f * a_s;
			const float trackW = 30.0f * a_s, trackH = 16.0f * a_s;
			constexpr std::string_view name = "AUTO BATTLE";
			const float total = pad + icon + 9.0f * a_s + LabelWidth(size, name, tracking) + 14.0f * a_s + trackW + pad;
			const V     at{ a_display.x - 40.0f * a_s - total, 30.0f * a_s - (1.0f - in) * 8.0f * a_s };
			const V     max{ at.x + total, at.y + h };

			bool hovered = false;
			if (Hit("autoBattle", at, { total, h }, hovered) && live) {
				state.autoAsked = !on;
			} else if (focused && a_input.activate) {
				state.autoAsked = !on;
			}
			if (state.autoAsked && *state.autoAsked != on) {
				state.autoAskedAt = state.clock;
				Battle::Command command;
				command.type = Battle::Command::Type::kAutoBattle;
				command.on = *state.autoAsked;
				Battle::Queue(command);
				Sound("UIMenuOK");
			}
			const bool  shown = state.autoAsked.value_or(on);
			const float lit = state.tw.Toward("autoBattle", 0, shown ? 1.0f : 0.0f, 12.0f);
			const float hover = state.tw.Toward("autoBattleHover", 0, hovered || focused ? 1.0f : 0.0f, 16.0f);

			Glass(a_draw, at, max, in, 0.0f);
			Lit(a_draw, at, max, in * lit);
			if (hover > 0.01f) {
				Draw::AddRectFilled(a_draw, at, max, Alpha(kTileHover, in * hover * (1.0f - lit)), 0.0f, 0);
			}
			if (focused) {
				FocusOutline(a_draw, at, max, 0.0f, in);
			}
			const U32 color = Alpha(Mix(kTextDim, kText, std::max(lit, hover * 0.6f)), in);
			Glyph(a_draw, { at.x + pad + icon * 0.5f, at.y + h * 0.5f }, icon, 0xF0E7, color);
			Label(a_draw, { at.x + pad + icon + 9.0f * a_s, at.y + (h - size) * 0.5f }, color, size, name, tracking);
			// The switch: a track, its knob sliding over and lighting up.
			const V tmin{ max.x - pad - trackW, at.y + (h - trackH) * 0.5f };
			const V tmax{ tmin.x + trackW, tmin.y + trackH };
			Draw::AddRectFilled(a_draw, tmin, tmax, Alpha(Mix(kTile, kAccent, lit), in * (0.6f + 0.4f * lit)), trackH * 0.5f, 0);
			Draw::AddRect(a_draw, tmin, tmax, Alpha(Mix(kGlassEdge, kEdge, lit), in), trackH * 0.5f, 0, 1.0f);
			const float knobR = trackH * 0.5f - 3.0f * a_s;
			const V     knob{ tmin.x + trackH * 0.5f + lit * (trackW - trackH), tmin.y + trackH * 0.5f };
			Draw::AddCircleFilled(a_draw, knob, knobR, Alpha(Mix(kTextDim, kText, lit), in), 16);

			// What it means right now, under the switch.
			const std::string_view line = shown ? "they start the fight"sv : "they wait to be attacked"sv;
			const float            lineW = TextSize(13.5f * a_s, line).x;
			Text(a_draw, { max.x - lineW, max.y + 8.0f * a_s }, Alpha(shown ? kText : kTextDim, in), 13.5f * a_s, line);

			if (hovered || focused) {
				if (hovered) {
					Hovering(0x720);
				}
				auto tip = std::format("Auto battle: {}\nOn: a companion who sees an enemy starts the fight. Those on Hold back wait, and nobody starts one while you sneak.\n"
									   "Off: they wait until an enemy attacks, or you do.{}",
					shown ? "on" : "off", a_view.drawOnSight ? "\nEither way, weapons come out when an enemy is in view." : "");
				if (hovered) {
					Tip(std::move(tip));
				} else {
					TipBeside(std::move(tip), at, max);
				}
			}
			return { at, { max.x, max.y + 8.0f * a_s + 18.0f * a_s } };
		}

		// --- Orders answered -------------------------------------------------------------------

		// Corner brackets around a point: a target lock.
		void Brackets(Im::ImDrawList* a_draw, V a_c, float a_r, U32 a_color, float a_thickness)
		{
			const float l = a_r * 0.5f;
			for (const float sx : { -1.0f, 1.0f }) {
				for (const float sy : { -1.0f, 1.0f }) {
					const V corner{ a_c.x + sx * a_r, a_c.y + sy * a_r };
					Draw::AddLine(a_draw, corner, { corner.x - sx * l, corner.y }, a_color, a_thickness);
					Draw::AddLine(a_draw, corner, { corner.x, corner.y - sy * l }, a_color, a_thickness);
				}
			}
		}

		// The answer to sending someone: a spark runs from the companion to the enemy, brackets
		// lock on with a shockwave, and a line under the enemy says who was sent. When a companion
		// actually turns on their mark the brackets go red with a flash. A marked enemy keeps its
		// brackets, white while the companion is on the way.
		void DrawSends(Im::ImDrawList* a_draw, const View::Snapshot& a_view, V a_display, float a_s, float a_appear)
		{
			const float alpha = Stage(a_appear, 0.1f, 0.6f);
			auto        findEnemy = [&](RE::FormID a_id) -> const View::Enemy* {
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

			// Who took up their mark since the last frame.
			for (const auto& m : a_view.party) {
				const bool on = m.marked && m.target == m.marked;
				auto&      was = state.wasOn[m.id];
				if (on && was != m.marked) {
					state.engaged.push_back({ m.marked, m.id, state.clock });
				}
				was = on ? m.marked : 0;
			}

			// Every marked enemy: brackets, red once someone is fighting them.
			for (const auto& e : a_view.enemies) {
				bool marked = false, fought = false;
				for (const auto& m : a_view.party) {
					if (m.marked == e.id) {
						marked = true;
						fought = fought || m.target == e.id;
					}
				}
				if (!marked || !e.head.on) {
					continue;
				}
				const float red = state.tw.Toward("lockRed", e.id, fought ? 1.0f : 0.0f, 8.0f);
				const float breathe = 0.85f + 0.15f * std::sin(state.clock * 4.0f);
				Brackets(a_draw, ScreenOf(e.head, a_display), 25.0f * a_s, Alpha(Mix(kText, kRed, red), alpha * breathe), 2.0f * a_s);
			}

			// Fresh orders.
			std::erase_if(state.sent, [](const State::Beat& a_beat) { return state.clock - a_beat.at > 1.9f; });
			std::unordered_set<RE::FormID> labelled;
			for (const auto& beat : state.sent) {
				const auto* enemy = findEnemy(beat.enemy);
				if (!enemy || !enemy->head.on) {
					continue;
				}
				const float t = state.clock - beat.at;
				const V     p = ScreenOf(enemy->head, a_display);
				// The companion hears it, and a spark runs to the enemy.
				if (const auto* m = findMember(beat.member); m && m->head.on && t < 0.5f) {
					const V     from = ScreenOf(m->head, a_display);
					const float heard = Clamp01(t / 0.45f);
					Draw::AddCircle(a_draw, from, (12.0f + 26.0f * EaseOut(heard)) * a_s, Alpha(kText, alpha * (1.0f - heard)), 32, 2.0f * a_s);
					const float run = EaseOut(t / 0.36f);
					const float gone = 1.0f - Clamp01((t - 0.36f) / 0.1f);
					for (int i = 0; i < 7; ++i) {
						const float k = std::max(run - 0.045f * static_cast<float>(i), 0.0f);
						const V     at{ from.x + (p.x - from.x) * k, from.y + (p.y - from.y) * k };
						Draw::AddCircleFilled(a_draw, at, (5.5f - 0.65f * static_cast<float>(i)) * a_s, Alpha(kText, alpha * gone * (1.0f - static_cast<float>(i) / 7.0f)), 16);
					}
				}
				// Lock on as the spark lands: brackets snap in, two rings run out.
				const float lt = t - 0.3f;
				if (lt < 0.0f) {
					continue;
				}
				const float snap = EaseOutBack(Clamp01(lt / 0.3f));
				const float fade = 1.0f - Clamp01((lt - 1.0f) / 0.5f);
				Brackets(a_draw, p, (25.0f + 46.0f * (1.0f - snap)) * a_s, Alpha(kText, alpha * fade), 2.8f * a_s);
				for (int ring = 0; ring < 2; ++ring) {
					const float w = Clamp01((lt - 0.1f * static_cast<float>(ring)) / 0.6f);
					if (w > 0.0f && w < 1.0f) {
						Draw::AddCircle(a_draw, p, (18.0f + 84.0f * EaseOut(w)) * a_s, Alpha(ring == 0 ? kText : kEdge, alpha * (1.0f - w)), 48, (3.0f - 2.0f * w) * a_s);
					}
				}
				// Who was sent, once per enemy: everyone sent at once reads as one order.
				if (!labelled.insert(beat.enemy).second) {
					continue;
				}
				int         count = 0;
				std::string who;
				for (const auto& other : state.sent) {
					if (other.enemy == beat.enemy && std::fabs(other.at - beat.at) < 0.05f) {
						++count;
						if (const auto* m = findMember(other.member)) {
							who = m->name;
						}
					}
				}
				const auto  line = count > 1 ? "EVERYONE SENT"s : Upper(who) + " SENT";
				const float show = Clamp01(lt / 0.15f) * fade;
				TextOnWorld(a_draw, { p.x, p.y + 54.0f * a_s - (1.0f - Clamp01(lt / 0.25f)) * 6.0f * a_s }, kText, 15.0f * a_s, line, alpha * show);
			}

			// Companions turning on their mark.
			std::erase_if(state.engaged, [](const State::Beat& a_beat) { return state.clock - a_beat.at > 1.4f; });
			for (const auto& beat : state.engaged) {
				const auto* enemy = findEnemy(beat.enemy);
				const auto* m = findMember(beat.member);
				if (!enemy || !enemy->head.on || !m) {
					continue;
				}
				const V     p = ScreenOf(enemy->head, a_display);
				const float t = state.clock - beat.at;
				const float w = Clamp01(t / 0.55f);
				Draw::AddCircle(a_draw, p, (20.0f + 60.0f * EaseOut(w)) * a_s, Alpha(kRed, alpha * (1.0f - w)), 48, 3.0f * a_s);
				const float show = Clamp01(t / 0.15f) * (1.0f - Clamp01((t - 0.9f) / 0.5f));
				TextOnWorld(a_draw, { p.x, p.y + 74.0f * a_s }, kRed, 15.0f * a_s, Upper(m->name) + " ENGAGES", alpha * show);
			}
		}

		// --- Sending someone at an enemy ----------------------------------------------------------

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

			// Asked through the function, never io.MouseClicked: that field sits past the key table,
			// whose size differs between the header and Menu Framework's ImGui, so in game it read
			// as never pressed and the list could neither send nor close.
			const V    mouse = Im::GetIO()->MousePos;
			const bool press = Im::IsMouseClicked(0);
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
					bool        sending = false;
					for (const auto& m : a_view.party) {
						if (m.summon) {
							continue;
						}
						if (item.action == 0 && a_selected && m.id == a_selected->id) {
							sending = m.marked != enemy->id;
							SendAfter(m.id, sending ? enemy->id : 0);
						} else if (item.action == 1) {
							sending = true;
							SendAfter(m.id, enemy->id);
						} else if (item.action == 2 && m.marked == enemy->id) {
							SendAfter(m.id, 0);
						}
					}
					Sound(sending ? "UIItemMeleeUp" : "UIMenuCancel");
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

		constexpr std::array<Hint, 7> padHints{ { { "LB / RB", "Companion" }, { "D-pad", "Move" }, { "A", "Choose" }, { "X", "Ability now" }, { "RS", "Turn view" },
			{ "LT / RT", "Zoom" }, { "B", "Close" } } };
		constexpr std::array<Hint, 7> keyHints{ { { "Click", "Send at an enemy" }, { "Tab", "Companion" }, { "Arrows", "Move" }, { "Enter", "Choose" },
			{ "Right-drag", "Turn view" }, { "Wheel", "Zoom" }, { "Esc", "Close" } } };

		// Each key in its own small cap, what it does beside it.
		void DrawHints(Im::ImDrawList* a_draw, V a_display, float a_s, float a_alpha)
		{
			// Overhead adds how to move about, before Close.
			std::vector<Hint> hints(padUsed.load() ? padHints.begin() : keyHints.begin(), padUsed.load() ? padHints.end() : keyHints.end());
			if (state.page.member && state.guide.open) {
				hints = padUsed.load() ? std::vector<Hint>{ { "A", "Next" }, { "D-pad", "Back / Next" }, { "B", "Close guide" } } :
				                         std::vector<Hint>{ { "Enter", "Next" }, { "Left / Right", "Back / Next" }, { "Esc", "Close guide" } };
			} else if (state.page.member) {
				hints = padUsed.load() ? std::vector<Hint>{ { "D-pad", "Choose" }, { "A", "Learn" }, { "X", "Carry" }, { "Y", "Give back" }, { "LT / RT", "Path" }, { "LB / RB", "Companion" }, { "View", "Guide" }, { "B", "Back" } } :
				                         std::vector<Hint>{ { "Click", "Choose" }, { "Enter", "Learn" }, { "1 / 2", "Carry" }, { "Right-click", "Give back" }, { "Q / E", "Path" }, { "Tab", "Companion" }, { "F1", "Guide" }, { "Esc", "Back" } };
			} else 			if (state.spells.member) {
				hints = padUsed.load() ? std::vector<Hint>{ { "Up / Down", "Spell" }, { "Left / Right", "School" }, { "A", "Choose" }, { "B", "Back" } } :
				                         std::vector<Hint>{ { "Click", "Choose" }, { "Wheel", "Scroll" }, { "Left / Right", "School" }, { "Esc", "Back" } };
			} else if (state.overhead) {
				hints.insert(hints.end() - 1, padUsed.load() ? Hint{ "LS", "Move" } : Hint{ "Mid-drag / WASD", "Move" });
			}
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

		int ItemCount(int a_section, const View::Member* a_member, std::size_t a_enemies, bool a_partyView)
		{
			switch (a_section) {
			case kSpellSection:
				return a_member && a_partyView && ChoosesSpells(a_member->orders.role) ? 2 : 0;
			case kRoleSection:
				return a_partyView ? static_cast<int>(Role::kCount) : 0;  // roles are set out of a fight
			case kFocusSection:
			case kPositionSection:
				return a_member ? 1 : 0;  // one value, stepped with left and right
			case kAbilitySection:
				return a_member ? AbilityRowsOf(*a_member).Count() : 0;
			case kFieldSection:
				return static_cast<int>(a_enemies);
			case kOrdersSection:
				return static_cast<int>(orderShouts.size()) + 1;  // + the auto battle switch
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
			SelectTheme();
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
				ClosePathPage();
				padDx = padDy = padShoulder = 0;
				padA = padB = padX = padY = false;
				padView = false;
				stickX = stickY = triggerL = triggerR = leftX = leftY = 0.0f;
				overheadToggle = false;
				Camera::SetOverhead(state.overhead);
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
					// The spell list, while open, takes the pad and the arrows, and B and Esc close it
					// rather than the plan.
					Input listInput;
					int   listDy = 0;
					bool  listBack = spellListBack.exchange(false);
					bool  closePlan = cancel;
					if (state.spells.member && !state.closing) {
						listInput = input;
						listDy = dy;
						listBack = listBack || cancel;
						input = {};
						dy = 0;
						closePlan = false;
					}
					// The path page, likewise: it takes the pad and the arrows, B and Esc close it, and
					// the triggers (or Q and E) turn to the next path.
					Input pageInput;
					int   pageDy = 0;
					int   pageTab = 0;
					// The guide, over the page, takes the keys while it is open: Enter, A and Right go on,
					// Left goes back, Esc and B close it. F1 and View open and close it.
					int        guideMove = 0;
					bool       guideClose = false;
					const bool guideKey = padView.exchange(false) || Im::IsKeyPressed(Im::ImGuiKey_F1, false);
					if (!state.page.member || state.closing) {
						state.guide.open = false;
					}
					const bool pageBack = pathPageBack.exchange(false) || (state.page.member && cancel);
					if (pageBack && state.guide.open) {
						guideClose = true;
						closePlan = false;
					} else if (pageBack) {
						if (state.page.member) {
							ClosePathPage();
							Sound("UIMenuCancel");
						}
						closePlan = false;
					}
					if (guideKey && state.page.member && !state.closing) {
						if (state.guide.open) {
							guideClose = true;
						} else {
							OpenGuide();
							Sound("UIMenuOK");
						}
					}
					if (state.page.member && !state.closing) {
						pageInput = input;
						pageDy = dy;
						input = {};
						dy = 0;
						closePlan = false;
						const float pull = triggerR.load() - triggerL.load();
						if (!state.page.triggerDown && std::fabs(pull) > 0.6f) {
							state.page.triggerDown = true;
							pageTab = pull > 0.0f ? 1 : -1;
						} else if (std::fabs(pull) < 0.3f) {
							state.page.triggerDown = false;
						}
						pageTab += static_cast<int>(Im::IsKeyPressed(Im::ImGuiKey_E, false)) - static_cast<int>(Im::IsKeyPressed(Im::ImGuiKey_Q, false));
					}
					if (state.guide.open) {
						guideMove = std::clamp(pageInput.dx + (pageInput.activate ? 1 : 0), -1, 1);
						pageInput = {};
						pageDy = 0;
						pageTab = 0;
						shoulder = 0;
					}
					hitsBlocked = state.guide.open;
					if (closePlan && !state.closing) {
						open = false;
					}
					if (shoulder) {
						StepMember(*view, shoulder);
					}

					// Turning the view: the right stick, or dragging with the right mouse button. The
					// wheel and the triggers zoom.
					if (view->driving && !state.closing) {
						const float speed = Settings::Get().orbitSpeed;
						auto        live = [](float a_value) {
							const float rest = 0.15f;
							return std::fabs(a_value) < rest ? 0.0f : (a_value - std::copysign(rest, a_value)) / (1.0f - rest);
						};
						float yaw = live(stickX.load()) * 2.4f * speed * dt;
						float pitch = live(stickY.load()) * 1.5f * speed * dt;
						if (Im::IsMouseDown(1)) {
							yaw += io->MouseDelta.x * 0.0055f * speed;
							pitch -= io->MouseDelta.y * 0.0055f * speed;
						}
						float zoomSteps = state.page.member ? 0.0f : (triggerR.load() - triggerL.load()) * 5.0f * dt;
						if (!state.enemyMenu && !state.spells.member && !state.page.member) {
							zoomSteps += io->MouseWheel;
						}
						if (yaw != 0.0f || pitch != 0.0f) {
							Camera::Turn(yaw, pitch);
						}
						if (zoomSteps != 0.0f) {
							Camera::Zoom(zoomSteps);
						}

						// Overhead, free move: drag with the middle button (the ground follows the
						// pointer), or the left stick or WASD. A middle click without a drag brings the
						// view back over the fight.
						if (state.overhead) {
							float right = live(leftX.load()) * 0.9f * dt, forward = live(leftY.load()) * 0.9f * dt;
							right += (static_cast<float>(Im::IsKeyDown(Im::ImGuiKey_D)) - static_cast<float>(Im::IsKeyDown(Im::ImGuiKey_A))) * 0.9f * dt;
							forward += (static_cast<float>(Im::IsKeyDown(Im::ImGuiKey_W)) - static_cast<float>(Im::IsKeyDown(Im::ImGuiKey_S))) * 0.9f * dt;
							if (Im::IsMouseClicked(2)) {
								state.panDragged = 0.0f;
							}
							if (Im::IsMouseDown(2)) {
								right -= io->MouseDelta.x / std::max(display.y, 1.0f);
								forward += io->MouseDelta.y / std::max(display.y, 1.0f);
								state.panDragged += std::fabs(io->MouseDelta.x) + std::fabs(io->MouseDelta.y);
							}
							if (Im::IsMouseReleased(2) && state.panDragged < 6.0f) {
								Camera::Recentre();
								Sound("UIMenuFocus");
							}
							if (right != 0.0f || forward != 0.0f) {
								Camera::Pan(right, forward);
							}
						}
					}
					bool switchView = overheadToggle.exchange(false) && !state.closing;

					const auto* member = Selected(*view);
					// No enemies about: the party view, the camera face to face with the companion.
					const bool partyView = view->enemies.empty() && !view->inCombat;
					Camera::Focus(member && !state.closing ? member->id : 0, partyView);
					// A fight locks the role tiles; the pad starts on, or moves down to, the focus.
					if (!partyView && state.section == kRoleSection) {
						state.section = kFocusSection;
						state.item = 0;
					}

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
					// Abilities: down from a badge to its AUTO, on to the path tile and the healer's
					// line; up the same way, back to the badge last chosen.
					if (dy && state.section == kAbilitySection && member) {
						const auto rows = AbilityRowsOf(*member);
						const int  item = state.item;
						const int  up = rows.columns > 0 ? std::min(state.abilityColumn, rows.columns - 1) * 2 + 1 : -1;
						int        to = -1;
						if (dy > 0) {
							to = rows.Badge(item) ? item + 1 : (rows.Auto(item) ? (rows.path >= 0 ? rows.path : rows.slider) : (item == rows.path ? rows.slider : -1));
						} else {
							to = rows.Auto(item) ? item - 1 : (item == rows.path ? up : (item == rows.slider ? (rows.path >= 0 ? rows.path : up) : -1));
						}
						if (to >= 0) {
							state.item = to;
							dy = 0;
						}
					}
					if (dy) {
						int next = state.section;
						for (int tries = 0; tries < kSectionCount; ++tries) {
							next = std::clamp(next + (dy > 0 ? 1 : -1), 0, kSectionCount - 1);
							if (ItemCount(next, member, view->enemies.size(), partyView) > 0) {
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
						// The abilities from below: their bottom row, the healer's line or the path tile.
						if (next == kAbilitySection && dy < 0 && member) {
							const auto rows = AbilityRowsOf(*member);
							state.item = rows.slider >= 0 ? rows.slider : (rows.path >= 0 ? rows.path : std::max(rows.columns * 2 - 1, 0));
						}
					}
					const int count = ItemCount(state.section, member, view->enemies.size(), partyView);
					// Left and right walk the items of a row; on the focus, the position and the
					// healer's slider they change the value instead.
					const bool cycles = state.section == kFieldSection || state.section == kOrdersSection || state.section == kSpellSection;
					bool       moved = cycles && input.dx != 0 && count > 0;
					if (moved) {
						state.item = (state.item + input.dx + count) % count;
					}
					// Abilities: left and right go round the badges, or round the AUTO switches under
					// them; the path tile has nothing beside it, and the healer's line moves.
					if (state.section == kAbilitySection && member && input.dx != 0) {
						const auto rows = AbilityRowsOf(*member);
						if ((rows.Badge(state.item) || rows.Auto(state.item)) && rows.columns > 0) {
							state.item = ((state.item / 2 + input.dx + rows.columns) % rows.columns) * 2 + state.item % 2;
							moved = true;
						} else if (state.item == rows.path) {
							moved = true;
						}
					}
					// Across the role tiles, left and right stay on their row.
					if (state.section == kRoleSection && input.dx != 0) {
						state.item = (state.item / 3) * 3 + std::clamp(state.item % 3 + input.dx, 0, 2);
						moved = true;
					}
					state.item = count > 0 ? std::clamp(state.item, 0, count - 1) : 0;
					if (state.section == kAbilitySection && member && state.item < AbilityRowsOf(*member).columns * 2) {
						state.abilityColumn = state.item / 2;
					}
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
						const bool sending = member->marked != state.fieldEnemy;
						SendAfter(member->id, sending ? state.fieldEnemy : 0);
						Sound(sending ? "UIItemMeleeUp" : "UIMenuCancel");
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
						Draw::AddRectFilledMultiColor(draw, { cx - bandW, top }, { cx, bottom }, Alpha(kAccent, 0.0f), Alpha(kAccent, titleIn * (palette.Subdued() ? 0.18f : 0.85f)), Alpha(kAccent, titleIn * (palette.Subdued() ? 0.18f : 0.85f)),
							Alpha(kAccent, 0.0f));
						Draw::AddRectFilledMultiColor(draw, { cx, top }, { cx + bandW, bottom }, Alpha(kAccent, titleIn * (palette.Subdued() ? 0.18f : 0.85f)), Alpha(kAccent, 0.0f), Alpha(kAccent, 0.0f),
							Alpha(kAccent, titleIn * (palette.Subdued() ? 0.18f : 0.85f)));
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
					// A companion's path: the page follows LB and RB to whoever is chosen, and closes
					// if the add-on is not there for them.
					if (state.page.member && (!member || !member->paths)) {
						ClosePathPage();
					}
					if (state.page.member && member && state.page.member != member->id) {
						state.page.member = member->id;
						state.page.seenMember = 0;
					}
					const bool pageOpen = state.page.member != 0 && member;
					const float pageIn = state.tw.Toward("pathPage", 0, pageOpen ? 1.0f : 0.0f, 10.0f);
					if (!pageOpen) {
						panels.push_back(DrawOrders(draw, *view, { display.x * 0.5f, ruleY + 46.0f * s }, s, appear * (1.0f - pageIn), input));
					}
					panels.push_back(DrawStrip(draw, *view, member, display, s, appear));
					if (pageOpen) {
						CloseSpellList();
						panels.push_back(DrawPathPage(draw, *member, view->tuning, { 40.0f * s, ruleY + 40.0f * s }, { display.x - 40.0f * s, display.y - 240.0f * s }, s,
							appear, pageInput, pageDy, pageTab));
						if (state.guide.open) {
							DrawGuide(draw, *member, display, s, appear, guideMove, guideClose);
						}
					}
					if (member && !pageOpen) {
						const float width = 440.0f * s;
						panels.push_back(DrawCompanion(draw, *view, *member, { display.x - 40.0f * s - width, 112.0f * s }, width, s, appear, cardInput));
						const float sheetIn = state.tw.Toward("partyView", 0, partyView ? 1.0f : 0.0f, 8.0f);
						if (sheetIn > 0.01f) {
							const Rect sheet = DrawSheet(draw, *member, { 40.0f * s, 112.0f * s }, 380.0f * s, s, appear * sheetIn, cardInput);
							panels.push_back(sheet);
							// Spells are chosen out of a fight only, for whoever the sheet is showing.
							if (state.spells.member && (state.spells.member != member->id || !partyView || !ChoosesSpells(member->orders.role))) {
								CloseSpellList();
							}
							if (state.spells.member && sheetIn > 0.5f) {
								const float top = 204.0f * s;  // under the party orders
								panels.push_back(DrawSpellList(draw, *member, { sheet.max.x + 4.0f * s, top }, 420.0f * s, display.y - top - 190.0f * s, s,
									appear * sheetIn, listInput, listDy, listBack, sheet));
							}
						} else {
							CloseSpellList();
						}
					}
					bool clickedView = false;
					panels.push_back(DrawViewSwitch(draw, *view, s, appear, clickedView));
					panels.push_back(DrawAutoBattle(draw, *view, display, s, appear, input));
					switchView = switchView || clickedView;
					if (switchView) {
						if (view->driving) {
							state.overhead = !state.overhead;
							Camera::SetOverhead(state.overhead);
							Sound("UIMenuOK");
						} else {
							Notices::Push("The overhead view needs Glide to the companion (Battle plan settings).", Notices::Kind::kWarning);
						}
					}
					DrawHints(draw, display, s, Stage(appear, 0.35f, 0.5f));
					if (state.keyboardNav) {
						Hovering(0x1000 + static_cast<std::uint64_t>(state.section) * 64 + static_cast<std::uint64_t>(state.item));
					}

					DrawOverhead(back, *view, member, display, s, appear);
					// Face to face, the markers over heads would only be in the way.
					if (!partyView) {
						DrawWorld(back, *view, member, display, s, appear, panels);
						DrawSends(back, *view, display, s, appear);
						DrawEnemyMenu(draw, *view, member, display, s);
					}
				}
			}
			Im::End();
			Im::PopStyleColor(2);
			Im::PopStyleVar(2);
			DrawTip(display, s);
			spellListOpen = state.spells.member != 0;
			pathPageOpen = state.page.member != 0;

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

		bool planWindowWasOpen{ false };

		void __stdcall RenderHUD()
		{
			SelectTheme();
			// Menu Framework may close the window on its own (its toggle key, a load).
			if (plan && !plan->IsOpen && Battle::PlanOpen()) {
				Battle::RequestClose();
			}
			// Once the window is gone, the freeze and blur the plan kept off go back to whoever
			// still wants them.
			const bool windowOpen = plan && plan->IsOpen;
			if (planWindowWasOpen && !windowOpen) {
				Battle::PlanWindowClosed(MF::IsAnyBlockingWindowOpened());
			}
			planWindowWasOpen = windowOpen;
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
				SelectTheme();
				Im::PushStyleColor(Im::ImGuiCol_Text, kText);
				Im::PushStyleColor(Im::ImGuiCol_TextDisabled, kTextFaint);
				Im::PushStyleColor(Im::ImGuiCol_WindowBg, kInk);
				Im::PushStyleColor(Im::ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
				Im::PushStyleColor(Im::ImGuiCol_FrameBg, kPanel);
				Im::PushStyleColor(Im::ImGuiCol_FrameBgHovered, palette.frameHover);
				Im::PushStyleColor(Im::ImGuiCol_FrameBgActive, palette.frameActive);
				Im::PushStyleColor(Im::ImGuiCol_Button, kPanel);
				Im::PushStyleColor(Im::ImGuiCol_ButtonHovered, palette.buttonHover);
				Im::PushStyleColor(Im::ImGuiCol_ButtonActive, palette.buttonActive);
				Im::PushStyleColor(Im::ImGuiCol_CheckMark, kEdge);
				Im::PushStyleColor(Im::ImGuiCol_SliderGrab, kEdge);
				Im::PushStyleColor(Im::ImGuiCol_SliderGrabActive, palette.sliderActive);
				Im::PushStyleColor(Im::ImGuiCol_Header, palette.header);
				Im::PushStyleColor(Im::ImGuiCol_HeaderHovered, palette.headerHover);
				Im::PushStyleColor(Im::ImGuiCol_HeaderActive, palette.buttonActive);
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
		std::atomic<int>           captured{ -1 };  // -1 nothing yet, -2 cancelled, else key | modifier << 16 (0 clears)
		std::atomic<std::uint32_t> captureModifier{ 0 };  // Shift, Ctrl or Alt held while binding, for the button
		int                        captureSlot{ 0 };  // 0 the battle plan, 1 to 4 the party orders, 5 the overhead view
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
			captureModifier = 0;
			captureStarted = ::GetTickCount64();
			capturing = true;
		}

		void Prepare()
		{
			if (!haveDraft) {
				draft = Settings::Chosen();
				haveDraft = true;
			}
			// Nothing pending: follow the player's settings, which a save or an INI edit may have moved.
			if (!dirty && Settings::AppliedRevision() == Settings::RequestedRevision() && !capturing.load()) {
				draft = Settings::Chosen();
				feedback = Settings::LastSaveFailed() ? "Could not save FightWithMe.ini. Check file access, then change the setting again." : "";
			}
			const int key = captured.exchange(-1);
			if (capturing.load() && key != -1) {
				if (key >= 0) {
					auto& target = captureSlot == 0 ? draft.planKey :
					               captureSlot == 5 ? draft.overheadKey :
					                                  draft.orderKeys[static_cast<std::size_t>(captureSlot - 1)];
					const auto code = static_cast<std::uint32_t>(key);
					target = Normalized({ code & 0xFFFF, code >> 16 });
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
				// Roles are settled out of a fight, here as in the battle plan.
				const bool fight = a_view.inCombat || !a_view.enemies.empty();
				Im::BeginDisabled(fight);
				RosterCombo("##role", role, static_cast<int>(Role::kCount), RoleName,
					[&](int i) { return m.arsenalKnown ? Why(LackFor(static_cast<Role>(i), m.arsenal)) : std::string_view{}; }, changed);
				Im::EndDisabled();
				Help(fight ? "Roles are set out of a fight." : roles[static_cast<std::size_t>(role)].summary.data());
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
			if (Group("Before a fight", 0xF06E)) {
				if (Checks("sight")) {
					Check("Weapons out when an enemy is in view", draft.drawOnSight,
						"Out of a fight, an enemy that a companion or you can see brings their weapons out. They put them away a few seconds after the last one is "
						"out of view, unless yours are out.");
					Check("Auto battle", draft.autoBattle,
						"A companion who sees an enemy starts the fight instead of waiting to be attacked. Companions on Hold back wait, and nobody starts one "
						"while you sneak. Also a switch at the top right of the battle plan.");
					Im::EndTable();
				}
				Im::BeginDisabled(!draft.drawOnSight && !draft.autoBattle);
				if (Rows("sightRange")) {
					Slider("Sight range", draft.sightRange, 1000.0f, 5000.0f, "%.0f", "How far away an enemy can be and still count as in view. 2500 is about a long bowshot.");
					Im::EndTable();
				}
				Im::EndDisabled();
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
			if (ResetButton("Role strength, role guessing, what happens before a fight and standing down back to their defaults. Companions' orders are not touched.")) {
				const auto d = Settings::Defaults();
				draft.styleWeight = d.styleWeight;
				draft.guessRoles = d.guessRoles;
				draft.drawOnSight = d.drawOnSight;
				draft.autoBattle = d.autoBattle;
				draft.sightRange = d.sightRange;
				draft.leaveEmptyFights = d.leaveEmptyFights;
				draft.leaveAfter = d.leaveAfter;
				dirty = true;
			}
			Footer();
		}

		// --- Paths cover ------------------------------------------------------------------

		// With Paths - Follower Ability Skilltree installed, a role's numbers come from the ranks each
		// companion buys on their path, and the rest of a role's settings are FightWithMe.ini's
		// (Settings::LockRoles): the page says so in place of settings that are not theirs to change.
		bool PathsInstalled()
		{
			const auto view = Battle::ReadView();
			return view && view->paths;
		}

		void PathCover(Role a_role, const char* a_text)
		{
			auto*       draw = Im::GetWindowDrawList();
			const V     at = Im::GetCursorScreenPos();
			const float w = Im::GetContentRegionAvail().x;
			const float pad = E(1.2f);
			const float textX = at.x + pad + E(4.2f);
			const float wrap = std::max(at.x + w - pad - textX, E(8.0f));
			const float kicker = E(0.8f), title = E(2.0f), body = E(1.0f);
			const float ratio = body / std::max(Im::GetFontSize(), 1.0f);
			const float bodyH = Im::CalcTextSize(a_text, nullptr, false, wrap / ratio).y * ratio;
			const float titleY = at.y + pad + kicker + E(0.4f);
			const float bodyY = titleY + title * 1.1f + E(0.35f);
			const float h = std::max(bodyY + bodyH + pad - at.y, E(6.5f));
			const V     max{ at.x + w, at.y + h };

			Draw::AddRectFilled(draw, at, max, kInk, 0.0f, 0);
			Band(draw, at, max, 0.55f);
			Draw::AddRect(draw, at, max, Alpha(kEdge, 0.6f), 0.0f, 0, 1.0f);
			Draw::AddRectFilled(draw, at, { at.x + 2.0f, max.y }, kEdge, 0.0f, 0);

			const V mark{ at.x + pad + E(1.9f), at.y + h * 0.5f };
			Halo(draw, mark, E(3.4f), kAccent, 0.8f);
			Diamond(draw, mark, E(1.75f), IM_COL32(24, 11, 11, 250), kEdge, 1.8f);
			Diamond(draw, mark, E(1.4f), IM_COL32(0, 0, 0, 0), Alpha(kText, 0.35f), 1.0f);
			Glyph(draw, mark, E(1.3f), Info(a_role).glyph, kText);

			Label(draw, { textX, at.y + pad }, kTextDim, kicker, "PATHS - FOLLOWER ABILITY SKILLTREE", E(0.16f));
			Text(draw, { textX, titleY }, kText, title, "Your Followers Are Guided by Their Path");
			Draw::AddText(draw, Im::GetFont(), body, { textX, bodyY }, kTextDim, a_text, nullptr, wrap);
			Im::Dummy({ w, h });

			Space(0.3f);
			if (Im::Button("Open the battle plan")) {
				Open();
			}
			Help("A companion's path opens from their card in the battle plan: the Path tile, or one of the two abilities they carry.");
		}

		// --- Tanks -----------------------------------------------------------------------

		int exampleLevel = 30;

		void __stdcall RenderTanks()
		{
			Prepare();
			SettingsPage page("FightWithMeTanks");
			Header("TANKS");
			if (PathsInstalled()) {
				PathCover(Role::kTank, "Values are only modifiable via INI or by removing Paths");
				Footer();
				return;
			}
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
					Slider("Flash", draft.glowSeconds, 0.0f, 4.0f, "%.1f s", "How long the gold flash shows on a tank as they taunt. 0 turns it off.");
					Im::EndTable();
				}
				Im::EndDisabled();
			}
			if (Group("Bracing", 0xF132)) {
				Im::BeginDisabled(!draft.taunt);
				if (Checks("tauntBrace")) {
					Check("Brace on taunt", draft.brace, "Each taunt braces the tank with extra armor for as long as it holds the enemies it turned.");
					Im::EndTable();
				}
				Im::BeginDisabled(!draft.brace);
				if (Rows("tauntArmor")) {
					Slider("Armor", t.tauntArmorBase, 0.0f, 100.0f, "%.0f", "Armor a taunt adds before the tank's level.");
					Slider("More per level", t.tauntArmorPerLevel, 0.0f, 5.0f, "%.1f", "Armor added for each level of the tank.");
					Slider("Most armor", t.tauntArmorMax, 0.0f, 400.0f, "%.0f", "A taunt never adds more than this.");
					Im::EndTable();
				}
				const auto line = std::format("A taunt braces a level 10 tank with +{:.0f} armor for {:.0f} s, level 30 with +{:.0f} for {:.0f} s, level 60 with +{:.0f} for {:.0f} s.",
					Rules::TauntArmor(10, t), Rules::HoldSeconds(10, 10, t), Rules::TauntArmor(30, t), Rules::HoldSeconds(30, 30, t), Rules::TauntArmor(60, t),
					Rules::HoldSeconds(60, 60, t));
				Note(line.c_str());
				Im::EndDisabled();
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
					Slider("Hold time", t.holdSeconds, 1.0f, 60.0f, "%.0f s", "Seconds an enemy stays on the tank, whatever else their own AI would rather do.");
					Slider("From level 30", t.holdSecondsAt30, 1.0f, 60.0f, "%.0f s", "The hold once the tank reaches level 30.");
					Slider("From level 60", t.holdSecondsAt60, 1.0f, 90.0f, "%.0f s", "The hold once the tank reaches level 60.");
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
			Note("The sound and animations are forms named in FightWithMe.ini.");
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
				t.holdSeconds = dt.holdSeconds;
				t.holdSecondsAt30 = dt.holdSecondsAt30;
				t.holdSecondsAt60 = dt.holdSecondsAt60;
				t.tauntRadiusBase = dt.tauntRadiusBase;
				t.tauntRadiusPerSkill = dt.tauntRadiusPerSkill;
				t.tauntRadiusMax = dt.tauntRadiusMax;
				t.holdBreakDistance = dt.holdBreakDistance;
				t.tauntRangedReach = dt.tauntRangedReach;
				t.tauntArmorBase = dt.tauntArmorBase;
				t.tauntArmorPerLevel = dt.tauntArmorPerLevel;
				t.tauntArmorMax = dt.tauntArmorMax;
				draft.brace = d.brace;
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
			if (PathsInstalled()) {
				PathCover(Role::kHealer, "Values are only modifiable via INI or by removing Paths");
				Footer();
				return;
			}
			auto& t = draft.tuning;
			if (Group("Heal", 0xF4BE)) {
				if (Checks("healOptions")) {
					Check("Allow heals", draft.heal,
						"A healer's own gift, not a spell: you and everyone in the party within reach healed at once, no magicka spent. Anyone can be a Healer.");
					Check("Any spell in their hands", draft.healersAnySpell,
						"The spells you can give a healer's hands on their sheet: any heal, ward, armor, cloak or summons, and attack spells up to Novice, then Apprentice from level 10 and Adept from 25, never Expert or Master. On: attack spells of any strength too.");
					Im::EndTable();
				}
				Im::BeginDisabled(!draft.heal);
				if (Rows("healValues")) {
					Slider("Health", t.healAmountBase, 0.0f, 300.0f, "%.0f", "Health each heal gives everyone it reaches, before the healer's level.");
					Slider("More per level", t.healAmountPerLevel, 0.0f, 15.0f, "%.1f", "Health added for each level of the healer.");
					Slider("Most health", t.healAmountMax, 50.0f, 1500.0f, "%.0f", "A heal never gives more than this.");
					Slider("Between heals", t.healCooldown, 10.0f, 180.0f, "%.0f s", "Seconds a healer waits after each heal.");
					Slider("Reach", t.healReach, 500.0f, 8000.0f, "%.0f units", "Everyone in the party this close to the healer is healed.");
					Im::EndTable();
				}
				const auto line = std::format("A level 10 healer heals everyone for {:.0f}, level 30 for {:.0f}, level 60 for {:.0f}, every {:.0f} s.",
					Rules::TeamHealAmount(10, t), Rules::TeamHealAmount(30, t), Rules::TeamHealAmount(60, t), t.healCooldown);
				Note(line.c_str());
				Im::EndDisabled();
			}
			if (Group("Rejuvenate", 0xF5BB)) {
				if (Checks("rejuvenateOptions")) {
					Check("Allow Rejuvenate", draft.rejuvenate,
						"A healer quickens your magicka and every companion's for a while. Ready again after twice as long as it lasts.");
					Im::EndTable();
				}
				Im::BeginDisabled(!draft.rejuvenate);
				if (Rows("rejuvenateValues")) {
					Slider("Faster by", t.rejuvenateBonus, 10.0f, 300.0f, "%.0f%%", "How much faster magicka comes back while it lasts. 100% is twice as fast.");
					Slider("Lasts", t.rejuvenateSeconds, 5.0f, 300.0f, "%.0f s", "How long it lasts, before level 30.");
					Slider("From level 30", t.rejuvenateSecondsAt30, 5.0f, 300.0f, "%.0f s", "How long it lasts once the healer reaches level 30.");
					Slider("From level 60", t.rejuvenateSecondsAt60, 5.0f, 600.0f, "%.0f s", "How long it lasts once the healer reaches level 60.");
					Im::EndTable();
				}
				const auto line = std::format("Lasts {:.0f} s (ready again after {:.0f} s), {:.0f} s from level 30 ({:.0f} s), {:.0f} s from level 60 ({:.0f} s).",
					Rules::RejuvenateSeconds(1, t), Rules::RejuvenateCooldown(1, t), Rules::RejuvenateSeconds(30, t), Rules::RejuvenateCooldown(30, t),
					Rules::RejuvenateSeconds(60, t), Rules::RejuvenateCooldown(60, t));
				Note(line.c_str());
				Im::EndDisabled();
			}
			Note("Each healer's line for the party, and whether they heal and rejuvenate on their own, is set per companion in the battle plan.");
			if (ResetButton("Every healer setting on this page back to its default.")) {
				const auto d = Settings::Defaults();
				draft.heal = d.heal;
				draft.healersAnySpell = d.healersAnySpell;
				draft.rejuvenate = d.rejuvenate;
				t.healAmountBase = d.tuning.healAmountBase;
				t.healAmountPerLevel = d.tuning.healAmountPerLevel;
				t.healAmountMax = d.tuning.healAmountMax;
				t.healCooldown = d.tuning.healCooldown;
				t.healReach = d.tuning.healReach;
				t.rejuvenateBonus = d.tuning.rejuvenateBonus;
				t.rejuvenateSeconds = d.tuning.rejuvenateSeconds;
				t.rejuvenateSecondsAt30 = d.tuning.rejuvenateSecondsAt30;
				t.rejuvenateSecondsAt60 = d.tuning.rejuvenateSecondsAt60;
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
						"With me (what To me! gives everyone): they keep this close to you and fight from there. Melee companions take anything that comes near you; archers and casters shoot the nearest enemy, however far.");
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
			if (Group("Sending someone after an enemy", 0xF024)) {
				if (Checks("marks")) {
					Check("Outline marked enemies", draft.markGlow,
						"While the battle plan is open, an enemy you send someone after is outlined, through walls. The outline goes when the plan closes.");
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
				draft.markGlow = d.markGlow;
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
					Slider("Turning speed", draft.orbitSpeed, 0.2f, 3.0f, "%.1fx",
						"How fast the right stick, or dragging with the right mouse button, turns the view around the companion or the fight.");
					Slider("Overhead tilt", draft.overheadPitch, 35.0f, 88.0f, "%.0f deg",
						"How steeply the overhead view looks down when it first opens. 88 is straight down; lower shows more of the room's shape. Tilt it in the plan with the right stick.");
					Im::EndDisabled();
					Im::EndTable();
				}
				if (Checks("cutaway")) {
					Im::BeginDisabled(!draft.cameraGlide);
					Check("Cut the ceiling away overhead", draft.cutaway,
						"When something blocks the view from above (a cave roof, a ceiling, an overhang), the overhead view leaves out everything above the tallest fighter's head, so nobody is hidden. In the open nothing is cut. Off: the camera comes down under the cover instead.");
					Im::EndDisabled();
					Im::EndTable();
				}
				if (draft.cutaway && draft.cameraGlide && Rows("cutawayRows")) {
					Slider("Cut above heads", draft.cutawayMargin, 0.0f, 400.0f, "%.0f units",
						"How far above the tallest head the cut goes. Raise it to keep more of the walls; lower it if a low ceiling still shows.");
					Im::EndTable();
				}
			}
			if (Group("Abilities", 0xF0E7)) {
				if (Checks("abilities")) {
					Check("Role specials", draft.specials,
						"Bodyguard (tank), Volley (archer), War cry (bruiser), Ward (mage, healer) and Disarm (fighter): a button in the plan, and on their own where each companion's switch allows.");
					Check("Ability effects", draft.abilityEffects,
						"Healing auras, protective barriers, wind and earth bursts, and weapon particles for abilities. Red marks enemies held by a taunt. Requires Triumvirate, Elemental Destruction Magic Redux and Vibrant Weapons (Fire Frost Shock); missing sources use simpler vanilla effects.");
					Im::EndTable();
				}
			}
			if (Group("Interface", 0xF06E)) {
				if (Rows("planLook")) {
					Im::TableNextRow();
					Im::TableNextColumn();
					Im::AlignTextToFramePadding();
					Im::TextUnformatted("Theme");
					Im::TableNextColumn();
					Im::SetNextItemWidth(-1);
					if (Im::BeginCombo("##uiTheme", UITheme::Name(draft.uiTheme))) {
						for (int i = 0; i < UITheme::kCount; ++i) {
							if (Im::Selectable(UITheme::Name(i), draft.uiTheme == i) && draft.uiTheme != i) {
								draft.uiTheme = i;
								dirty = true;
							}
						}
						Im::EndCombo();
					}
					Help("Vanilla: charcoal and silver. Oxblood: the original red. Nordic: engraved stone, beveled metal and inset highlights. All use the same windows and controls. Applies to the battle plan, Paths, notices and these settings.");
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
				draft.orbitSpeed = d.orbitSpeed;
				draft.overheadPitch = d.overheadPitch;
				draft.cutaway = d.cutaway;
				draft.cutawayMargin = d.cutawayMargin;
				draft.abilityEffects = d.abilityEffects;
				draft.uiScale = d.uiScale;
				draft.uiTheme = d.uiTheme;
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

		constexpr std::array<PadChoice, 4> overheadPadChoices{ {
			{ 0, "Unbound" },
			{ 0x80, "Right-stick click" },
			{ 0x40, "Left-stick click" },
			{ 0x20, "Back / View" },
		} };

		void __stdcall RenderControls()
		{
			Prepare();
			SettingsPage page("FightWithMeControls");
			Header("CONTROLS");
			Heading("KEY BINDINGS", 0xF11C);
			Note("Select the binding, then press a key, holding Shift, Ctrl or Alt with it for a combination. Esc cancels; Delete clears.");
			Space();
			if (Im::BeginTable("bindings", 4, Im::ImGuiTableFlags_SizingStretchProp)) {
				Im::TableSetupColumn("Action", Im::ImGuiTableColumnFlags_WidthStretch, 1.4f);
				Im::TableSetupColumn("Modifier", Im::ImGuiTableColumnFlags_WidthStretch, 0.6f);
				Im::TableSetupColumn("Key", Im::ImGuiTableColumnFlags_WidthStretch, 1.0f);
				Im::TableSetupColumn("", Im::ImGuiTableColumnFlags_WidthFixed, E(3.5f));
				// One row per binding: the battle plan, the four party orders, the overhead view.
				auto binding = [&](int a_slot, const char* a_label, Hotkey& a_hotkey) {
					Im::PushID(a_slot);
					Im::TableNextRow();
					Im::TableNextColumn();
					Im::AlignTextToFramePadding();
					Im::TextUnformatted(a_label);
					Im::TableNextColumn();
					Im::BeginDisabled(a_hotkey.key == 0);
					Im::SetNextItemWidth(-1);
					if (Im::BeginCombo("##modifier", ModifierName(a_hotkey.modifier))) {
						for (const std::uint32_t modifier : { 0u, kShift, kCtrl, kAlt }) {
							if (Im::Selectable(ModifierName(modifier), modifier == a_hotkey.modifier) && modifier != a_hotkey.modifier) {
								a_hotkey.modifier = modifier;
								dirty = true;
							}
						}
						Im::EndCombo();
					}
					Im::EndDisabled();
					Help("Held with the key. A key with no modifier works whatever else is held, so sprinting does not get in the way.");
					Im::TableNextColumn();
					const bool waiting = capturing.load() && captureSlot == a_slot;
					const auto held = captureModifier.load();
					const auto name = !waiting ? KeyName(a_hotkey.key) : (held ? std::format("{} + ...", ModifierName(held)) : std::string("Press a key..."));
					if (Im::Button(name.c_str(), { -1, 0 })) {
						if (waiting) {
							CancelCapture();
						} else {
							StartCapture(a_slot);
						}
					}
					Im::TableNextColumn();
					if (Im::Button("Clear", { -1, 0 })) {
						a_hotkey = {};
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
				binding(5, "Overhead view (in the plan)", draft.overheadKey);
				Im::EndTable();
			}
			// Two actions on one binding: only the first of them would ever happen.
			const std::array<std::pair<const char*, const Hotkey*>, 6> bound{ {
				{ "Attack!", &draft.orderKeys[0] },
				{ "To me!", &draft.orderKeys[1] },
				{ "Hold back", &draft.orderKeys[2] },
				{ "As you were", &draft.orderKeys[3] },
				{ "Open the battle plan", &draft.planKey },
				{ "Overhead view", &draft.overheadKey },
			} };
			std::string clash;
			for (std::size_t i = 0; i < bound.size() && clash.empty(); ++i) {
				for (std::size_t j = i + 1; j < bound.size() && clash.empty(); ++j) {
					if (bound[i].second->key && *bound[i].second == *bound[j].second) {
						clash = std::format("{} and {} are both on {}; only {} will happen.", bound[i].first, bound[j].first, HotkeyName(*bound[i].second), bound[i].first);
					}
				}
			}
			if (!clash.empty()) {
				Note(clash.c_str());
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
					Im::TableNextRow();
					Im::TableNextColumn();
					Im::AlignTextToFramePadding();
					Im::TextUnformatted("Overhead view (in the plan)");
					Im::TableNextColumn();
					int overhead = 0;
					for (int i = 0; i < static_cast<int>(overheadPadChoices.size()); ++i) {
						if (overheadPadChoices[static_cast<std::size_t>(i)].button == draft.overheadGamepadKey) {
							overhead = i;
						}
					}
					Im::SetNextItemWidth(-1);
					if (Im::BeginCombo("##overheadPad", overheadPadChoices[static_cast<std::size_t>(overhead)].name)) {
						for (int i = 0; i < static_cast<int>(overheadPadChoices.size()); ++i) {
							if (Im::Selectable(overheadPadChoices[static_cast<std::size_t>(i)].name, i == overhead)) {
								draft.overheadGamepadKey = overheadPadChoices[static_cast<std::size_t>(i)].button;
								dirty = true;
							}
						}
						Im::EndCombo();
					}
					Help("Switches between the companion and the whole fight seen from above while the plan is open. The right stick turns the view either way.");
					Im::EndTable();
				}
			}
			if (Group("In the battle plan", 0xF05A)) {
				if (Im::BeginTable("planControls", 3, Im::ImGuiTableFlags_SizingStretchProp | Im::ImGuiTableFlags_RowBg)) {
					Im::TableSetupColumn("What", Im::ImGuiTableColumnFlags_WidthStretch, 1.2f);
					Im::TableSetupColumn("Keyboard and mouse", Im::ImGuiTableColumnFlags_WidthStretch, 1.0f);
					Im::TableSetupColumn("Gamepad", Im::ImGuiTableColumnFlags_WidthStretch, 1.0f);
					Im::TableHeadersRow();
					static constexpr std::array<std::array<const char*, 3>, 10> rows{ {
						{ "Choose a companion", "Click a crest, Tab", "LB / RB" },
						{ "Move between rows", "Up / Down", "D-pad up / down" },
						{ "Pick or step a value", "Click, Left / Right", "D-pad left / right" },
						{ "Give a party order", "Click it, or its key", "Party orders row, A" },
						{ "Send someone after an enemy", "Click the enemy", "Battlefield row, A" },
						{ "Ability now / switch", "Click", "X / Y" },
						{ "Turn the view", "Drag with the right button", "Right stick" },
						{ "Zoom", "Mouse wheel", "LT / RT" },
						{ "Overhead view", "Its key, or the button top left", "The button set above" },
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
				draft.overheadKey = d.overheadKey;
				draft.overheadGamepadKey = d.overheadGamepadKey;
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

		// Whether Shift, Ctrl or Alt (either side) is held, from the game's own keyboard state.
		bool ModifierDown(std::uint32_t a_modifier)
		{
			auto* devices = RE::BSInputDeviceManager::GetSingleton();
			auto* keyboard = devices ? devices->GetKeyboard() : nullptr;
			if (!keyboard || a_modifier == 0) {
				return false;
			}
			// curState rather than IsPressed(), which would link CommonLib's whole device class.
			const auto& keys = keyboard->GetRuntimeData().curState;
			for (const std::uint32_t code : { 0x2Au, 0x36u, 0x1Du, 0x9Du, 0x38u, 0xB8u }) {
				if (ModifierOf(code) == a_modifier && (keys[code] & 0x80) != 0) {
					return true;
				}
			}
			return false;
		}

		// The modifier held right now, Ctrl first; 0 for none.
		std::uint32_t HeldModifier()
		{
			for (const std::uint32_t modifier : { kCtrl, kShift, kAlt }) {
				if (ModifierDown(modifier)) {
					return modifier;
				}
			}
			return 0;
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
				if (!button || captured.load() != -1) {
					return true;  // the answer is in, waiting for the page to take it
				}
				const auto key = button->GetIDCode();
				// Shift, Ctrl and Alt only ever go with another key: held, they are shown on the button.
				if (const auto modifier = ModifierOf(key)) {
					if (button->IsDown()) {
						captureModifier = modifier;
					} else if (button->IsUp() && captureModifier.load() == modifier) {
						captureModifier = 0;
					}
					return true;
				}
				if (!button->IsDown()) {
					return true;
				}
				if (key == kEscape) {
					captured = -2;
				} else if (key == 0xD3) {
					captured = 0;  // Delete
				} else {
					captured = static_cast<int>(key | HeldModifier() << 16);
				}
				return true;
			}
			const bool open = plan && plan->IsOpen;
			const auto& settings = Settings::Get();

			if (a_event->GetDevice() == RE::INPUT_DEVICE::kGamepad) {
				// The right stick turns the view. Events come when the stick moves, so the last
				// one says where it is held.
				if (a_event->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
					if (auto* stick = a_event->AsThumbstickEvent(); stick && stick->IsRight()) {
						stickX = open ? stick->xValue : 0.0f;
						stickY = open ? stick->yValue : 0.0f;
					} else if (stick && stick->IsLeft()) {
						leftX = open ? stick->xValue : 0.0f;
						leftY = open ? stick->yValue : 0.0f;
					}
					return open;
				}
				if (a_event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
					return open;
				}
				auto* button = a_event->AsButtonEvent();
				if (!button) {
					return open;
				}
				const auto key = button->GetIDCode();
				// The triggers zoom, by how far they are pulled.
				if (key == kPadLT || key == kPadRT) {
					(key == kPadLT ? triggerL : triggerR) = open ? button->Value() : 0.0f;
					return open;
				}
				const bool fresh = button->IsDown() && (padHeld & key) == 0;
				if (button->IsPressed()) {
					padHeld |= key;
				} else {
					padHeld &= ~key;
				}
				if (open) {
					padUsed = true;
					if (fresh && settings.overheadGamepadKey && key == settings.overheadGamepadKey) {
						overheadToggle = true;
						return true;
					}
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
						case kPadView:
							padView = true;
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
			// The binding this press means: one whose modifier is held beats one that needs
			// none, and on a tie the party orders come first.
			constexpr int kPlanAction = 4, kOverheadAction = 5;
			int           action = -1, best = 0;
			const auto    consider = [&](int a_action, const Hotkey& a_hotkey) {
				const int fit = Fit(a_hotkey, key, ModifierDown(a_hotkey.modifier));
				if (fit > best) {
					action = a_action;
					best = fit;
				}
			};
			for (std::size_t i = 0; i < settings.orderKeys.size(); ++i) {
				consider(static_cast<int>(i), settings.orderKeys[i]);
			}
			consider(kPlanAction, settings.planKey);
			if (open) {
				consider(kOverheadAction, settings.overheadKey);
			}
			if (action >= 0 && action < kPlanAction && (open || GameplayAllowsPlan())) {
				Battle::Command command;
				command.type = Battle::Command::Type::kShout;
				command.shout = orderShouts[static_cast<std::size_t>(action)];
				Battle::Queue(command);
				if (open) {
					Sound("UIMenuOK");
				}
				return true;
			}
			if (open) {
				if (key == kEscape && spellListOpen) {
					spellListBack = true;  // the list closes; the plan stays
					return true;
				}
				if (key == kEscape && pathPageOpen) {
					pathPageBack = true;
					return true;
				}
				if (key == kEscape || action == kPlanAction) {
					Close();
					return true;
				}
				if (action == kOverheadAction) {
					overheadToggle = true;
					return true;
				}
				return false;
			}
			if (action == kPlanAction && GameplayAllowsPlan()) {
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

#include "FWM/Menu.h"
#include "FWM/AbilityResource.h"
#include "FWM/TreeNavigation.h"

#include "FWM/AbilityLink.h"
#include "FWM/Battle.h"
#include "FWM/Camera.h"
#include "FWM/TreeEntrance.h"
#include "FWM/HealCircle.h"
#include "FWM/Host.h"
#include "FWM/RejuvenatePolicy.h"
#include "FWM/Leveling.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Rules.h"
#include "FWM/Settings.h"
#include "FWM/Text.h"
#include "FWM/UITheme.h"

#include "FightWithMePathsAPI.h"

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
		constexpr U32 kGold = IM_COL32(246, 198, 96, 255);  // a level just earned
		constexpr U32 kHealth = IM_COL32(178, 44, 40, 255);
		constexpr U32 kMagicka = IM_COL32(84, 130, 204, 255);
		constexpr U32 kStamina = IM_COL32(118, 174, 94, 255);
		constexpr U32 kRejuvenated = IM_COL32(120, 210, 255, 255);  // magicka quickened by Rejuvenate
		const U32& kFocusRing = palette.focusRing;
		constexpr U32 kShade = IM_COL32(0, 0, 0, 255);

		void SelectTheme() { palette = UITheme::For(Settings::Theme()); }

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
		std::atomic<bool> roleListOpen{ false };  // and the role list likewise
		std::atomic<bool> roleListBack{ false };
		std::atomic<bool> pathPageOpen{ false };  // and the path page, likewise
		std::atomic<bool> pathPageBack{ false };
		std::atomic<bool> padView{ false };  // View: the guide, on the path page

		// Path of the Companions beside Fight With Me draws into the battle plan. On its side, the frame
		// it is drawing into, while it draws; on Fight With Me's, the last frame and its answer.
		FightWithMePathsAPI::Plan*   hostPlan{ nullptr };
		FightWithMePathsAPI::Plan    planFrame{};
		bool                         planOpened{ false };      // the plan opened since the last frame drawn into it
		std::atomic<std::uint64_t>   treeInPlanUntil{ 0 };     // our tree is over the plan: our input handler takes the pad till then

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

		// Every word in the plan is drawn this much larger than the sizes in the layout say. One
		// factor for all of it keeps the type in proportion; measuring goes through it too, so
		// what is placed by a measurement stays where it belongs.
		constexpr float kTextScale = 1.2f;

		V TextSize(float a_size, std::string_view a_text)
		{
			a_size *= kTextScale;
			const std::string text(a_text);
			const auto        raw = Im::CalcTextSize(text.c_str());
			const float       ratio = a_size / std::max(Im::GetFontSize(), 1.0f);
			return { raw.x * ratio, raw.y * ratio };
		}

		void Text(Im::ImDrawList* a_draw, V a_pos, U32 a_color, float a_size, std::string_view a_text)
		{
			a_size *= kTextScale;
			const std::string text(a_text);
			Draw::AddText(a_draw, Im::GetFont(), a_size, a_pos, a_color, text.c_str());
		}

		void TextCentered(Im::ImDrawList* a_draw, V a_centre, U32 a_color, float a_size, std::string_view a_text)
		{
			const auto size = TextSize(a_size, a_text);
			Text(a_draw, { a_centre.x - size.x * 0.5f, a_centre.y - size.y * 0.5f }, a_color, a_size, a_text);
		}


		// Rotate each UTF-8 glyph tangent to the arc; the lower arc reads left to right.
		void ArcText(Im::ImDrawList* d, V centre, U32 color, float size,
			float radius, std::string_view label, bool lower)
		{
			if (label.empty()) return;
			float total = 0;
            const float tracking=size*.065f;
            std::size_t count=0;
			for (std::size_t i = 0; i < label.size();) {
				const auto bytes = Text::CharLength(label, i);
				total += TextSize(size, label.substr(i, bytes)).x;
				i += bytes;
                ++count;
			}
			total += tracking*static_cast<float>(count>0?count-1:0);
            const float fit = std::min(1.f, radius * 2.35f / std::max(total, 1.f));
			size *= fit;
			float cursor = -total * fit * .5f;
			for (std::size_t i = 0; i < label.size();) {
				const auto bytes = Text::CharLength(label, i);
				const auto glyph = label.substr(i, bytes);
				const float advance = TextSize(size, glyph).x;
				const float angle = (cursor + advance * .5f) / radius;
				const V at{centre.x + radius * std::sin(angle), centre.y + (lower ? 1.f : -1.f) * radius * std::cos(angle)};
				const float turn = lower ? -angle : angle;
				const int begin = d->VtxBuffer.Size;
				TextCentered(d, at, color, size, glyph);
				for (int v = begin; v < d->VtxBuffer.Size; ++v) {
					auto& pos = d->VtxBuffer.Data[v].pos;
					const float x = pos.x - at.x, y = pos.y - at.y;
					pos = {at.x + x * std::cos(turn) - y * std::sin(turn), at.y + x * std::sin(turn) + y * std::cos(turn)};
				}
				cursor += advance + tracking*fit;
				i += bytes;
			}
		}
		void AbilityName(Im::ImDrawList* d, V centre, U32 color, float scale, std::string_view label)
		{
			// Choose a word boundary that balances the two arcs in the active theme's font.
			std::size_t split = std::string_view::npos;
			float best = std::numeric_limits<float>::max();
			for (auto i = label.find(' '); i != std::string_view::npos; i = label.find(' ', i + 1)) {
				const float difference = std::abs(TextSize(22, label.substr(0, i)).x - TextSize(22, label.substr(i + 1)).x);
				if (difference < best) { best = difference; split = i; }
			}
			ArcText(d, centre, color, 22 * scale, 87 * scale, label.substr(0, split), false);
			if (split != std::string_view::npos)
				ArcText(d, centre, color, 22 * scale, 94 * scale, label.substr(split + 1), true);
		}

		// Centred by the ink, not the box: a box takes in the room its last glyph leaves after
		// itself, which pulls a short number such as a level visibly to the left.
		void InkCentered(Im::ImDrawList* a_draw, V a_centre, U32 a_color, float a_size, std::string_view a_text)
		{
			const auto size = TextSize(a_size, a_text);
			float      shift = 0.0f;
			auto*      font = Im::GetFont();
			if (font && !a_text.empty() && font->FontSize > 0.0f) {
				const auto* first = Im::ImFontManger::FindGlyph(font, static_cast<unsigned char>(a_text.front()));
				const auto* last = Im::ImFontManger::FindGlyph(font, static_cast<unsigned char>(a_text.back()));
				if (first && last) {
					const float scale = a_size * kTextScale / font->FontSize;
					shift = (first->X0 - (last->AdvanceX - last->X1)) * 0.5f * scale;
				}
			}
			Text(a_draw, { a_centre.x - size.x * 0.5f - shift, a_centre.y - size.y * 0.5f }, a_color, a_size, a_text);
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
			a_size *= kTextScale;
			const std::string text(a_text);
			Draw::AddText(a_draw, Im::GetFont(), a_size, a_pos, a_color, text.c_str(), nullptr, a_width);
			const float ratio = a_size / std::max(Im::GetFontSize(), 1.0f);
			return Im::CalcTextSize(text.c_str(), nullptr, false, a_width / ratio).y * ratio;
		}

		// Small capitals with open tracking, for section labels. A whole character at a time, so
		// accented and non-Latin letters stay whole.
		void Label(Im::ImDrawList* a_draw, V a_pos, U32 a_color, float a_size, std::string_view a_text, float a_tracking)
		{
			float x = a_pos.x;
			for (std::size_t at = 0, n = 0; at < a_text.size(); at += n) {
				n = Text::CharLength(a_text, at);
				const auto glyph = a_text.substr(at, n);
				Text(a_draw, { x, a_pos.y }, a_color, a_size, glyph);
				x += TextSize(a_size, glyph).x + a_tracking;
			}
		}

		// Capitals for headers, in whatever language the words are in.
		std::string Upper(std::string_view a_text)
		{
			return Text::Upper(a_text);
		}

		float LabelWidth(float a_size, std::string_view a_text, float a_tracking)
		{
			float w = 0.0f;
			for (std::size_t at = 0, n = 0; at < a_text.size(); at += n) {
				n = Text::CharLength(a_text, at);
				w += TextSize(a_size, a_text.substr(at, n)).x + a_tracking;
			}
			return w - a_tracking;
		}

		// An icon that is a picture, by its private codepoint (Paths.h), or none if it is a font glyph
		// or its file did not load. Loaded once each, at a size that stays sharp on a big screen.
		auto Picture(std::uint32_t a_code)
		{
			switch (a_code) {
			case Paths::kGlyphWolfHead:
				{
					static const auto wolf = MF::LoadTexture("Data/Interface/FightWithMe/wolf-head.svg", { 256, 256 });
					return wolf;
				}
			case Paths::kGlyphBarbedSun:
				{
					static const auto sun = MF::LoadTexture("Data/Interface/FightWithMe/barbed-sun.svg", { 256, 256 });
					return sun;
				}
			case Paths::kGlyphSteelHelm:
				{
					static const auto helm = MF::LoadTexture("Data/Interface/FightWithMe/weak-will.svg", { 256, 256 });
					return helm;
				}
			case Paths::kGlyphBastion:
				{
					static const auto bastion = MF::LoadTexture("Data/Interface/FightWithMe/last-bastion.svg", { 256, 256 });
					return bastion;
				}
			default:
				return decltype(MF::LoadTexture("")){};
			}
		}

		// The framework's FontAwesome::Pop clears ALL fonts. Nested icons and rank
		// labels must pop only their own font or they invalidate the enclosing menu.
		// A missing optional face leaves the current font alone and needs no pop.
		// The header's ImGuiContext is a newer ImGui than some framework builds run
		// (3.70 is 1.90.8), so its FontStack can't be read: a push shows as a change
		// of font, and a face seen once still counts when pushed over itself.
		struct ScopedFont
		{
			bool pushed{ false };
			explicit ScopedFont(const char* name, bool enabled = true)
			{
				if (!enabled || !name) return;
				static std::unordered_map<std::string, const Im::ImFont*> faces;
				const Im::ImFont* before = Im::GetFont();
				MF::PushFont(name);
				const Im::ImFont* after = Im::GetFont();
				auto& face = faces[name];
				if (after != before) face = after;
				pushed = after != before || (face && face == after);
			}
			~ScopedFont() { if (pushed) Im::PopFont(); }
			ScopedFont(const ScopedFont&) = delete;
			ScopedFont& operator=(const ScopedFont&) = delete;
		};

		void Glyph(Im::ImDrawList* a_draw, V a_centre, float a_size, std::uint32_t a_code, U32 a_color)
		{
			// Large icons use the original vector outlines rasterized at 256px, avoiding enlarged font-atlas pixels.
			if (a_size >= 26 && a_code >= 0xF000 && a_code <= 0xF8FF) {
				static std::unordered_map<std::uint32_t, decltype(MF::LoadTexture(""))> pictures;
				auto [it, inserted] = pictures.try_emplace(a_code);
				if (inserted) it->second = MF::LoadTexture(std::format("Data/Interface/FightWithMe/glyphs/{:04x}.svg",a_code),{256,256});
				if (it->second) {
					const float x=std::round(a_centre.x-a_size*.5f),y=std::round(a_centre.y-a_size*.5f);
					Draw::AddImage(a_draw,it->second,{x,y},{x+a_size,y+a_size},{0,0},{1,1},a_color);
					return;
				}
			}
			if (a_code == Paths::kGlyphWolfHead || a_code == Paths::kGlyphBarbedSun || a_code == Paths::kGlyphSteelHelm || a_code == Paths::kGlyphBastion) {
				if (const auto picture = Picture(a_code)) {
					// The pictures fill their square where a glyph leaves a margin: a little larger than the size.
					const float half = a_size * 0.58f;
					if (palette.Engraved()) {
						Draw::AddImage(a_draw, picture, { a_centre.x - half, a_centre.y - half + 1.0f }, { a_centre.x + half, a_centre.y + half + 1.0f }, { 0, 0 }, { 1, 1 },
							Alpha(kShade, static_cast<float>(a_color >> 24) / 255.0f * 0.8f));
					}
					Draw::AddImage(a_draw, picture, { a_centre.x - half, a_centre.y - half }, { a_centre.x + half, a_centre.y + half }, { 0, 0 }, { 1, 1 }, a_color);
					return;
				}
				a_code = a_code == Paths::kGlyphWolfHead ? 0xF6D3 :
					a_code == Paths::kGlyphBarbedSun ? 0xF4BE :
					a_code == Paths::kGlyphSteelHelm ? 0xF6E8 : 0xF3ED;
			}
			const auto utf8 = FontAwesome::UnicodeToUtf8(a_code);
			ScopedFont font("fa-solid-900.ttf");
			const auto  bounds = Im::CalcTextSize(utf8.c_str());
			const float ratio = a_size / std::max(Im::GetFontSize(), 1.0f);
			if (palette.Engraved()) {
				// An inset shadow changes the material, never the glyph's size or position.
				Draw::AddText(a_draw, Im::GetFont(), a_size,
					{ a_centre.x - bounds.x * ratio * 0.5f, a_centre.y - bounds.y * ratio * 0.5f + 1.0f },
					Alpha(kShade, static_cast<float>(a_color >> 24) / 255.0f * 0.8f), utf8.c_str());
			}
			Draw::AddText(a_draw, Im::GetFont(), a_size, { a_centre.x - bounds.x * ratio * 0.5f, a_centre.y - bounds.y * ratio * 0.5f }, a_color, utf8.c_str());
		}

		// --- Shapes ------------------------------------------------------------------------

		float uiScale = 1.0f;  // this frame's scale, for decorations drawn by helpers that take no size
		bool  hitsBlocked = false;  // the guide is over the page: nothing beneath it answers the pointer


		// Silhouettes stay inside existing hit bounds. Thin resource bars and world masks
		// keep their exact geometry; only menu surfaces adopt the selected theme.
		bool Shaped(V lo, V hi)
		{
			return hi.x - lo.x >= 12 * uiScale && hi.y - lo.y >= 8 * uiScale &&
				!(lo.x == 0 && lo.y == 0 && hi.x > 800 * uiScale);
		}
		std::array<V, 8> PlateContour(V lo, V hi)
		{
			const float cut = std::min(14 * uiScale, std::min(hi.x-lo.x, hi.y-lo.y) * .24f);
			const float side = palette.Engraved() ? cut : cut * .3f;
			return {{ {lo.x+cut,lo.y}, {hi.x-cut,lo.y}, {hi.x,lo.y+side}, {hi.x,hi.y-cut},
				{hi.x-side,hi.y}, {lo.x+side,hi.y}, {lo.x,hi.y-cut}, {lo.x,lo.y+side} }};
		}
		void PanelFill(Im::ImDrawList* d, V lo, V hi, U32 fill, float = 0, int = 0)
		{
			if (!Shaped(lo,hi) || palette.style == UITheme::Style::Vanilla) {
				Draw::AddRectFilled(d,lo,hi,fill,Shaped(lo,hi) ? std::min(10 * uiScale,(hi.y-lo.y)*.35f) : 0,0);
				return;
			}
			const auto p = PlateContour(lo,hi);
			Draw::AddConvexPolyFilled(d,p.data(),static_cast<int>(p.size()),fill);
		}
		void PanelEdge(Im::ImDrawList* d, V lo, V hi, U32 edge, float = 0, int = 0, float thickness = 1)
		{
			if (!Shaped(lo,hi) || palette.style == UITheme::Style::Vanilla) {
				Draw::AddRect(d,lo,hi,edge,Shaped(lo,hi) ? std::min(10 * uiScale,(hi.y-lo.y)*.35f) : 0,0,thickness);
				return;
			}
			const auto p = PlateContour(lo,hi);
			for (std::size_t i=0; i<p.size(); ++i) Draw::AddLine(d,p[i],p[(i+1)%p.size()],edge,thickness);
		}
		void PanelGradient(Im::ImDrawList* d, V lo, V hi, U32 tl, U32 tr, U32 br, U32 bl)
		{
			if (!Shaped(lo,hi)) { Draw::AddRectFilledMultiColor(d,lo,hi,tl,tr,br,bl); return; }
			const int begin = d->VtxBuffer.Size;
			PanelFill(d,lo,hi,IM_COL32(255,255,255,255));
			for (int i=begin; i<d->VtxBuffer.Size; ++i) {
				auto& v = d->VtxBuffer.Data[i];
				const float x = Clamp01((v.pos.x-lo.x)/(hi.x-lo.x)), y = Clamp01((v.pos.y-lo.y)/(hi.y-lo.y));
				v.col = Alpha(Mix(Mix(tl,tr,x),Mix(bl,br,x),y),static_cast<float>(v.col >> 24)/255.f);
			}
		}

		// Vanilla medallions, Oxblood pointed seals, Nordic carved hexagonal stones.
		void Emblem(Im::ImDrawList* d, V c, float r, U32 fill, U32 edge, float thickness, int kind = 0)
		{
			if (palette.style == UITheme::Style::Vanilla && kind != 2) {
				Draw::AddCircleFilled(d,c,r,fill,48);
				Draw::AddCircle(d,c,r,edge,48,thickness);
				return;
			}
			const int n = palette.Engraved() ? (kind == 1 ? 8 : 6) : (kind == 1 ? 6 : kind == 2 ? 8 : 4);
			std::array<V,8> p{};
			for (int i=0; i<n; ++i) {
				const float a = -1.5707963f + 6.2831853f * static_cast<float>(i)/static_cast<float>(n);
				p[static_cast<std::size_t>(i)] = {c.x+std::cos(a)*r,c.y+std::sin(a)*r};
			}
			Draw::AddConvexPolyFilled(d,p.data(),n,fill);
			for (int i=0; i<n; ++i) Draw::AddLine(d,p[static_cast<std::size_t>(i)],p[static_cast<std::size_t>((i+1)%n)],edge,thickness);
		}
		// One readable face throughout each theme, including rank III, details and callouts.
        const char* MenuFontName() { return palette.Engraved() ? "BarlowSemiCondensed-SemiBold" : palette.Subdued() ? "Spectral-SemiBold" : "Lora-SemiBold"; }
		struct MenuFont : ScopedFont
		{
			MenuFont() : ScopedFont(MenuFontName()) {}
		};
		struct MasterFont : ScopedFont
		{
			explicit MasterFont(bool enabled = true) : ScopedFont(MenuFontName(), enabled) {}
		};
		struct SealContour { std::array<V,64> points{}; int count{0}; };
		SealContour MasterContour(V c,float r)
		{
			SealContour shape;
			if (palette.style == UITheme::Style::Oxblood) {
				constexpr std::array<V,8> shield{{{0,-1.14f},{.70f,-.78f},{.96f,-.20f},{.73f,.62f},
					{0,1.16f},{-.73f,.62f},{-.96f,-.20f},{-.70f,-.78f}}};
				shape.count=8;
				for (int i=0;i<8;++i) shape.points[static_cast<std::size_t>(i)]={c.x+shield[static_cast<std::size_t>(i)].x*r,c.y+shield[static_cast<std::size_t>(i)].y*r};
			} else {
				shape.count=palette.Engraved()?6:48;
				for (int i=0;i<shape.count;++i) {
					const float a=-1.5707963f+6.2831853f*static_cast<float>(i)/static_cast<float>(shape.count);
					shape.points[static_cast<std::size_t>(i)]={c.x+std::cos(a)*r,c.y+std::sin(a)*r};
				}
			}
			return shape;
		}
        SealContour NodeContour(V c,float r,int kind,bool master) {
            if(master)return MasterContour(c,r);
            SealContour shape;
            shape.count=palette.Subdued() && kind!=2?48:palette.Engraved()?(kind==1?8:6):(kind==1?6:kind==2?8:4);
            for(int i=0;i<shape.count;++i){const float a=-1.5707963f+6.2831853f*i/shape.count;shape.points[i]={c.x+std::cos(a)*r,c.y+std::sin(a)*r};}
            return shape;
        }
        void NodeContourStroke(Im::ImDrawList* d,V c,float r,int kind,bool master,U32 ink,float thickness) {
            const auto shape=NodeContour(c,r,kind,master);
            for(int i=0;i<shape.count;++i)Draw::AddLine(d,shape.points[i],shape.points[(i+1)%shape.count],ink,thickness);
        }
		void SealStroke(Im::ImDrawList* d,V c,float r,U32 ink,float thickness)
		{
			const auto p=MasterContour(c,r);
			for (int i=0;i<p.count;++i) Draw::AddLine(d,p.points[static_cast<std::size_t>(i)],p.points[static_cast<std::size_t>((i+1)%p.count)],ink,thickness);
		}

		void Mastery(Im::ImDrawList* d, V c, float r, float alpha, bool numeral = true)
		{
			const auto outer=MasterContour(c,r*1.12f),inner=MasterContour(c,r*.87f);
			const U32 base=palette.Engraved()?IM_COL32(48,57,53,255):palette.Subdued()?IM_COL32(36,36,36,255):IM_COL32(66,12,20,255);
			const U32 dust=palette.Engraved()?IM_COL32(122,143,128,255):palette.Subdued()?IM_COL32(218,218,218,255):IM_COL32(171,39,53,255);
			// Matte material, without directional bevels, rivets or reflective trim.
			const auto savedFlags=d->Flags;
			d->Flags &= ~Im::ImDrawListFlags_AntiAliasedFill;
			for (int i=0;i<outer.count;++i) {
				const auto j=static_cast<std::size_t>(i),k=static_cast<std::size_t>((i+1)%outer.count);
				Draw::AddQuadFilled(d,outer.points[j],outer.points[k],inner.points[k],inner.points[j],Alpha(base,alpha*.94f));
			}
			d->Flags=savedFlags;
			// Soft pigment/luminous dust fills the band instead of a hard metal edge.
			for (int band=0;band<7;++band) {
				const float v=static_cast<float>(band)/6.f;
				SealStroke(d,c,r*(.9f+.19f*v),Alpha(dust,alpha*(.025f+.065f*std::sin(v*3.1415927f))),r*.045f);
			}
			for (int i=0;i<34;++i) {
				const int side=(i*13)%outer.count;
				const float u=.12f+.76f*static_cast<float>((i*17)%23)/22.f;
				const V a=outer.points[static_cast<std::size_t>(side)],b=outer.points[static_cast<std::size_t>((side+1)%outer.count)];
				const V p{c.x+((a.x+(b.x-a.x)*u)-c.x)*(.82f+.14f*static_cast<float>(i%5)/4.f),
					c.y+((a.y+(b.y-a.y)*u)-c.y)*(.82f+.14f*static_cast<float>(i%5)/4.f)};
				if (palette.Engraved()) {
					Draw::AddLine(d,p,{p.x+r*(.035f+.018f*(i%3)),p.y-r*.018f},Alpha(i%3?dust:kShade,alpha*.22f),std::max(.6f,r*.016f));
				} else if (palette.Subdued()) {
					Draw::AddCircleFilled(d,p,r*(i%9==0?.027f:.011f),Alpha(dust,alpha*(i%9==0?.8f:.35f)),8);
				} else {
					Draw::AddLine(d,p,{p.x+r*.025f,p.y+r*(.035f+.012f*(i%4))},Alpha(i%3?dust:kShade,alpha*.2f),std::max(.6f,r*.02f));
				}
			}
			if (palette.Engraved()) {
				// Three carved rune strokes in the upper stone face.
				for (int i=-1;i<=1;++i) {
					const float x=c.x+static_cast<float>(i)*r*.19f,y=c.y-r*.91f;
					Draw::AddLine(d,{x,y-r*.085f},{x,y+r*.065f},Alpha(kShade,alpha*.9f),2.5f*uiScale);
					Draw::AddLine(d,{x,y-r*.06f},{x+r*.065f,y},Alpha(dust,alpha*.65f),uiScale);
				}
			} else if (!palette.Subdued()) {
				// An inked ritual mark, integrated into the crimson seal.
				const V p{c.x,c.y-r*1.04f};
				const std::array<V,3> mark{{{p.x-r*.09f,p.y+r*.06f},{p.x,p.y-r*.1f},{p.x+r*.09f,p.y+r*.06f}}};
				for (int i=0;i<3;++i) Draw::AddLine(d,mark[static_cast<std::size_t>(i)],mark[static_cast<std::size_t>((i+1)%3)],Alpha(dust,alpha*.8f),uiScale);
				Draw::AddLine(d,{p.x,p.y-r*.14f},{p.x,p.y+r*.12f},Alpha(dust,alpha*.7f),uiScale);
			}
			if (numeral) {
				const float y=c.y+r*.91f;
				PanelFill(d,{c.x-r*.40f,y-r*.21f},{c.x+r*.40f,y+r*.21f},Alpha(base,alpha));
				MasterFont font;
				TextCentered(d,{c.x,y},Alpha(kText,alpha),r*.32f,"III");
			}
		}
		void NodeSelection(Im::ImDrawList* d,V c,float r,int kind,bool master,float focus,float phase,float alpha)
		{
			// Filled silhouettes blend into a diffuse backlight. Draw before the node.
			const float pulse=.5f+.5f*std::sin(phase*1.7f);
			const U32 tone=palette.Subdued()?IM_COL32(222,222,222,255):palette.Engraved()?IM_COL32(135,192,164,255):IM_COL32(225,47,66,255);
			for (int i=0;i<24;++i) {
				const float v=static_cast<float>(i)/23.f;
				const float radius=r*(1.68f-.72f*v);
				const U32 fill=Alpha(tone,alpha*focus*(.006f+.035f*v*v)*(.85f+.15f*pulse));
				if (master) {
					const auto shape=MasterContour(c,radius);
					Draw::AddConvexPolyFilled(d,shape.points.data(),shape.count,fill);
				} else Emblem(d,c,radius,fill,0,0,kind);
			}
		}

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
				PanelGradient(a_draw, a_min, a_max,
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
				PanelEdge(a_draw, a_min, a_max, Alpha(kGlassEdge, a_alpha), 0.0f, 0, 1.0f);
				Draw::AddLine(a_draw, { a_min.x + s, a_min.y + s }, { a_max.x - s, a_min.y + s }, Alpha(kText, a_alpha * 0.18f), 1.0f);
				Draw::AddLine(a_draw, { a_min.x + s, a_min.y + s }, { a_min.x + s, a_max.y - s }, Alpha(kText, a_alpha * 0.10f), 1.0f);
				return;
			}
			if (palette.Subdued()) {
				PanelGradient(a_draw, a_min, a_max, Alpha(kGlass, a_alpha), Alpha(kGlass, a_alpha),
					Alpha(IM_COL32(5, 7, 9, 232), a_alpha), Alpha(IM_COL32(5, 7, 9, 232), a_alpha));
				PanelEdge(a_draw, a_min, a_max, Alpha(kGlassEdge, a_alpha), 0.0f, 0, 1.0f);
				return;
			}
			PanelFill(a_draw, { a_min.x + 3.0f * uiScale, a_min.y + 6.0f * uiScale }, { a_max.x + 3.0f * uiScale, a_max.y + 8.0f * uiScale },
				Alpha(kShade, a_alpha * 0.28f), 0.0f, 0);
			PanelFill(a_draw, a_min, a_max, Alpha(kGlass, a_alpha), 0.0f, 0);
			PanelEdge(a_draw, a_min, a_max, Alpha(kGlassEdge, a_alpha), 0.0f, 0, 1.0f);
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
		void Frame(Im::ImDrawList* d, V lo, V hi, float alpha)
		{
			Glass(d,lo,hi,alpha,0);
			const float inset=5*uiScale;
			PanelEdge(d,{lo.x+inset,lo.y+inset},{hi.x-inset,hi.y-inset},Alpha(kEdge,alpha*.3f));
			if (hi.x-lo.x>160*uiScale) Gem(d,{(lo.x+hi.x)*.5f,lo.y},4*uiScale,alpha);
		}

		// An oxblood band fading to the right: where a header sits.
		// a_tone takes the place of the theme's accent (a companion's rarity, say); 0 keeps the accent.
		void Band(Im::ImDrawList* a_draw, V a_min, V a_max, float a_alpha, U32 a_tone = 0)
		{
			const U32 tone = a_tone ? a_tone : kAccent;
			if (palette.Engraved()) {
				PanelGradient(a_draw, a_min, a_max, Alpha(kText, a_alpha * 0.06f), Alpha(kText, a_alpha * 0.02f),
					Alpha(kShade, a_alpha * 0.12f), Alpha(kShade, a_alpha * 0.12f));
				if (a_tone) {
					// Engraved stone has no colour of its own: the tone washes in from the left.
					PanelGradient(a_draw, a_min, a_max, Alpha(tone, a_alpha * 0.3f), Alpha(tone, 0.0f), Alpha(tone, 0.0f), Alpha(tone, a_alpha * 0.3f));
				}
				
				
				return;
			}
			if (palette.Subdued()) {
				PanelGradient(a_draw, a_min, a_max, Alpha(tone, a_alpha * 0.26f), Alpha(tone, 0.0f),
					Alpha(tone, 0.0f), Alpha(tone, a_alpha * 0.26f));
				
				return;
			}
			PanelGradient(a_draw, a_min, a_max, Alpha(tone, a_alpha * 0.9f), Alpha(tone, 0.0f), Alpha(tone, 0.0f),
				Alpha(tone, a_alpha * 0.9f));
		}

		// The chosen row: an oxblood bar fading right, a bright edge on its left, a red line
		// running on past its end, and the slashes.
		void Highlight(Im::ImDrawList* a_draw, V a_min, V a_max, float a_alpha)
		{
			if (a_alpha <= 0.01f) {
				return;
			}
			if (palette.Engraved()) {
				PanelGradient(a_draw, a_min, a_max, Alpha(kAccent, a_alpha * 0.7f), Alpha(kAccent, a_alpha * 0.22f),
					Alpha(kAccent, a_alpha * 0.22f), Alpha(kAccent, a_alpha * 0.7f));
				Draw::AddLine(a_draw, a_min, { a_min.x, a_max.y }, Alpha(kEdge, a_alpha), 1.0f);
				EtchedCorners(a_draw, a_min, a_max, a_alpha);
				return;
			}
			if (palette.Subdued()) {
				PanelGradient(a_draw, a_min, a_max, Alpha(kText, a_alpha * 0.13f), Alpha(kText, 0.0f),
					Alpha(kText, 0.0f), Alpha(kText, a_alpha * 0.13f));
				Draw::AddLine(a_draw, a_min, { a_min.x, a_max.y }, Alpha(kText, a_alpha * 0.8f), 1.0f);
				return;
			}
			PanelGradient(a_draw, a_min, a_max, Alpha(kAccent, a_alpha * 0.9f), Alpha(kAccent, a_alpha * 0.16f), Alpha(kAccent, a_alpha * 0.16f),
				Alpha(kAccent, a_alpha * 0.9f));
			PanelFill(a_draw, a_min, { a_min.x + 2.0f * uiScale, a_max.y }, Alpha(kEdge, a_alpha), 0.0f, 0);
			Draw::AddLine(a_draw, { a_min.x, a_max.y }, { a_max.x + 12.0f * uiScale, a_max.y }, Alpha(kEdge, a_alpha * 0.6f), 1.0f);
			const float h = a_max.y - a_min.y;
			Slashes(a_draw, { a_max.x - 24.0f * uiScale, a_min.y + h * 0.28f }, h * 0.44f, Alpha(kText, a_alpha * 0.55f));
		}

		// The chosen tile: oxblood rising from its floor, a bright line along the floor.
		void Lit(Im::ImDrawList* a_draw, V a_min, V a_max, float a_alpha)
		{
			if (palette.Engraved()) {
				PanelGradient(a_draw, a_min, a_max, Alpha(kEdge, a_alpha * 0.17f), Alpha(kEdge, a_alpha * 0.17f),
					Alpha(kAccent, a_alpha * 0.65f), Alpha(kAccent, a_alpha * 0.65f));
				EtchedCorners(a_draw, a_min, a_max, a_alpha);
				return;
			}
			if (palette.Subdued()) {
				PanelGradient(a_draw, a_min, a_max, Alpha(kText, a_alpha * 0.02f), Alpha(kText, a_alpha * 0.02f),
					Alpha(kText, a_alpha * 0.14f), Alpha(kText, a_alpha * 0.14f));
				return;
			}
			PanelGradient(a_draw, a_min, a_max, Alpha(kAccent, a_alpha * 0.06f), Alpha(kAccent, a_alpha * 0.06f), Alpha(kAccent, a_alpha * 0.78f),
				Alpha(kAccent, a_alpha * 0.78f));
			PanelFill(a_draw, { a_min.x, a_max.y - 2.0f * uiScale }, a_max, Alpha(kEdge, a_alpha), 0.0f, 0);
		}

		// A key in its own square cap; returns the cap's width.
		float Keycap(Im::ImDrawList* a_draw, V a_at, float a_height, float a_size, std::string_view a_key, float a_alpha)
		{
			const float w = std::max(a_height, TextSize(a_size, a_key).x + 12.0f * uiScale);
			PanelFill(a_draw, a_at, { a_at.x + w, a_at.y + a_height }, Alpha(IM_COL32(22, 22, 23, 235), a_alpha), 0.0f, 0);
			PanelEdge(a_draw, a_at, { a_at.x + w, a_at.y + a_height }, Alpha(kGlassEdge, a_alpha * 1.4f), 0.0f, 0, 1.0f);
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
			PanelFill(a_draw, a_min, { a_min.x + a_width, a_min.y + a_height }, Alpha(IM_COL32(255, 255, 255, 20), a_alpha), 0.0f, 0);
			if (a_fraction > 0.003f) {
				PanelFill(a_draw, a_min, { a_min.x + a_width * Clamp01(a_fraction), a_min.y + a_height }, Alpha(a_color, a_alpha), 0.0f, 0);
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

		// Progress round a diamond, from its top point clockwise as far as a_t: XP toward a level.
		void DiamondRing(Im::ImDrawList* a_draw, V c, float r, float a_t, U32 a_track, U32 a_fill, float a_thickness)
		{
			const std::array<V, 5> points{ V{ c.x, c.y - r }, V{ c.x + r, c.y }, V{ c.x, c.y + r }, V{ c.x - r, c.y }, V{ c.x, c.y - r } };
			for (std::size_t i = 0; i < 4; ++i) {
				Draw::AddLine(a_draw, points[i], points[i + 1], a_track, a_thickness);
			}
			float left = Clamp01(a_t) * 4.0f;
			for (std::size_t i = 0; i < 4 && left > 0.0f; ++i) {
				const float part = std::min(left, 1.0f);
				const V     a = points[i], b = points[i + 1];
				Draw::AddLine(a_draw, a, { a.x + (b.x - a.x) * part, a.y + (b.y - a.y) * part }, a_fill, a_thickness);
				left -= part;
			}
		}

		// The colours rarity has in most games: grey, green, blue, purple, orange.
		U32 RarityColour(Traits::Rarity a_rarity)
		{
			switch (a_rarity) {
			case Traits::Rarity::kUncommon: return IM_COL32(112, 198, 98, 255);
			case Traits::Rarity::kRare: return IM_COL32(82, 150, 236, 255);
			case Traits::Rarity::kEpic: return IM_COL32(176, 102, 234, 255);
			case Traits::Rarity::kLegendary: return IM_COL32(242, 162, 52, 255);
			default: return IM_COL32(150, 150, 146, 255);
			}
		}

		// A companion's band in their rarity's colour, deepened so white names stay easy to read. A
		// companion with no trait of their own keeps the theme's band.
		U32 RarityBand(Traits::Rarity a_rarity)
		{
			return a_rarity > Traits::Rarity::kCommon ? Mix(kShade, RarityColour(a_rarity), 0.72f) : 0;
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
				return Tr("no threat");
			case Rules::Danger::kEasy:
				return Tr("easy");
			case Rules::Danger::kEven:
				return Tr("an even match");
			case Rules::Danger::kHard:
				return Tr("hard");
			default:
				return Tr("deadly");
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
                kinds += kinds.empty() ? "" : Tr(", ");
                kinds += a_kind;
			};
			if (a_enemy.boss) {
				add(Tr("boss"));
			}
			if (a_enemy.caster) {
				add(Tr("caster"));
			}
			if (a_enemy.healer) {
				add(Tr("healer"));
			}
			if (a_enemy.archer) {
				add(Tr("archer"));
			}
			return kinds.empty() ? std::string(Tr("warrior")) : kinds;
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

		std::string_view HandLabel(std::size_t a_hand) { return a_hand == kRightHand ? Tr("RIGHT HAND") : Tr("LEFT HAND"); }

		bool Fits(const View::Spell& a_spell, std::size_t a_hand) { return a_hand==2 || a_spell.twoHanded || (a_hand == kRightHand ? a_spell.right : a_spell.left); }

		// What a role's spells may be, in one line.
		std::string SpellRule(const View::Member& a_member)
		{
			if (a_member.orders.role == Role::kMage) {
				return std::string(Tr("A Mage holds any spell but a heal."));
			}
			if (Settings::Get().healersAnySpell) {
				return std::string(Tr("A Healer holds any spell (Healers settings)."));
			}
			const auto cap = Rules::HealerTierCap(a_member.level);
			std::string line = Trf("A Healer holds any spell, with attack spells up to {}", Tr(Rules::tierNames[static_cast<std::size_t>(cap)]));
			if (cap == Rules::SpellTier::kNovice) {
				line += Tr(" (Apprentice from level 10)");
			} else if (cap == Rules::SpellTier::kApprentice) {
				line += Tr(" (Adept from level 25)");
			}
			return line + ".";
		}

		// --- State -------------------------------------------------------------------------

		enum Section : int
		{
			kRoleSection,
			kTacticsSection,
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
			bool       tacticsOpen{ false };
			RE::FormID tacticsMember{ 0 };
			float      clock{ 0.0f };
			Tweens     tw;

			std::string tip;
			std::string tipHeading;
			std::string tipRank;
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
                int transition{0}; // 1: return to companion, 2: switch companion
                float transitionAt{0.f};
                RE::FormID nextMember{0};
                bool transitionApplied{false};
                bool followRole{false};
				Role       path{ Role::kFighter };
				int        cursor{ 0 };
				float      openedAt{ 0.0f };
				float      tabAt{ 0.0f };  // the path last changed: its nodes come in again
				bool       pathPickerOpen{ false };
				float      pickerT{ 0.0f };
				int        pathPickerCursor{ 0 };
				bool       detailOpen{ false };
				float      detailT{ 0.0f };  // shared transition for the tree and its docked detail panel
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

			// The roles beside the card, out of a fight: open while the role or the list is pointed at,
			// or from the pad until it chooses or backs out.
			struct RoleList
			{
				RE::FormID member{ 0 };
				int        item{ 0 };
				bool       viaPad{ false };
				float      openedAt{ 0.0f };
				float      away{ 0.0f };  // seconds the pointer has been off the role and the list
				V          tileMin{};
				V          tileMax{};
			} roles;
		};

		State state;

		// Information, not a description of a control: shown whatever the tooltip setting says.
		void Readout(std::string_view a_text)
		{
			state.tip = std::string(a_text);
			state.tipHeading.clear();
			state.tipRank.clear();
			state.tipShown = true;
			state.tipAnchored = false;
		}

		void Tip(std::string_view a_text)
		{
			if (Settings::Get().tooltips) {
				Readout(a_text);
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
				if (!member.CanTakeRole()) {
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
				if (member.CanTakeRole()) {
					ids.push_back(member.id);
				}
			}
			if (ids.empty()) {
				return;
			}
			const auto it = std::find(ids.begin(), ids.end(), state.selected);
			int        index = it == ids.end() ? 0 : static_cast<int>(it - ids.begin());
			index = (index + a_step + static_cast<int>(ids.size())) % static_cast<int>(ids.size());
			const auto next=ids[static_cast<std::size_t>(index)];
            if(state.page.transition)return;
            if(state.page.member && next!=state.selected) {
                state.page.transition=2; state.page.transitionAt=state.clock;
                state.page.nextMember=next; state.page.transitionApplied=false;
                state.page.pathPickerOpen=false;
            } else state.selected=next;
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

		// Keyboard and pad focus: a soft theme-coloured glow and clear corner brackets.
		void FocusOutline(Im::ImDrawList* a_draw, V a_min, V a_max, float, float a_alpha)
		{
			const float pulse = 0.8f + 0.2f * std::sin(state.clock * 3.0f);
            for (int layer = 8; layer > 0; --layer) {
                const float spread = static_cast<float>(layer) * 1.8f * uiScale;
                Draw::AddRect(a_draw, {a_min.x-spread,a_min.y-spread}, {a_max.x+spread,a_max.y+spread},
                    Alpha(kFocusRing, a_alpha * pulse * .055f), 6.0f*uiScale, 0, 3.0f*uiScale);
            }
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
			PanelFill(a_draw, { 0, 0 }, a_display, Alpha(palette.vignette, a_alpha), 0.0f, 0);
			PanelGradient(a_draw, { 0, 0 }, { a_display.x, edge }, dark, dark, clear, clear);
			PanelGradient(a_draw, { 0, a_display.y - edge * 1.2f }, a_display, clear, clear, dark, dark);
			PanelGradient(a_draw, { 0, 0 }, { edge, a_display.y }, dark, clear, clear, dark);
			PanelGradient(a_draw, { a_display.x - edge, 0 }, a_display, clear, dark, dark, clear);
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
				if (!member.head.on || !member.CanTakeRole()) {
					continue;
				}
				const V     p = ScreenOf(member.head, a_display);
				const float chosen = state.tw.Toward("worldChosen", member.id, member.id == chosenId ? 1.0f : 0.0f, 10.0f);
				const float r = (10.0f + 3.0f * chosen) * a_s;
				Halo(a_draw, p, r * 2.4f, kAccent, chosen * (0.7f + 0.3f * std::sin(state.clock * 3.0f)) * alpha);
					Emblem(a_draw, p, r * 1.25f, Alpha(Mix(IM_COL32(16, 16, 17, 245), (palette.Subdued() ? palette.points : IM_COL32(64, 16, 15, 248)), chosen), alpha), Alpha(Mix(kTextDim, kEdge, chosen), alpha),
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
				Emblem(a_draw, p, r * 1.15f, Alpha(IM_COL32(16, 17, 19, 245), alpha), Alpha(rim, alpha), (1.6f + lift) * a_s);
				Glyph(a_draw, p, r * 0.95f, EnemyGlyph(enemy), Alpha(rim, alpha));
				if (focused) {
					Draw::AddCircle(a_draw, p, r + 9.0f * a_s, Alpha(kFocusRing, alpha * (0.65f + 0.35f * std::sin(state.clock * 5.0f))), 40, 1.6f * a_s);
				}
				TextOnWorld(a_draw, { p.x, p.y - r * 1.15f - 14.0f * a_s }, rim, 18.0f * a_s, std::format("{}", enemy.level), alpha * pop);
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
					std::string tip = Trf("{}, level {}\n{}, {} for {}.", enemy.name, enemy.level, Describe(enemy), DangerWord(danger),
						a_selected ? a_selected->name : std::string(Tr("you")));
					if (enemy.healthMax > 0.0f) {
						tip += Trf("\nHealth {:.0f} / {:.0f}  ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â·  Magicka {:.0f} / {:.0f}  ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â·  Stamina {:.0f} / {:.0f}", std::max(enemy.healthNow, 0.0f),
							enemy.healthMax, std::max(enemy.magickaNow, 0.0f), enemy.magickaMax, std::max(enemy.staminaNow, 0.0f), enemy.staminaMax);
					}
					if (const auto* tank = enemy.heldBy ? findMember(enemy.heldBy) : nullptr) {
						tip += Trf("\nHeld by {}'s taunt.", tank->name);
					}
					// Who they are and how they stand is shown whatever the tooltip setting; only the
					// hint on what a click does goes with it.
					if (Settings::Get().tooltips) {
						tip += Tr("\nClick to send someone after them.");
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
			return s >= 60 ? Trf("{}:{:02} left", s / 60, s % 60) : Trf("{} s left", s);
		}

		std::string StatusText(const View::Status& a_status)
		{
			std::string text = std::format("{}\n{}", a_status.name, a_status.detail);
			if (a_status.left >= 0.0f) {
				text += "\n" + TimeLeft(a_status.left);
			} else if (a_status.progress >= 0.0f) {
				text += Trf("\n{:.0f}% of the way back", a_status.progress * 100.0f);
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
				(!m.CanTakeRole() ? summons : companions).push_back(&m);
			}
			const float spacing = 142.0f * a_s;
			const float smallSpacing = 64.0f * a_s;
			const float total = spacing * std::max<float>(static_cast<float>(companions.size()) - 1.0f, 0.0f) +
			                    (summons.empty() ? 0.0f : 40.0f * a_s + smallSpacing * static_cast<float>(summons.size()));
			const float startX = a_display.x * 0.5f - total * 0.5f;
			const float baseY = a_display.y - 136.0f * a_s;

			if (companions.empty() && summons.empty()) {
				TextCentered(a_draw, { a_display.x * 0.5f, baseY }, Alpha(kTextDim, Stage(a_appear, 0.2f, 0.5f)), 16.0f * a_s,
					Tr("No companions in reach. Followers nearby, Walk With Me's party and summons appear here."));
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
				Emblem(a_draw, { c.x + 2.0f * a_s, c.y + 5.0f * a_s }, R, Alpha(kShade, a * 0.35f), Alpha(kShade, 0.0f), 1.0f);
				Emblem(a_draw, c, R, Alpha(Mix(IM_COL32(17, 17, 18, 245), (palette.Subdued() ? palette.points : IM_COL32(60, 15, 14, 248)), sel), a),
					Alpha(m.down ? kRed : Mix(kGlassEdge, kEdge, std::max(sel, hover * 0.5f)), a * (m.down ? pulse : 1.0f)), (1.0f + 0.8f * sel) * a_s);
				if (sel > 0.01f) {
					Emblem(a_draw, c, R - 5.0f * a_s, Alpha(kShade, 0.0f), Alpha(kEdge, a * sel * 0.45f), 1.0f);
				}
				Glyph(a_draw, c, R * 0.62f * (1.0f + 0.06f * hover), Info(m.orders.role).glyph, Alpha(Mix(kTextDim, kText, std::max(sel, hover * 0.6f)), a));

				// The level, on a small diamond at the crest's right shoulder.
				{
					const V     lc{ c.x + R * 0.86f, c.y - R * 0.42f };
					const float lr = 13.0f * a_s;
					const U32   frame = m.rarity > Traits::Rarity::kCommon ? RarityColour(m.rarity) : Mix(kGlassEdge, kEdge, std::max(sel, hover * 0.5f));
					const float fresh = m.leveling && m.levelUpAge < 3.0f ? 1.0f - m.levelUpAge / 3.0f : 0.0f;
					if (fresh > 0.0f) {
						Halo(a_draw, lc, lr * 2.4f, kGold, a * fresh);
					}
					Diamond(a_draw, lc, lr, Alpha(IM_COL32(17, 17, 18, 250), a), Alpha(Mix(frame, kGold, fresh), a), 1.3f * a_s);
					InkCentered(a_draw, lc, Alpha(Mix(kText, kGold, fresh), a), 12.5f * a_s, std::format("{}", m.level));
				}

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
				const float nameSize=17.f*a_s*std::min(1.f,(spacing-12*a_s)/std::max(TextSize(17*a_s,m.name).x,1.f));
				TextCentered(a_draw, { x, ny }, Alpha(m.down ? kRed : Mix(kTextDim, kText, std::max(sel, hover)), a), nameSize, m.name);
				const auto roleLabel=m.down?std::string(Tr("DOWN")):std::string(Tr(Info(m.orders.role).name));
				TextCentered(a_draw, { x, ny + 19.0f * a_s }, Alpha(m.down ? kRed : kTextFaint, a), 12.0f * a_s, roleLabel);
			}

			// A short red rule glides along the line under the chosen name.
			const float rx = state.tw.Toward("stripRule", 0, selectedX, 12.0f);
			const float ruleAlpha = Stage(a_appear, 0.3f, 0.4f);
			PanelFill(a_draw, { rx - 26.0f * a_s, lineY - 1.0f * a_s }, { rx + 26.0f * a_s, lineY + 1.0f * a_s }, Alpha(kEdge, ruleAlpha), 0.0f, 0);
			Diamond(a_draw, { rx, lineY }, 4.0f * a_s, Alpha(kAccent, ruleAlpha), Alpha(kEdge, ruleAlpha), 1.0f);

			// Summons sit to the side, smaller: they are protected and healed, not commanded.
			float sx = companions.empty() ? a_display.x*.5f-smallSpacing*static_cast<float>(summons.size()-1)*.5f :
                startX + spacing * static_cast<float>(companions.size() - 1) + 40.0f * a_s + smallSpacing * 0.5f;
			for (std::size_t i = 0; i < summons.size(); ++i) {
				const auto& m = *summons[i];
				const float enter = Stage(a_appear, 0.2f + 0.06f * static_cast<float>(companions.size() + i), 0.45f);
				const V     c{ sx, baseY + (1.0f - enter) * 36.0f * a_s };
				const float R = 18.0f * a_s;
				Emblem(a_draw, c, R, Alpha(IM_COL32(17, 17, 18, 220), enter * 0.85f), Alpha(kGlassEdge, enter), 1.0f);
				Glyph(a_draw, c, R * 0.8f, m.creature ? 0xF1B0 : 0xF0D0, Alpha(kTextFaint, enter));
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
				return std::string(Tr("Unbound"));
			}
			wchar_t    name[64]{};
			const LONG param = static_cast<LONG>((a_key & 0x7F) << 16 | (a_key & 0x80 ? 1 << 24 : 0));
			if (::GetKeyNameTextW(param, name, static_cast<int>(std::size(name))) > 0) {
				const int   size = ::WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
				std::string out(static_cast<std::size_t>(std::max(size - 1, 0)), '\0');
				::WideCharToMultiByte(CP_UTF8, 0, name, -1, out.data(), size, nullptr, nullptr);
				return out;
			}
			return Trf("Key {}", a_key);
		}

		// Shift, Ctrl or Alt, either side.
		const char* ModifierName(std::uint32_t a_modifier)
		{
			switch (ModifierOf(a_modifier)) {
			case kShift:
				return Trc("Shift");
			case kCtrl:
				return Trc("Ctrl");
			case kAlt:
				return Trc("Alt");
			default:
				return Trc("None");
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
		Rect Panel(Im::ImDrawList* a_draw, V a_origin, float a_width, float a_pad, float a_glass, F&& a_body, bool a_soft = false)
		{
			Draw::ChannelsSplit(a_draw, 2);
			Draw::ChannelsSetCurrent(a_draw, 1);
			const float height = a_body(V{ a_origin.x + a_pad, a_origin.y + a_pad }, a_width - a_pad * 2.0f);
			const V     max{ a_origin.x + a_width, a_origin.y + height + a_pad * 2.0f };
			Draw::ChannelsSetCurrent(a_draw, 0);
			if (a_soft) {
                // A feathered shadow lets the battlefield show through around the abilities.
                auto shade = [&](float u, float v) {
                    const float fade = std::pow(std::max(0.f, 1.f-std::pow(2.f*u-1.f, 4.f)), .7f) * std::sin(v*3.14159265f);
                    return Alpha(palette.inset, a_glass*.78f*fade);
                };
                for (int j=0;j<8;++j) for(int i=0;i<8;++i) {
                    const float u=i/8.f,v=j/8.f,un=(i+1)/8.f,vn=(j+1)/8.f;
                    Draw::AddRectFilledMultiColor(a_draw,{a_origin.x+a_width*u,a_origin.y+(max.y-a_origin.y)*v},
                        {a_origin.x+a_width*un,a_origin.y+(max.y-a_origin.y)*vn},shade(u,v),shade(un,v),shade(un,vn),shade(u,vn));
                }
            } else Frame(a_draw, a_origin, max, a_glass);
			Draw::ChannelsMerge(a_draw);
			return { a_origin, max };
		}

		// Fire, frost, shock, poison, magic: the order of View::Member::resist.
		constexpr std::array<std::uint32_t, 5> kElements{ { 0xF06D, 0xF2DC, 0xF0E7, 0xF714, 0xF0D0 } };

		// What a resistance looks like: a weakness in red, any resistance bright, none faint.
		U32 ResistTone(float a_value) { return a_value < 0.0f ? kEdge : (a_value > 0.0f ? kText : kTextFaint); }

		// The role box in a fight: the role they fight as, lit, and beside it what they bring to
		// it: armor and damage, then what turns aside.
		// The roles in the order a line forms up, front to back: the order the role list shows them in.
		constexpr std::array<Role, static_cast<std::size_t>(Role::kCount)> kRoleRow{ Role::kTank, Role::kFighter, Role::kBruiser, Role::kArcher, Role::kMage,
			Role::kHealer };

		int RoleSlot(Role a_role)
		{
			const auto it = std::find(kRoleRow.begin(), kRoleRow.end(), a_role);
			return it == kRoleRow.end() ? 0 : static_cast<int>(it - kRoleRow.begin());
		}

		// The role they fight as and, beside it, what they bring to it. Out of a fight (a_choosable)
		// clicking the role, or activating it with the pad, opens the list beside the card.
		void RoleInFight(Im::ImDrawList* a_draw, const View::Member& a_member, Role a_role, V a_min, float a_width, float a_tileW, float a_height, float a_s,
			float a_alpha, bool a_choosable = false, bool a_focused = false, bool a_activate = false)
		{
			const auto& info = Info(a_role);
			{
				const V    max{ a_min.x + a_tileW, a_min.y + a_height };
				const bool open = state.roles.member == a_member.id;
				bool       hovered = false;
				const bool clicked = Hit("roleInFight", a_min, { a_tileW, a_height }, hovered);
				const float lift = state.tw.Toward("roleTile", a_member.id, a_choosable && (hovered || a_focused || open) ? 1.0f : 0.0f, 14.0f);
				Lit(a_draw, a_min, max, a_alpha);
				PanelEdge(a_draw, a_min, max, Alpha(Mix(kEdge, kText, lift * 0.6f), a_alpha * (0.5f + 0.5f * lift)), 0.0f, 0, 1.0f);
				const float cx = a_min.x + a_tileW * 0.5f;
				Glyph(a_draw, { cx, a_min.y + a_height * 0.38f }, 34.0f * a_s, info.glyph, Alpha(kText, a_alpha));
				TextCentered(a_draw, { cx, a_min.y + a_height * 0.72f }, Alpha(kText, a_alpha), 16.0f * a_s, Tr(info.name));
				if (a_focused) {
					FocusOutline(a_draw, a_min, max, 7.0f * a_s, a_alpha);
				}
				if (a_choosable) {
					// A chevron toward where the list opens.
					Glyph(a_draw, { a_min.x + 12.0f * a_s, a_min.y + 13.0f * a_s }, 10.0f * a_s, 0xF053, Alpha(kText, a_alpha * (0.4f + 0.6f * lift)));
					if (clicked || (a_focused && a_activate)) {
						if (!open) {
							state.roles = {};
							state.roles.member = a_member.id;
							state.roles.item = RoleSlot(a_role);
							state.roles.viaPad = !hovered;
							state.roles.openedAt = state.clock;
							CloseSpellList();
							Sound("UIMenuFocus");
						} else {
							state.roles = {};
							Sound("UIMenuCancel");
						}
						state.roles.tileMin = a_min;
						state.roles.tileMax = max;
					}
				}
				if (hovered) {
					Hovering(0x180);
					if (a_choosable) Tip(std::string(Tr(info.summary)));
					else {
						Tip(Trf("{}\n{}\nRoles are set out of a fight: open the plan with nobody fighting to change it.", Tr(info.name), Tr(info.summary)));
					}
				}
			}

			const float x0 = a_min.x + a_tileW + 8.0f * a_s;
			const float w = a_min.x + a_width - x0;
			const float frameH = 62.0f * a_s;
			const float half = (w - 8.0f * a_s) * 0.5f;
			const std::array<std::pair<std::string_view, std::string>, 2> big{ {
				{ Tr("ARMOR"), std::format("{:.0f}", a_member.armor) },
				{ Tr("DAMAGE"), a_member.damage > 0.0f ? std::format("{:.0f}", a_member.damage) : std::string("-") },
			} };
			for (std::size_t i = 0; i < big.size(); ++i) {
				const V bmin{ x0 + static_cast<float>(i) * (half + 8.0f * a_s), a_min.y };
				const V bmax{ bmin.x + half, bmin.y + frameH };
				PanelFill(a_draw, bmin, bmax, Alpha(kTile, a_alpha), 0.0f, 0);
				PanelEdge(a_draw, bmin, bmax, Alpha(kGlassEdge, a_alpha), 0.0f, 0, 1.0f);
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
				Emblem(a_draw, c, dr, Alpha(weak ? IM_COL32(58, 15, 14, 245) : IM_COL32(19, 19, 20, 245), a_alpha),
					Alpha(weak ? kEdge : (some ? kText : kGlassEdge), a_alpha * (some || weak ? 0.9f : 1.3f)), (weak || some ? 1.3f : 1.0f) * a_s);
				Glyph(a_draw, c, 11.0f * a_s, kElements[i], Alpha(ResistTone(value), a_alpha));
				TextCentered(a_draw, { c.x, c.y + dr + 11.0f * a_s }, Alpha(ResistTone(value), a_alpha), 12.5f * a_s, std::format("{:.0f}%", value));
			}
		}

		// A companion's level, big, in a diamond framed in their rarity's colour, the XP toward the
		// next running round it. A level just earned flares gold for a moment.
		void LevelMark(Im::ImDrawList* a_draw, const View::Member& a_member, V c, float r, float a_alpha, float a_s, float a_size)
		{
			const U32   frame = a_member.rarity > Traits::Rarity::kCommon ? RarityColour(a_member.rarity) : kEdge;
			const float age = a_member.leveling ? a_member.levelUpAge : 99.0f;
			const float fresh = age < 3.0f ? 1.0f - age / 3.0f : 0.0f;
			const float rr = r * (age < 0.6f ? 1.0f + 0.22f * (1.0f - Smooth(age / 0.6f)) : 1.0f);
			if (fresh > 0.0f) {
				Halo(a_draw, c, rr * 2.2f, kGold, a_alpha * fresh);
			}
			Diamond(a_draw, c, rr, Alpha((palette.Subdued() ? palette.inset : IM_COL32(24, 11, 11, 250)), a_alpha), Alpha(Mix(frame, kGold, fresh), a_alpha), 1.6f * a_s);
			Diamond(a_draw, c, rr - 4.5f * a_s, Alpha(kShade, 0.0f), Alpha(kText, a_alpha * 0.28f), 1.0f);
			if (a_member.leveling) {
				// Caught up with the player: a full ring, quieter, as nothing more can be earned for now.
				const float t = a_member.levelCapped ? 1.0f : (a_member.xpNext > 0.0f ? a_member.xp / a_member.xpNext : 0.0f);
				// Keyed by level too: a new level starts its ring afresh instead of running it backwards.
				const float shown = state.tw.Toward("xpRing", (static_cast<std::uint64_t>(a_member.id) << 16) | static_cast<std::uint64_t>(a_member.level), t, 5.0f);
				const U32   fill = a_member.levelCapped ? Alpha(Mix(frame, kShade, 0.4f), a_alpha * 0.85f) : Alpha(Mix(frame, kGold, 0.55f), a_alpha);
				DiamondRing(a_draw, c, rr + 6.0f * a_s, shown, Alpha(kShade, a_alpha * 0.55f), fill, 3.2f * a_s);
			}
			InkCentered(a_draw, c, Alpha(Mix(kText, kGold, fresh), a_alpha), a_size, std::format("{}", a_member.level));
		}

		// What the level's diamond says when pointed at.
		std::string LevelReadout(const View::Member& a_member)
		{
			if (!a_member.leveling) {
				return Trf("Level {}", a_member.level);
			}
			if (a_member.levelCapped) {
				return Trf("Level {}\nCaught up with you: no XP until you level up.", a_member.level);
			}
			return Trf("Level {}\n{:.0f} of {:.0f} XP toward level {}.\nWhen a fight is over, every enemy the party brought down counts, whoever struck last.", a_member.level,
				a_member.xp, a_member.xpNext, a_member.level + 1);
		}

		// --- Unique traits ------------------------------------------------------------------------

		std::uint32_t KindGlyph(Traits::Kind a_kind)
		{
			switch (a_kind) {
			case Traits::Kind::kEvade: return 0xF070;     // an eye struck through
			case Traits::Kind::kSummon: return 0xF0C0;    // figures
			case Traits::Kind::kAura: return 0xF185;      // a sun
			case Traits::Kind::kOnHit: return 0xF05B;     // crosshairs
			case Traits::Kind::kInstinct: return 0xF5DC;  // a brain
			case Traits::Kind::kNerve: return 0xF70C;     // running
			default: return 0xF0E7;                       // a bolt
			}
		}

		// What their own mod gave them, under the roles: a gem in their rarity's colour, the rarity's
		// name, and at most two rows of chips. The last chip holds any overflow. Returns the height.
		float DrawTraits(Im::ImDrawList* a_draw, const View::Member& a_member, V a_pos, float a_width, float a_s, float a_alpha)
		{
			const U32   tone = RarityColour(a_member.rarity);
			const bool  shine = a_member.rarity >= Traits::Rarity::kEpic;
			const float pulse = shine ? 0.5f + 0.5f * std::sin(state.clock * 2.4f) : 0.0f;

			const float gemR = 7.5f * a_s;
			const V     gem{ a_pos.x + gemR, a_pos.y + 7.0f * a_s };
			if (shine) {
				Draw::AddCircleFilled(a_draw, gem, gemR * (1.9f + 0.3f * pulse), Alpha(tone, a_alpha * (0.10f + 0.10f * pulse)), 20);
			}
			Diamond(a_draw, gem, gemR, Alpha(Mix(kShade, tone, 0.75f), a_alpha), Alpha(Mix(tone, IM_COL32(255, 255, 255, 255), 0.35f), a_alpha), 1.2f * a_s);
			const float lx = gem.x + gemR + 9.0f * a_s;
			Label(a_draw, { lx, a_pos.y + 1.0f * a_s }, Alpha(kTextDim, a_alpha), 11.0f * a_s, Upper(Tr("Unique traits")), 1.6f * a_s);
			const auto  rarity = Upper(Tr(Traits::RarityName(a_member.rarity)));
			const float rw = LabelWidth(11.0f * a_s, rarity, 1.6f * a_s);
			Label(a_draw, { a_pos.x + a_width - rw, a_pos.y + 1.0f * a_s }, Alpha(tone, a_alpha), 11.0f * a_s, rarity, 1.6f * a_s);
			bool rarityHovered = false;
			Hit("traitRarity", a_pos, { a_width, 18.0f * a_s }, rarityHovered);
			if (rarityHovered) {
				Readout(Trf("{} distinct signature abilities. Locked abilities and morale drawbacks do not raise rarity. Shared combat perks and utility effects are excluded.",
					Traits::Counted(a_member.traits)));
			}

			float y = a_pos.y + 21.0f * a_s;
			if (a_member.traits.empty()) {
				Text(a_draw, { lx, y }, Alpha(kTextFaint, a_alpha), 13.5f * a_s, Tr("No signature abilities detected."));
				return y + 18.0f * a_s - a_pos.y;
			}
			const float chipH = 26.0f * a_s;
			const float chipGap = 6.0f * a_s;
			const float chipPad = 9.0f * a_s;
			const float glyphW = 16.0f * a_s;
			const float size = 13.5f * a_s;
			const float w = (a_width - chipGap) * 0.5f;
			const auto visible = Traits::CardVisible(a_member.traits.size());
			const auto slots = std::min(a_member.traits.size(), Traits::kCardSlots);
			for (std::size_t index = 0; index < slots; ++index) {
				const bool overflow = index == visible;
				const auto& trait = a_member.traits[index];
				const auto name = overflow ? Trf("+{} more", a_member.traits.size() - visible) : trait.name;
				const V min{ a_pos.x + static_cast<float>(index % 2) * (w + chipGap), y + static_cast<float>(index / 2) * (chipH + chipGap) };
				const V max{ min.x + w, min.y + chipH };
				bool    hovered = false;
				Im::PushID(3100 + static_cast<int>(index));
				Hit("trait", min, { w, chipH }, hovered);
				Im::PopID();
				const float lit = state.tw.Toward("traitHover", static_cast<std::uint64_t>(a_member.id) * 16 + static_cast<std::uint64_t>(index), hovered ? 1.0f : 0.0f, 14.0f);
				const bool locked = !overflow && trait.locked;
				const float dim = locked ? 0.45f : 1.0f;
				PanelFill(a_draw, min, max, Alpha(Mix(kTile, kTileHover, lit), a_alpha), 0.0f, 0);
				PanelEdge(a_draw, min, max, Alpha(Mix(kGlassEdge, tone, locked ? 0.2f : 0.55f + 0.45f * lit), a_alpha), 0.0f, 0, 1.0f);
				PanelFill(a_draw, min, { min.x + 2.0f * a_s, max.y }, Alpha(tone, a_alpha * dim), 0.0f, 0);
				Glyph(a_draw, { min.x + chipPad + glyphW * 0.5f, min.y + chipH * 0.5f }, 12.5f * a_s, overflow ? 0xF141 : locked ? 0xF023 : KindGlyph(trait.kind),
					Alpha(Mix(kTextDim, kText, lit), a_alpha * dim));
				const float textX = min.x + chipPad + glyphW + 6.0f * a_s;
				const float textW = std::max(0.0f, max.x - chipPad - textX);
				auto caption = name;
				if (TextSize(size, caption).x > textW) {
					while (!caption.empty() && TextSize(size, caption + "...").x > textW) {
						auto last = caption.size() - 1;
						while (last > 0 && (static_cast<unsigned char>(caption[last]) & 0xC0) == 0x80) {
							--last;
						}
						caption.resize(last);
					}
					caption += "...";
				}
				Draw::PushClipRect(a_draw, { textX, min.y }, { max.x - chipPad, max.y }, true);
				Text(a_draw, { textX, min.y + (chipH - TextSize(size, name).y) * 0.5f },
					Alpha(Mix(kTextDim, kText, 0.6f + 0.4f * lit), a_alpha * dim), size, caption);
				Draw::PopClipRect(a_draw);
				if (hovered) {
					Hovering(0x3100 + static_cast<std::uint64_t>(index));
					std::string text;
					const auto end = overflow ? a_member.traits.size() : index + 1;
					for (auto detail = index; detail < end; ++detail) {
						const auto& item = a_member.traits[detail];
						if (!text.empty()) {
							text += overflow ? "\n" : "\n\n";
						}
						text += std::format("{}  ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â·  {}", item.name, Tr(Traits::KindName(item.kind)));
						if (overflow) {
							if (item.locked) {
								text += std::format(" ({})", Tr("Locked"));
							}
							continue;
						}
						if (!item.what.empty()) {
							text += "\n" + item.what;
						}
						if (!item.when.empty()) {
							text += "\n" + item.when;
						}
						if (item.locked) {
							text += std::format("\n{}", Tr("Locked for now: their mod keeps it back until something is done, a quest of theirs, say."));
						}
					}
					Readout(text);
				}
			}
			return y + static_cast<float>((slots + 1) / 2) * (chipH + chipGap) - chipGap - a_pos.y;
		}

		// Beside the card, out of a fight: every role, front of the line to the back, each with what
		// it does and the special that comes with it. Choosing one closes the list and returns it.
		Rect DrawRoleList(Im::ImDrawList* a_draw, const View::Member& a_member, float a_right, float a_bottom, float a_s, float a_appear, const Input& a_input, int a_dy,
			bool a_back, std::optional<Role>& a_picked)
		{
			auto&       list = state.roles;
			const float width = 410.0f * a_s;
			const float headH = 46.0f * a_s;
			const float rowH = 48.0f * a_s;
			const float pad = 12.0f * a_s;
			const float height = headH + rowH * static_cast<float>(kRoleRow.size()) + pad;
			const float open = Smooth((state.clock - list.openedAt) / 0.16f);
			const float alpha = a_appear * open;
			const float top = std::clamp(list.tileMin.y - headH, 112.0f * a_s, std::max(a_bottom - height, 112.0f * a_s));
			const V     min{ a_right - 10.0f * a_s - width + (1.0f - open) * 24.0f * a_s, top };
			const V     max{ min.x + width, min.y + height };
			auto        close = [&] { list = {}; };

			PanelFill(a_draw, min, max, Alpha(kGlass, alpha), 0.0f, 0);
			PanelEdge(a_draw, min, max, Alpha(kGlassEdge, alpha), 0.0f, 0, 1.0f);
			Band(a_draw, { min.x + 1.0f, min.y + 1.0f }, { max.x - 1.0f, min.y + headH }, alpha);
			Label(a_draw, { min.x + 16.0f * a_s, min.y + 15.0f * a_s }, Alpha(kText, alpha), 12.5f * a_s, Upper(Trf("A role for {}", a_member.name)), 2.0f * a_s);

			// The pad walks the rows; the pointer just points.
			if (list.viaPad && a_dy) {
				list.item = std::clamp(list.item + a_dy, 0, static_cast<int>(kRoleRow.size()) - 1);
				Sound("UIMenuFocus");
			}
			const auto* io = Im::GetIO();
			float       y = min.y + headH + pad * 0.5f;
			for (std::size_t i = 0; i < kRoleRow.size(); ++i) {
				const Role  role = kRoleRow[i];
				const auto& info = Info(role);
				const auto  lack = a_member.arsenalKnown ? LackFor(role, a_member.arsenal) : Lack::kNothing;
				const bool  current = role == a_member.orders.role;
				const bool  playable = lack == Lack::kNothing || current;
				const V     rmin{ min.x + pad * 0.5f, y };
				const V     rmax{ max.x - pad * 0.5f, y + rowH - 4.0f * a_s };
				bool        hovered = false;
				Im::PushID(2600 + static_cast<int>(i));
				const bool clicked = Hit("roleRow", rmin, { rmax.x - rmin.x, rmax.y - rmin.y }, hovered) && open > 0.6f;
				Im::PopID();
				const bool  focused = list.viaPad && list.item == static_cast<int>(i);
				const float lit = state.tw.Toward("roleRow", static_cast<std::uint64_t>(a_member.id) * 8 + i, hovered || focused ? 1.0f : 0.0f, 16.0f);
				if (current) {
					Lit(a_draw, rmin, rmax, alpha);
				} else if (lit > 0.01f) {
					PanelFill(a_draw, rmin, rmax, Alpha(kTileHover, alpha * lit), 0.0f, 0);
				}
				if (focused) {
					FocusOutline(a_draw, rmin, rmax, 7.0f * a_s, alpha);
				}
				const float dim = playable ? 1.0f : 0.4f;
				const V     ec{ rmin.x + 28.0f * a_s, (rmin.y + rmax.y) * 0.5f };
				Emblem(a_draw, ec, 18.0f * a_s, Alpha(IM_COL32(17, 17, 18, 245), alpha), Alpha(Mix(kGlassEdge, RoleAccent(role), current ? 1.0f : lit), alpha * dim), 1.3f * a_s);
				Glyph(a_draw, ec, 15.0f * a_s, info.glyph, Alpha(Mix(kTextDim, kText, current ? 1.0f : lit), alpha * dim));
				const float tx = rmin.x + 58.0f * a_s;
				Text(a_draw, { tx, rmin.y + 7.0f * a_s }, Alpha(current ? kText : Mix(kTextDim, kText, lit), alpha * dim), 16.0f * a_s, Tr(info.name));
				// Theirs now, not on offer, or the special that comes with it.
				std::string tag;
				U32         tagTone = kTextFaint;
				if (current) {
					tag = Upper(Tr("Their role"));
				} else if (!playable) {
					tag = Upper(Tr("Not on offer"));
					tagTone = kRed;
				} else if (const auto special = SpecialOf(role); special != Special::kNone) {
					tag = Upper(Tr(Info(special).name));
				}
				if (!tag.empty()) {
					const float tw = LabelWidth(10.0f * a_s, tag, 1.6f * a_s);
					Label(a_draw, { rmax.x - 10.0f * a_s - tw, rmin.y + 10.0f * a_s }, Alpha(tagTone, alpha), 10.0f * a_s, tag, 1.6f * a_s);
				}
				if (hovered || focused) Tip(std::string(playable ? Tr(info.summary) : Tr(Why(lack))));
				if (hovered) {
					Hovering(0x2600 + static_cast<std::uint64_t>(i));
				}
				if (clicked || (focused && a_input.activate)) {
					if (!playable) {
						Sound("UIMenuCancel");
					} else {
						if (!current) {
							a_picked = role;
						}
						Sound("UIMenuOK");
						close();
						return { min, max };
					}
				}
				y += rowH;
			}

			if (a_back) {
				Sound("UIMenuCancel");
				close();
				return { min, max };
			}
			// Opened by clicking: it stays until a choice or an outside click. The gap
			// between the button and list belongs to the open control.
			if (!list.viaPad && io) {
				const V    mouse{ io->MousePos.x, io->MousePos.y };
				const bool onTile = Inside(mouse, list.tileMin, list.tileMax);
				const bool onList = Inside(mouse, min, max);
				const bool onGap = Inside(mouse, { max.x - 4.0f * a_s, std::min(min.y, list.tileMin.y) }, { list.tileMin.x + 4.0f * a_s, std::max(max.y, list.tileMax.y) });
				if (!onTile && !onList && !onGap && Im::IsMouseClicked(0) && state.clock - list.openedAt > .1f) {
					close();
				}
			}
			return { min, max };
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

		int Unspent(const View::Member& a_member) { return a_member.unlimited ? Paths::kUnlimited : std::max(a_member.pointsEarned - a_member.pointsSpent, 0); }

		// The first node worth looking at: something that can be learned now, or the first ability.
		int FirstWorthy(const View::Member& a_member, Role a_path)
		{
			for (std::size_t i = 0; i < Paths::kNodes; ++i) {
				if (Paths::CanLearn(a_member.record, a_path, i, a_member.pointsEarned, a_member.unlimited) == Paths::Block::kNone) {
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

		// The camera's rise for the tree. Drawn into Fight With Me's plan, the tree tells the plan,
		// whose camera it is; the plan's own calls carry what the tree last said.
		void TreeLift(float a_amount)
		{
			if (hostPlan) {
				hostPlan->lift = a_amount;
			} else {
				Camera::TreeLift(Host::Orders() ? planFrame.lift : a_amount);
			}
		}

		void OpenPathPage(const View::Member& a_member, int a_node)
		{
			auto& page = state.page;
            page.transition=0;
			page.member = a_member.id;
			page.path = a_member.orders.role;
            page.followRole=false;
			page.cursor = a_node >= 0 ? a_node : FirstWorthy(a_member, page.path);
			page.detailOpen = a_node >= 0;
			page.detailT = 0.0f;
			page.openedAt = state.clock;
			page.tabAt = state.clock + TreeEntrance::kReveal;
			page.resetArmed = false;
			page.seenMember = 0;
			page.previewNode = -1;
			page.previewRank = 0;
			CloseSpellList();
			if (!Settings::Get().pathsGuideSeen) {
				OpenGuide(TreeEntrance::kComplete + .2f);
			}
		}

		void ClosePathPage(bool animate=false) {
            auto& page=state.page;
            if(animate && page.member) {
                if(page.transition)return;
                page.transition=1;page.transitionAt=state.clock;page.transitionApplied=false;
                state.selected=page.member;page.pathPickerOpen=false;
                return;
            }
            page.member=0;page.transition=0;TreeLift(0.f);
        }

        void TickTreeTransition() {
            auto& page=state.page;
            if(!page.transition)return;
            state.guide.open=false;
            const float age=state.clock-page.transitionAt;
            const bool exiting=page.transition==1;
            const float swap=exiting?TreeEntrance::kExitSwap:TreeEntrance::kSwitchSwap;
            if(age>=swap && !page.transitionApplied) {
                page.transitionApplied=true;
                if(exiting){state.selected=page.member;page.member=0;TreeLift(0.f);}
                else {
                    state.selected=page.nextMember;page.member=page.nextMember;page.followRole=true;
                    page.seenMember=0;page.detailOpen=false;page.detailT=0.f;
                    page.resetArmed=false;page.tabAt=page.transitionAt+TreeEntrance::kSwitchReveal;
                }
            }
            if(age>=(exiting?TreeEntrance::kExitComplete:TreeEntrance::kSwitchComplete))page.transition=0;
        }

        void DrawTreeTransition(V display) {
            const auto& page=state.page;
            const auto entrance=TreeEntrance::At(state.clock-page.openedAt);
            TreeLift(page.member && !state.closing?std::max(entrance.lift,.0001f):0.f);
            auto* d=Im::GetForegroundDrawList();
            const U32 black=IM_COL32(0,0,0,255),clear=IM_COL32(0,0,0,0);
            if(page.member && entrance.black>0.f)
                Draw::AddRectFilled(d,{0,0},display,Alpha(black,entrance.black),0,0);
            if(!page.transition)return;
            const float age=state.clock-page.transitionAt;
            if(page.transition==1) {
                Draw::AddRectFilled(d,{0,0},display,Alpha(black,TreeEntrance::ExitBlack(age)),0,0);
            } else {
                const auto wipe=TreeEntrance::SwitchWipe(age);
                const float feather=display.x*.10f;
                const float left=wipe.left*(display.x+feather)-feather;
                const float right=wipe.right*(display.x+feather);
                const float solidLeft=age<TreeEntrance::kSwitchReveal?left:left+feather;
                if(right-feather>solidLeft)Draw::AddRectFilled(d,{std::max(0.f,solidLeft),0},{std::min(display.x,right-feather),display.y},black,0,0);
                if(age<TreeEntrance::kSwitchReveal)
                    Draw::AddRectFilledMultiColor(d,{right-feather,0},{right,display.y},black,clear,clear,black);
                else
                    Draw::AddRectFilledMultiColor(d,{left,0},{left+feather,display.y},clear,black,black,clear);
            }
        }

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
			float a_appear, const Input& a_input, bool a_dock = false, std::vector<Rect>* a_extra = nullptr)
		{
			const float swap = Smooth(state.swap);
			const bool  live = !state.closing;
			const bool  nav = state.keyboardNav && live && !state.spells.member;  // the spell list has the pad while open
			const float pad = (a_dock ? 16.0f : 24.0f) * a_s;
			const float gap = 14.0f * a_s;

			Orders orders = a_member.orders;
			bool   changed = false;
			float  y = a_origin.y;

			auto enter = [&](int a_index) { return Stage(a_appear, 0.05f + 0.07f * static_cast<float>(a_index), 0.5f); };
			auto origin = [&](int a_index) {
				return V{ a_origin.x + (1.0f - enter(a_index)) * 48.0f * a_s + (1.0f - swap) * 14.0f * a_s, y };
			};
			auto alphaOf = [&](int a_index) { return enter(a_index) * (0.25f + 0.75f * swap); };

			if (!a_dock) {
			// --- Who, and their role ---
			{
				const float alpha = alphaOf(0);
				const Rect  r = Panel(a_draw, origin(0), a_width, pad, enter(0), [&](V p, float inner) {
                    // The name in capitals on an oxblood band across the panel's top.
                    const float bandTop = p.y - pad + 1.0f;
                    const float bandH = pad + 42.0f * a_s;
                    Band(a_draw, { p.x - pad + 1.0f, bandTop }, { p.x + inner + pad - 1.0f, bandTop + bandH }, alpha, RarityBand(a_member.rarity));
                    {
                        const V     mc{ p.x + inner - 32.0f * a_s, bandTop + bandH * 0.5f };
                        const float mr = 32.0f * a_s;
                        LevelMark(a_draw, a_member, mc, mr, alpha, a_s, 28.0f * a_s);
                        bool hovered = false;
                        Hit("levelMark", { mc.x - mr, mc.y - mr }, { mr * 2.0f, mr * 2.0f }, hovered);
                        if (hovered) {
                            Hovering(0x190);
                            Readout(LevelReadout(a_member));
                        }
                        // A level just earned is called out beside the diamond, where the slashes sit.
                        const float fresh = a_member.leveling && a_member.levelUpAge < 3.0f ? 1.0f - a_member.levelUpAge / 3.0f : 0.0f;
                        Slashes(a_draw, { mc.x - mr - 40.0f * a_s, bandTop + bandH * 0.3f }, bandH * 0.4f, Alpha(kText, alpha * 0.45f * (1.0f - fresh)));
                        if (fresh > 0.0f) {
                            const auto  word = Upper(Tr("Level up"));
                            const float lw = LabelWidth(12.0f * a_s, word, 2.0f * a_s);
                            Label(a_draw, { mc.x - mr - 14.0f * a_s - lw, bandTop + bandH * 0.5f - 8.0f * a_s }, Alpha(kGold, alpha * fresh), 12.0f * a_s, word, 2.0f * a_s);
                        }
                    }
                    const std::string upper = Upper(a_member.name);
                    Text(a_draw, { p.x, bandTop + (bandH - TextSize(26.0f * a_s, upper).y) * 0.5f }, Alpha(kText, alpha), 26.0f * a_s, upper);
                    float yy = bandTop + bandH + 14.0f * a_s;
                    const auto  roleName = std::string(Tr(Info(orders.role).name));
                    std::string status;
                    const bool  noArrows = orders.role == Role::kArcher && a_member.bow && a_member.ammoName.empty();
                    if (a_member.down) {
                        status += Tr("  ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â·  down");
                    } else if (noArrows) {
                        status += Tr("  ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â·  no arrows");
                    } else if (a_member.drained) {
                        status += Tr("  ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â·  out of magicka, falling back");
                    } else if (a_member.inCombat && !a_member.targetName.empty()) {
						status = a_member.targetName;
                    }
                    Text(a_draw, { p.x, yy }, Alpha(RoleAccent(orders.role), alpha), 15.5f * a_s, roleName);
                    float statusX = p.x + TextSize(15.5f * a_s, roleName).x;
                    if (!a_member.down && !noArrows && !a_member.drained && !status.empty()) {
                        const V c{statusX+18*a_s,yy+10*a_s};
                        for(float side : {-1.f,1.f}) {
                            const U32 color=Alpha(kTextDim,alpha);
                            Draw::AddLine(a_draw,{c.x-side*7*a_s,c.y+8*a_s},{c.x+side*7*a_s,c.y-8*a_s},color,2*a_s);
                            Draw::AddLine(a_draw,{c.x-side*9*a_s,c.y+2*a_s},{c.x-side*2*a_s,c.y+8*a_s},color,1.5f*a_s);
                            Draw::AddLine(a_draw,{c.x+side*7*a_s,c.y-8*a_s},{c.x+side*7*a_s,c.y-3*a_s},color,a_s);
                        }
                        statusX+=36*a_s;
                    }
                    Text(a_draw, {statusX, yy}, Alpha(a_member.down || noArrows || a_member.drained ? kRed : kTextDim, alpha), 15.5f * a_s, status);
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
                            const auto text = std::format("{:.0f} / {:.0f}", std::max(now, 0.0f), most);
                            Text(a_draw, { p.x + i * (barW + 8.0f * a_s), yy + 8.0f * a_s }, Alpha(quick ? kRejuvenated : kTextDim, alpha), 13.0f * a_s, text);
                        }
                    }
                    yy += 38.0f * a_s;

                    // The box holds the role they fight as and, beside it, what they bring to it. Roles
                    // are chosen out of a fight: there, clicking the role opens the list of roles.
                    const bool  locked = a_view.inCombat || !a_view.enemies.empty();
                    const float lockIn = state.tw.Toward("roleLocked", 0, locked ? 1.0f : 0.0f, 8.0f);
                    SectionLabel(a_draw, { p.x, yy }, a_s, alpha, Tr("ROLE"), nav && state.section == kRoleSection && !locked);
                    if (locked) Glyph(a_draw, {p.x+inner-8*a_s,yy+7.5f*a_s},10*a_s,0xF023,Alpha(kTextFaint,alpha*lockIn));
                    yy += 24.0f * a_s;
                    const float boxH = 140.0f * a_s;
                    const float tileW = (inner - 2.0f * 8.0f * a_s) / 3.0f;
                    RoleInFight(a_draw, a_member, orders.role, { p.x, yy }, inner, tileW, boxH, a_s, alpha, !locked && live, nav && !locked && state.section == kRoleSection,
                        a_input.activate);
                    yy += boxH;
                    if (Host::Abilities() && a_member.CanTakeRole()) {
                        yy += 16.0f * a_s;
                        yy += DrawTraits(a_draw, a_member, { p.x, yy }, inner, a_s, alpha);
                    }
                    return yy - p.y;
				});
				y = r.max.y + gap;
			}

			// --- What they go for, and where they stand ---
            {
                if (state.tacticsMember != a_member.id) { state.tacticsMember=a_member.id; state.tacticsOpen=false; }
                const bool navTactics=nav && state.section==kTacticsSection;
                const float headerY=y;
                const Rect header=Panel(a_draw,origin(1),a_width,16*a_s,enter(1),[&](V p,float inner){
                    bool hovered=false;
                    const float buttonW=44*a_s, height=40*a_s;
                    const V end{p.x+buttonW,p.y+height};
                    if((Hit("tacticsToggle",p,{buttonW,height},hovered) || (navTactics && a_input.activate)) && live){state.tacticsOpen=!state.tacticsOpen;Sound("UIMenuOK");}
                    if(navTactics) { FocusOutline(a_draw,p,end,0,alphaOf(1)); TipBeside(std::string(Tr("Formations")),p,end); }
                    PanelFill(a_draw,p,end,Alpha(palette.inset,alphaOf(1)*.9f));
                    PanelEdge(a_draw,p,end,Alpha(kEdge,alphaOf(1)*(hovered||state.tacticsOpen?.9f:.45f)));
                    Glyph(a_draw,{p.x+buttonW*.5f,p.y+height*.5f},23*a_s,0xF0C0,Alpha(kText,alphaOf(1)));
                    const float lineStart=p.x+buttonW+16*a_s,lineEnd=p.x+inner,lineY=p.y+height*.5f;
                    Draw::AddLine(a_draw,{lineStart,lineY},{lineEnd,lineY},Alpha(kEdge,alphaOf(1)*.45f),a_s);
                    if(palette.Engraved()) {
                        for(float x : {lineStart+10*a_s,lineEnd-10*a_s}) Draw::AddQuad(a_draw,{x-5*a_s,lineY},{x,lineY-4*a_s},{x+5*a_s,lineY},{x,lineY+4*a_s},Alpha(kEdge,alphaOf(1)*.7f),a_s);
                    } else if(palette.Subdued()) {
                        for(float x : {lineStart,lineEnd}) Draw::AddCircle(a_draw,{x,lineY},2*a_s,Alpha(kTextDim,alphaOf(1)),12,a_s);
                    } else {
                        Diamond(a_draw,{lineEnd-5*a_s,lineY},4*a_s,Alpha(kAccent,alphaOf(1)),Alpha(kEdge,alphaOf(1)),a_s);
                    }
                    if(hovered) Tip(std::string(Tr("Formations")));
                    return height;
                },true);
                y=header.max.y+gap;
                const float expanded=state.tw.Toward("tacticsOpen",a_member.id,state.tacticsOpen?1.f:0.f,14.f);
                if(expanded>.01f) {
                const float alpha = alphaOf(1)*expanded;
                const V popup{a_origin.x-a_width-gap,std::min(headerY,Im::GetIO()->DisplaySize.y-370*a_s)+(1-expanded)*10*a_s};

                const Rect r=Panel(a_draw,popup,a_width,pad,enter(1)*expanded,[&](V p,float inner) {
                    float yy = p.y;

                    // Focus: one value at a time, stepped with the arrows.
                    const bool focusRow = nav && state.tacticsOpen && state.section == kFocusSection;
                    SectionLabel(a_draw, { p.x, yy }, a_s, alpha, Tr("FOCUS"), focusRow);
                    yy += 24.0f * a_s;
                    {
                        const float h = 52.0f * a_s;
                        const V     min{ p.x, yy }, max{ p.x + inner, yy + h };
                        PanelFill(a_draw, min, max, Alpha(kTile, alpha), 0.0f, 0);
                        PanelEdge(a_draw, min, max, Alpha(kGlassEdge, alpha), 0.0f, 0, 1.0f);
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
                        std::string name(Tr(info.name));
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
                            Tip(std::string(Tr(info.summary)));
                        } else if (focusRow) {
                            TipBeside(std::string(Tr(info.summary)), min, max);
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
                    const bool positionRow = nav && state.tacticsOpen && state.section == kPositionSection;
                    SectionLabel(a_draw, { p.x, yy }, a_s, alpha, Tr("POSITION"), positionRow);
                    yy += 24.0f * a_s;
                    {
                        const int   count = kPositionChoices;
                        const float h = 48.0f * a_s;
                        const float segW = inner / count;
                        const V     min{ p.x, yy }, max{ p.x + inner, yy + h };
                        const float dr = 18.0f * a_s;
                        Draw::AddLine(a_draw, { min.x - 6.0f * a_s, min.y + h * 0.5f }, { max.x + 6.0f * a_s, min.y + h * 0.5f }, Alpha(kGlassEdge, alpha * 0.8f), 1.0f);
                        for (int i = 0; i < count; ++i) {
                            Emblem(a_draw, { min.x + segW * (i + 0.5f), min.y + h * 0.5f }, dr, Alpha(IM_COL32(19, 19, 20, 245), alpha), Alpha(kGlassEdge, alpha * 1.3f), 1.0f);
                        }
                        if (positionRow) {
                            FocusOutline(a_draw, min, max, 0.0f, alpha);
                        }
                        if (positionRow && a_input.dx) {
                            const int next = (static_cast<int>(Live(orders.position)) - kFirstPosition + (a_input.dx > 0 ? 1 : -1) + count) % count;
                            orders.position = static_cast<Position>(kFirstPosition + next);
                            changed = true;
                            Sound("UIMenuFocus");
                        }
                        const float px = state.tw.Toward("positionPill", 0, static_cast<float>(static_cast<int>(Live(orders.position)) - kFirstPosition), 14.0f);
                        const V pc{ min.x + segW * (px + 0.5f), min.y + h * 0.5f };
                        Halo(a_draw, pc, dr * 1.6f, kAccent, alpha * 0.8f);
                        Emblem(a_draw, pc, dr + 1.0f * a_s, Alpha(kAccent, alpha * 0.85f), Alpha(kEdge, alpha), 1.6f * a_s);
                        int shown = static_cast<int>(Live(orders.position)) - kFirstPosition;
                        for (int i = 0; i < count; ++i) {
                            const auto position = static_cast<Position>(kFirstPosition + i);
                            const V    smin{ min.x + segW * i, min.y };
                            bool       hovered = false;
                            Im::PushID(2200 + i);
                            if (Hit("position", smin, { segW, h }, hovered) && live && orders.position != position) {
                                orders.position = position;
                                changed = true;
                                Sound("UIMenuOK");
                            }
                            Im::PopID();
                            if (hovered) {
                                shown = i;
                                Hovering(0x180 + static_cast<std::uint64_t>(i));
                                Tip(std::format("{}\n{}", Tr(Info(position).name), Tr(Info(position).summary)));
                            }
                            const float hover = state.tw.Toward("positionHover", static_cast<std::uint64_t>(i), hovered ? 1.0f : 0.0f, 16.0f);
                            const float on = orders.position == position ? 1.0f : hover * 0.6f;
                            Glyph(a_draw, { smin.x + segW * 0.5f, min.y + h * 0.5f }, 15.0f * a_s, Info(position).glyph,
                                Alpha(Mix(kTextDim, kText, on), alpha));
                        }
                        yy += h + 12.0f * a_s;
                        // One short line naming the stance, so the icons never have to be guessed.
                        const auto  stance = static_cast<Position>(kFirstPosition + shown);
                        const std::string line(Tr(Info(stance).name));
                        TextCentered(a_draw, { p.x + inner * 0.5f, yy + 8.0f * a_s }, Alpha(stance == orders.position ? kText : kTextDim, alpha), 16.0f * a_s, line);
                        if (positionRow) {
                            TipBeside(std::string(Tr(Info(stance).summary)), min, max);
                        }
                        yy += 20.0f * a_s;
                    }
                    return yy - p.y;
				});
                if(a_extra) a_extra->push_back(r);
                } // expanded tactics
			}

			}
			// --- Large ability dock, separate from the companion settings ---
			// Tanks and healers have their taunt or heal; every role its special.
			// With the Paths add-on: the two abilities their path carries, and the path.
			const bool    tree = a_member.paths;
			const bool    tank = !tree && orders.role == Role::kTank;
			const bool    healer = !tree && orders.role == Role::kHealer;
			const bool    primary = tank || healer;
			const Special special = tree ? Special::kNone : SpecialOf(orders.role);
			const bool    hasSlider = HasSlider(a_member);
			if (a_dock) {
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
					// Ability icons carry the hierarchy; no panel heading.
					yy += 8.0f * a_s;

					const int   count = tree ? static_cast<int>(Paths::kSlots) : (primary ? 1 : 0) + (healer ? 1 : 0) + (special != Special::kNone ? 1 : 0);
					const float slot = inner / static_cast<float>(std::max(count, 1));
					const float radius = std::min(56.0f * a_s, slot * 0.36f);
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
						int           rank{ 0 };
						bool          orb{ false };       // Magnus: its ball of light instead of an icon
						bool          empty{ false };
					};
					auto badge = [&](Badge a_badge) {
                        const int resourceRank=a_badge.rank>0?a_badge.rank:(a_member.level>=60?3:a_member.level>=30?2:1);
                        if (!a_badge.empty)
                            a_badge.tip += Trf("\nCost: {:.0f}% maximum {}",AbilityResource::Share(resourceRank)*100,ChoosesSpells(orders.role)?Tr("magicka"):Tr("stamina"));
						const V     c{ top.x + slot * (static_cast<float>(placed) + 0.5f), top.y + radius - 10.0f * a_s };
						const float id = static_cast<float>(a_badge.index);
						++placed;
						const float charge = a_badge.cooldown > 0.0f && a_badge.ready > 0.0f ? Clamp01(1.0f - a_badge.ready / a_badge.cooldown) : 1.0f;
						const bool  cooling = charge < 1.0f;  // cooldowns run on after a fight, so the wait shows then too
						const bool affordable=a_member.unlimited || (ChoosesSpells(orders.role)?a_member.magicka:a_member.stamina)>=AbilityResource::Share(resourceRank);
						const bool lit = a_badge.usable && !cooling && affordable;

						bool hovered = false;
						Im::PushID(a_badge.index);
						const bool clicked = Hit("badge", { c.x - slot*.47f, c.y - radius - 6*a_s }, { slot*.94f, radius*2+45*a_s }, hovered) && live;
						const bool shortcut = !state.spells.member && !state.roles.member && !Im::GetIO()->WantTextInput &&
							((placed==1 && Im::IsKeyPressed(Im::ImGuiKey_1,false)) || (placed==2 && Im::IsKeyPressed(Im::ImGuiKey_2,false)) || (placed==3 && Im::IsKeyPressed(Im::ImGuiKey_3,false)));
						Im::PopID();
						const float hover = state.tw.Toward("badgeHover", static_cast<std::uint64_t>(a_badge.index), hovered ? 1.0f : 0.0f, 16.0f);
						float&      press = state.tw.Raw("badgePress", static_cast<std::uint64_t>(a_badge.index), 0.0f);
						if (hovered || focusItem == a_badge.badgeItem) {
							Hovering(0x400 + static_cast<std::uint64_t>(a_badge.index) * 2);
							Tip(a_badge.tip);
							if (Settings::Get().tooltips && !a_badge.empty) {
								state.tipHeading = a_badge.name;
								state.tipRank = a_badge.rank > 0 ? Trf("Rank {}", RankWord(a_badge.rank)) : std::string();
							}
							if (!hovered) {
								state.tipAnchored = true;
								state.tipMin = { c.x - radius, c.y - radius };
								state.tipMax = { c.x + radius, c.y + radius };
							}
						}
						if ((clicked || shortcut || (focusItem == a_badge.badgeItem && a_input.activate) || a_badge.padNow) && a_badge.use) {
							if (a_badge.clickable && live && (!a_member.inCombat || (!cooling && !a_member.down))) {
								press = 1.0f;
								a_badge.use();
								Sound("UIMenuOK");
							} else {
								Sound("UIMenuCancel");
							}
						}
						press = Follow(press, 0.0f, state.tw.dt, 6.0f);
						const float rr = radius * (1.0f + 0.06f * press + 0.03f * hover);


						const bool master=a_badge.rank>=3;
						const float focus=std::max(hover,focusItem==a_badge.badgeItem?1.f:0.f);
						NodeSelection(a_draw,c,rr,0,master,lit?(.35f+.65f*focus):focus,state.clock+id,alpha);
						if(master){
							const auto body=MasterContour(c,rr*.95f);
							Draw::AddConvexPolyFilled(a_draw,body.points.data(),body.count,Alpha(palette.inset,alpha));
						} else Emblem(a_draw,c,rr,Alpha(palette.inset,alpha),Alpha(kEdge,alpha*(lit?.7f:.28f)),1.3f*a_s);
						if(master){const auto well=MasterContour(c,rr*.82f);Draw::AddConvexPolyFilled(a_draw,well.points.data(),well.count,Alpha(kAccent,alpha*(lit?.48f:.12f)));}
						else Emblem(a_draw,c,rr*.82f,Alpha(kAccent,alpha*(lit?.48f:.12f)),0,0);

						if (a_badge.orb) {
							DrawOrb(a_draw, c, rr * 0.5f, true, a_s, alpha * (cooling ? 0.35f : 1.0f));
						} else {
							Glyph(a_draw, c, rr*(.74f+.06f*press), a_badge.glyph,
								Alpha(a_badge.usable ? kText : kTextDim, alpha * (a_badge.usable ? (cooling ? 0.3f : 1.0f) : 0.45f)));
						}

						// The cooldown: the wait still to come, as a wedge from the hand round to noon.
						if (cooling) {
                            // The sweep follows the exact perimeter of this themed badge.
                            auto shape = MasterContour(c, master ? rr*.95f : rr);
                            if (!master && palette.style == UITheme::Style::Oxblood) {
                                shape.count=4;
                                shape.points[0]={c.x,c.y-rr}; shape.points[1]={c.x+rr,c.y};
                                shape.points[2]={c.x,c.y+rr}; shape.points[3]={c.x-rr,c.y};
                            }
                            float perimeter=0;
                            for(int i=0;i<shape.count;++i){auto a=shape.points[i],b=shape.points[(i+1)%shape.count];perimeter+=std::hypot(b.x-a.x,b.y-a.y);}
                            float travelled=0;
                            const float cutoff=charge*perimeter;
                            for(int i=0;i<shape.count;++i){
                                const V a=shape.points[i],b=shape.points[(i+1)%shape.count];
                                const float length=std::hypot(b.x-a.x,b.y-a.y);
                                const float t=std::clamp((cutoff-travelled)/std::max(length,.001f),0.f,1.f);
                                const V hand{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};
                                if(t<1) Draw::AddTriangleFilled(a_draw,c,hand,b,Alpha(kShade,alpha*.62f));
                                if(t>0) Draw::AddLine(a_draw,a,hand,Alpha(kEdge,alpha),2.5f*a_s);
                                travelled+=length;
                            }
							const auto seconds = std::format("{:.0f}", std::ceil(a_badge.ready));
							TextCentered(a_draw, { c.x + 1.0f * a_s, c.y + 1.5f * a_s }, Alpha(kShade, alpha * 0.9f), 21.0f * a_s, seconds);
							TextCentered(a_draw, c, Alpha(kText, alpha), 21.0f * a_s, seconds);
						}


						if (a_badge.rank >= 3) Mastery(a_draw,c,rr,alpha);
						Keycap(a_draw,{c.x-radius-9*a_s,c.y+radius*.5f},24*a_s,13*a_s,std::to_string(placed),alpha);
						// The name, and the AUTO switch under it.
						const float ny = c.y + radius + 28.0f * a_s;
						{
							MasterFont titleFont(a_badge.rank >= 3);
							const auto title=Upper(a_badge.name);
							const float fs=17.f*a_s*std::min(1.f,(slot-12*a_s)/std::max(TextSize(17.f*a_s,title).x,1.f));
							TextCentered(a_draw,{c.x,ny},Alpha(a_badge.usable ? kText : kTextDim,alpha),fs,title);
						}
						if (a_badge.autoOn) {
							const float pw = 104.0f * a_s, ph = 26.0f * a_s;
							const V     pmin{ c.x - pw * 0.5f, ny + 15.0f * a_s };
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
							PanelFill(a_draw, pmin, { pmin.x + pw, pmin.y + ph }, Alpha(Mix(IM_COL32(255, 255, 255, 10), kAccent, k), alpha), 0.0f, 0);
							PanelEdge(a_draw, pmin, { pmin.x + pw, pmin.y + ph }, Alpha(Mix(kGlassEdge, kEdge, std::max(k, over ? 0.7f : 0.0f)), alpha), 0.0f, 0,
								1.0f);
							TextCentered(a_draw, { c.x, pmin.y + ph * 0.5f }, Alpha(Mix(kTextFaint, kText, k), alpha), 11.5f * a_s, *a_badge.autoOn ? Tr("AUTO") : Tr("MANUAL"));
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
						b.glyph = tank ? 0xF0A1 : Paths::Info(Paths::Ability::kHeal).glyph;
						b.name = tank ? Tr("Taunt") : Tr("Heal");
						b.ready = tank ? a_member.tauntReady : a_member.healReady;
						b.cooldown = tank ? a_member.tauntCooldown : a_view.tuning.healCooldown;
						b.usable = a_member.inCombat;
						b.clickable = b.usable;
						b.autoOn = &orders.autoAbility;
						b.badgeItem = primaryBadge;
						b.autoItem = primaryAuto;
						if (tank) {
							b.tip = Trf("Holds enemies up to level {}\nReach: {:.0f}\nDuration: {:.0f} s\nStamina: {:.0f}", a_member.tauntCertain,
								a_member.tauntRadius, a_member.tauntHold, a_member.tauntStamina);
							if (a_member.tauntArmor > 0.0f) {
								b.tip += Trf("\nBraces them: +{:.0f} armor for as long as it holds, more as they rise in level.", a_member.tauntArmor);
							}
						} else {
							b.tip = Trf("A circle of healing light where it helps most: +{:.0f} health over {:.0f} s to anyone in it, more as they rise in level; undead in it burn; hurt companions walk into it. Spends magicka on activation.\nReady again {:.0f} s after use.",
								a_member.healAmount, HealCircle::kSeconds, a_view.tuning.healCooldown);
						}
						if (!a_member.inCombat) {
							b.tip += Tr("\nIn a fight only.");
						}
						b.autoTip = tank ? std::string(Tr("AUTO on: they taunt by themselves when it is ready and worth it.\nOff: only when you press Taunt.")) :
						                   std::string(Tr("AUTO on: they heal by themselves when anyone is down or drops below the line.\nOff: only when you press Heal."));
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
						b.name = Tr("Rejuvenate");
						b.ready = a_member.rejuvenateReady;
						b.cooldown = a_member.rejuvenateCooldown;
						b.usable = a_member.inCombat;
						b.clickable = b.usable;
						b.autoOn = &orders.autoRejuvenate;
						b.badgeItem = rejuvenateBadge;
						b.autoItem = rejuvenateAuto;
						b.tip = Trf("Restores {:g}% maximum magicka per second (up to {:.0f}) inside the circle for {:.0f} s. Overlapping circles do not stack.\nReady again {:.0f} s after use.{}",
							RejuvenatePolicy::Rate(a_member.level>=60?3:a_member.level>=30?2:1)*100, RejuvenatePolicy::Cap(a_member.level>=60?3:a_member.level>=30?2:1), a_member.rejuvenateSeconds, Rules::RejuvenateCooldown(a_member.level,a_view.tuning), a_member.inCombat ? "" : Tr("\nIn a fight only."));
						b.autoTip = Tr("AUTO on: they rejuvenate by themselves when you, a mage or a healer runs out of magicka or nearly does.\nOff: only when you press Rejuvenate.");
						b.use = [&] { Simple(Battle::Command::Type::kRejuvenateNow, a_member.id); };
						badge(std::move(b));
					}
					if (special != Special::kNone) {
						const auto& info = Info(special);
						Badge       b;
						b.index = 1;
						b.glyph = info.glyph;
						b.name = std::string(Tr(info.name));
						b.ready = a_member.specialReady;
						b.cooldown = a_member.specialCooldown;
						b.usable = a_member.inCombat;
						b.clickable = b.usable;
						b.autoOn = &orders.autoSpecial;
						b.badgeItem = specialBadge;
						b.autoItem = specialAuto;
						b.tip = Trf("{}\nCooldown: {:.0f} s{}", Tr(info.summary), SpecialCooldown(special),
							a_member.inCombat ? "" : Tr("\nIn a fight only."));
						b.autoTip = Trf("AUTO on: {}\nOff: only when you press {}.", Tr(info.own), Tr(info.name));
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
						b.empty = ability == Paths::Ability::kNone;
						b.rank = b.empty ? 0 : rank;
						b.glyph = info.glyph ? info.glyph : 0xF128;
						b.name = ability == Paths::Ability::kNone ? std::string(Tr("Empty")) : std::string(Tr(info.name));
						b.ready = a_member.unlimited ? 0.0f : carried.ready;
						b.cooldown = carried.cooldown;
						b.usable = ability != Paths::Ability::kNone;
						b.clickable = b.usable;
						b.autoOn = b.usable ? &carriedAutoOn[s] : nullptr;
						b.orb = ability == Paths::Ability::kMagnus;
						b.badgeItem = carriedBadge[s];
						b.autoItem = carriedAuto[s];
						auto words = node >= 0 ? Paths::Describe(Paths::PathOf(orders.role)[static_cast<std::size_t>(node)].perk, rank) : std::string();
                        b.tip = words;
                        if (ability == Paths::Ability::kTaunt) {
                            b.tip += Trf("\nHolds enemies up to level {}", a_member.tauntCertain);
                        }
                        if (!b.empty) {
                            b.tip += Trf("\nCooldown: {:.0f} s", carried.cooldown);
                            if (a_member.unlimited)
                                b.tip += Tr("\nDebug Mode: usable outside combat.\nLaunched abilities aim 10 m ahead.");
                        } else {
                            b.tip = Tr("This follower has no abilities at the moment.");
                        }
                        b.autoTip = Trf("AUTO on: {}\nOff: only when you press {}.", Tr(info.own), Tr(info.name));
                        b.padNow = s == 0 && padNow;
                        b.padFlip = s == 0 && padFlip;
						b.use = [&, ability, node] {
							if (a_member.inCombat || a_member.unlimited) {
								Battle::Command command;
								command.type = Battle::Command::Type::kAbilityNow;
								command.member = a_member.id;
								command.ability = ability;
								Battle::Queue(command);
							} else {
								OpenPathPage(a_member, std::max(node, 0));
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

                    // A themed constellation medallion opens the tree; its name appears on hover.
                    if (tree) {
                        const float h=58.f*a_s;
                        const V centre{p.x+inner*.5f,yy+h*.5f};
                        const V min{centre.x-32.f*a_s,yy},max{centre.x+32.f*a_s,yy+h};
                        bool hovered=false;
                        const bool clicked=Hit("pathTile",min,{64.f*a_s,h},hovered)&&live;
                        const bool focused=focusItem==pathButton;
                        const int unspent=std::max(a_member.pointsEarned-a_member.pointsSpent,0);
                        const float hover=state.tw.Toward("pathTile",a_member.id,hovered||focused?1.f:0.f,16.f);
                        Halo(a_draw,centre,40.f*a_s,kEdge,alpha*(.12f+.45f*hover));
                        Emblem(a_draw,centre,25.f*a_s,Alpha(palette.inset,alpha*.7f),Alpha(kEdge,alpha*(.55f+.45f*hover)),1.f*a_s);
                        const V root{centre.x,centre.y+12*a_s},left{centre.x-11*a_s,centre.y-2*a_s},right{centre.x+11*a_s,centre.y-2*a_s},topNode{centre.x,centre.y-14*a_s};
                        for(const V branch:{left,right}){Draw::AddLine(a_draw,root,branch,Alpha(kText,alpha),1.4f*a_s);Draw::AddLine(a_draw,branch,topNode,Alpha(kText,alpha*.7f),1.2f*a_s);}
                        for(const V point:{root,left,right,topNode})Diamond(a_draw,point,3.5f*a_s,Alpha(kText,alpha),0,0);
                        if(unspent>0){const V pc{centre.x+29*a_s,centre.y+20*a_s};Diamond(a_draw,pc,12*a_s,Alpha(kRed,alpha),Alpha(kEdge,alpha*.5f),1.f);TextCentered(a_draw,pc,Alpha(kText,alpha),11*a_s,std::to_string(unspent));}
                        if(focused)FocusOutline(a_draw,min,max,0.f,alpha);
                        if(hovered||focused){Hovering(0x480);Tip(Tr("Skill tree"));}
                        if(clicked||(focused&&a_input.activate)){OpenPathPage(a_member,-1);Sound("UIMenuOK");}
                        yy+=h+6.f*a_s;
                    }

					// The healer's line: a slider in steps of five.
					if (hasSlider) {
						yy += 10.0f * a_s;
						int         threshold = orders.healThreshold;
						const float labelW = 150.0f * a_s;
						Text(a_draw, { p.x, yy - 9.0f * a_s }, Alpha(orders.autoAbility ? kTextDim : kTextFaint, alpha), 15.0f * a_s, Trf("Heal below {}%", threshold));
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
						PanelFill(a_draw, { tmin.x, tmin.y + th * 0.5f - 0.5f }, { tmin.x + tw, tmin.y + th * 0.5f + 0.5f }, Alpha(kGlassEdge, alpha * 1.4f), 0.0f, 0);
						PanelFill(a_draw, { tmin.x, tmin.y + th * 0.25f }, { tmin.x + tw * t, tmin.y + th * 0.75f }, Alpha(kEdge, alpha), 0.0f, 0);
						Emblem(a_draw, { tmin.x + tw * t, tmin.y + th * 0.5f }, (8.0f + (dragging ? 2.0f : 0.0f)) * a_s, Alpha(kText, alpha), Alpha(kShade, alpha * 0.5f), 1.0f);
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
							const std::string line = waiting ? Trf("{} heard you...", a_member.name) : a_member.answer;
							const bool        good = !waiting && a_member.answerDone;
							const U32         color = waiting ? kTextDim : (good ? kText : kAmber);
							Glyph(a_draw, { p.x + 7.0f * a_s, yy + 9.0f * a_s }, 12.0f * a_s, waiting ? 0xF141 : (good ? 0xF00C : 0xF071), Alpha(color, alpha * in));
							Text(a_draw, { p.x + 22.0f * a_s, yy }, Alpha(color, alpha * in), 15.0f * a_s, line);
							yy += 18.0f * a_s * in;
						}
					}
					return yy - p.y;
				},true);
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
        float HandTiles(Im::ImDrawList* draw, const View::Member& member, V at, float inner, float scale, float alpha, const Input& input) {
            if(!member.CanTakeRole()) return at.y;
            const bool caster=ChoosesSpells(member.orders.role),live=!state.closing;
            const bool nav=state.keyboardNav&&live&&!state.spells.member&&state.section==kSpellSection;
            SectionLabel(draw,at,scale,alpha,Tr("LOADOUT"),nav);float y=at.y+28*scale;
            for(std::size_t index=caster?0:2;index<3;index++) {
                const auto& pool=member.pools[index];const bool weapon=index==2;
                const V lo{at.x,y},hi{at.x+inner,y+58*scale};bool hovered=false;
                Im::PushID(6000+static_cast<int>(index));const bool click=Hit("pool",lo,{inner,58*scale},hovered)&&live;Im::PopID();
                const bool focused=nav&&state.item==static_cast<int>(caster?index:0);
                const bool open=state.spells.member==member.id&&state.spells.hand==index;
                const float glow=state.tw.Toward("poolHover",index,hovered||focused||open?1.f:0.f,16.f);
                PanelFill(draw,lo,hi,Alpha(Mix(kTile,kTileHover,glow),alpha),0,0);
                PanelEdge(draw,lo,hi,Alpha(Mix(kGlassEdge,kEdge,glow),alpha),0,0,1);
                Lit(draw,lo,hi,alpha*glow*.65f);
                Glyph(draw,{lo.x+26*scale,lo.y+29*scale},19*scale,weapon?0xF6DE:0xE2CA,Alpha(kEdge,alpha));
                const auto title=weapon?Tr("WEAPON POOL"):index==0?Tr("LEFT SPELL POOL"):Tr("RIGHT SPELL POOL");
                Label(draw,{lo.x+52*scale,lo.y+10*scale},Alpha(kText,alpha),11*scale,title,1.2f*scale);
                const auto summary=pool.empty()?std::string(Tr("Automatic choice")):Trn(static_cast<int>(pool.size()),"{} selected","{} selected");
                Text(draw,{lo.x+52*scale,lo.y+29*scale},Alpha(kTextDim,alpha),15*scale,summary);
                Glyph(draw,{hi.x-18*scale,lo.y+29*scale},12*scale,open?0xF078:0xF054,Alpha(kTextDim,alpha));
                if(focused)FocusOutline(draw,lo,hi,0,alpha);
                if(hovered||focused)Tip(weapon?std::string(Tr("Choose one or more weapons to start the battle with. One is chosen at random and kept for that battle. Shields pair with one-handed weapons. Tanks can also use two-handed weapons.")):std::string(Tr("Select multiple spells. Their combat AI chooses from this pool as the fight changes.")));
                if(click||(focused&&input.activate)){if(open)CloseSpellList();else OpenSpellList(member,index);Sound("UIMenuOK");}
                y+=66*scale;
            }
            return y+12*scale;
        }

		// With no fight on, the chosen companion's sheet sits on the left, the way a bestiary
		// page sits beside its creature: what they hold, and what they are best at. Their vitals,
		// armor, damage and resistances are on the card.
		Rect DrawSheet(Im::ImDrawList* a_draw, const View::Member& a_member, V a_origin, float a_width, float a_s, float a_appear, const Input& a_input)
		{
			const float swap = Smooth(state.swap);
			const float pad = 24.0f * a_s;
			float       y = a_origin.y;
			auto        enter = [&](int a_index) { return Stage(a_appear, 0.08f + 0.07f * static_cast<float>(a_index), 0.5f); };
			auto        origin = [&](int a_index) { return V{ a_origin.x - (1.0f - enter(a_index)) * 48.0f * a_s - (1.0f - swap) * 14.0f * a_s, y }; };
			auto        alphaOf = [&](int a_index) { return enter(a_index) * (0.25f + 0.75f * swap); };

			// What they hold, and what they are best at: lists, one hairline under each row. Who they
			// are, their vitals and what turns aside are on the card already.
			{
				const float alpha = alphaOf(0);
				const Rect  r = Panel(a_draw, origin(0), a_width, pad, enter(0), [&](V p, float inner) {
                    float yy = p.y;
                    // A mage or healer: the two hands as tiles, each naming its chosen spell.
                    const bool casts = ChoosesSpells(a_member.orders.role);
                    yy = HandTiles(a_draw,a_member,{p.x,yy},inner,a_s,alpha,a_input);
                    // Arrows for a bow: an archer with nothing to shoot shows before a fight does.
                    if (a_member.bow) {
                        const bool none = a_member.ammoName.empty();
                        Glyph(a_draw, { p.x + 9.0f * a_s, yy + 9.0f * a_s }, 14.0f * a_s, Info(Role::kArcher).glyph, Alpha(none ? kEdge : kText, alpha));
                        Text(a_draw, { p.x + 30.0f * a_s, yy }, Alpha(none ? kEdge : kText, alpha), 15.5f * a_s,
                            none ? std::string(Tr("No arrows")) : std::format("{}  x{}", a_member.ammoName, a_member.ammoCount));
                        yy += 24.0f * a_s;
                        Draw::AddLine(a_draw, { p.x, yy }, { p.x + inner, yy }, Alpha(kGlassEdge, alpha * 0.7f), 1.0f);
                        yy += 8.0f * a_s;
                    }
                    yy += 12.0f * a_s;

                    SectionLabel(a_draw, { p.x, yy }, a_s, alpha, Tr("BEST AT"), false);
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
		// could be taught, a tab per school. Rows toggle pool membership; Done or Escape closes the list.
		Rect DrawSpellList(Im::ImDrawList* a_draw, const View::Member& a_member, V a_origin, float a_width, float a_maxHeight, float a_s, float a_appear,
			const Input& a_input, int a_dy, bool a_back, const Rect& a_sheet)
		{
			auto&            list = state.spells;
			const auto       hand = list.hand;
			const bool weapons=hand==2;
            const auto& pool=a_member.pools[hand];
            const auto& options=weapons?a_member.weaponOptions:a_member.spellOptions;
            const RE::FormID current=pool.empty()?0:pool.front();
			list.openT = std::min(list.openT + state.tw.dt / 0.2f, 1.0f);
			const float a = EaseOut(list.openT) * a_appear;

			// A tab for every school with something for this hand, after "All".
			std::array<int, static_cast<std::size_t>(Rules::School::kCount)> perSchool{};
			for (const auto& spell : options) {
				if (Fits(spell, hand)) {
					++perSchool[static_cast<std::size_t>(spell.school)];
				}
			}
			std::vector<int> tabs{ -1 };
			for (std::size_t i = 0; i < perSchool.size(); ++i) {
				if (!weapons && perSchool[i] > 0) {
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
				for (const auto& spell : options) {
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
			const float rowH = 44.0f * a_s, headH = 30.0f * a_s;
			const float headerH = 46.0f * a_s, tabsH = (weapons?54.0f:70.0f) * a_s;
			std::vector<float> top(rows.size());
			float              content = 0.0f;
			for (std::size_t i = 0; i < rows.size(); ++i) {
				top[i] = content;
				content += rows[i].kind == kKnowHeading || rows[i].kind == kTeachHeading ? headH : rowH;
			}
			const float inner = a_width - pad * 2.0f;
			const float footH = 30.0f * a_s;
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
			Label(a_draw, { p.x, p.y + 2.0f * a_s }, Alpha(kText, a), 15.0f * a_s, weapons?Tr("WEAPON POOL"):hand==0?Tr("LEFT SPELL POOL"):Tr("RIGHT SPELL POOL"), 1.1f * a_s);

			{
				const V    xc{ p.x + inner - 8.0f * a_s, p.y + 11.0f * a_s };
				const bool overX = Inside(mouse, { xc.x - 12.0f * a_s, xc.y - 12.0f * a_s }, { xc.x + 12.0f * a_s, xc.y + 12.0f * a_s });
				Glyph(a_draw, xc, 13.0f * a_s, 0xF00D, Alpha(overX ? kText : kTextDim, a));
				if (overX && press) {
					a_back = true;
				}
			}

			// Weapon pools have an instruction instead of the spell-school selector.
            if(weapons) {
                Paragraph(a_draw, {p.x,p.y+headerH}, Alpha(kTextDim,a), 14.f*a_s,
                    Tr("Choose one or more weapons to start the battle with"),inner);
            } else {
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
					Emblem(a_draw, c, dr, Alpha(IM_COL32(19, 19, 20, 245), a), Alpha(kGlassEdge, a * 1.3f), 1.0f);
				}
				const V pc{ p.x + segW * (slide + 0.5f), cy };
				Halo(a_draw, pc, dr * 1.6f, kAccent, a * 0.7f);
				Emblem(a_draw, pc, dr + 1.0f * a_s, Alpha(kAccent, a * 0.85f), Alpha(kEdge, a), 1.4f * a_s);
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
				const auto  name = school < 0 ? std::string(Tr("Every school")) : std::string(Tr(Rules::schoolNames[static_cast<std::size_t>(school)]));
				const auto line = name;
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
					const std::string_view words = weapons?Tr("INVENTORY"):row.kind == kKnowHeading ? Tr("THEY KNOW") : Tr("YOU CAN TEACH THEM");
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
				const bool held = id ? LoadoutPolicy::Contains(pool,id) : pool.empty();
                const bool allowed = !spell || spell->allowed;
                const float rowAlpha = allowed ? a : a * .4f;
				const float hover = state.tw.Toward("spellRow", (static_cast<std::uint64_t>(id) << 1) | (spell ? 1 : 0), pointed || focused ? 1.0f : 0.0f, 18.0f);
				if (pointed) {
					Hovering(0x800 + i);
				}
				if (held && allowed) {
					Highlight(a_draw, rmin, rmax, a);
				} else {
					PanelFill(a_draw, rmin, rmax, Alpha(IM_COL32(255, 255, 255, 14), rowAlpha * hover), 0.0f, 0);
				}
				const U32 tone = !allowed ? kTextFaint : spell ? SchoolColour(spell->school) : kTextFaint;
				const V   dc{ rmin.x + 18.0f * a_s, (rmin.y + rmax.y) * 0.5f };
				Emblem(a_draw, dc, 12.0f * a_s, Alpha(IM_COL32(19, 19, 20, 245), a), Alpha(Mix(kGlassEdge, tone, spell ? 0.9f : 0.0f), a), 1.0f);
				Glyph(a_draw, dc, 11.0f * a_s, held?0xF00C:weapons?(spell && spell->shield?0xF132:0xF6DE):spell?SchoolGlyph(spell->school):0xF074, Alpha(tone, rowAlpha));
				const std::string name = spell ? spell->name : std::string(Tr("Automatic choice"));
				Draw::PushClipRect(a_draw, {rmin.x+40*a_s,rmin.y}, {rmax.x-(spell?95.f:8.f)*a_s,rmax.y}, true);
				Text(a_draw, { rmin.x + 40.0f * a_s, (rmin.y + rmax.y) * 0.5f - TextSize(16.0f * a_s, name).y * 0.5f }, Alpha(!allowed ? kTextFaint : held || pointed || focused ? kText : kTextDim, rowAlpha),
					16.0f * a_s, name);
                Draw::PopClipRect(a_draw);
				float right = rmax.x - 8.0f * a_s;
				if (spell) {
					const auto tier = weapons?Trf("x{}",spell->count):std::string(Tr(Rules::tierNames[static_cast<std::size_t>(spell->tier)]));
					right -= TextSize(13.0f * a_s, tier).x;
					Text(a_draw, { right, (rmin.y + rmax.y) * 0.5f - TextSize(13.0f * a_s, tier).y * 0.5f }, Alpha(kTextFaint, rowAlpha), 13.0f * a_s, tier);

				}
				if (focused) {
					FocusOutline(a_draw, rmin, rmax, 0.0f, a);
				}
				if (pointed || focused) {
					std::string tip;
					if (!allowed) {
                        tip = weapons ? Trf("{}\nUnavailable for {}.", spell->name, Tr(Info(a_member.orders.role).name)) : Trf("{}\nUnavailable for this role or level.", spell->name);
                        if (held) tip += Tr("\nClick to remove from this pool.");
                    } else if (!spell) {
						tip = Tr("Automatic choice\nClears this pool and lets them choose from options allowed for their role.");
					} else if(weapons) {
                        tip=spell->shield?Trf("{}\nPairs with a one-handed weapon.\nClick to add or remove.",spell->name):Trf("{}\n{:.0f} base damage, {} in inventory.\nClick to add or remove.",spell->name,spell->cost,spell->count);
                    } else {
						tip = Trf("{}\n{} {}  ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â·  {:.0f} magicka{}{}", spell->name, Tr(Rules::tierNames[static_cast<std::size_t>(spell->tier)]),
							Tr(Rules::schoolNames[static_cast<std::size_t>(spell->school)]), spell->cost, spell->twoHanded ? Tr("  ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â·  takes both hands") : "",
							spell->known ? "" : Tr("\nOne of yours: choosing it teaches them, and they keep it."));
					}
					if (pointed) {
						Tip(std::move(tip));
					} else {
						TipBeside(std::move(tip), rmin, rmax);
					}
				}
				if ((pointed && press) || (focused && a_input.activate)) {
                    if (allowed || held) {
                        ChooseSpell(a_member.id, hand, id);
                        Sound(spell && !spell->known ? "UIItemMeleeUp" : "UIMenuOK");
                    } else Sound("UIMenuCancel");
				}
			}
			if (rows.size() == 1) {
				TextCentered(a_draw, { p.x + inner * 0.5f, lmin.y + rowH * 1.8f }, Alpha(kTextFaint, a), 14.0f * a_s, Tr("Nothing else for this hand."));
			}
			Draw::PopClipRect(a_draw);
			// A thin bar when there is more than fits.
			if (most > 0.0f) {
				const float barH = listH * listH / content;
				const float barY = lmin.y + (listH - barH) * (list.scroll / most);
				PanelFill(a_draw, { lmax.x + 2.0f * a_s, lmin.y }, { lmax.x + 4.0f * a_s, lmax.y }, Alpha(kGlassEdge, a), 0.0f, 0);
				PanelFill(a_draw, { lmax.x + 2.0f * a_s, barY }, { lmax.x + 4.0f * a_s, barY + barH }, Alpha(kEdge, a), 0.0f, 0);
			}

			// Persistent multi-selection: only Done/Escape/outside closes the pool.
            Text(a_draw,{p.x,max.y-pad-17*a_s},Alpha(kTextDim,a),12*a_s,Trf("{} selected",pool.size()));
            const V doneLo{max.x-pad-66*a_s,max.y-pad-24*a_s},doneHi{max.x-pad,max.y-pad};
            bool doneHover=false;
            if(Hit("poolDone",doneLo,{66*a_s,24*a_s},doneHover)){CloseSpellList();Sound("UIMenuCancel");}
            TextCentered(a_draw,{(doneLo.x+doneHi.x)*.5f,(doneLo.y+doneHi.y)*.5f},Alpha(doneHover?kText:kTextDim,a),13*a_s,Tr("Done"));

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
			const float h = 76.0f * a_s;
			const float segW = 120.0f * a_s;
			const float width = segW * static_cast<float>(orderShouts.size());
			const V     min{ a_centre.x - width * 0.5f, a_centre.y + (1.0f - alpha) * -10.0f * a_s };
			const V     max{ min.x + width, min.y + h };
			const auto& settings = Settings::Get();


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
				const V pmin{ min.x + px * segW + 4*a_s, min.y + 1.0f };
				const V pmax{ min.x + (px+1)*segW - 4*a_s, max.y - 1.0f };
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
				const auto  tip = Trf("{}{}\nReplaces every companion's own orders.", Tr(info.summary), key);
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
				const bool  on = static_cast<int>(i) == active;
				const float lit = std::max({ on ? 1.0f : 0.0f, hover * 0.7f, flash });
				const float cx = smin.x + segW * 0.5f;
				PanelFill(a_draw,{smin.x+4*a_s,smin.y},{smax.x-4*a_s,smax.y},Alpha(Mix(palette.inset,kAccent,lit*.45f),alpha*.9f));
				PanelEdge(a_draw,{smin.x+4*a_s,smin.y},{smax.x-4*a_s,smax.y},Alpha(kEdge,alpha*(.2f+.5f*lit)),0,0,1.f);
				Glyph(a_draw, { cx, smin.y + 27.0f * a_s - 2.0f * a_s * hover }, (24.0f + 3.0f * flash) * a_s, orderGlyphs[i], Alpha(Mix(kTextDim, kText, lit), alpha));
				const std::string name = Upper(Tr(info.name));
				const float fs=13.f*a_s*std::min(1.f,(segW-16*a_s)/std::max(LabelWidth(13*a_s,name,.6f*a_s),1.f));
				const float lw=LabelWidth(fs,name,.6f*a_s);
				Label(a_draw, { cx - lw * 0.5f, smin.y + 46.0f * a_s }, Alpha(Mix(kTextDim, kText, lit), alpha), fs, name, .6f * a_s);
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
		// Learned nodes retain their theme colour; eligible ranks stay steady and locked nodes stay dim.
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
			float         focus{ -1.0f };    // optional eased selection, shared with tree connections
			float         appear{ 1.0f };
			float         rankedAge{ 99.0f };  // seconds since it last gained a rank
		};

		void DrawNode(Im::ImDrawList* a_draw, V c, float r, const NodeLook& a_node, float a_s, float a_alpha)
		{
			const float pop = EaseOut(a_node.appear);
			const float alpha = a_alpha * Clamp01(a_node.appear * 1.4f);
			const float focus = a_node.focus < 0 ? (a_node.selected ? 1.0f : 0.0f) : a_node.focus;
			const float unlockDuration = a_node.rank >= 3 ? 2.2f : 1.8f;
			const float unlock = a_node.rankedAge >= 0 && a_node.rankedAge < unlockDuration ? Clamp01(a_node.rankedAge / unlockDuration) : 1.0f;
			const float swell = 1.0f + .04f * std::sin(Clamp01(unlock / .38f) * 3.14159265f);
			const float rr = r * swell * (0.86f + 0.14f * pop) * (1.0f + 0.045f * a_node.hover + 0.035f * focus);
			const bool master = a_node.kind == Paths::Kind::kAbility && a_node.rank >= 3;
			const bool  learned = a_node.rank > 0;
			const bool  maxed = a_node.rank >= a_node.most;
			const float breathe = 0.5f; // Availability is steady; only focus and earned ranks animate.

            // Quiet staggered waves follow the actual silhouette, behind the opaque node.
            // Mastery gathers four contours inward; lower ranks release three outward.
            const int contourKind=a_node.kind==Paths::Kind::kBranch?1:a_node.kind==Paths::Kind::kTrait?2:0;
            if(unlock<1.f) {
                const U32 glow=palette.Subdued()?kText:kEdge;
                const int waves=master?4:3;
                for(int wave=0;wave<waves;++wave) {
                    const float t=(unlock-wave*.12f)/.62f;
                    if(t<=0.f || t>=1.f)continue;
                    const float envelope=std::pow(std::sin(t*3.14159265f),2.f);
                    const float radius=rr*(master?1.62f-.51f*Smooth(t):1.03f+.37f*Smooth(t));
                    for(int layer=0;layer<3;++layer) {
                        const float ink=(layer==0?.018f:layer==1?.045f:.17f)*(master?1.12f:1.f);
                        NodeContourStroke(a_draw,c,radius,contourKind,master,Alpha(glow,alpha*envelope*ink),(layer==0?10.f:layer==1?5.f:1.25f)*a_s);
                    }
                }
                if(master) {
                    const float seal=std::pow(std::sin(Clamp01((unlock-.52f)/.48f)*3.14159265f),2.f);
                    NodeContourStroke(a_draw,c,rr*1.12f,contourKind,true,Alpha(glow,alpha*seal*.38f),2.f*a_s);
                }
            }
			if (focus > .01f) NodeSelection(a_draw,c,rr,a_node.kind == Paths::Kind::kBranch ? 1 : a_node.kind == Paths::Kind::kTrait ? 2 : 0,master,focus,state.clock,alpha);
			if (a_node.capstone) {
				if (!learned && a_node.buyable && focus > .01f) {
					Halo(a_draw, c, rr * 2.2f, kAccent, alpha * focus * .45f);
				}
				DrawOrb(a_draw, c, rr, learned, a_s, alpha * (a_node.locked ? 0.75f : 1.0f));
			} else {
				// What has been learned glows; what can be bought breathes a fainter glow.
				if (learned && focus > .01f) {
					Halo(a_draw, c, rr * 2.0f, kAccent, alpha * focus * (maxed ? 0.75f : 0.5f));
				} else if (a_node.buyable && focus > .01f) {
					Halo(a_draw, c, rr * 1.9f, kAccent, alpha * focus * .32f);
				}
				const U32 plate = Alpha(a_node.locked ? IM_COL32(14, 14, 15, 220) : IM_COL32(19, 19, 20, 245), alpha);
				const U32 well = Alpha(kAccent, alpha * (maxed ? 0.48f : 0.30f));
				const U32 edge = Alpha(Mix(learned ? kEdge : (a_node.buyable ? Mix(kTextDim, kText, breathe * .5f) : kGlassEdge), kText, a_node.hover*.35f),
					alpha * (a_node.locked ? 0.7f : 1.0f));
				const float t = 1.4f * a_s;
				switch (a_node.kind) {
				case Paths::Kind::kAbility:
					if (master) {
						const auto body=MasterContour(c,rr*.95f),wellShape=MasterContour(c,rr*.84f);
						Draw::AddConvexPolyFilled(a_draw,body.points.data(),body.count,plate);
						Draw::AddConvexPolyFilled(a_draw,wellShape.points.data(),wellShape.count,well);
						break;
					}
					Emblem(a_draw, c, rr, plate, edge, t);
					if (learned) {
						Emblem(a_draw, c, rr * 0.84f, well, Alpha(kShade, 0.0f), 1.0f);
					}
					Emblem(a_draw, c, rr + 6.0f * a_s, Alpha(kShade, 0.0f), Alpha(kGlassEdge, alpha * (a_node.locked ? 0.5f : 1.0f)), 1.0f);
					for (const V d : { V{ 0, -1 }, V{ 1, 0 }, V{ 0, 1 }, V{ -1, 0 } }) {
						Stud(a_draw, { c.x + d.x * (rr + 6.0f * a_s), c.y + d.y * (rr + 6.0f * a_s) }, 2.6f * a_s, alpha * (learned ? 1.0f : 0.6f));
					}
					break;
				case Paths::Kind::kBranch:
					Emblem(a_draw,c,rr,plate,edge,t,1);
					if (learned) Emblem(a_draw,c,rr*.84f,well,0,1,1);
					Emblem(a_draw,c,rr+4*a_s,0,Alpha(kGlassEdge,alpha*.7f),1,1);
					break;
				case Paths::Kind::kTrait:
					Emblem(a_draw, c, rr, plate, edge, t, 2);
					if (learned) {
						Emblem(a_draw, c, rr * 0.82f, well, Alpha(kShade, 0.0f), 1.0f, 2);
					}
					Emblem(a_draw, c, rr + 5.0f * a_s, Alpha(kShade, 0.0f), Alpha(kGlassEdge, alpha * (a_node.locked ? 0.4f : 0.8f)), 1.0f, 2);
					break;
				}
				const float glyphSize = (a_node.kind == Paths::Kind::kAbility ? 0.62f : 0.66f) * rr;
				Glyph(a_draw, c, glyphSize, a_node.glyph, Alpha(learned ? kText : (a_node.locked ? kTextFaint : kTextDim), alpha * (a_node.locked ? 0.6f : 1.0f)));
			}

			if (master) Mastery(a_draw,c,rr,alpha);
            // Darken the entire unlearned silhouette, including its icon and frame.
            // Earned ranks keep their normal finish even if their next rank needs a level.
            if(!learned) {
                const float shadeRadius=rr+(a_node.kind==Paths::Kind::kAbility?6.f:a_node.kind==Paths::Kind::kBranch?4.f:5.f)*a_s;
                const auto shape=NodeContour(c,shadeRadius,contourKind,master);
                Draw::AddConvexPolyFilled(a_draw,shape.points.data(),shape.count,Alpha(IM_COL32(0,0,0,255),alpha*(a_node.locked?.72f:.36f)));
            }
			// Locked: a small lock tucked under its lower right.
			if (a_node.locked) {
				const V lc{ c.x + rr * 0.72f, c.y + rr * 0.72f };
				Draw::AddCircleFilled(a_draw, lc, 8.0f * a_s, Alpha(IM_COL32(16, 16, 18, 250), alpha), 16);
				Draw::AddCircle(a_draw, lc, 8.0f * a_s, Alpha(kGlassEdge, alpha * 1.4f), 16, 1.0f);
				Glyph(a_draw, lc, 8.5f * a_s, 0xF023, Alpha(kTextDim, alpha));
			}

            // Three ranks sit on silhouette corners (or evenly spaced circle edges).
            // Keep them on rank III as well as its numeral, with no row under the name.
            const float rim=master?rr*1.12f:rr+(a_node.kind==Paths::Kind::kAbility?6.f:a_node.kind==Paths::Kind::kBranch?4.f:5.f)*a_s;
            const auto contour=NodeContour(c,rim,contourKind,master);
            const std::array<int,3> corners{{contour.count-contour.count/3,0,contour.count/3}};
            for(int k=0;k<a_node.most;++k) {
                const bool on=k<a_node.rank,next=!on && k==a_node.rank && a_node.buyable;
                const int corner=a_node.most==1?0:a_node.most==2?corners[k*2]:corners[k%3];
                const V pc=contour.points[corner];
                const float settle=on && k==a_node.rank-1 && unlock<1.f?std::sin(unlock*3.14159265f):0.f;
                const float size=(9.0f+.6f*settle)*a_s;
                Draw::AddCircleFilled(a_draw,pc,size+1.5f*a_s,Alpha(IM_COL32(15,15,17,255),alpha),12);
                Diamond(a_draw,pc,size,Alpha(on?kEdge:IM_COL32(26,26,28,255),alpha),Alpha(on?kText:next?kEdge:kGlassEdge,alpha*(on?.65f:1.f)),a_s);
            }
		}

		// Prose with its numbers picked out: a word holding a digit, and the "s" after one, is drawn
		// in the strong colour and a touch heavier. Returns the height; with no draw list it only measures.
		float RichText(Im::ImDrawList* a_draw, V a_pos, float a_size, std::string_view a_text, float a_width, U32 a_base, U32 a_strong)
		{
			// Measure the exact face and pixel size passed to AddText, without the
			// current window's font scale or CalcTextSize's rounded word widths.
			auto widthOf = [&](std::string_view text) {
				return Im::ImFontManger::CalcTextSizeA(Im::GetFont(), a_size * kTextScale,
					std::numeric_limits<float>::max(), 0.f, text.data(), text.data() + text.size(), nullptr).x;
			};
			const float space = widthOf(" ");
			const float inkRoom = std::max(1.f, a_width - 2.f * uiScale);
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
				const float w = widthOf(word);
				if (x > 0.0f && x + w > inkRoom) {
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
				PanelGradient(a_draw, a_min, max, Alpha(kAccent, a_alpha * (0.55f + 0.2f * hover)), Alpha(kAccent, a_alpha * 0.12f),
					Alpha(kAccent, a_alpha * 0.12f), Alpha(kAccent, a_alpha * (0.55f + 0.2f * hover)));
			} else if (hover > 0.01f) {
				PanelFill(a_draw, a_min, max, Alpha(kTileHover, a_alpha * hover), 0.0f, 0);
			}
			PanelEdge(a_draw, a_min, max, Alpha(a_lit || a_warn ? tone : kGlassEdge, a_alpha * (0.6f + 0.4f * hover)), 0.0f, 0, 1.0f);
			const U32   ink = Alpha(a_warn ? kAmber : Mix(a_lit ? kText : kTextDim, kText, hover), a_alpha);
			const float cy = a_min.y + a_size.y * 0.5f;
			if (a_label.empty()) {
				Glyph(a_draw, { a_min.x + a_size.x * 0.5f, cy }, 14.0f * a_s, a_glyph, ink);
				return clicked;
			}
			const float size = 11.5f * a_s, track = 1.6f * a_s, gw = a_glyph ? 20.0f * a_s : 0.0f;
			const float x = a_min.x + (a_size.x - LabelWidth(size, a_label, track) - gw) * 0.5f;
			if (a_glyph) Glyph(a_draw, { x + 6.0f * a_s, cy }, 11.0f * a_s, a_glyph, ink);
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
					PanelEdge(a_draw, { a_min.x - g, a_min.y - g }, { max.x + g, max.y + g },
						Alpha(kAccent, a_alpha * (0.16f + 0.08f * breathe + 0.12f * hover) * static_cast<float>(4 - i) / 3.0f), 0.0f, 0, 3.0f * a_s);
				}
				const U32 left = Mix(kAccent, kEdge, 0.15f + 0.45f * hover);
				PanelGradient(a_draw, a_min, max, Alpha(left, a_alpha), Alpha(kAccent, a_alpha * 0.6f), Alpha(kAccent, a_alpha * 0.6f), Alpha(left, a_alpha));
			}
			PanelEdge(a_draw, a_min, max, Alpha(Mix(kEdge, kText, 0.25f + 0.5f * hover), a_alpha), 0.0f, 0, 1.2f * a_s);
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
				PanelFill(a_draw, a_min, max, Alpha(Mix(kTile, kTileHover, hover), a_alpha), 0.0f, 0);
				if (a_lit) {
					Lit(a_draw, a_min, max, a_alpha);
				}
			}
			PanelEdge(a_draw, a_min, max, Alpha(a_enabled ? Mix(kGlassEdge, kEdge, std::max(hover, a_primary ? 0.6f : 0.0f)) : kGlassEdge, a_alpha * (a_enabled ? 1.0f : 0.6f)),
				0.0f, 0, 1.0f);
			const float lw = LabelWidth(13.0f * a_s, a_label, 1.6f * a_s);
			Label(a_draw, { a_min.x + (a_size.x - lw) * 0.5f, a_min.y + a_size.y * 0.5f - 7.5f * a_s }, Alpha(a_enabled ? kText : kTextFaint, a_alpha), 13.0f * a_s,
				a_label, 1.6f * a_s);
			return clicked;
		}

		// Path header controls: bonus investment, help and unlimited points.
		// Caller positions and clips these helpers; returned bounds cover their controls
		// so blank-background dismissal can exclude them. The original hit IDs remain.
		Rect DrawPathBonus(Im::ImDrawList* a_draw, const View::Member& a_member, const Rules::Tuning& a_tuning, V bonusMin, float a_s, float alpha, bool live)
		{
			auto&       page = state.page;
			const auto  path = page.path;
			const auto& tree = Paths::PathOf(path);
			const auto& record = a_member.record;
			const int   onPath = Paths::Spent(record, path);
			const int   invested = Paths::Invested(record, path);
			const bool  mastered = Paths::Mastered(record, path);
			const int   bonusRoom = Rules::PathBonusRoom(path, onPath, a_tuning);
			const bool  extrasPreviousHitsBlocked = hitsBlocked;
			hitsBlocked = hitsBlocked || !live;
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
			const V     bmin = bonusMin;
			const V     bmax{ bmin.x + 12.0f * a_s + 16.0f * a_s + 9.0f * a_s + numberW + 9.0f * a_s + wordW + 14.0f * a_s + tallyW, bmin.y + h };
			const float dim = working ? 1.0f : 0.55f;
			bool        hovered = false;
			Hit("pathBonus", bmin, { bmax.x - bmin.x, h }, hovered);
			const float hover = state.tw.Toward("pathBonusHover", 0, hovered ? 1.0f : 0.0f, 16.0f);
			Halo(a_draw, { bmin.x + 28.0f * a_s + numberW * 0.5f, (bmin.y + bmax.y) * 0.5f }, 46.0f * a_s, kAccent, alpha * flash);
			PanelFill(a_draw, bmin, bmax,
				Alpha(Mix((palette.Subdued() ? palette.inset : IM_COL32(26, 12, 11, 235)), kAccent, 0.25f * hover + 0.35f * flash), alpha * (0.6f + 0.4f * dim)), 0.0f, 0);
			PanelEdge(a_draw, bmin, bmax, Alpha(Mix(kGlassEdge, kEdge, working ? 0.6f + 0.4f * hover : 0.25f * hover), alpha), 0.0f, 0, 1.0f);
			PanelFill(a_draw, bmin, { bmin.x + 2.0f * a_s, bmax.y }, Alpha(working ? kEdge : kTextFaint, alpha), 0.0f, 0);
			constexpr std::array<std::uint32_t, 6> glyphs{ 0xF0E7, 0xF3ED, 0xF21E, 0xF6E3, 0xF2EA, 0xF0D0 };  // bolt, shield, heartbeat, hammer, arrow back, wand
			const float                            cy = (bmin.y + bmax.y) * 0.5f;
			float                                  x = bmin.x + 12.0f * a_s;
			Glyph(a_draw, { x + 8.0f * a_s, cy }, 13.0f * a_s, glyphs[static_cast<std::size_t>(path) % glyphs.size()], Alpha(working ? kEdge : kTextDim, alpha));
			x += 16.0f * a_s + 9.0f * a_s;
			const float big = numberSize * bump;
			Text(a_draw, { x + (numberW - TextSize(big, number).x) * 0.5f, cy - TextSize(big, number).y * 0.5f },
				Alpha(Mix(working ? kText : kTextDim, kEdge, flash * 0.5f), alpha), big, number);
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
						Tip(std::string(Tr("Take a point back out of the bonus.")));
					} else if (invested == 0) {
						Readout(std::string(Tr("No points in the bonus to take back.")));
					} else {
						Readout(a_member.respec ? std::string(Tr("Points come back out of the bonus once the fight is over.")) : std::string(Paths::Why(Paths::Block::kNoRespec)));
					}
				}
				if (GhostButton(a_draw, "pathInvestChip", plus, { h, h }, 0xF067, "", canPut, false, a_s, alpha * (canPut ? 1.0f : 0.4f), 0x7262) && live && canPut) {
					PathCommand(Battle::Command::Type::kInvest, a_member.id, path);
					Sound("UIMenuOK");
				}
				if (Im::IsItemHovered()) {
					if (canPut) {
						Tip(Trf("Put a leftover point straight into the bonus: {} now, {} with it.", Rules::PathBonusText(path, bonus),
							Rules::PathBonusText(path, Rules::PathBonus(path, onPath + 1, a_tuning))));
					} else if (investBlock == Paths::Block::kNotMastered) {
						int left = 0;
						for (std::size_t i = 0; i < Paths::kNodes; ++i) {
							left += tree[i].Empty() ? 0 : std::max(tree[i].most - record.ranks[static_cast<std::size_t>(path)][i], 0);
						}
						Readout(Trf("Points go straight into the bonus once every node on this path is mastered: {} to go.", Trn(left, "{} more rank", "{} more ranks")));
					} else if (investBlock == Paths::Block::kBonusFull) {
						Readout(Trf("The bonus is at its highest: {}.", Rules::PathBonusText(path, bonus)));
					} else if (investBlock == Paths::Block::kNoPoints) {
						Readout(std::string(Tr("Not enough perk points")));
					} else {
						Readout(std::string(Paths::Why(investBlock)));
					}
				}
			}
			if (hovered) {
				Hovering(0x7250);
				const auto  rate = Rules::Percent(per);
				std::string what;
				switch (path) {
				case Role::kFighter:
					what = Trf("{} faster with their weapons in a fight", rate);
					break;
				case Role::kTank:
					what = Trf("{} less damage from every blow, arrow and harmful spell ({} at most)", rate, Rules::Percent(Rules::kMostDamageCut));
					break;
				case Role::kHealer:
					what = Trf("{} more to all the healing they do: their abilities, the spells they cast and the potions they drink", rate);
					break;
				case Role::kBruiser:
					what = Trf("{} more damage with their weapons in a fight", rate);
					break;
				case Role::kArcher:
					what = Trf("{} faster stamina recovery in combat ({} at most); arrows are always unlimited", rate, Rules::Percent(Rules::kMostArcherStamina));
					break;
				case Role::kMage:
					what = Trf("{} less magicka for every spell they cast ({} off at most)", rate, Rules::Percent(Rules::kMostCostCut));
					break;
				default:
					break;
				}
				auto text =
					Trf("Path bonus. Every point spent on the {}: {}.\n{} spent here: {}.", Tr(Paths::Info(path).title), what, onPath, Rules::PathBonusText(path, bonus));
				if (invested > 0) {
					text += Trf("\n{} of them put straight into the bonus.", invested);
				}
				if (bonusRoom <= 0) {
					text += Tr("\nAt its highest: more points change nothing.");
				} else if (mastered) {
					text += Tr("\nEvery node is mastered: leftover points can go straight into it.");
				} else {
					text += Tr("\nOnce every node here is mastered, leftover points can go straight into it.");
				}
				if (!theirs) {
					text += Trf("\nOnly while they are a {}; they are a {} now.", Tr(Info(path).name), Tr(Info(a_member.orders.role).name));
				}
				Readout(std::move(text));
			}
			hitsBlocked = extrasPreviousHitsBlocked;
			return { bmin, { bmax.x + 10.0f * a_s + 2.0f * h, bmax.y } };
		}
		Rect DrawPathUtilities(Im::ImDrawList* a_draw, const View::Member& a_member, V guideCenter, float a_s, float alpha, bool live)
		{
			const float breathe = 0.5f + 0.5f * std::sin(state.clock * 2.6f);
			const bool  extrasPreviousHitsBlocked = hitsBlocked;
			hitsBlocked = hitsBlocked || !live;
			// The guide: a ? in the header, as well as F1 and View on a pad.
			const V     gc = guideCenter;
			const float gr = 16.0f * a_s;
			bool        hovered = false;
			const bool  clicked = Hit("pathGuide", { gc.x - gr - 6.0f * a_s, gc.y - gr - 6.0f * a_s }, { gr * 2.0f + 12.0f * a_s, gr * 2.0f + 30.0f * a_s }, hovered) && live;
			const float hover = state.tw.Toward("pathGuide", 0, hovered ? 1.0f : 0.0f, 16.0f);
			if (hovered) {
				Hovering(0x7240);
				Tip(std::string(Tr("How paths work, step by step. F1 opens it too, and View on a pad.")));
			}
			Halo(a_draw, gc, gr * 2.2f, kAccent, alpha * 0.6f * hover);
			Draw::AddCircleFilled(a_draw, gc, gr, Alpha(Mix(palette.inset, kAccent, hover * 0.7f), alpha), 32);
			Draw::AddCircle(a_draw, gc, gr, Alpha(Mix(kTextFaint, kText, hover), alpha), 32, 1.2f * a_s);
			Glyph(a_draw, gc, 14.0f * a_s, 0xF128, Alpha(Mix(kTextDim, kText, hover), alpha));
			const float gw = LabelWidth(9.5f * a_s, Tr("GUIDE"), 1.8f * a_s);
			Label(a_draw, { gc.x - gw * 0.5f, gc.y + gr + 6.0f * a_s }, Alpha(Mix(kTextDim, kText, hover), alpha), 9.5f * a_s, Tr("GUIDE"), 1.8f * a_s);
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
				const bool  pressed = Hit("pathDebug", { uc.x - gr - 14.0f * a_s, uc.y - gr - 6.0f * a_s }, { gr * 2.0f + 28.0f * a_s, gr * 2.0f + 30.0f * a_s }, over) && live;
				const float lift = state.tw.Toward("pathDebug", 0, over ? 1.0f : 0.0f, 16.0f);
				if (over) {
					Hovering(0x7230);
					Tip(on ? std::string(Tr("Debug Mode. Turn off to restore earned progression.")) :
						std::string(Tr("Debug Mode: free ranks, unlimited perk points and ability testing.")));
				}
				Halo(a_draw, uc, gr * 2.4f, kAccent, alpha * (on ? 0.6f + 0.3f * breathe : 0.6f * lift));
				const U32 rest = palette.inset;
				Draw::AddCircleFilled(a_draw, uc, gr, Alpha(on ? Mix(kAccent, kEdge, 0.2f + 0.4f * lift) : Mix(rest, kAccent, lift * 0.7f), alpha), 32);
				Draw::AddCircle(a_draw, uc, gr, Alpha(on ? Mix(kEdge, kText, 0.4f + 0.4f * breathe) : Mix(kTextFaint, kText, lift), alpha), 32, (on ? 1.6f : 1.2f) * a_s);
				Glyph(a_draw, uc, 15.0f * a_s, 0xF188, Alpha(on ? kText : Mix(kTextDim, kText, lift), alpha));
				const auto  word = on ? std::string_view(Tr("DEBUG: ON")) : std::string_view(Tr("DEBUG"));
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
			hitsBlocked = extrasPreviousHitsBlocked;
			const float left = (a_member.unlimitedAllowed || a_member.unlimited) ? gc.x - 136.0f * a_s : gc.x - 30.0f * a_s;
			return { { left, gc.y - 26.0f * a_s }, { gc.x + 30.0f * a_s, gc.y + 40.0f * a_s } };
		}
		// Artwork belongs to the Effects pack. Load each theme/class only on first use.
		void DrawPathBackdrop(Im::ImDrawList* d, V origin, float width, float height, float scale, float alpha, Role role)
		{
			constexpr std::array<const char*, 3> themes{"vanilla", "oxblood", "nordic"};
			constexpr std::array<const char*, 6> classes{"fighter", "tank", "healer", "bruiser", "archer", "mage"};
			const auto theme = static_cast<std::size_t>(palette.style);
			const auto path = static_cast<std::size_t>(role);
			if (theme >= themes.size() || path >= classes.size()) return;
			using Picture = decltype(MF::LoadTexture(""));
			static std::array<Picture, 18> pictures{};
			static std::array<bool, 18> attempted{};
			const auto slot = theme * classes.size() + path;
			if (!attempted[slot]) {
				attempted[slot] = true;
				pictures[slot] = MF::LoadTexture(std::format("Data/Interface/FightWithMe/backdrops/{}-{}.png", themes[theme], classes[path]));
			}
			if (!pictures[slot]) return;
			const float side = std::min(width * .48f, height * .57f);
			const float left = theme == 1 ? 22.f : width - side - 22.f;
			const float opacity = theme == 2 ? .27f : .23f;
			// Feather again at presentation time so even bright spell artwork stays behind the tree.
			for (int band = 0; band < 24; ++band) {
				const float top = static_cast<float>(band) / 24.f, bottom = static_cast<float>(band + 1) / 24.f;
				const int begin = d->VtxBuffer.Size;
				Draw::AddImage(d, pictures[slot], {origin.x + left * scale, origin.y + (116 + top * side) * scale},
					{origin.x + (left + side) * scale, origin.y + (116 + bottom * side) * scale}, {0, top}, {1, bottom}, IM_COL32(255,255,255,255));
				for (int v = begin; v < d->VtxBuffer.Size; ++v) {
					auto& vertex = d->VtxBuffer.Data[v];
					const float y = (vertex.pos.y - origin.y - 116 * scale) / (side * scale);
					vertex.col = Alpha(IM_COL32(255,255,255,255), alpha * opacity * Smooth(y / .08f) * Smooth((1 - y) / .42f));
				}
			}
		}
		void DrawPathAtmosphere(Im::ImDrawList* d, V origin, float width, float height, float scale, float alpha, float clock)
		{
			const bool ember = palette.style == UITheme::Style::Oxblood, snow = palette.Engraved();
			auto point = [&](float x, float y) -> V { return {origin.x + x * scale, origin.y + y * scale}; };
			Draw::PushClipRect(d, point(18, 119), point(width - 18, height - 183), true);
			// Dry pigment and carved hatch marks tie the atmosphere to the illustration medium.
			for (int grain = 0; grain < 180; ++grain) {
				const float x = 24 + std::fmod(grain * 173.13f, width - 48);
				const float y = 124 + std::fmod(grain * 97.71f, height - 310);
				const float edgeWeight = .25f + .75f * std::abs(2 * x / width - 1);
				const U32 pigment = ember ? IM_COL32(155,78,59,255) : snow ? IM_COL32(162,151,125,255) : IM_COL32(175,175,175,255);
				const float length = ember ? 12.f + grain % 23 : 3.f + grain % 9;
				Draw::AddLine(d, point(x,y), point(x+length,y+(ember?-length*.48f:snow?1.3f:-.7f)), Alpha(pigment,alpha*edgeWeight*(ember?.07f:.035f)), (grain%3==0?1.3f:.65f)*scale);
			}
			// Sample a continuous density field. Adjacent cells share vertex colors, so
			// the mist has no stacked stroke edges, polygonal joins or opaque bands.
			constexpr int columns=96, rows=40;
			const float mistTop=119.f, mistHeight=height-302.f;
			auto density=[&](float x,float y) {
				const float u=x/width;
				float value=0.f;
				for(int ribbon=0;ribbon<(snow?3:2);++ribbon) {
					float distance,spread,along;
					if(ember) {
						const float v=(y-142.f)/(height-330.f);
						distance=x-(width*(ribbon==0?.08f:.90f)+42.f*std::sin(v*7.f+clock*.13f+ribbon));
						spread=55.f;along=std::pow(std::sin(Clamp01(v)*3.14159265f),2.f);
					} else {
						const float centre=snow?180.f+ribbon*62.f+42.f*std::sin(u*6.f+clock*.11f+ribbon*1.4f)+12.f*std::sin(u*12.f-clock*.15f):
							310.f+ribbon*220.f+58.f*std::sin(u*4.f+clock*.07f+ribbon*2.f);
						distance=y-centre;spread=snow?36.f+12.f*std::sin(u*5.f+clock*.09f+ribbon):72.f+20.f*std::sin(u*4.f+clock*.08f+ribbon);
						along=std::pow(std::sin(Clamp01(u)*3.14159265f),2.f);
					}
					value+=std::exp(-.5f*distance*distance/(spread*spread))*along*(snow?.065f:ember?.075f:.055f);
				}
				const float edgeFade=Smooth((y-mistTop)/48.f)*Smooth((mistTop+mistHeight-y)/64.f);
				return Alpha(ember?IM_COL32(126,46,36,255):snow?IM_COL32(111,134,121,255):IM_COL32(172,172,172,255),alpha*value*edgeFade);
			};
			std::array<U32,columns+1> previous{},next{};
			for(int col=0;col<=columns;++col)previous[col]=density(width*col/columns,mistTop);
			for(int row=0;row<rows;++row) {
				const float y0=mistTop+mistHeight*row/rows,y1=mistTop+mistHeight*(row+1)/rows;
				for(int col=0;col<=columns;++col)next[col]=density(width*col/columns,y1);
				for(int col=0;col<columns;++col) {
					if(((previous[col]|previous[col+1]|next[col]|next[col+1])>>24)==0)continue;
					Draw::AddRectFilledMultiColor(d,point(width*col/columns,y0),point(width*(col+1)/columns,y1),previous[col],previous[col+1],next[col+1],next[col]);
				}
				previous=next;
			}
			const int count = snow ? 100 : ember ? 108 : 64;
			const float travel = std::max(height - 280.f, 1.f);
			for (int i = 0; i < count; ++i) {
				const float seed = static_cast<float>(i), depth = (i % 5) / 4.f;
				const float period = (ember ? 9.f : snow ? 24.f : 30.f) + (i % 9) * (snow ? 1.2f : 1.7f);
				std::uint32_t hash = (static_cast<std::uint32_t>(i) + 1u) * 747796405u + 2891336453u;
				hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
				hash = (hash >> 22u) ^ hash;
				const float u = std::fmod(clock / period + (hash & 65535u) / 65536.f, 1.f);
				const float anchor = (hash >> 16u) / 65536.f * std::max(width - 50, 1.f);
				const float drift = snow ? 65 * u + 26 * std::sin(clock * .25f + seed) : (ember ? 34.f : 24.f) * std::sin(clock * (ember ? .65f : .18f) + seed);
				const float x = !snow&&!ember ? 25.f+u*(width-50.f) : 25.f+std::fmod(anchor+drift+width,width-50.f);
				const float y = !snow&&!ember ? 130.f+(hash>>16u)/65536.f*(travel-20.f)+14.f*std::sin(clock*.15f+seed) : 120.f+travel*(ember?1.f-u:u);
				const V at = point(x,y);
				const float life = std::sin(u * 3.14159265f), flicker = ember ? .72f + .28f * std::sin(clock * (3 + i % 4) + seed) : 1.f;
				const float opacity = alpha * life * flicker * (.32f + .42f * depth);
				const float size = (snow ? 1.1f + 1.9f * depth : ember ? .9f + 1.8f * depth : .55f + .6f * depth) * scale;
				const bool ash = ember && i % 4 == 0;
				const U32 tint = snow ? IM_COL32(213,202,173,255) : ember ? (ash ? IM_COL32(152,128,121,255) : i % 3 ? IM_COL32(207,71,43,255) : IM_COL32(239,174,110,255)) : IM_COL32(230,230,230,255);
				if(!snow&&!ember) {
					// Fine sideways silver dust: soft halos and tiny cores, never tumbling flakes.
					Draw::AddCircleFilled(d,at,size*3.5f,Alpha(tint,opacity*.04f),16);
					Draw::AddCircleFilled(d,at,size,Alpha(tint,opacity*.50f),12);
					if(i%6==0)Draw::AddLine(d,point(x-7.f-depth*5.f,y),at,Alpha(tint,opacity*.12f),.65f*scale);
					continue;
				}
				if (ember && !ash) for (int halo = 3; halo > 0; --halo)
					Draw::AddCircleFilled(d, at, size * (1 + halo * 1.3f), Alpha(tint, opacity * .025f), 12);
				const float turn = clock * (snow ? .22f : .1f) + seed;
				const float dx = std::cos(turn) * size, dy = std::sin(turn) * size;
				// Irregular pigment flecks and paper slivers instead of round bokeh/snowflakes.
				const std::array<V,4> fleck{{{at.x-dx,at.y-dy},{at.x+dy*.7f,at.y-dx*.7f},{at.x+dx*1.8f,at.y+dy*1.8f},{at.x-dy*.5f,at.y+dx*.5f}}};
				Draw::AddConvexPolyFilled(d, fleck.data(), 4, Alpha(tint,opacity*(snow?.65f:ash?.5f:.85f)));
				if (ember && !ash) {
					Draw::AddLine(d, at, point(x-3,y+8+depth*7), Alpha(tint,opacity*.28f), size*.6f);
				} else if (!snow && !ember && i%9==0) {
					Draw::AddCircleFilled(d, at, size*2.8f, Alpha(tint,opacity*.055f), 12);
				}
			}
			Draw::PopClipRect(d);
		}

		// The selected perk: ranks, learning, equipped abilities and refunds.
		// Its vertical rank ladder, circular equip sockets and all progression controls
		// retain their original draw and command logic. Bounds describe its content area;
		// the caller supplies the sliding background, clipping and close control.
		void DrawPathDetail(Im::ImDrawList* a_draw, const View::Member& a_member, const Rules::Tuning& a_tuning, V min, V max, float a_s, float alpha, const Input& a_input,
			bool live)
		{
			auto&       page = state.page;
			const auto  path = page.path;
			const auto& tree = Paths::PathOf(path);
			const auto& record = a_member.record;
			const int   onPath = Paths::Spent(record, path);
			const bool  mastered = Paths::Mastered(record, path);
			const int   bonusRoom = Rules::PathBonusRoom(path, onPath, a_tuning);
			const float detailW = max.x - min.x;
			if (detailW <= 0.0f || max.y <= min.y || page.cursor < 0 || page.cursor >= static_cast<int>(Paths::kNodes))
				return;
			if (tree[static_cast<std::size_t>(page.cursor)].Empty())
				return;
			int others = 0, othersHeld = 0;
			for (std::size_t i = 0; i < Paths::kNodes; ++i) {
				if (!tree[i].Empty() && !tree[i].capstone) {
					++others;
					othersHeld += record.ranks[static_cast<std::size_t>(path)][i] > 0 ? 1 : 0;
				}
			}
			auto shut = [&](std::size_t index) {
				const auto& n = tree[index];
				if (a_member.unlimited) return false;
				return n.kind == Paths::Kind::kTrait ? record.peak < Paths::RequiredLevel(n, record.ranks[static_cast<std::size_t>(path)][index] + 1) :
					!Paths::TierOpen(record, path, n.tier) || (n.capstone && othersHeld < others);
			};
			// Original pointer-only controls also respect the drawer's live gate.
			const bool detailPreviousHitsBlocked = hitsBlocked;
			hitsBlocked = hitsBlocked || !live;
			// --- The card for the chosen node ---
			// Its mark and name, what it does with the numbers picked out, a ladder of its ranks, and
			// one thing to press. At the foot, the two abilities carried into a fight as sockets to
			// drop it in, and under them the rarely used switches.
			const auto  chosen = static_cast<std::size_t>(page.cursor);
			const auto& node = tree[chosen];
			const int   rank = record.ranks[static_cast<std::size_t>(path)][chosen];
			const auto  learnBlock = Paths::CanLearn(record, path, chosen, a_member.pointsEarned, a_member.unlimited);
			const auto  forgetBlock = Paths::CanForget(record, path, chosen);
			const float dx = min.x;
			const float dTop = min.y;
			const float dBottom = max.y;
			const float swap = Smooth((state.clock - page.tabAt) / 0.25f);
			float       y = dTop;
			// The parent window owns the single divider; this content is docked into it.
			const float da = alpha * (0.3f + 0.7f * swap);
            if(!a_member.unlimited && node.kind!=Paths::Kind::kTrait && record.peak<Paths::RequiredLevel(node,1)) {
                Glyph(a_draw,{dx+detailW*.5f,dTop+58.f*a_s},28.f*a_s,0xF023,Alpha(kTextDim,da));
                TextCentered(a_draw,{dx+detailW*.5f,dTop+111.f*a_s},Alpha(kTextDim,da),17.f*a_s,Trf("Requires level {}",Paths::RequiredLevel(node,1)));
                hitsBlocked=detailPreviousHitsBlocked;
                return;
            }
			const float breathe = 0.5f + 0.5f * std::sin(state.clock * 3.0f);
			const bool  isAbility = node.kind == Paths::Kind::kAbility;
			const bool  known = isAbility && rank > 0;
			const auto& slots = record.slots[static_cast<std::size_t>(path)];
			const bool bruiserBlocked=!a_member.unlimited&&path==Role::kBruiser&&!a_member.arsenal.twoHanded;
			const bool canLearn = !bruiserBlocked && learnBlock == Paths::Block::kNone;
			const bool  canForget = forgetBlock == Paths::Block::kNone && a_member.respec;
			const int   cost = a_member.unlimited ? 0 : Paths::CostOf(path, chosen, rank + 1);
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
				std::string kind(Tr("TRAIT  /  ALWAYS ON"));
				if (isAbility) {
					kind = node.capstone ? Tr("CAPSTONE") : (node.base > 0 ? Tr("ABILITY  /  EVERYONE STARTS WITH IT") : Tr("ABILITY"));
				} else if (node.kind == Paths::Kind::kBranch) {
					kind = Upper(Trf("Branch of {}", Tr(Paths::Info(node.ability).name)));
				}
				const float tx = dx + 88.0f * a_s;
				const float room = dx + detailW - tx;
				Label(a_draw, { tx, y + 8.0f * a_s }, Alpha(kEdge, da), 10.5f * a_s, kind, 2.0f * a_s);
				MasterFont titleFont(isAbility && rank >= 3);
				const auto  title = Upper(Tr(node.name));
				const float size = std::min(25.0f * a_s, 25.0f * a_s * room / std::max(LabelWidth(25.0f * a_s, title, 2.8f * a_s), 1.0f));
				Label(a_draw, { tx, y + 26.0f * a_s }, Alpha(kText, da), size, title, size * 0.11f);
				const auto status =
					rank >= node.most ? std::string(Tr("Mastered")) : (rank > 0 ? Trf("Rank {} of {}", RankWord(rank), RankWord(node.most)) : std::string(Tr("Not learned yet")));
				Text(a_draw, { tx, y + 58.0f * a_s }, Alpha(rank > 0 ? kEdge : kTextFaint, da), 14.0f * a_s, status);
				y += (node.capstone ? 104.0f : 96.0f) * a_s;
			}
			Draw::AddLine(a_draw, { dx, y }, { dx + detailW, y }, Alpha(kGlassEdge, da), 1.0f);
			Stud(a_draw, { dx + detailW * 0.5f, y }, 3.0f * a_s, da);
			y += 16.0f * a_s;

			// Facts flow onto another row instead of running past the drawer edge.
			{
				std::vector<std::pair<std::uint32_t, std::string>> facts;
				if (isAbility) {
					Paths::Record next = record;
					if (rank == 0) {
						next.ranks[static_cast<std::size_t>(path)][chosen] = 1;
					}
					facts.emplace_back(0xF254, Trf("Ready again in {:.0f} s", Paths::Cooldown(next, path, node.ability)));
                    facts.emplace_back(0xF0E7,Trf("{:.0f}% maximum {}",AbilityResource::Share(std::max(1,rank))*100,ChoosesSpells(path)?Tr("magicka"):Tr("stamina")));
					if (!node.capstone) {
						int bheld = 0, bmost = 0;
						for (std::size_t b = chosen + 1; b <= chosen + 2 && b < Paths::kNodes; ++b) {
							bheld += record.ranks[static_cast<std::size_t>(path)][b];
							bmost += tree[b].most;
						}
						facts.emplace_back(0xF126, Trf("Branches {} / {}", bheld, bmost));
					}
				} else if (node.kind == Paths::Kind::kBranch) {
					const int parent = Paths::ParentOf(path, chosen);
					const int parentRank = parent >= 0 ? record.ranks[static_cast<std::size_t>(path)][static_cast<std::size_t>(parent)] : 0;
					facts.emplace_back(0xF0C1, parentRank > 0 ? Trf("{} at rank {}", Tr(Paths::Info(node.ability).name), RankWord(parentRank)) :
																Trf("Needs {} first", Tr(Paths::Info(node.ability).name)));
				} else {
					facts.emplace_back(0xF534, Tr("II: Level 25   /   III: Level 50"));
				}
				float fx = dx, rowH = 18.f * a_s;
				for (const auto& [glyph, words] : facts) {
					const float factW = 20.f * a_s + TextSize(14.f * a_s, words).x;
					if (fx > dx && fx + factW > max.x) {
						y += rowH + 8.f * a_s;
						fx = dx;
						rowH = 18.f * a_s;
					}
					Glyph(a_draw, { fx + 7.0f * a_s, y + 9.0f * a_s }, 12.0f * a_s, glyph, Alpha(kEdge, da));
					const float factH = RichText(a_draw, { fx + 20.f * a_s, y }, 14.f * a_s, words,
						max.x - fx - 20.f * a_s, Alpha(kTextDim, da), Alpha(kTextDim, da));
					rowH = std::max(rowH, factH);
					fx += factW + 22.f * a_s;
				}
				y += rowH + 12.f * a_s;
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
						PanelGradient(a_draw, rmin, rmax, band, Alpha(kAccent, 0.0f), Alpha(kAccent, 0.0f), band);
					}
					if (hover > 0.01f) {
						PanelFill(a_draw, rmin, rmax, Alpha(kTileHover, da * hover), 0.0f, 0);
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
						const auto  price = a_member.unlimited ? std::string(Tr("Free")) : std::to_string(Paths::CostOf(path, chosen, r));
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
					why = Paths::Requirement(learnBlock,node,rank+1);
					if (learnBlock == Paths::Block::kCapstone) {
						why = Trf("Requires all other perks unlocked: {} / {}", othersHeld, others);
					} else if (learnBlock == Paths::Block::kLevel) {
						why = Trf("Requires level {}", Paths::RequiredLevel(node, rank + 1));
					} else if (learnBlock == Paths::Block::kParent) {
						why = Paths::Requirement(learnBlock,node,rank+1);
					} else if (learnBlock == Paths::Block::kNoPoints) {
						why = Tr("Not enough perk points");
					}
				}
				if(bruiserBlocked)why=Tr("Requires a two-handed melee weapon in inventory.");
                // What stands in the way may need two lines; the plate grows upwards from the button's floor.
				const float whyH = why.empty() ? 0.0f : RichText(nullptr, {}, 13.5f * a_s, why, mainW - 52.0f * a_s, 0, 0);
				const float plateH = why.empty() ? actionH : std::max(actionH, whyH + 18.0f * a_s);
				const float top = actionY + actionH - plateH;
				state.guide.areas[kGuideAction] = { { dx - 10.0f * a_s, top - 10.0f * a_s }, { dx + detailW + 10.0f * a_s, actionY + actionH + 10.0f * a_s } };

				// With room above the button, an ability says when they use it on their own.
				if (isAbility) {
					const auto& info = Paths::Info(node.ability);
					const auto own = Trf("On their own: {}", info.own.empty() ? Tr("When it is needed.") : Tr(info.own));
					const float ownH = RichText(nullptr, {}, 13.5f * a_s, own, detailW - 24.0f * a_s, 0, 0);
					if (y + ownH <= top - 10.0f * a_s) {
						Glyph(a_draw, { dx + 7.0f * a_s, y + 9.0f * a_s }, 12.0f * a_s, 0xF5DC, Alpha(kTextFaint, da));
						RichText(a_draw, { dx + 24.0f * a_s, y }, 13.5f * a_s, own, detailW - 24.0f * a_s, Alpha(kTextFaint, da), Alpha(kTextDim, da));
					}
				}

				// With every node on the path mastered, points left over go straight into its bonus.
				const bool canInvest = rank >= node.most && mastered && Paths::CanInvest(record, path, a_member.pointsEarned, bonusRoom) == Paths::Block::kNone;
				if (canInvest) {
					if (ActionButton(a_draw, "pathInvest", { dx, actionY }, { mainW, actionH }, Tr("INTO THE BONUS"), 1, a_s, da, 0x7270) || (live && a_input.activate)) {
						PathCommand(Battle::Command::Type::kInvest, a_member.id, path);
						Sound("UIMenuOK");
					}
					if (Im::IsItemHovered()) {
						Tip(Trf("Every node here is mastered: put a leftover point straight into the path bonus, {} now, {} with it.",
							Rules::PathBonusText(path, Rules::PathBonus(path, onPath, a_tuning)), Rules::PathBonusText(path, Rules::PathBonus(path, onPath + 1, a_tuning))));
					}
				} else if (rank >= node.most) {
					const float cy = actionY + actionH * 0.5f;
					const auto  word = std::string(Tr("MASTERED"));
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
					if (ActionButton(a_draw, "pathLearn", { dx, actionY }, { mainW, actionH }, rank == 0 ? Tr("LEARN") : Tr("RANK UP"), cost, a_s, da, 0x7200) ||
						(live && a_input.activate)) {
						PathCommand(Battle::Command::Type::kLearn, a_member.id, path, chosen);
						Sound("UIMenuOK");
					}
				} else {
					const V pmin{ dx, top }, pmax{ dx + mainW, actionY + actionH };
					PanelFill(a_draw, pmin, pmax, Alpha(kAmber, da * 0.06f), 0.0f, 0);
					PanelEdge(a_draw, pmin, pmax, Alpha(kAmber, da * 0.35f), 0.0f, 0, 1.0f);
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
							Tip(std::string(Tr("Give back a rank.")));
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
				SectionLabel(a_draw, { dx, loadY }, a_s, da, path == a_member.orders.role ? Tr("INTO A FIGHT") : Tr("CARRIED ON THIS PATH"), false);
				std::string hint;
				U32         tone = kTextFaint;
				if (known && a_member.inCombat) {
					hint = Tr("Carrying waits for the fight to end");
					tone = kAmber;
				} else if (known && !carried) {
					hint = Trf("Choose a slot for {}", Tr(node.name));
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
						PanelFill(a_draw, cmin, cmax, Alpha(kTile, da * lift), 0.0f, 0);
					}
					const V     sc{ (cmin.x + cmax.x) * 0.5f, cmin.y + 32.0f * a_s };
					const float sr = 26.0f * a_s;
					if (here) {
						Halo(a_draw, sc, sr * 2.1f, kAccent, da * 0.85f);
					} else if (offer) {
						Halo(a_draw, sc, sr * 1.9f, kAccent, da * (0.12f + 0.16f * breathe + 0.4f * hover));
					}
					if (offer) NodeSelection(a_draw,sc,sr,0,false,hover,state.clock,da);
					Draw::AddCircleFilled(a_draw, sc, sr, Alpha(IM_COL32(13, 13, 14, 248), da), 40);
					Draw::AddCircle(a_draw, sc, sr, Alpha(here ? kEdge : Mix(kGlassEdge, kText, lift * 0.4f), da), 40, here ? 1.8f * a_s : 1.0f);
					Draw::AddCircle(a_draw, sc, sr + 5.0f * a_s, Alpha(kGlassEdge, da * 0.6f), 40, 1.0f);

					auto inside = [&](Paths::Ability a_ability, float a_alpha) {
						if (a_alpha <= 0.01f || a_ability == Paths::Ability::kNone) {
							return;
						}
						const auto& info = Paths::Info(a_ability);
						if (Paths::Rank(record,path,a_ability) >= 3) Mastery(a_draw,sc,sr,a_alpha);
						if (a_ability == Paths::Ability::kMagnus) {
							DrawOrb(a_draw, sc, 13.0f * a_s, true, a_s, a_alpha);
							return;
						}
						Emblem(a_draw, sc, 17.0f * a_s, Alpha(kAccent, a_alpha), Alpha(kEdge, a_alpha), 1.2f * a_s);
						Glyph(a_draw, sc, 13.0f * a_s, info.glyph ? info.glyph : 0xF128, Alpha(kText, a_alpha));
					};
					inside(slots[s], da * (1.0f - hover));
					inside(node.ability, da * hover);
					// Its key, on the rim, as a hotkey sits on an ability.
					Keycap(a_draw, { sc.x - sr * 0.92f - 9.0f * a_s, sc.y + sr * 0.38f - 9.0f * a_s }, 18.0f * a_s, 11.0f * a_s, s == 0 ? "1" : "2", da);
					const auto  ability = hover > 0.5f ? node.ability : slots[s];
					const auto& info = Paths::Info(ability);
					TextCentered(a_draw, { sc.x, sc.y + sr + 18.0f * a_s }, Alpha(hover > 0.5f ? kText : (here ? kText : kTextDim), da), 15.0f * a_s,
						info.name.empty() ? std::string_view(Tr("Empty")) : Tr(info.name));
					if (hover > 0.5f) {
						const auto  word = std::string(Tr("CARRY HERE"));
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
				const auto label = armed ? Trf("AGAIN: ALL {} BACK", onPath) : std::string(Tr("START OVER"));
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

			hitsBlocked = detailPreviousHitsBlocked;
		}

		// Fullscreen path tree and the docked detail panel, with two carried abilities beneath.
		// Opens over the party view and follows LB and RB to the next companion.
		// Nodes and their hit targets share the same animated translation.
		Rect DrawPathPage(Im::ImDrawList* d, const View::Member& member, const Rules::Tuning& tuning, V lo, V hi, float s, float appear, const Input& input, int dy, int tab)
		{
			auto& page = state.page;
            if(page.followRole){page.path=member.orders.role;page.cursor=FirstWorthy(member,page.path);page.followRole=false;page.pathPickerOpen=false;}
            const auto entrance=TreeEntrance::At(state.clock-page.openedAt);
            if(!entrance.visible)return {lo,hi};
            const bool live = !state.closing && !state.guide.open && !page.transition && entrance.interactive;
			const bool pickerTakingInput = page.pathPickerOpen;
			if (page.pathPickerOpen && live) {
				page.pathPickerCursor = (page.pathPickerCursor + dy + 6) % 6;
				if (input.activate) {
					page.path = static_cast<Role>(page.pathPickerCursor);
					page.cursor = FirstWorthy(member, page.path);
					page.tabAt = state.clock;
					page.detailOpen = false;
					page.pathPickerOpen = false;
					Sound("UIMenuOK");
				}
			}
			const bool treeLive = live && !pickerTakingInput;
			if (tab && live) {
				page.pathPickerOpen = false;
				page.path = static_cast<Role>((static_cast<int>(page.path) + tab + 6) % 6);
				page.tabAt = state.clock;
				page.resetArmed = false;
				page.detailOpen = false;
			}
			const auto  role = page.path;
			const auto& tree = Paths::PathOf(role);
			const auto& ranks = member.record.ranks[static_cast<std::size_t>(role)];
			
			// Refresh progression feedback and clear transient selection when changing companion/path.
			if (page.seenMember != member.id || page.seenPath != role) {
				if (page.seenMember != 0) {
					page.detailOpen = false;
					page.pathPickerOpen = false;
				}
				page.detailT = 0;
				page.previewNode = -1;
				page.previewRank = 0;
				page.resetArmed = false;
				page.seenMember = member.id;
				page.seenPath = role;
				page.seen = ranks;
				page.rankedAt.fill(-99.0f);
				page.lastUnspent = Unspent(member);
			}
			for (std::size_t i = 0; i < Paths::kNodes; ++i) {
				if (ranks[i] > page.seen[i]) {
					page.rankedAt[i] = state.clock;
					Sound("UIMenuOK");
				}
				page.seen[i] = ranks[i];
			}
			if (page.lastUnspent != Unspent(member)) {
				page.lastUnspent = Unspent(member);
				page.pointsAt = state.clock;
			}
			if (state.guide.open)
				page.detailOpen = true;
			page.detailT = std::clamp(page.detailT + (page.detailOpen ? 1.0f : -1.0f) * std::min(Im::GetIO()->DeltaTime, .05f) / .42f, 0.0f, 1.0f);
			page.pickerT = std::clamp(page.pickerT + (page.pathPickerOpen ? 1.0f : -1.0f) * std::min(Im::GetIO()->DeltaTime, .05f) / .24f, 0.0f, 1.0f);
			const float pickerReveal = Smooth(page.pickerT);
			const float reveal = Smooth(page.detailT);
			const bool  activateToInspect = treeLive && input.activate && !page.detailOpen;
			if (activateToInspect)
				page.detailOpen = true;
			const float alpha = appear * EaseOut((state.clock - page.openedAt) / 0.3f);
			const float w = (hi.x - lo.x) / s, h = (hi.y - lo.y) / s;
			const float split = w - 480.0f;
			const float drawerX = split + (w - split) * (1.0f - reveal);
			// The tree uses the whole pane, then makes room for the detail drawer.
			const float canvasW = drawerX - 24.0f;
			bool        nodeUnderMouse = false;
			// The same palette, type, materials and ornaments as every existing window.
			const U32 ivory = kText, muted = kTextDim, faint = kTextFaint;
			const U32 edge = kEdge, bright = kFocusRing, structure = kGlassEdge;
			// Reclaimed tab space lets the emblems grow without crowding their branches.
			const float nodeScale = std::clamp(canvasW / 1376.0f, .93f, 1.0f);
			const float extra = std::max(h - 732.0f, 0.0f);
			const float apexY = 180.0f + extra * .18f;
			const float branchY = h - 298.0f;
			const float abilityY = branchY - 202.0f * nodeScale;
			auto        pos = [&](float x, float y) -> V { return { lo.x + x * s, lo.y + y * s }; };
			auto        ink = [&](U32 c, float a = 1.0f) { return Alpha(c, alpha * a); };
			auto line = [&](float x, float y, float xx, float yy, U32 c, float a = 1.0f, float width = 1.0f) { Draw::AddLine(d, pos(x, y), pos(xx, yy), ink(c, a), width * s); };
			auto text = [&](float x, float y, float size, std::string_view str, U32 c) { Text(d, pos(x, y), ink(c), size * s, str); };
			auto caps = [&](float x, float y, float size, std::string_view str, U32 c, float tracking = 2.0f) { Label(d, pos(x, y), ink(c), size * s, str, tracking * s); };
			auto midcaps = [&](float x, float y, float size, std::string_view str, U32 c, float tracking = 2.0f) {
				const float width = LabelWidth(size * s, str, tracking * s) / s;
				caps(x - width / 2, y, size, str, c, tracking);
			};
			auto hit = [&](const char* id, float x, float y, float ww, float hh, bool& hovered) {
				if (!treeLive) { hovered = false; return false; }
				return Hit(id, pos(x, y), { ww * s, hh * s }, hovered);
			};

			Draw::PushClipRect(d, lo, hi, true);
			// Dark painted grounds preserve contrast while matching each illustration medium.
			const bool vanilla=palette.style==UITheme::Style::Vanilla, manuscript=palette.Engraved();
			PanelGradient(d,lo,hi,ink(vanilla?IM_COL32(3,3,3,255):manuscript?IM_COL32(10,12,11,255):IM_COL32(12,5,5,255)),
				ink(vanilla?IM_COL32(9,9,9,255):manuscript?IM_COL32(19,20,16,255):IM_COL32(22,9,8,255)),
				ink(vanilla?IM_COL32(2,2,2,255):manuscript?IM_COL32(8,10,9,255):IM_COL32(7,3,3,255)),
				ink(vanilla?IM_COL32(6,6,6,255):manuscript?IM_COL32(14,15,12,255):IM_COL32(14,6,5,255)));
			DrawPathBackdrop(d, lo, canvasW, h, s, alpha * Smooth((state.clock - page.tabAt) / .65f), role);
			DrawPathAtmosphere(d, lo, canvasW, h, s, alpha, state.clock);
			for (const float side : { -1.0f, 1.0f }) {
				const float x = side < 0 ? 18.0f : w - 18;
				line(x, 18, x - side * 64, 18, edge, .35f);
				line(x, 18, x, 18 + 44, edge, .35f);
				line(x, h - 18, x - side * 64, h - 18, edge, .25f);
				line(x, h - 18, x, h - 62, edge, .25f);
			}

			// Only the selected path occupies the header. Other roles live in its dropdown.
			const Rect pickerBounds{ pos(30, 24), pos(324, 96) };
			midcaps(canvasW * .5f, 25, 14, Upper(Trf("{}  /  LEVEL {}", member.name, member.level)), muted, 2.5f);
			midcaps(canvasW * .5f, 51, 34, Upper(Tr(Paths::Info(role).title)), ivory, 4.0f);
			line(canvasW * .5f - 155, 108, canvasW * .5f - 12, 108, edge, .24f);
			line(canvasW * .5f + 12, 108, canvasW * .5f + 155, 108, edge, .24f);
			Diamond(d, pos(canvasW * .5f, 108), 4 * s, 0, ink(edge, .7f), s);
			const Rect bonusBounds = DrawPathBonus(d, member, tuning, pos(w - 510, 43), s * 1.05f, alpha * .85f, treeLive);
			const Rect utilityBounds = DrawPathUtilities(d, member, pos(w - 55, 48), s * 1.2f, alpha, treeLive);
			const int points = Unspent(member);
			state.guide.areas[kGuideTabs] = { pickerBounds.min, pickerBounds.max };

			// Fixed levels make the parent/branch relationship readable without boxed columns.
			state.guide.areas[kGuideTree] = { pos(30, 126), pos(canvasW - 30, h - 38) };
			std::array<V, Paths::kNodes> locations{};
			const std::array<float, 3>   familyX{ canvasW * .18f, canvasW * .5f, canvasW * .82f };
			for (std::size_t i = 0; i < 9; ++i) {
				const std::size_t family = i / 3, part = i % 3;
				locations[i] = { familyX[family] + (part == 1 ? -94.0f : part == 2 ? 94.0f : 0.0f) * nodeScale,
					(part == 0 ? abilityY : branchY) + (family == 1 ? -18.0f : 12.0f) * nodeScale };
			}
			locations[9] = { canvasW * .5f, apexY };
			// Other roles use this upper family for a fourth ability and its upgrades.
			locations[10] = { canvasW * .5f - 220.0f * nodeScale, apexY + 30.0f * nodeScale };
			locations[11] = { canvasW * .5f + 220.0f * nodeScale, apexY + 30.0f * nodeScale };
			for (std::size_t i = 12; i < Paths::kNodes; ++i) locations[i] = { familyX[i - 12], h - 135 };
			std::array<float, Paths::kNodes> nodeAppear{}, nodeFocus{};
			for (std::size_t i = 0; i < Paths::kNodes; ++i) {
				const float delay = i < 12 ? .055f * static_cast<float>(i / 3) + .065f * static_cast<float>(i % 3) : .22f;
				nodeAppear[i] = Smooth((state.clock - page.tabAt - delay) / .5f);
				locations[i].y += 12 * (1 - nodeAppear[i]);
				nodeFocus[i] = state.tw.Toward("pathNodeFocus", static_cast<std::uint64_t>(role) * 32 + i,
					page.detailOpen && static_cast<int>(i) == page.cursor ? 1.0f : 0.0f, 8.0f);
			}
			int others = 0, held = 0;
			for (std::size_t i = 0; i < Paths::kNodes; ++i)
				if (!tree[i].Empty() && !tree[i].capstone) {
					++others;
					held += ranks[i] > 0 ? 1 : 0;
				}
			const bool apexOpen = member.unlimited || (held >= others && Paths::TierOpen(member.record, role, tree[9].tier));

			page.cursor = std::clamp(page.cursor, 0, static_cast<int>(Paths::kNodes) - 1);
			if (tree[static_cast<std::size_t>(page.cursor)].Empty())
				page.cursor = 0;
			if (treeLive && (input.dx || dy)) {
                std::array<TreeNavigation::Node, Paths::kNodes> navigation{};
                for (std::size_t i = 0; i < Paths::kNodes; ++i)
                    navigation[i] = { locations[i].x, TreeNavigation::Row(i), !tree[i].Empty() };
                page.cursor = TreeNavigation::Move(navigation, page.cursor, input.dx, dy);
				page.resetArmed = false;
				page.detailOpen = true;
			}
			// Selection is safe: inspecting a node never spends points.
			std::array<bool, Paths::kNodes> nodeHover{};
			for (std::size_t i = 0; i < Paths::kNodes; ++i) {
				if (tree[i].Empty())
					continue;
				const auto  p = locations[i];
				const float radius = tree[i].capstone ? 51.0f : tree[i].kind == Paths::Kind::kAbility ? 52.0f : 34.0f;
				bool        over = false;
				Im::PushID(9200 + static_cast<int>(i));
				const bool  trait = tree[i].kind == Paths::Kind::kTrait;
				const float hitScale = trait ? 1.0f : nodeScale;
				const float hitHalfWidth = tree[i].kind == Paths::Kind::kBranch ? 73.0f : 105.0f;
				const bool ability = tree[i].kind == Paths::Kind::kAbility || tree[i].capstone;
				if (hit("pathNode", p.x - (trait ? 112.0f : hitHalfWidth * hitScale), p.y - (ability ? 105.f : radius + 10) * hitScale,
						trait            ? 234.0f :
						tree[i].capstone ? 430.0f * hitScale :
										   hitHalfWidth * 2.0f * hitScale,
						trait ? 135.0f : (ability ? 215.f : radius * 2 + 74) * hitScale, over)) {
					page.cursor = static_cast<int>(i);
					page.resetArmed = false;
					page.detailOpen = true;
					page.previewNode = -1;
					page.previewRank = 0;
				}
				if (over && treeLive && Im::IsMouseClicked(1)) {
					page.cursor = static_cast<int>(i);
					page.resetArmed = false;
					page.detailOpen = true;
					page.previewNode = -1;
					page.previewRank = 0;
					if (member.respec && Paths::CanForget(member.record, role, i) == Paths::Block::kNone) {
						PathCommand(Battle::Command::Type::kForget, member.id, role, i);
						Sound("UIMenuCancel");
					}
				}
				nodeUnderMouse = nodeUnderMouse || over;
				nodeHover[i] = over;
				Im::PopID();
			}
			const auto  chosen = static_cast<std::size_t>(page.cursor);

			// Curves only connect an ability to its own two upgrades.
			for (std::size_t i = 0; i < 12; i += 3) {
				if (tree[i].Empty() || tree[i].capstone)
					continue;
				for (std::size_t child = i + 1; child <= i + 2; ++child) {
					if (tree[child].Empty())
						continue;
					const auto  p = locations[i], q = locations[child];
					const bool  lit = ranks[child] > 0;
					const float focus = std::max(nodeFocus[child], nodeFocus[i]);
					const float arrive = std::min(nodeAppear[i], nodeAppear[child]);
					const U32   col = Mix(lit ? edge : structure, bright, focus);
					const float direction = q.x < p.x ? -1.0f : 1.0f;
					V           last = pos(p.x + direction * (i == 9 ? 55.0f : 32.0f) * nodeScale, p.y + (i == 9 ? 0.0f : 25.0f) * nodeScale);
					const V     start = last, end = i == 9 ? pos(q.x - direction * 39.0f * nodeScale, q.y) : pos(q.x, q.y - 39 * nodeScale);
					auto along = [&](float t) -> V {
						const float u = 1 - t;
						return { u * u * u * start.x + 3 * u * u * t * start.x + 3 * u * t * t * end.x + t * t * t * end.x,
							u * u * u * start.y + 3 * u * u * t * (start.y + 50 * s * nodeScale) + 3 * u * t * t * (end.y - 45 * s * nodeScale) + t * t * t * end.y };
					};
					for (int k = 1; k <= 32; ++k) {
						const V next = along(static_cast<float>(k) / 32.0f);
						Draw::AddLine(d, last, next, ink(col, arrive * (lit ? .065f : .02f) * (1 + focus)), 7 * s);
						Draw::AddLine(d, last, next, ink(col, arrive * (lit ? .6f : .24f) + arrive * focus * .3f), (1.15f + .65f * focus) * s);
						last = next;
					}
					if (lit) {
						const float t = std::fmod(state.clock * .21f + static_cast<float>(child) * .17f, 1.0f);
						const V spark = along(t);
						const float shimmer = std::sin(t * 3.1415927f) * arrive;
						Halo(d, spark, 8 * s, edge, alpha * shimmer * .6f);
						Draw::AddCircleFilled(d, spark, 1.65f * s, ink(ivory, .7f * shimmer), 12);
					}
				}
				const bool open = member.unlimited || Paths::TierOpen(member.record, role, tree[i].tier);
				if (!open)
					midcaps(locations[i].x, locations[i].y + 115 * nodeScale, 13,
						Trf("LEVEL {}", Paths::RequiredLevel(tree[i], 1)), faint, 1.8f);
			}
			// Capstone gets space and an explicit condition, never misleading dependency lines.
			const float apexX = locations[9].x;
			if (tree[9].capstone) {
				caps(apexX + 99 * nodeScale, apexY - 52 * nodeScale, 15, Tr("CAPSTONE"), muted, 2.2f);
				text(apexX + 99 * nodeScale, apexY + 16 * nodeScale, 21 * nodeScale,
					!Paths::TierOpen(member.record,role,tree[9].tier) ? Tr("Requires level 50") : held >= others ? Tr("Every other perk unlocked") : Trf("Perks unlocked: {} / {}", held, others), muted);
				const float trackW = 254 * nodeScale;
				PanelFill(d, pos(apexX + 99 * nodeScale, apexY + 52 * nodeScale), pos(apexX + 99 * nodeScale + trackW, apexY + 55 * nodeScale), ink(structure, .5f), 0,
					0);
				PanelFill(d, pos(apexX + 99 * nodeScale, apexY + 52 * nodeScale),
					pos(apexX + 99 * nodeScale + trackW * static_cast<float>(held) / std::max(others, 1), apexY + 55 * nodeScale), ink(edge, .8f), 0, 0);
			}

			auto medallion = [&](V p, float radius, const Paths::Node& n, int r, bool selected, bool locked, bool buyable, std::size_t index) {
				NodeLook look;
				look.kind = n.kind;
				look.glyph = n.glyph;
				look.rank = r;
				look.most = n.most;
				look.selected = selected;
				look.locked = locked;
				look.buyable = buyable;
				look.capstone = n.capstone;
				look.appear = nodeAppear[index];
				look.focus = nodeFocus[index];
				look.rankedAge = state.clock - page.rankedAt[index];
				look.hover = state.tw.Toward("pathNodeHover", static_cast<std::uint64_t>(role) * 32 + index, nodeHover[index] ? 1.0f : 0.0f, 16.0f);
				DrawNode(d, pos(p.x, p.y), radius * s * nodeScale, look, s * nodeScale, alpha);
                if(!member.unlimited && member.record.peak<Paths::RequiredLevel(n,1))return;
				const auto label = Tr(n.name);
				if (n.kind == Paths::Kind::kAbility || n.capstone) {
					AbilityName(d, pos(p.x, p.y), ink(locked ? muted : ivory, nodeAppear[index]), s * nodeScale, label);
				} else {
					const float size = 19 * nodeScale;
					const float labelW = std::min(166.f * nodeScale, canvasW * .32f - 188.f * nodeScale - 22.f);
					const float measured = TextSize(size * s, label).x / s;
					TextCentered(d, pos(p.x, p.y + (radius + 39) * nodeScale), ink(locked ? muted : ivory, nodeAppear[index]),
						size * s * std::min(1.f, labelW / std::max(measured, 1.f)), label);
				}
			};
			for (std::size_t i = 0; i < 12; ++i) {
				const auto& n = tree[i];
				if (n.Empty())
					continue;
				const bool locked = !member.unlimited && (n.capstone ? !apexOpen : !Paths::TierOpen(member.record, role, n.tier));
				medallion(locations[i],
					n.capstone                      ? 51.0f :
					n.kind == Paths::Kind::kAbility ? 52.0f :
													  34.0f,
					n, ranks[i], page.detailOpen && i == chosen, locked, Paths::CanLearn(member.record, role, i, member.pointsEarned, member.unlimited) == Paths::Block::kNone, i);
			}
			const float traitsY = h - 229;
			line(canvasW * .5f - 185, traitsY + 24, canvasW * .5f - 85, traitsY + 24, edge, .2f);
			line(canvasW * .5f + 85, traitsY + 24, canvasW * .5f + 185, traitsY + 24, edge, .2f);
			midcaps(canvasW * .5f, traitsY + 16, 13, Tr("INNATE GIFTS"), muted, 2.8f);
			for (std::size_t i = 12; i < Paths::kNodes; ++i) {
				if (tree[i].Empty())
					continue;
				const auto p = locations[i];
				const bool selected = page.detailOpen && i == chosen;
				NodeLook   look;
				look.kind = tree[i].kind;
				look.glyph = tree[i].glyph;
				look.rank = ranks[i];
				look.most = tree[i].most;
				look.selected = selected;
				look.appear = nodeAppear[i];
				look.focus = nodeFocus[i];
				look.locked = !member.unlimited && member.record.peak < Paths::RequiredLevel(tree[i], ranks[i] + 1);
				look.buyable = Paths::CanLearn(member.record, role, i, member.pointsEarned, member.unlimited) == Paths::Block::kNone;
				look.rankedAge = state.clock - page.rankedAt[i];
				look.hover = state.tw.Toward("pathNodeHover", static_cast<std::uint64_t>(role) * 32 + i, nodeHover[i] ? 1.0f : 0.0f, 16.0f);
				DrawNode(d, pos(p.x, p.y - 5), 28 * s, look, s, alpha);
				const auto label = Tr(tree[i].name);
				const float fit = std::min(1.f, canvasW * .27f / std::max(TextSize(19 * s, label).x / s, 1.f));
				TextCentered(d, pos(p.x, p.y + 55), ink(ivory), 19 * s * fit, label);
				TextCentered(d, pos(p.x, p.y + 79), ink(muted), 15 * s, Trf("Rank {} / {}", ranks[i], tree[i].most));
			}

			// Preserve the original panel's vertical reading order inside the sliding drawer.
			if (reveal > .001f) {
				const float dx = drawerX + 28, dw = w - split - 56;
				PanelGradient(d, pos(drawerX - 44, 126), pos(drawerX + 474, h - 26),
					ink(palette.inset, 0), ink(palette.inset, .94f * reveal), ink(palette.inset, .94f * reveal), ink(palette.inset, 0));
				line(drawerX, 151, drawerX, h - 50, edge, .3f * reveal);
				Draw::PushClipRect(d, pos(drawerX + 1, 127), pos(w - 6, h - 26), true);
				const bool panelLive = treeLive && page.detailOpen && page.detailT > .92f;
				Input      detailInput = input;
				if (!panelLive || activateToInspect)
					detailInput = {};
				DrawPathDetail(d, member, tuning, pos(dx, 139), pos(dx + dw, h - 38), s * 1.15f, alpha * reveal, detailInput, panelLive && !activateToInspect);
				bool closeOver = false;
				if (panelLive && hit("pathDetailClose", dx + dw - 28, 133, 28, 28, closeOver)) {
					page.detailOpen = false;
					page.resetArmed = false;
					page.previewNode = -1;
					page.previewRank = 0;
				}
				Glyph(d, pos(dx + dw - 14, 147), 15 * s, 0xF00D, ink(closeOver ? ivory : muted));
				Draw::PopClipRect(d);
			}
			const bool inDrawer = reveal > .001f && Inside(Im::GetIO()->MousePos, pos(drawerX, 126), pos(drawerX + 480, h - 26));
			const bool inHeaderControl = Inside(Im::GetIO()->MousePos, bonusBounds.min, bonusBounds.max) || Inside(Im::GetIO()->MousePos, utilityBounds.min, utilityBounds.max) || Inside(Im::GetIO()->MousePos, pickerBounds.min, pickerBounds.max);
			if (treeLive && page.detailOpen && Im::IsMouseClicked(0) && !nodeUnderMouse && !inDrawer && !inHeaderControl) {
				page.detailOpen = false;
				page.resetArmed = false;
				page.previewNode = -1;
				page.previewRank = 0;
			}
			// This badge belongs to the tree pane, never to the moving detail card.
			bool canSpend = false;
			if (points > 0 || member.unlimited) {
				for (std::size_t i = 0; i < Paths::kNodes; ++i)
					canSpend = canSpend || Paths::CanLearn(member.record, role, i, member.pointsEarned, member.unlimited) == Paths::Block::kNone;
				const int room = Rules::PathBonusRoom(role, Paths::Spent(member.record, role), tuning);
				canSpend = canSpend || Paths::CanInvest(member.record, role, member.pointsEarned, room) == Paths::Block::kNone;
			}
			const V pointsCentre = pos(drawerX - 53, h - 65);
			const float pointsGlow = canSpend ? .27f + .08f * std::sin(state.clock * 2.0f) : 0.0f;
			Halo(d, pointsCentre, 52 * s, kRed, alpha * pointsGlow);
			Diamond(d, pointsCentre, 29 * s, ink(IM_COL32(112, 25, 26, 255)), ink(canSpend ? kRed : IM_COL32(142, 59, 56, 255)), 1.6f * s);
			Diamond(d, pointsCentre, 24 * s, 0, ink(kRed, .24f), 1 * s);
			if (member.unlimited)
				Glyph(d, pointsCentre, 25 * s, 0xF534, ink(ivory));
			else
				InkCentered(d, pointsCentre, ink(ivory), points >= 100 ? 21 * s : 27 * s, std::to_string(points));
			state.guide.areas[kGuidePoints] = { pos(drawerX - 94, h - 107), pos(drawerX - 12, h - 23) };
			if (treeLive && Inside(Im::GetIO()->MousePos, pos(drawerX - 88, h - 100), pos(drawerX - 18, h - 30)))
				Tip(member.unlimited ? std::string(Tr("Unlimited perk points")) : canSpend ? Trf("{} perk points. A perk or path bonus can be learned here.", points) : Trf("{} perk points. No eligible purchase on this path yet.", points));

			// Paths on its own has no roles to pick in a plan: the path that counts is chosen here, under
			// the selector, and only out of a fight.
			if (Host::PathsAlone() && !page.pathPickerOpen && pickerReveal < .01f) {
				if (role == member.orders.role) {
					Diamond(d, pos(44, 121), 4 * s, ink(edge), ink(ivory, .6f), 1 * s);
					caps(56, 114, 11, Upper(Tr("The path they follow")), muted, 1.8f);
				} else {
					const bool fight = member.inCombat;
					const bool follow = GhostButton(d, "followPath", pos(30, 104), { 294 * s, 34 * s }, Info(role).glyph, Upper(Tr("Follow this path")), !fight, false,
						s, alpha, 0x4E0);
					if (Im::IsItemHovered() && treeLive) {
						Tip(fight ? std::string(Tr("Paths are changed out of a fight.")) :
						            Trf("{} fights by the {} from now on: its abilities and traits count, and the points on the others wait.", member.name,
										Tr(Paths::Info(role).title)));
					}
					if (follow && treeLive) {
						if (fight) {
							Sound("UIMenuCancel");
						} else {
							SetOrders(member, Retask(member.orders, role));
							Sound("UIMenuOK");
						}
					}
				}
			}

			// Draw the selector last so its list stays above nodes and consumes their input.
			bool pickerOver = false;
			const bool pickerClicked = live && Hit("pathPicker", pickerBounds.min, { 294 * s, 72 * s }, pickerOver);
			if (pickerClicked) {
				page.pathPickerOpen = !pickerTakingInput;
				page.pathPickerCursor = static_cast<int>(role);
				Sound("UIMenuFocus");
			}
			const float pickerGlow = state.tw.Toward("pathPickerGlow", 0, pickerOver || page.pathPickerOpen ? 1.0f : 0.0f, 10.0f);
			PanelGradient(d, pickerBounds.min, pickerBounds.max,
				ink(palette.inset, .35f + .4f * pickerGlow), ink(palette.inset, 0), ink(palette.inset, 0), ink(palette.inset, .35f + .4f * pickerGlow));
			line(87, 94, 319, 94, edge, .2f + .4f * pickerGlow);
			Emblem(d, pos(61, 59), 28 * s, ink(palette.inset, .6f), ink(edge, .45f + .3f * pickerGlow), s);
			Glyph(d, pos(61, 59), 26 * s, Info(role).glyph, ink(ivory));
			TextCentered(d, pos(195, 59), ink(ivory), 23*s, Tr(Info(role).name));
			Glyph(d, pos(300, 60), 13 * s, page.pathPickerOpen ? 0xF077 : 0xF078, ink(muted));
			if (pickerOver)
				Tip(std::string(Tr("Choose a path to inspect. Q / E or the triggers also switch paths.")));
			if (pickerReveal > .001f && live) {
				const Rect listBounds{ pos(30, 103), pos(324, 429) };
				Draw::PushClipRect(d, listBounds.min, pos(324, 103 + 326 * pickerReveal), true);
				Frame(d, listBounds.min, listBounds.max, alpha * pickerReveal);
				for (int i = 0; i < 6; ++i) {
					const auto pick = static_cast<Role>(i);
					const float yy = 110 + 52.0f * i;
					bool over = false;
					Im::PushID(9100 + i);
					const bool clicked = page.pathPickerOpen && pickerReveal > .92f && Hit("pathChoice", pos(37, yy), { 280 * s, 52 * s }, over);
					Im::PopID();
					if (pick == role || over || (state.keyboardNav && i == page.pathPickerCursor))
						Highlight(d, pos(37, yy), pos(317, yy + 52), alpha * (pick == role ? .8f : .5f));
					Glyph(d, pos(62, yy + 26), 21 * s, Info(pick).glyph, ink(pick == role ? ivory : muted));
					text(88, yy + 12, 21, Tr(Info(pick).name), pick == role ? ivory : muted);
					if (pick == member.orders.role)
						Diamond(d, pos(298, yy + 26), 4 * s, ink(edge), ink(ivory, .6f), 1 * s);
					if (clicked) {
						page.path = pick;
						page.cursor = FirstWorthy(member, pick);
						page.tabAt = state.clock;
						page.detailOpen = false;
						page.pathPickerOpen = false;
						page.resetArmed = false;
						Sound("UIMenuOK");
					}
				}
				if (!pickerClicked && Im::IsMouseClicked(0) && !Inside(Im::GetIO()->MousePos, pickerBounds.min, pickerBounds.max) && !Inside(Im::GetIO()->MousePos, listBounds.min, listBounds.max))
					page.pathPickerOpen = false;
				Draw::PopClipRect(d);
			}
			Draw::PopClipRect(d);
			return { lo, hi };
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
			{ kGuideNone, Ntr("PATHS") },
			{ kGuidePoints, Ntr("POINTS") },
			{ kGuideTabs, Ntr("SIX PATHS") },
			{ kGuideTree, Ntr("THE TREE") },
			{ kGuideCard, Ntr("THE CARD") },
			{ kGuideAction, Ntr("LEARNING") },
			{ kGuideSockets, Ntr("INTO A FIGHT") },
			{ kGuideButton, Ntr("THAT'S ALL") },
		} };

		std::string GuideText(int a_step, const View::Member& a_member)
		{
			switch (a_step) {
			case 0:
				return std::string(Tr("Every companion has a path for each of the six roles: abilities to learn, ranks that make them stronger, and traits that are "
				       "always on. A few short steps show how it works."));
			case 1:
				return Trf("{} earns a point for every level past {}, counted from the highest level they have reached, so someone met late "
								   "arrives with points to spend. The same points serve all six paths.",
					a_member.name, a_member.pointsAbove);
			case 2:
				if (Host::PathsAlone()) {
					return Trf("One for each role. Points can go into any of them, but only the path {} follows counts in a fight; Follow this path, "
							   "under the dropdown at the top left, changes it. Q and E, the triggers or the dropdown turn between them.",
						a_member.name);
				}
				return Trf("One for each role. Points can go into any of them, but only the path of the role {} has now counts in a fight. Q "
								   "and E, the triggers or the dropdown at the top left turn between them.",
					a_member.name);
			case 3:
				return std::string(Tr("Abilities cost 1, 2 and 3 points for ranks I, II and III. Branches and innate traits cost one point per rank. "
				       "Innate trait ranks II and III require level 25 and 50. Third abilities require level 35; fourth abilities require level 50. Magnus also requires every other perk unlocked."));
			case 4:
				return std::string(Tr("Whatever the cursor is on is read here, its numbers picked out. The ladder shows each rank: ticked once held, the next with "
				       "its price. Point at a rank to read what it would do."));
			case 5:
				return std::string(a_member.respec ? Tr("Spend points here, or with Enter (A on a pad). Out of a fight a rank comes back for free: the small button "
				                         "beside this one, or a right-click on the node.") :
				                         Tr("Spend points here, or with Enter (A on a pad). Giving points back is switched off in this game, so choose "
				                         "with care."));
			case 6:
				return std::string(Tr("Two abilities go into every fight. Choose one they know, then a slot, or press 1 or 2 (X on a pad). They use them on their "
				       "own when the moment comes, or when you call for them from their card."));
			default:
				return std::string(Tr("Start over, at the foot of the card, gives back every point on a path. This guide is under the ?, or F1 (View on a pad), "
				       "whenever you want it again."));
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
			PanelFill(a_draw, { 0, 0 }, { a_display.x, lit.min.y }, dark, 0.0f, 0);
			PanelFill(a_draw, { 0, lit.max.y }, a_display, dark, 0.0f, 0);
			PanelFill(a_draw, { 0, lit.min.y }, { lit.min.x, lit.max.y }, dark, 0.0f, 0);
			PanelFill(a_draw, { lit.max.x, lit.min.y }, { a_display.x, lit.max.y }, dark, 0.0f, 0);
			const float breathe = 0.5f + 0.5f * std::sin(state.clock * 2.8f);
			if (lit.max.x - lit.min.x > 6.0f * a_s) {
				for (int i = 1; i <= 3; ++i) {
					const float g = static_cast<float>(i) * 3.0f * a_s;
					PanelEdge(a_draw, { lit.min.x - g, lit.min.y - g }, { lit.max.x + g, lit.max.y + g },
						Alpha(kAccent, in * (0.18f + 0.1f * breathe) * static_cast<float>(4 - i) / 3.0f), 0.0f, 0, 3.0f * a_s);
				}
				PanelEdge(a_draw, lit.min, lit.max, Alpha(kEdge, in * (0.7f + 0.3f * breathe)), 0.0f, 0, 1.4f * a_s);
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
			PanelFill(a_draw, cmin, { cmax.x, cmin.y + 2.0f * a_s }, Alpha(kEdge, ca), 0.0f, 0);
			float y = cmin.y + padC;
			const auto counter = welcome ? std::string(Tr("GUIDE")) : Trf("GUIDE  /  {} OF {}", guide.step, last);
			Label(a_draw, { cmin.x + padC, y }, Alpha(kEdge, ca), 10.5f * a_s, counter, 2.2f * a_s);
			y += 20.0f * a_s;
			Label(a_draw, { cmin.x + padC, y }, Alpha(kText, ca), 23.0f * a_s, Tr(step.title), 3.6f * a_s);
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
				const std::array<Shape, 3> shapes{ { { Paths::Kind::kAbility, 0xF6DE, Tr("ABILITY"), Tr("used in a fight") },
					{ Paths::Kind::kBranch, 0xF126, Tr("BRANCH"), Tr("adds to its ability") }, { Paths::Kind::kTrait, 0xF3ED, Tr("TRAIT"), Tr("always on") } } };
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
			const auto  verb = welcome ? std::string_view(Tr("BEGIN")) : (guide.step == last ? std::string_view(Tr("DONE")) : std::string_view(Tr("NEXT")));
			const bool  next = ActionButton(a_draw, "guideNext", nextAt, { nextW, bh }, verb, -1, a_s, ca, 0x7600, guide.step == last ? 0xF00C : 0xF061);
			bool        back = false;
			if (guide.step > 0) {
				back = GhostButton(a_draw, "guideBack", { nextAt.x - 10.0f * a_s - bh, y }, { bh, bh }, 0xF060, "", false, false, a_s, ca, 0x7601);
			}
			const float xs = 30.0f * a_s;
			const bool  close = GhostButton(a_draw, "guideClose", { cmax.x - 12.0f * a_s - xs, cmin.y + 12.0f * a_s }, { xs, xs }, 0xF00D, "", false, false, a_s, ca, 0x7602);
			if (Im::IsItemHovered()) {
				Tip(std::string(Tr("Close the guide. It is under the ? whenever you want it.")));
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
			PanelFill(a_draw, { 0.0f, 0.0f }, a_display, Alpha(kShade, alpha * 0.3f), 0.0f, 0);
			const float pulse = 0.5f + 0.5f * std::sin(state.clock * 3.2f);
			for (const auto& enemy : a_view.enemies) {
				const bool marked = a_selected && a_selected->marked == enemy.id;
				GroundMark(a_draw, enemy.footRing, a_display, marked ? kText : kFoe, alpha, 0.24f, (enemy.boss ? 3.0f : 2.2f) * a_s + (marked ? 1.2f * pulse * a_s : 0.0f));
			}
			for (const auto& member : a_view.party) {
				const bool chosen = a_selected && member.id == a_selected->id;
				GroundMark(a_draw, member.footRing, a_display, kAlly, alpha * (!member.CanTakeRole() ? 0.7f : 1.0f), chosen ? 0.36f : 0.2f,
					(chosen ? 2.6f + 0.9f * pulse : 2.0f) * a_s);
				if (member.down && member.foot.on) {
					Draw::AddCircle(a_draw, ScreenOf(member.foot, a_display), (16.0f + 10.0f * pulse) * a_s, Alpha(kAmber, alpha), 32, 2.0f * a_s);
				}
			}
			GroundMark(a_draw, a_view.playerRing, a_display, kText, alpha, 0.26f, 2.4f * a_s);
			if (a_view.playerHead.on) {
				TextOnWorld(a_draw, ScreenOf(a_view.playerHead, a_display), kText, 14.0f * a_s, Tr("YOU"), alpha);
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
				return std::string(Tr("Back"));
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
			const std::array<std::pair<std::string_view, std::uint32_t>, 2> items{ { { Tr("COMPANION"), 0xF007 }, { Tr("OVERHEAD"), 0xF279 } } };
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
					PanelFill(a_draw, min, max, Alpha(kTileHover, in), 0.0f, 0);
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
						Tip(Tr("The overhead view moves the camera, so it needs Glide to the companion on (Battle plan settings, Camera)."));
					} else if (i == 1) {
						Tip(Tr("The whole fight from above: blue rings for the party, red for enemies, white for you. Indoors the ceiling is cut away "
							"above everyone's heads. Click an enemy to send someone after them."));
					} else {
						Tip(Tr("The camera on the companion you are commanding."));
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
				Text(a_draw, { at.x + w + 8.0f * a_s, at.y + h + 8.0f * a_s + (capH - TextSize(13.5f * a_s, Tr("switches")).y) * 0.5f }, Alpha(kTextDim, in), 13.5f * a_s,
					Tr("switches"));
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
			const std::string_view name = Tr("AUTO BATTLE");
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
				PanelFill(a_draw, at, max, Alpha(kTileHover, in * hover * (1.0f - lit)), 0.0f, 0);
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
			PanelFill(a_draw, tmin, tmax, Alpha(Mix(kTile, kAccent, lit), in * (0.6f + 0.4f * lit)), trackH * 0.5f, 0);
			PanelEdge(a_draw, tmin, tmax, Alpha(Mix(kGlassEdge, kEdge, lit), in), trackH * 0.5f, 0, 1.0f);
			const float knobR = trackH * 0.5f - 3.0f * a_s;
			const V     knob{ tmin.x + trackH * 0.5f + lit * (trackW - trackH), tmin.y + trackH * 0.5f };
			Draw::AddCircleFilled(a_draw, knob, knobR, Alpha(Mix(kTextDim, kText, lit), in), 16);

			// What it means right now, under the switch.

			if (hovered || focused) {
				if (hovered) {
					Hovering(0x720);
				}
				auto tip = Trf("Auto battle: {}\nOn: a companion who sees an enemy starts the fight. Those on Hold back wait, and nobody starts one while you sneak.\n"
									   "Off: they wait until an enemy attacks, or you do.{}",
					shown ? "on" : "off", a_view.drawOnSight ? Tr("\nEither way, weapons come out when an enemy is in view.") : "");
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
				const auto  line = Upper(count > 1 ? std::string(Tr("Everyone sent")) : Trf("{} sent", who));
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
				TextOnWorld(a_draw, { p.x, p.y + 74.0f * a_s }, kRed, 15.0f * a_s, Upper(Trf("{} engages", m->name)), alpha * show);
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
				anyOn = anyOn || (m.CanTakeRole() && m.marked == enemy->id);
			}
			if (a_selected) {
				items.push_back({ a_selected->marked == enemy->id ? Trf("Call {} off", a_selected->name) : Trf("Send {}", a_selected->name), 0 });
			}
			items.push_back({ std::string(Tr("Send everyone")), 1 });
			if (anyOn) {
				items.push_back({ std::string(Tr("Call everyone off")), 2 });
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
			Text(a_draw, { min.x + inset, min.y + 8.0f * a_s }, Alpha(kText, a), 13.5f * a_s, Upper(Trf("{}  /  level {}", enemy->name, enemy->level)));

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
						if (!m.CanTakeRole()) {
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

		// Each key in its own small cap, what it does beside it.
		void DrawHints(Im::ImDrawList* a_draw, V a_display, float a_s, float a_alpha)
		{
			const std::array<Hint, 7> padHints{ { { Tr("LB / RB"), Tr("Companion") }, { Tr("D-pad"), Tr("Move") }, { "A", Tr("Choose") }, { "X", Tr("Ability now") }, { Tr("RS"), Tr("Turn view") },
				{ Tr("LT / RT"), Tr("Zoom") }, { "B", Tr("Close") } } };
			const std::array<Hint, 7> keyHints{ { { "1 / 2", Tr("Abilities") }, { Tr("Tab"), Tr("Companion") }, { Tr("Arrows"), Tr("Move") }, { Tr("Enter"), Tr("Choose") },
				{ Tr("Right-drag"), Tr("Turn view") }, { Tr("Wheel"), Tr("Zoom") }, { Tr("Esc"), Tr("Close") } } };
			// Overhead adds how to move about, before Close.
			std::vector<Hint> hints(padUsed.load() ? padHints.begin() : keyHints.begin(), padUsed.load() ? padHints.end() : keyHints.end());
			// The abilities are Path of the Companions's: without it the plan has none to use.
			if (Host::Orders() && !AbilityLink::Active()) {
				const auto abilities = padUsed.load() ? Tr("Ability now") : Tr("Abilities");
				std::erase_if(hints, [&](const Hint& a_hint) { return a_hint.what == abilities; });
			}
			// Path of the Companions's panel turns the view only while its camera follows the companion.
			if (Host::Abilities()) {
				hints = padUsed.load() ? std::vector<Hint>{ { Tr("LB / RB"), Tr("Companion") }, { Tr("D-pad"), Tr("Move") }, { "A", Tr("Choose") }, { "X", Tr("Ability now") }, { "B", Tr("Close") } } :
				                         std::vector<Hint>{ { "1 / 2", Tr("Abilities") }, { Tr("Tab"), Tr("Companion") }, { Tr("Arrows"), Tr("Move") }, { Tr("Enter"), Tr("Choose") }, { Tr("Esc"), Tr("Close") } };
				if (Camera::Driving()) {
					hints.insert(hints.end() - 1, padUsed.load() ? Hint{ Tr("RS"), Tr("Turn view") } : Hint{ Tr("Right-drag"), Tr("Turn view") });
					hints.insert(hints.end() - 1, padUsed.load() ? Hint{ Tr("LT / RT"), Tr("Zoom") } : Hint{ Tr("Wheel"), Tr("Zoom") });
				}
			}
			if (state.page.member && state.guide.open) {
				hints = padUsed.load() ? std::vector<Hint>{ { "A", Tr("Next") }, { Tr("D-pad"), Tr("Back / Next") }, { "B", Tr("Close guide") } } :
				                         std::vector<Hint>{ { Tr("Enter"), Tr("Next") }, { Tr("Left / Right"), Tr("Back / Next") }, { Tr("Esc"), Tr("Close guide") } };
			} else if (state.page.member) {
				hints = padUsed.load() ? std::vector<Hint>{ { Tr("D-pad"), Tr("Choose") }, { "A", Tr("Learn") }, { "X", Tr("Carry") }, { "Y", Tr("Give back") }, { Tr("LT / RT"), Tr("Path") }, { Tr("LB / RB"), Tr("Companion") }, { Tr("View"), Tr("Guide") }, { "B", Tr("Back") } } :
				                         std::vector<Hint>{ { Tr("Click"), Tr("Choose") }, { Tr("Enter"), Tr("Learn") }, { "1 / 2", Tr("Carry") }, { Tr("Right-click"), Tr("Give back") }, { "Q / E", Tr("Path") }, { Tr("Tab"), Tr("Companion") }, { "F1", Tr("Guide") }, { Tr("Esc"), Tr("Back") } };
			} else 			if (state.spells.member) {
				hints = padUsed.load() ? std::vector<Hint>{ { Tr("Up / Down"), Tr("Spell") }, { Tr("Left / Right"), Tr("School") }, { "A", Tr("Choose") }, { "B", Tr("Back") } } :
				                         std::vector<Hint>{ { Tr("Click"), Tr("Choose") }, { Tr("Wheel"), Tr("Scroll") }, { Tr("Left / Right"), Tr("School") }, { Tr("Esc"), Tr("Back") } };
			} else if (state.overhead) {
				hints.insert(hints.end() - 1, padUsed.load() ? Hint{ Tr("LS"), Tr("Move") } : Hint{ Tr("Mid-drag / WASD"), Tr("Move") });
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
				return a_member && a_partyView && a_member->CanTakeRole() ? (ChoosesSpells(a_member->orders.role)?3:1) : 0;
			case kRoleSection:
				return a_partyView ? 1 : 0;  // the role tile, out of a fight; it opens the list
			case kTacticsSection:
                return a_member ? 1 : 0;
            case kFocusSection:
			case kPositionSection:
				return a_member && state.tacticsOpen ? 1 : 0;  // one value, stepped with left and right
			case kAbilitySection:
				// Beside Fight With Me, Path of the Companions's dock: one stop, which it walks itself.
				if (Host::Orders()) {
					return a_member && planFrame.member == a_member->id && planFrame.max[0] > planFrame.min[0] ? 1 : 0;
				}
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
            auto* draw = Im::GetForegroundDrawList();
            const bool ability = !state.tipHeading.empty();
            const float pad = 18.f * a_s;
            const float size = (ability ? 14.f : 15.f) * a_s;
            const float width = std::max(1.f, std::min((ability ? 320.f : 300.f) * a_s, a_display.x - 2 * pad - 24.f));
            auto measure = [&](float fontSize, std::string_view words) {
                return Im::ImFontManger::CalcTextSizeA(Im::GetFont(), fontSize * kTextScale,
                    std::numeric_limits<float>::max(), width, words.data(), words.data() + words.size(), nullptr);
            };
            const float titleH = ability ? measure(17.f * a_s, state.tipHeading).y : 0.f;
            const float rankH = state.tipRank.empty() ? 0.f : measure(11.5f * a_s, state.tipRank).y + 5.f * a_s;
            const float headingH = ability ? titleH + rankH + 18.f * a_s : 0.f;
            // Separate paragraphs retain comfortable spacing without blank-line padding tricks.
            std::vector<std::string_view> lines;
            float bodyH = 0.f, bodyW = 0.f;
            for (std::size_t start = 0; start < state.tip.size();) {
                const auto end = std::min(state.tip.find('\n', start), state.tip.size());
                const auto words = std::string_view(state.tip).substr(start, end - start);
                if (!words.empty()) {
                    if (!lines.empty()) bodyH += 6.f * a_s;
                    const auto extent = measure(size, words);
                    bodyH += extent.y;
                    bodyW = std::max(bodyW, extent.x);
                    lines.push_back(words);
                }
                start = end + 1;
            }
            const V box{ (ability ? width : std::min(bodyW, width)) + 2 * pad, headingH + bodyH + 2 * pad };
            const V mouse = Im::GetIO()->MousePos;
            V at{ mouse.x + 20.f * a_s, mouse.y + 18.f * a_s };
            if (state.tipAnchored) {
                at = { state.tipMin.x - box.x - 14.f * a_s, state.tipMin.y };
                if (at.x < 12.f) at.x = state.tipMax.x + 14.f * a_s;
            }
            at.x = std::max(12.f, std::min(at.x, a_display.x - box.x - 12.f));
            at.y += (1.f - state.tipFade) * 6.f * a_s;
            at.y = std::max(12.f, std::min(at.y, a_display.y - box.y - 12.f));
            Glass(draw, at, Add(at, box), state.tipFade, 0.f);
            float y = at.y + pad;
            if (ability) {
                Paragraph(draw, {at.x + pad, y}, Alpha(kText, state.tipFade), 17.f * a_s, state.tipHeading, width);
                y += titleH;
                if (!state.tipRank.empty()) {
                    y += 5.f * a_s;
                    Paragraph(draw, {at.x + pad, y}, Alpha(kEdge, state.tipFade), 11.5f * a_s, state.tipRank, width);
                    y += rankH - 5.f * a_s;
                }
                Draw::AddLine(draw, {at.x + pad, y + 8.f * a_s}, {at.x + box.x - pad, y + 8.f * a_s}, Alpha(kGlassEdge, state.tipFade), a_s);
                y += 18.f * a_s;
            }
            for (const auto words : lines) {
                Paragraph(draw, {at.x + pad, y}, Alpha(ability ? kTextDim : kText, state.tipFade), size, words, width);
                y += measure(size, words).y + 6.f * a_s;
            }
		}

		void __stdcall RenderPlan()
		{
			SelectTheme();
			MenuFont menuFont;
			if (!plan || !plan->IsOpen) {
				return;
			}
			auto*       io = Im::GetIO();
			const float dt = std::min(io->DeltaTime, 0.05f);
			state.tw.dt = dt;
			state.clock += dt;
            TickTreeTransition();

			if (justOpened.exchange(false)) {
				state.open = 0.0f;
				state.closing = false;
				state.section = kRoleSection;
				state.item = 0;
				state.roles = {};
				planFrame = {};
				planOpened = true;
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
			const float s = std::min({std::clamp(display.y / 1080.0f, 0.65f, 2.5f) * Settings::Get().uiScale,display.x/1760.f,display.y/1080.f});
			// Fit the fullscreen tree and rank controls even when the general menu scale is larger.
			const float pathScale = std::min({ s, display.x / 1600.0f, display.y / 1080.0f });
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
					TextCentered(draw, { display.x * 0.5f, display.y * 0.5f }, Alpha(kTextDim, appear), 18.0f * s, Tr("Gathering the party..."));
				} else {
					// Input from the pad, the keyboard and the mouse. Path of the Companions's tree, open
					// over the plan, reads them itself.
					const bool pathsTree = planFrame.tree;
					Input input;
					int   dy = padDy.exchange(0), shoulder = padShoulder.exchange(0);
					input.dx = padDx.exchange(0);
					input.activate = padA.exchange(false);
					const bool cancel = padB.exchange(false) && !state.page.transition && !pathsTree;
					input.now = padX.exchange(false);
					input.flipAuto = padY.exchange(false);
					input.dx += Im::IsKeyPressed(Im::ImGuiKey_RightArrow) - Im::IsKeyPressed(Im::ImGuiKey_LeftArrow);
					dy += Im::IsKeyPressed(Im::ImGuiKey_DownArrow) - Im::IsKeyPressed(Im::ImGuiKey_UpArrow);
					if (Im::IsKeyPressed(Im::ImGuiKey_Tab, false)) {
						shoulder += io->KeyShift ? -1 : 1;
					}
					input.activate = input.activate || Im::IsKeyPressed(Im::ImGuiKey_Enter, false);
					if (state.closing || state.page.transition || pathsTree) {
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
                    if (state.tacticsOpen && !state.page.member && !state.spells.member && !state.roles.member && cancel) {
                        state.tacticsOpen = false;
                        state.section = kTacticsSection;
                        closePlan = false;
                        Sound("UIMenuCancel");
                    }
					if (state.spells.member && !state.closing) {
						listInput = input;
						listDy = dy;
						listBack = listBack || cancel;
						input = {};
						dy = 0;
						closePlan = false;
					}
					// The role list, likewise, when the pad opened it; B and Esc close it however it opened.
		Input roleInput;
		int   roleDy = 0;
		bool  roleBack = roleListBack.exchange(false);
		if (state.roles.member && !state.closing) {
			if (state.roles.viaPad) {
				roleInput = input;
				roleDy = dy;
				input = {};
				dy = 0;
			}
			roleBack = roleBack || cancel;
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
					const bool pageBack = (pathPageBack.exchange(false) || (state.page.member && cancel)) && !state.page.transition;
					if (pageBack && state.guide.open) {
						guideClose = true;
						closePlan = false;
					} else if (pageBack) {
						if (state.page.member) {
							if (state.page.pathPickerOpen) {
								state.page.pathPickerOpen = false;
							} else if (state.page.detailOpen) {
								state.page.detailOpen = false;
								state.page.resetArmed = false;
								state.page.previewNode = -1;
								state.page.previewRank = 0;
							} else {
								ClosePathPage(true);
							}
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
					if(state.page.transition){pageInput={};pageDy=pageTab=shoulder=0;}
					if (state.guide.open) {
						guideMove = std::clamp(pageInput.dx + (pageInput.activate ? 1 : 0), -1, 1);
						pageInput = {};
						pageDy = 0;
						pageTab = 0;
						shoulder = 0;
					}
					hitsBlocked = state.guide.open || state.page.transition;
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
						float zoomSteps = state.page.member || pathsTree ? 0.0f : (triggerR.load() - triggerL.load()) * 5.0f * dt;
						if (!state.enemyMenu && !state.spells.member && !state.page.member && !pathsTree) {
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
					bool switchView = overheadToggle.exchange(false) && !state.closing && !pathsTree;

					// Path of the Companions's tree turned to another companion: the plan follows.
					if (planFrame.select) {
						state.selected = std::exchange(planFrame.select, 0u);
					}
					const auto* member = Selected(*view);
					// No enemies about: the party view, the camera face to face with the companion.
					const bool partyView = view->enemies.empty() && !view->inCombat;
					Camera::Focus(member && !state.closing ? member->id : 0, partyView);
					// A fight locks the role tiles; the pad starts on, or moves down to, the focus.
					if (!partyView && state.section == kRoleSection) {
						state.section = kTacticsSection;
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

					// Abilities: down from a badge to its AUTO, on to the path tile and the healer's
					// line; up the same way, back to the badge last chosen. Path of the Companions's dock
					// walks itself: it takes the moves and says when the cursor runs off its top or bottom.
					int  dockDx = 0, dockDy = 0, dockEnter = 0;
					bool dockActivate = false;
					if (Host::Orders() && state.section == kAbilitySection && member) {
						dockDy = dy;
						dy = std::exchange(planFrame.exit, 0);
					} else if (dy && Host::Abilities() && state.section == kAbilitySection && member) {
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
						state.item = 0;
						// The abilities from below: their bottom row, the healer's line or the path tile.
						if (next == kAbilitySection && Host::Orders()) {
							dockEnter = dy > 0 ? 1 : -1;
						} else if (next == kAbilitySection && dy < 0 && member) {
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
					if (Host::Orders() && state.section == kAbilitySection && member) {
						dockDx = input.dx;
						moved = moved || input.dx != 0;
						dockActivate = std::exchange(input.activate, false);
					} else if (state.section == kAbilitySection && member && input.dx != 0) {
						const auto rows = AbilityRowsOf(*member);
						if ((rows.Badge(state.item) || rows.Auto(state.item)) && rows.columns > 0) {
							state.item = ((state.item / 2 + input.dx + rows.columns) % rows.columns) * 2 + state.item % 2;
							moved = true;
						} else if (state.item == rows.path) {
							moved = true;
						}
					}
					state.item = count > 0 ? std::clamp(state.item, 0, count - 1) : 0;
					// Out of a fight the party orders are hidden: the row is the auto battle switch alone.
					if (partyView && state.section == kOrdersSection) {
						state.item = static_cast<int>(orderShouts.size());
					}
					if (Host::Abilities() && state.section == kAbilitySection && member && state.item < AbilityRowsOf(*member).columns * 2) {
						state.abilityColumn = state.item / 2;
					}
					// X and Y are the first ability's, wherever the cursor is.
					const bool dockNow = Host::Orders() && std::exchange(input.now, false);
					const bool dockFlip = Host::Orders() && std::exchange(input.flipAuto, false);
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

					const float ruleY = 68.0f * s;
					if (!state.page.member && !pathsTree) {
						// Title in the game's own capitals, with a rule that draws outward from the centre.
						const float titleIn = Stage(appear, 0.0f, 0.55f);
						const float titleY = 44.0f * s - (1.0f - titleIn) * 10.0f * s;
						{
							const float            size = 24.0f * s;
							const float            tracking = 6.0f * s;
							const std::string_view title = partyView ? Tr("YOUR PARTY") : Tr("BATTLE PLAN");
							const float            w = LabelWidth(size, title, tracking);
							const float            cx = display.x * 0.5f;
							const float            bandW = w * 0.5f + 70.0f * s;
							const float            top = titleY - 22.0f * s, bottom = titleY + 18.0f * s;
							// The band is darkest under the title and fades out to both sides.
							PanelGradient(draw, { cx - bandW, top }, { cx, bottom }, Alpha(kAccent, 0.0f),
								Alpha(kAccent, titleIn * (palette.Subdued() ? 0.18f : 0.85f)), Alpha(kAccent, titleIn * (palette.Subdued() ? 0.18f : 0.85f)), Alpha(kAccent, 0.0f));
							PanelGradient(draw, { cx, top }, { cx + bandW, bottom }, Alpha(kAccent, titleIn * (palette.Subdued() ? 0.18f : 0.85f)),
								Alpha(kAccent, 0.0f), Alpha(kAccent, 0.0f), Alpha(kAccent, titleIn * (palette.Subdued() ? 0.18f : 0.85f)));
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

					}
					std::vector<Rect> panels;
					// A companion's path: the page follows LB and RB to whoever is chosen, and closes
					// if the add-on is not there for them.
					if (state.page.member && (!member || !member->paths)) {
						ClosePathPage();
					}
					if (state.page.member && member && state.page.member != member->id) {
						state.page.member = member->id;
                        state.page.followRole=true;
						state.page.seenMember = 0;
						state.page.detailOpen = false;
						state.page.detailT = 0.0f;
						state.page.resetArmed = false;
					}
					const bool  ownPage = state.page.member != 0 && member;
					const bool  pageOpen = ownPage || pathsTree;
					const float pageIn = state.tw.Toward("pathPage", 0, pageOpen ? 1.0f : 0.0f, 10.0f);
					if (!pageOpen) {
						// Party orders are for a fight: out of one they would only be in the way.
						const float ordersIn = state.tw.Toward("ordersShown", 0, partyView ? 0.0f : 1.0f, 8.0f);
						if (ordersIn > 0.01f) {
							panels.push_back(DrawOrders(draw, *view, { display.x * 0.5f, ruleY + 46.0f * s }, s, appear * (1.0f - pageIn) * ordersIn, partyView ? Input{} : input));
						}
						panels.push_back(DrawStrip(draw, *view, member, display, s, appear));
					}
					if (pageOpen) {
						CloseSpellList();
						state.roles = {};
					}
					if (ownPage) {
						uiScale = pathScale;
						panels.push_back(DrawPathPage(draw, *member, view->tuning, { 0.0f, 0.0f },
                            display, pathScale, appear, pageInput, pageDy, pageTab));
						if (state.guide.open && TreeEntrance::At(state.clock-state.page.openedAt).interactive) {
							DrawGuide(draw, *member, display, pathScale, appear, guideMove, guideClose);
						}
					}
					// Path of the Companions's abilities, at the foot of the right-hand column where the
					// plan's own dock was, or its tree over the whole plan: it draws them, into this window.
					if (AbilityLink::Active()) {
						auto&       f = planFrame;
						const float dockW = 440.0f * s;
						f.member = member ? member->id : 0;
						f.x = display.x - 40.0f * s - dockW;
						f.y = display.y - 330.0f * s;
						f.width = dockW;
						f.scale = s;
						f.treeScale = pathScale;
						f.appear = appear;
						f.opened = std::exchange(planOpened, false);
						f.closing = state.closing;
						f.focused = state.section == kAbilitySection && member;
						f.keyboardNav = state.keyboardNav;
						f.enter = dockEnter;
						f.dx = dockDx;
						f.dy = dockDy;
						f.activate = dockActivate;
						f.now = dockNow;
						f.flipAuto = dockFlip;
						AbilityLink::DrawInPlan(f);
						uiScale = s;
						if (f.max[0] > f.min[0]) {
							panels.push_back({ { f.min[0], f.min[1] }, { f.max[0], f.max[1] } });
						}
						if (f.tree) {
							panels.push_back({ { 0.0f, 0.0f }, display });
						}
					}
					if (member && !pageOpen) {
						const float width = 440.0f * s;
						const Rect card = DrawCompanion(draw, *view, *member, { display.x - 40.0f * s - width, 112.0f * s }, width, s, appear, cardInput, false, &panels);
						panels.push_back(card);
						// Roles are chosen out of a fight, for whoever the card is showing.
						if (state.roles.member && (state.roles.member != member->id || !partyView)) {
							state.roles = {};
						}
						if (state.roles.member) {
							std::optional<Role> picked;
							panels.push_back(DrawRoleList(draw, *member, card.min.x, display.y - 120.0f * s, s, appear, roleInput, roleDy, roleBack, picked));
							if (picked) {
								SetOrders(*member, Retask(member->orders, *picked));
							}
						}
						const float sheetIn = state.tw.Toward("partyView", 0, partyView ? 1.0f : 0.0f, 8.0f);
						if (sheetIn > 0.01f) {
							const Rect sheet = DrawSheet(draw, *member, { 40.0f * s, 112.0f * s }, 380.0f * s, s, appear * sheetIn, cardInput);
							panels.push_back(sheet);
							// Spells are chosen out of a fight only, for whoever the sheet is showing.
							if (state.spells.member && (state.spells.member != member->id || !partyView || !member->CanTakeRole() || (state.spells.hand<2 && !ChoosesSpells(member->orders.role)))) {
								CloseSpellList();
							}
							if (state.spells.member && sheetIn > 0.5f) {
								const float top = 112.0f * s;  // level with the sheet: no party orders out of a fight
								panels.push_back(DrawSpellList(draw, *member, { sheet.max.x + 4.0f * s, top }, 420.0f * s, display.y - top - 190.0f * s, s, appear * sheetIn,
									listInput, listDy, listBack, sheet));
							}
						} else {
							CloseSpellList();
						}
					}
					if (!pageOpen) {
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
								Notices::Push(Tr("The overhead view needs Glide to the companion (Battle plan settings)."), Notices::Kind::kWarning);
							}
						}
					}
					if (!pathsTree) {
						DrawHints(draw, display, pageOpen ? pathScale : s, Stage(appear, 0.35f, 0.5f));
					}
					uiScale = s;
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
			uiScale = state.page.member ? pathScale : s;
			DrawTip(display, uiScale);
			uiScale = s;
			spellListOpen = state.spells.member != 0;
			roleListOpen = state.roles.member != 0;
			pathPageOpen = state.page.member != 0 || planFrame.tree;  // Esc goes back in the tree, Path of the Companions's or ours
            DrawTreeTransition(display);

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

		// --- Paths on its own -----------------------------------------------------------------------
		// Without Fight With Me there is no battle plan. Paths opens a panel at the bottom right
		// instead, the plan's right-hand column cut down to what a path needs: the companion's name,
		// level and points on their band, then the two abilities they carry and the skill tree,
		// drawn by the same code as the plan's ability dock. The tree opens over everything, as in
		// the plan.

		bool          aimPending{ false };  // the panel has just opened: whoever is under the crosshair comes first
		std::uint64_t aimAfter{ 0 };        // a view newer than this one says who that is

		// The band at the top: their name, their level, and the points they have to spend.
		Rect DrawPathsHeader(Im::ImDrawList* a_draw, const View::Snapshot& a_view, const View::Member* a_member, V a_origin, float a_width, float a_s, float a_appear)
		{
			const float pad = 24.0f * a_s;
			const float enter = Stage(a_appear, 0.05f, 0.5f);
			const float swap = Smooth(state.swap);
			const float alpha = enter * (0.25f + 0.75f * swap);
			const V     origin{ a_origin.x + (1.0f - enter) * 48.0f * a_s + (1.0f - swap) * 14.0f * a_s, a_origin.y };
			return Panel(a_draw, origin, a_width, pad, enter, [&](V p, float inner) {
				const float bandTop = p.y - pad + 1.0f;
				const float bandH = pad + 42.0f * a_s;
				Band(a_draw, { p.x - pad + 1.0f, bandTop }, { p.x + inner + pad - 1.0f, bandTop + bandH }, alpha,
					a_member ? RarityBand(a_member->rarity) : 0);
				float yy = bandTop + bandH + 14.0f * a_s;
				if (!a_member) {
					const auto title = Upper(Tr("Paths"));
					Text(a_draw, { p.x, bandTop + (bandH - TextSize(26.0f * a_s, title).y) * 0.5f }, Alpha(kText, alpha), 26.0f * a_s, title);
					yy += Paragraph(a_draw, { p.x, yy }, Alpha(kTextDim, alpha), 15.0f * a_s,
						Tr("No companions in reach. Followers who travel with you appear here once they are loaded."), inner);
					return yy - p.y;
				}
				const auto& member = *a_member;
				{
					const V     mc{ p.x + inner - 32.0f * a_s, bandTop + bandH * 0.5f };
					const float mr = 32.0f * a_s;
					LevelMark(a_draw, member, mc, mr, alpha, a_s, 28.0f * a_s);
					bool hovered = false;
					Hit("pathsLevel", { mc.x - mr, mc.y - mr }, { mr * 2.0f, mr * 2.0f }, hovered);
					if (hovered) {
						Hovering(0x4C0);
						Readout(Trf("Level {}. A perk point for every level past {}, from the highest they have reached.", member.level, member.pointsAbove));
					}
					Slashes(a_draw, { mc.x - mr - 40.0f * a_s, bandTop + bandH * 0.3f }, bandH * 0.4f, Alpha(kText, alpha * 0.45f));
				}
				const std::string upper = Upper(member.name);
				Text(a_draw, { p.x, bandTop + (bandH - TextSize(26.0f * a_s, upper).y) * 0.5f }, Alpha(kText, alpha), 26.0f * a_s, upper);

				// The path that counts in a fight, and the points to spend on any of them.
				const auto role = member.orders.role;
				Glyph(a_draw, { p.x + 8.0f * a_s, yy + 10.0f * a_s }, 15.0f * a_s, Info(role).glyph, Alpha(RoleAccent(role), alpha));
				Text(a_draw, { p.x + 26.0f * a_s, yy }, Alpha(RoleAccent(role), alpha), 15.5f * a_s, Tr(Paths::Info(role).title));
				{
					const int   unspent = Unspent(member);
					const V     pc{ p.x + inner - 15.0f * a_s, yy + 10.0f * a_s };
					const float glow = unspent > 0 || member.unlimited ? 0.3f + 0.08f * std::sin(state.clock * 2.0f) : 0.0f;
					Halo(a_draw, pc, 26.0f * a_s, kRed, alpha * glow);
					Diamond(a_draw, pc, 15.0f * a_s, Alpha(unspent > 0 || member.unlimited ? kRed : palette.inset, alpha), Alpha(kEdge, alpha * 0.5f), 1.0f);
					if (member.unlimited) {
						Glyph(a_draw, pc, 13.0f * a_s, 0xF534, Alpha(kText, alpha));
					} else {
						TextCentered(a_draw, pc, Alpha(kText, alpha), 13.0f * a_s, std::to_string(unspent));
					}
					const auto  word = Upper(Tr("Perk points"));
					const float size = 11.5f * a_s, track = 1.6f * a_s;
					Label(a_draw, { pc.x - 26.0f * a_s - LabelWidth(size, word, track), pc.y - size * 0.58f }, Alpha(kTextDim, alpha), size, word, track);
					bool hovered = false;
					Hit("pathsPoints", { pc.x - 140.0f * a_s, yy - 4.0f * a_s }, { 156.0f * a_s, 28.0f * a_s }, hovered);
					if (hovered) {
						Hovering(0x4C1);
						Readout(member.unlimited ? std::string(Tr("Unlimited perk points")) :
						                           Trf("{} of {} points spent. Points go into any path; only the one they follow counts in a fight.",
														member.pointsSpent, member.pointsEarned));
					}
				}
				yy += 28.0f * a_s;

				// Who else is about: a diamond each, the one shown lit. Tab, the shoulders or a click
				// on one moves along.
				std::vector<const View::Member*> others;
				for (const auto& m : a_view.party) {
					if (m.CanTakeRole()) {
						others.push_back(&m);
					}
				}
				if (others.size() > 1) {
					const float dot = 16.0f * a_s;
					const float x0 = p.x + inner * 0.5f - dot * static_cast<float>(others.size() - 1) * 0.5f;
					for (std::size_t i = 0; i < others.size(); ++i) {
						const V    c{ x0 + dot * static_cast<float>(i), yy + 4.0f * a_s };
						const bool on = others[i]->id == member.id;
						bool       hovered = false;
						Im::PushID(4900 + static_cast<int>(i));
						if (Hit("pathsCompanion", { c.x - dot * 0.5f, c.y - 8.0f * a_s }, { dot, 16.0f * a_s }, hovered) && !on && !state.closing) {
							state.selected = others[i]->id;
							Sound("UIMenuFocus");
						}
						Im::PopID();
						if (hovered) {
							Hovering(0x4D0 + static_cast<std::uint64_t>(i));
							Tip(others[i]->name);
						}
						Diamond(a_draw, c, (on ? 4.6f : 2.6f + (hovered ? 1.0f : 0.0f)) * a_s, Alpha(on ? kAccent : kTextFaint, alpha * (on ? 1.0f : 0.8f)),
							Alpha(on ? kEdge : kTextFaint, alpha * (on ? 1.0f : 0.0f)), 1.0f);
					}
					yy += 16.0f * a_s;
				}
				return yy - p.y;
			});
		}

		// The panel: the band above, the ability dock beneath it, both on the plan's right-hand edge.
		void DrawPathsPanel(Im::ImDrawList* a_draw, const View::Snapshot& a_view, const View::Member* a_member, V a_display, float a_s, float a_appear,
			const Input& a_input, std::vector<Rect>& a_panels)
		{
			const float width = 440.0f * a_s;
			const float left = a_display.x - 40.0f * a_s - width;
			const float dockTop = a_display.y - 330.0f * a_s;
			// Laid out once, off screen, to know how tall the band is; then drawn where it ends just
			// above the dock.
			const float headerH = state.tw.Raw("pathsHeaderHeight", a_member ? 1 : 0, 0.0f);
			const float top = a_member ? dockTop - 14.0f * a_s - headerH : a_display.y - 110.0f * a_s - headerH;
			const Rect  header = DrawPathsHeader(a_draw, a_view, a_member, { left, top }, width, a_s, a_appear);
			state.tw.Raw("pathsHeaderHeight", a_member ? 1 : 0, 0.0f) = header.max.y - header.min.y;
			a_panels.push_back(header);
			if (a_member) {
				a_panels.push_back(DrawCompanion(a_draw, a_view, *a_member, { left, dockTop }, width, a_s, a_appear, a_input, true));
			}
		}

		// The pad and the keys as the panel takes them, and the tree over it: while open, the tree
		// takes the pad and the arrows, B and Esc go back a step, and the triggers (or Q and E) turn to
		// the next path. The guide over the tree takes them first.
		struct PanelInput
		{
			Input input, page;
			int   dy{ 0 }, shoulder{ 0 }, pageDy{ 0 }, pageTab{ 0 }, guideMove{ 0 };
			bool  guideClose{ false };
			bool  close{ false };  // B or Esc with nothing left to go back from
		};

		PanelInput ReadPanelInput()
		{
			auto*      io = Im::GetIO();
			PanelInput in;
			auto&      input = in.input;
			int&       dy = in.dy;
			int&       shoulder = in.shoulder;
			dy = padDy.exchange(0);
			shoulder = padShoulder.exchange(0);
			input.dx = padDx.exchange(0);
			input.activate = padA.exchange(false);
			const bool cancel = padB.exchange(false) && !state.page.transition;
			input.now = padX.exchange(false);
			input.flipAuto = padY.exchange(false);
			input.dx += Im::IsKeyPressed(Im::ImGuiKey_RightArrow) - Im::IsKeyPressed(Im::ImGuiKey_LeftArrow);
			dy += Im::IsKeyPressed(Im::ImGuiKey_DownArrow) - Im::IsKeyPressed(Im::ImGuiKey_UpArrow);
			if (Im::IsKeyPressed(Im::ImGuiKey_Tab, false)) {
				shoulder += io->KeyShift ? -1 : 1;
			}
			input.activate = input.activate || Im::IsKeyPressed(Im::ImGuiKey_Enter, false);
			if (state.closing || state.page.transition) {
				input = {};
				dy = shoulder = 0;
			}
			if (input.dx || dy || input.activate || shoulder || input.now || input.flipAuto) {
				state.keyboardNav = true;
			}
			if (std::abs(io->MouseDelta.x) + std::abs(io->MouseDelta.y) > 2.0f) {
				state.keyboardNav = false;
			}
			in.close = cancel;

			const bool guideKey = padView.exchange(false) || Im::IsKeyPressed(Im::ImGuiKey_F1, false);
			if (!state.page.member || state.closing) {
				state.guide.open = false;
			}
			const bool pageBack = (pathPageBack.exchange(false) || (state.page.member && cancel)) && !state.page.transition;
			if (pageBack && state.guide.open) {
				in.guideClose = true;
				in.close = false;
			} else if (pageBack) {
				if (state.page.member) {
					if (state.page.pathPickerOpen) {
						state.page.pathPickerOpen = false;
					} else if (state.page.detailOpen) {
						state.page.detailOpen = false;
						state.page.resetArmed = false;
						state.page.previewNode = -1;
						state.page.previewRank = 0;
					} else {
						ClosePathPage(true);
					}
					Sound("UIMenuCancel");
				}
				in.close = false;
			}
			if (guideKey && state.page.member && !state.closing) {
				if (state.guide.open) {
					in.guideClose = true;
				} else {
					OpenGuide();
					Sound("UIMenuOK");
				}
			}
			if (state.page.member && !state.closing) {
				in.page = input;
				in.pageDy = dy;
				input = {};
				dy = 0;
				in.close = false;
				const float pull = triggerR.load() - triggerL.load();
				if (!state.page.triggerDown && std::fabs(pull) > 0.6f) {
					state.page.triggerDown = true;
					in.pageTab = pull > 0.0f ? 1 : -1;
				} else if (std::fabs(pull) < 0.3f) {
					state.page.triggerDown = false;
				}
				in.pageTab += static_cast<int>(Im::IsKeyPressed(Im::ImGuiKey_E, false)) - static_cast<int>(Im::IsKeyPressed(Im::ImGuiKey_Q, false));
			}
			if (state.page.transition) {
				in.page = {};
				in.pageDy = in.pageTab = shoulder = 0;
			}
			if (state.guide.open) {
				in.guideMove = std::clamp(in.page.dx + (in.page.activate ? 1 : 0), -1, 1);
				in.page = {};
				in.pageDy = 0;
				in.pageTab = 0;
				shoulder = 0;
			}
			return in;
		}

		// Whatever the pad sent since the last frame, dropped: it was not ours to read.
		void DropPad()
		{
			padDx = padDy = padShoulder = 0;
			padA = padB = padX = padY = false;
			padView = false;
			pathPageBack = false;
		}

		void __stdcall RenderPaths()
		{
			SelectTheme();
			MenuFont menuFont;
			if (!plan || !plan->IsOpen) {
				return;
			}
			auto*       io = Im::GetIO();
			const float dt = std::min(io->DeltaTime, 0.05f);
			state.tw.dt = dt;
			state.clock += dt;
            TickTreeTransition();

			if (justOpened.exchange(false)) {
				state.open = 0.0f;
				state.closing = false;
				state.section = kAbilitySection;
				state.item = 0;
				ClosePathPage();
				padDx = padDy = padShoulder = 0;
				padA = padB = padX = padY = false;
				padView = false;
				stickX = stickY = triggerL = triggerR = leftX = leftY = 0.0f;
				overheadToggle = false;
				const auto before = Battle::ReadView();
				aimAfter = before ? before->serial : 0;
				aimPending = true;
			}
			if (closeRequested.exchange(false) && !state.closing) {
				state.closing = true;
				Battle::RequestClose();
			}
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
			const float s = std::min({ std::clamp(display.y / 1080.0f, 0.65f, 2.5f) * Settings::Get().uiScale, display.x / 1760.f, display.y / 1080.f });
			const float pathScale = std::min({ s, display.x / 1600.0f, display.y / 1080.0f });
			uiScale = s;
			state.tipShown = false;

			Im::SetNextWindowPos({ 0, 0 }, Im::ImGuiCond_Always, { 0, 0 });
			Im::SetNextWindowSize(display, Im::ImGuiCond_Always);
			Im::PushStyleVar(Im::ImGuiStyleVar_WindowBorderSize, 0.0f);
			Im::PushStyleVar(Im::ImGuiStyleVar_WindowPadding, V{ 0, 0 });
			Im::PushStyleColor(Im::ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
			Im::PushStyleColor(Im::ImGuiCol_Text, kText);
			bool open = true;
			if (Im::Begin("PathsPanel", &open,
					Im::ImGuiWindowFlags_NoTitleBar | Im::ImGuiWindowFlags_NoResize | Im::ImGuiWindowFlags_NoMove | Im::ImGuiWindowFlags_NoScrollbar |
						Im::ImGuiWindowFlags_NoScrollWithMouse | Im::ImGuiWindowFlags_NoSavedSettings | Im::ImGuiWindowFlags_NoNav |
						Im::ImGuiWindowFlags_NoBringToFrontOnFocus)) {
				auto* draw = Im::GetWindowDrawList();
				auto* back = Im::GetBackgroundDrawList();
				const float pageIn = state.tw.Toward("pathPage", 0, state.page.member ? 1.0f : 0.0f, 10.0f);
				// The world stays in view around the panel; the tree darkens it like the plan.
				Vignette(back, display, Stage(appear, 0.0f, 0.5f) * (0.55f + 0.45f * pageIn));

				if (!view || !view->inGame) {
					TextCentered(draw, { display.x * 0.5f, display.y * 0.5f }, Alpha(kTextDim, appear), 18.0f * s, Tr("Gathering the party..."));
				} else {
					if (aimPending && view->serial > aimAfter) {
						aimPending = false;
						if (view->aimed) {
							state.selected = view->aimed;
						}
					}
					auto        in = ReadPanelInput();
					Input&      input = in.input;
					int         dy = in.dy, shoulder = in.shoulder;
					const Input pageInput = in.page;
					const int   pageDy = in.pageDy, pageTab = in.pageTab, guideMove = in.guideMove;
					const bool  guideClose = in.guideClose;
					hitsBlocked = state.guide.open || state.page.transition;
					if (in.close && !state.closing) {
						open = false;
					}
					if (shoulder) {
						StepMember(*view, shoulder);
					}

					// Turning the view as in the battle plan, round the companion only: the right stick or
					// a right-button drag turns it, the wheel and the triggers zoom, but not in the tree,
					// whose triggers turn to the next path.
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
						const float zoomSteps = state.page.member ? 0.0f : (triggerR.load() - triggerL.load()) * 5.0f * dt + io->MouseWheel;
						if (yaw != 0.0f || pitch != 0.0f) {
							Camera::Turn(yaw, pitch);
						}
						if (zoomSteps != 0.0f) {
							Camera::Zoom(zoomSteps);
						}
					}

					const auto* member = Selected(*view);
                    Camera::Focus(member && !state.closing ? member->id : 0, view->enemies.empty() && !view->inCombat);
					if (member && member->id != state.shown) {
						if (state.shown != 0) {
							Sound("UIMenuPrevNext");
						}
						state.swap = state.shown == 0 ? 1.0f : 0.0f;
						state.shown = member->id;
					}
					state.swap = std::min(state.swap + dt / 0.24f, 1.0f);

					// The dock as the pad walks it: round the badges, down to their switches and on to
					// the skill tree and the healer's line, the way the plan's ability rows go.
					Input cardInput = input;
					state.section = kAbilitySection;
					if (member) {
						const auto rows = AbilityRowsOf(*member);
						const int  item = state.item;
						if (dy) {
							const int up = rows.columns > 0 ? std::min(state.abilityColumn, rows.columns - 1) * 2 + 1 : -1;
							int       to = -1;
							if (dy > 0) {
								to = rows.Badge(item) ? item + 1 : (rows.Auto(item) ? (rows.path >= 0 ? rows.path : rows.slider) : (item == rows.path ? rows.slider : -1));
							} else {
								to = rows.Auto(item) ? item - 1 : (item == rows.path ? up : (item == rows.slider ? (rows.path >= 0 ? rows.path : up) : -1));
							}
							if (to >= 0) {
								state.item = to;
							}
						}
						if (input.dx != 0 && (rows.Badge(state.item) || rows.Auto(state.item)) && rows.columns > 0) {
							state.item = ((state.item / 2 + input.dx + rows.columns) % rows.columns) * 2 + state.item % 2;
							cardInput.dx = 0;
						}
						state.item = std::clamp(state.item, 0, std::max(rows.Count() - 1, 0));
						if (state.item < rows.columns * 2) {
							state.abilityColumn = state.item / 2;
						}
					}

					std::vector<Rect> panels;
					if (state.page.member && (!member || !member->paths)) {
						ClosePathPage();
					}
					if (state.page.member && member && state.page.member != member->id) {
						state.page.member = member->id;
                        state.page.followRole=true;
						state.page.seenMember = 0;
						state.page.detailOpen = false;
						state.page.detailT = 0.0f;
						state.page.resetArmed = false;
					}
					const bool pageOpen = state.page.member != 0 && member;
					if (pageOpen) {
						uiScale = pathScale;
						panels.push_back(DrawPathPage(draw, *member, view->tuning, { 0.0f, 0.0f },
                            display, pathScale, appear, pageInput, pageDy, pageTab));
						if (state.guide.open && TreeEntrance::At(state.clock-state.page.openedAt).interactive) {
							DrawGuide(draw, *member, display, pathScale, appear, guideMove, guideClose);
						}
					} else {
						DrawPathsPanel(draw, *view, member, display, s, appear * (1.0f - pageIn), cardInput, panels);
					}
					DrawHints(draw, display, pageOpen ? pathScale : s, Stage(appear, 0.35f, 0.5f));
					uiScale = s;
					if (state.keyboardNav) {
						Hovering(0x1000 + static_cast<std::uint64_t>(state.section) * 64 + static_cast<std::uint64_t>(state.item));
					}
				}
			}
			Im::End();
			Im::PopStyleColor(2);
			Im::PopStyleVar(2);
			uiScale = state.page.member ? pathScale : s;
			DrawTip(display, uiScale);
			uiScale = s;
			pathPageOpen = state.page.member != 0;
            DrawTreeTransition(display);

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

		// Beside Fight With Me there is no panel of ours: the battle plan calls this from inside its
		// window, once a frame, and the abilities sit where its ability dock was. The plan walks its own
		// pad cursor and hands over the moves meant for the dock; the dock says when the cursor runs off
		// its top or bottom. The tree, opened from the dock, covers the plan and reads the pad and the
		// keys here, and the plan's camera rises for it and turns to whoever it ends on.
		void DrawPathsInPlan(FightWithMePathsAPI::Plan& a_plan)
		{
			a_plan.min[0] = a_plan.min[1] = a_plan.max[0] = a_plan.max[1] = 0.0f;
			a_plan.exit = 0;
			a_plan.tree = false;
			a_plan.select = 0;
			hostPlan = &a_plan;

			SelectTheme();
			MenuFont    menuFont;
			auto*       io = Im::GetIO();
			const float dt = std::min(io->DeltaTime, 0.05f);
			state.tw.dt = dt;
			state.clock += dt;
			TickTreeTransition();
			if (a_plan.opened) {
				state.section = kAbilitySection;
				state.item = 0;
				ClosePathPage();
				DropPad();
				triggerL = triggerR = 0.0f;
			}
			state.closing = a_plan.closing;

			Battle::RefreshView();
			const auto  view = Battle::ReadView();
			const V     display = io->DisplaySize;
			const float s = a_plan.scale;
			const float pathScale = a_plan.treeScale;
			uiScale = s;
			state.tipShown = false;

			if (view && view->inGame) {
				auto* draw = Im::GetWindowDrawList();
				// The plan says who is shown, until the tree turns to someone else.
				if (!state.page.member && !state.page.transition) {
					state.selected = a_plan.member;
				}
				const bool tree = state.page.member != 0;
				Input      input, pageInput;
				int        dy = 0, shoulder = 0, pageDy = 0, pageTab = 0, guideMove = 0;
				bool       guideClose = false;
				if (tree) {
					const auto in = ReadPanelInput();
					shoulder = in.shoulder;
					pageInput = in.page;
					pageDy = in.pageDy;
					pageTab = in.pageTab;
					guideMove = in.guideMove;
					guideClose = in.guideClose;
				} else {
					DropPad();
					if (a_plan.focused && !state.closing) {
						input.dx = a_plan.dx;
						dy = a_plan.dy;
						input.activate = a_plan.activate;
					}
					input.now = a_plan.now && !state.closing;
					input.flipAuto = a_plan.flipAuto && !state.closing;
					state.keyboardNav = a_plan.keyboardNav && a_plan.focused;
				}
				hitsBlocked = state.guide.open || state.page.transition;
				if (shoulder) {
					StepMember(*view, shoulder);
				}

				const View::Member* member = Selected(*view);
				if (member && !tree && !state.page.transition && member->id != a_plan.member) {
					member = nullptr;  // the plan's choice has no abilities here
				}
				if (member && member->id != state.shown) {
					state.swap = state.shown == 0 ? 1.0f : 0.0f;
					state.shown = member->id;
				}
				state.swap = std::min(state.swap + dt / 0.24f, 1.0f);

				Input cardInput = input;
				state.section = kAbilitySection;
				if (member && !tree) {
					const auto rows = AbilityRowsOf(*member);
					// Onto the dock from above: its first badge; from below: its bottom row.
					if (a_plan.enter > 0) {
						state.item = 0;
					} else if (a_plan.enter < 0) {
						state.item = rows.slider >= 0 ? rows.slider : (rows.path >= 0 ? rows.path : std::max(rows.columns * 2 - 1, 0));
					}
					const int item = state.item;
					if (dy) {
						const int up = rows.columns > 0 ? std::min(state.abilityColumn, rows.columns - 1) * 2 + 1 : -1;
						int       to = -1;
						if (dy > 0) {
							to = rows.Badge(item) ? item + 1 : (rows.Auto(item) ? (rows.path >= 0 ? rows.path : rows.slider) : (item == rows.path ? rows.slider : -1));
						} else {
							to = rows.Auto(item) ? item - 1 : (item == rows.path ? up : (item == rows.slider ? (rows.path >= 0 ? rows.path : up) : -1));
						}
						if (to >= 0) {
							state.item = to;
						} else {
							a_plan.exit = dy > 0 ? 1 : -1;
						}
					}
					if (a_plan.focused && rows.Count() == 0) {
						a_plan.exit = a_plan.enter != 0 ? a_plan.enter : 1;  // nothing here to stop on
					}
					if (input.dx != 0 && (rows.Badge(state.item) || rows.Auto(state.item)) && rows.columns > 0) {
						state.item = ((state.item / 2 + input.dx + rows.columns) % rows.columns) * 2 + state.item % 2;
						cardInput.dx = 0;
					}
					state.item = std::clamp(state.item, 0, std::max(rows.Count() - 1, 0));
					if (state.item < rows.columns * 2) {
						state.abilityColumn = state.item / 2;
					}
				} else if (!tree && a_plan.focused && dy) {
					a_plan.exit = dy > 0 ? 1 : -1;  // nothing of theirs here: the cursor passes on
				}

				if (state.page.member && (!member || !member->paths)) {
					ClosePathPage();
				}
				if (state.page.member && member && state.page.member != member->id) {
					state.page.member = member->id;
					state.page.followRole = true;
					state.page.seenMember = 0;
					state.page.detailOpen = false;
					state.page.detailT = 0.0f;
					state.page.resetArmed = false;
				}
				if (state.page.member != 0 && member) {
					uiScale = pathScale;
					DrawPathPage(draw, *member, view->tuning, { 0.0f, 0.0f }, display, pathScale, a_plan.appear, pageInput, pageDy, pageTab);
					if (state.guide.open && TreeEntrance::At(state.clock - state.page.openedAt).interactive) {
						DrawGuide(draw, *member, display, pathScale, a_plan.appear, guideMove, guideClose);
					}
					DrawHints(draw, display, pathScale, Stage(a_plan.appear, 0.35f, 0.5f));
				} else if (member) {
					const Rect r = DrawCompanion(draw, *view, *member, { a_plan.x, a_plan.y }, a_plan.width, s, a_plan.appear, cardInput, true);
					a_plan.min[0] = r.min.x;
					a_plan.min[1] = r.min.y;
					a_plan.max[0] = r.max.x;
					a_plan.max[1] = r.max.y;
				}
				uiScale = s;
				if (state.keyboardNav) {
					Hovering(0x1000 + static_cast<std::uint64_t>(state.section) * 64 + static_cast<std::uint64_t>(state.item));
				}
				if (member && member->id != a_plan.member) {
					a_plan.select = member->id;
				}
			} else {
				DropPad();
			}

			uiScale = state.page.member ? pathScale : s;
			DrawTip(display, uiScale);
			uiScale = s;
			DrawTreeTransition(display);
			pathPageOpen = state.page.member != 0;
			a_plan.tree = state.page.member != 0;
			// Our input handler reads the pad for the tree for as long as the plan keeps drawing it.
			treeInPlanUntil = a_plan.tree && !state.closing ? ::GetTickCount64() + 250 : 0;

			// Coming onto the dock, the plan has ticked already.
			if (a_plan.enter != 0) {
				state.hoverKeyLast = state.hoverKey;
			}
			if (state.hoverKey != 0 && state.hoverKey != state.hoverKeyLast && !state.closing && state.clock - state.lastTick > 0.045f) {
				Sound("UIMenuFocus");
				state.lastTick = state.clock;
			}
			state.hoverKeyLast = state.hoverKey;
			state.hoverKey = 0;
			hostPlan = nullptr;
		}

		// --- HUD notices --------------------------------------------------------------------------

		bool planWindowWasOpen{ false };

		// A diamond's swell as it arrives: a little small, past full size, then settled.
		float Swell(float a_t)
		{
			const float t = Clamp01(a_t);
			return 0.82f + 0.18f * EaseOut(t) + 0.16f * std::sin(t * 3.14159f);
		}

		// Over a companion who has just levelled up, in two steps on the same spot: their new level in
		// a gold diamond, which fades, then the perk points they have to spend in a smaller red one,
		// which fades too. No points to spend, or no Paths add-on: the level alone.
		void LevelBadge(Im::ImDrawList* a_draw, const Leveling::Badge& a_badge, V a_display, float a_s)
		{
			constexpr float kLevelFor = 2.0f;
			const float     age = a_badge.age;
			const float     rise = Smooth(Clamp01(age / Leveling::kBadgeSeconds)) * 30.0f * a_s;
			const V         c{ a_badge.x * a_display.x, a_badge.y * a_display.y - 31.0f * a_s - rise };

			if (age < kLevelFor) {
				const float alpha = std::min(EaseOut(age / 0.4f), Clamp01((kLevelFor - age) / 0.55f));
				const float fresh = Clamp01(1.0f - age / 1.4f);
				const float r = 31.0f * a_s * Swell(age / 0.55f);
				Halo(a_draw, c, r * (2.1f + 0.8f * fresh), kGold, alpha * (0.5f + 0.5f * fresh));
				Diamond(a_draw, c, r, Alpha(IM_COL32(26, 17, 10, 238), alpha), Alpha(kGold, alpha), 2.6f * a_s);
				Diamond(a_draw, c, r - 5.5f * a_s, 0, Alpha(Mix(kText, kGold, 0.5f), alpha * 0.35f), 1.0f);
				InkCentered(a_draw, c, Alpha(Mix(kText, kGold, 0.3f + 0.7f * fresh), alpha), 28.0f * a_s, std::format("{}", a_badge.level));
				return;
			}
			if (a_badge.points <= 0) {
				return;
			}
			const float late = age - kLevelFor;
			const float alpha = std::min(EaseOut(late / 0.35f), Clamp01((Leveling::kBadgeSeconds - age) / 0.6f));
			if (alpha <= 0.01f) {
				return;
			}
			const float r = 23.0f * a_s * Swell(late / 0.45f);
			Halo(a_draw, c, r * 2.0f, kRed, alpha * (0.3f + 0.4f * Clamp01(1.0f - late / 1.2f)));
			Diamond(a_draw, c, r, Alpha(kRed, alpha), Alpha(Mix(kRed, IM_COL32(255, 255, 255, 255), 0.35f), alpha), 2.0f * a_s);
			InkCentered(a_draw, c, Alpha(IM_COL32(255, 255, 255, 255), alpha), 22.0f * a_s, std::format("{}", a_badge.points));
		}

		void __stdcall RenderHUD()
		{
			SelectTheme();
			MenuFont menuFont;
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
			const auto badges = Leveling::Badges();
			if (notices.empty() && badges.empty()) {
				return;
			}
			auto*       draw = Im::GetForegroundDrawList();
			const V     display = Im::GetIO()->DisplaySize;
			const float s = std::clamp(display.y / 1080.0f, 0.65f, 2.5f) * Settings::Get().uiScale;
			uiScale = s;
			for (const auto& badge : badges) {
				LevelBadge(draw, badge, display, s);
			}
			const auto  now = ::GetTickCount64();
			// Capture once: icon rendering must never choose the face or metrics of the words.
			auto* const noticeFont = Im::GetFont();
			float y = display.y * 0.64f;
			int shown = 0;
			for (auto it = notices.rbegin(); it != notices.rend() && shown < 4; ++it) {
				const float age = static_cast<float>(now - it->at) / 1000.0f;
				const float in = EaseOut(age / 0.28f);
				const float alpha = std::min(in, Clamp01((4.8f - age) / 0.9f));
				if (alpha <= 0.01f)
					continue;
				U32 accent = kEdge;
				std::uint32_t glyph = 0xF0C0;
				if (it->kind == Notices::Kind::kTaunt) {
					accent = kRed;
					glyph = 0xF0A1;
				} else if (it->kind == Notices::Kind::kHeal) {
					accent = kGreen;
					glyph = 0xF4BE;
				} else if (it->kind == Notices::Kind::kWarning) {
					accent = kAmber;
					glyph = 0xF071;
				}
				if (it->icon != 0)
					glyph = it->icon;
				const bool ability = !it->title.empty();
				const float bodySize = (ability ? 14.0f : 15.0f) * s;
				const float titleSize = 18.0f * s;
				const auto measureNotice = [&](float size, const std::string& words, float wrap = 0.0f) {
					return Im::ImFontManger::CalcTextSizeA(noticeFont, size * kTextScale, 1000000.f, wrap, words.c_str(), nullptr, nullptr);
				};
				const float textWidth = std::clamp(std::max(measureNotice(bodySize, it->text).x, measureNotice(titleSize, it->title).x) + 2 * s, 160 * s, 360 * s);
				const float bodyH = measureNotice(bodySize, it->text, textWidth).y;
				const float titleH = ability ? measureNotice(titleSize, it->title, textWidth).y + 7 * s : 0.f;
				const float h = std::max(ability ? 78 * s : 52 * s, bodyH + titleH + 32 * s);
				const float slide = (1.0f - in) * -16.0f * s;
				const V min{ 44 * s + slide, y - h };
				const V max{ min.x + textWidth + (ability ? 92 : 70) * s, y };
				Glass(draw, min, max, alpha * .94f, 0.0f);
				Draw::AddLine(draw, { min.x, min.y + 10 * s }, { min.x, max.y - 10 * s }, Alpha(accent, alpha * .8f), 2 * s);
				const V iconCentre{ min.x + (ability ? 32 : 23) * s, (min.y + max.y) * .5f };
				if (ability)
					Emblem(draw, iconCentre, 23 * s, Alpha(palette.inset, alpha), Alpha(accent, alpha * .6f), 1 * s);
				Glyph(draw, iconCentre, (ability ? 25 : 18) * s, glyph, Alpha(ability ? kText : accent, alpha));
				const float tx = min.x + (ability ? 66 : 46) * s;
				const float ty = min.y + (h - bodyH - titleH) * .5f;
				if (ability)
					Draw::AddText(draw, noticeFont, titleSize * kTextScale, { tx, ty }, Alpha(kText, alpha), it->title.c_str(), nullptr, textWidth);
				Draw::AddText(draw, noticeFont, bodySize * kTextScale, { tx, ty + titleH }, Alpha(ability ? kTextDim : kText, alpha), it->text.c_str(), nullptr, textWidth);
				y = min.y - 7 * s;
				++shown;
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
			ScopedFont font;
			Theme() : font((SelectTheme(), MenuFontName()))
			{
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
				const float round = palette.style == UITheme::Style::Vanilla ? E(.28f) : palette.Engraved() ? E(.06f) : 0.f;
				Im::PushStyleVar(Im::ImGuiStyleVar_FrameRounding, round);
				Im::PushStyleVar(Im::ImGuiStyleVar_GrabRounding, round);
				Im::PushStyleVar(Im::ImGuiStyleVar_PopupRounding, round);
				Im::PushStyleVar(Im::ImGuiStyleVar_FrameBorderSize, palette.Engraved() ? 2.f : 1.f);
				Im::PushStyleVar(Im::ImGuiStyleVar_ScrollbarRounding, round);
				Im::PushStyleVar(Im::ImGuiStyleVar_WindowPadding, V{ E(0.35f), E(0.25f) });
			}
			~Theme()
			{
				Im::PopStyleVar(8);
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
				Im::SetWindowFontScale(0.85f * kTextScale);  // compact, but grown with the rest of the text
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

		// Keep native keyboard, controller and accessibility behavior, but draw the
		// same shaped surfaces as the battle menu beneath native labels and values.
		struct ControlSurface
		{
			ControlSurface(V lo, V size, bool emblem = false)
			{
				const V hi{lo.x+size.x,lo.y+size.y};
				const bool hover = Im::IsMouseHoveringRect(lo,hi);
				const U32 fill = Im::GetColorU32(hover ? palette.frameHover : palette.inset);
				const U32 edge = Im::GetColorU32(hover ? kEdge : kGlassEdge);
				if (emblem) Emblem(Im::GetWindowDrawList(),{lo.x+size.x*.5f,lo.y+size.y*.5f},size.y*.48f,fill,edge,1);
				else { PanelFill(Im::GetWindowDrawList(),lo,hi,fill); PanelEdge(Im::GetWindowDrawList(),lo,hi,edge); }
				for (const auto col : {Im::ImGuiCol_FrameBg,Im::ImGuiCol_FrameBgHovered,Im::ImGuiCol_FrameBgActive,
					Im::ImGuiCol_Button,Im::ImGuiCol_ButtonHovered,Im::ImGuiCol_ButtonActive,
					Im::ImGuiCol_Header,Im::ImGuiCol_HeaderHovered,Im::ImGuiCol_HeaderActive,Im::ImGuiCol_Border,Im::ImGuiCol_BorderShadow}) Im::PushStyleColor(col,U32{0});
			}
			~ControlSurface() { Im::PopStyleColor(11); }
		};
		bool ThemeCheck(const char* label, bool* value)
		{
			const V lo=Im::GetCursorScreenPos(); const float h=Im::GetFrameHeight();
			ControlSurface surface(lo,{h,h},true);
			Im::PushStyleColor(Im::ImGuiCol_CheckMark,U32{0});
			const bool changed=Im::Checkbox(label,value);
			Im::PopStyleColor();
			if (*value) {
				const U32 ink=Im::GetColorU32(kText);
				auto* d=Im::GetWindowDrawList();
				Draw::AddLine(d,{lo.x+h*.30f,lo.y+h*.50f},{lo.x+h*.45f,lo.y+h*.64f},ink,1.8f);
				Draw::AddLine(d,{lo.x+h*.45f,lo.y+h*.64f},{lo.x+h*.72f,lo.y+h*.35f},ink,1.8f);
			}
			return changed;
		}
		bool ThemeButton(const char* label, V size = {0,0})
		{
			const float h=Im::GetFrameHeight();
			const V wanted{size.x<0 ? Im::GetContentRegionAvail().x : size.x>0 ? size.x : Im::CalcTextSize(label,nullptr,true).x+8,
				size.y>0 ? size.y : h};
			ControlSurface surface(Im::GetCursorScreenPos(),wanted);
			return Im::Button(label,size);
		}
		bool ThemeCombo(const char* label, const char* preview)
		{
			ControlSurface surface(Im::GetCursorScreenPos(),{Im::CalcItemWidth(),Im::GetFrameHeight()});
			return Im::BeginCombo(label,preview);
		}
		bool ThemeHeader(const char* label, int flags)
		{
			ControlSurface surface(Im::GetCursorScreenPos(),{Im::GetContentRegionAvail().x,Im::GetFrameHeight()});
			return Im::CollapsingHeader(label,flags);
		}
		void SliderGem(float fraction)
		{
			const V lo=Im::GetCursorScreenPos(); const float h=Im::GetFrameHeight(),w=Im::CalcItemWidth();
			const float r=h*.32f;
			const V c{lo.x+r+3+(w-2*r-6)*Clamp01(fraction),lo.y+h*.5f};
			Emblem(Im::GetWindowDrawList(),c,r,Im::GetColorU32(kAccent),Im::GetColorU32(kEdge),1);
		}
		bool ThemeFloat(const char* label,float* v,float low,float high,const char* format,int flags)
		{
			ControlSurface surface(Im::GetCursorScreenPos(),{Im::CalcItemWidth(),Im::GetFrameHeight()});
			SliderGem(high>low ? (*v-low)/(high-low) : 0);
			Im::PushStyleColor(Im::ImGuiCol_SliderGrab,U32{0}); Im::PushStyleColor(Im::ImGuiCol_SliderGrabActive,U32{0});
			const bool changed=Im::SliderFloat(label,v,low,high,format,flags); Im::PopStyleColor(2); return changed;
		}
		bool ThemeInt(const char* label,int* v,int low,int high,const char* format,int flags)
		{
			ControlSurface surface(Im::GetCursorScreenPos(),{Im::CalcItemWidth(),Im::GetFrameHeight()});
			SliderGem(high>low ? static_cast<float>(*v-low)/static_cast<float>(high-low) : 0);
			Im::PushStyleColor(Im::ImGuiCol_SliderGrab,U32{0}); Im::PushStyleColor(Im::ImGuiCol_SliderGrabActive,U32{0});
			const bool changed=Im::SliderInt(label,v,low,high,format,flags); Im::PopStyleColor(2); return changed;
		}

		bool Group(const char* a_title, std::uint32_t a_glyph, bool a_open = true)
		{
			Space();
			const auto p = Im::GetCursorScreenPos();
			Glyph(Im::GetWindowDrawList(), { p.x + E(0.45f), p.y + E(0.6f) }, E(0.72f), a_glyph, kEdge);
			Im::Dummy({ E(1.35f), E(1.2f) });
			Im::SameLine(0, 0);
			Im::PushStyleColor(Im::ImGuiCol_Text, kText);
			const bool expanded = ThemeHeader(a_title, a_open ? Im::ImGuiTreeNodeFlags_DefaultOpen : 0);
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
			Im::TableSetupColumn(Trc("Setting"), Im::ImGuiTableColumnFlags_WidthStretch, 0.40f);
			Im::TableSetupColumn(Trc("Value"), Im::ImGuiTableColumnFlags_WidthStretch, 0.60f);
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
			dirty |= ThemeFloat("##value", &a_value, a_low, a_high, a_format, Im::ImGuiSliderFlags_AlwaysClamp);
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
			if (ThemeInt("##value", &percent, a_low, a_high, "%d%%", Im::ImGuiSliderFlags_AlwaysClamp)) {
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
			dirty |= ThemeInt("##value", &a_value, a_low, a_high, a_format, Im::ImGuiSliderFlags_AlwaysClamp);
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
			dirty |= ThemeCheck("##value", &a_value);
			Help(a_hint);
			Im::PopID();
		}

		void Check(const char* a_name, bool& a_value, const char* a_hint)
		{
			Im::TableNextColumn();
			dirty |= ThemeCheck(a_name, &a_value);
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
				feedback = !Settings::LastSaveFailed()   ? "" :
				           Host::Abilities() ? Trc("Could not save FightWithMePaths_user.ini. Check file access, then change the setting again.") :
				                                Trc("Could not save FightWithMe.ini. Check file access, then change the setting again.");
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
			return static_cast<std::size_t>(std::count_if(a_view->party.begin(), a_view->party.end(), [](const View::Member& m) { return m.CanTakeRole(); }));
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
			Im::TextDisabled("%s", Trn(static_cast<long long>(count), "{} companion in reach", "{} companions in reach").c_str());
			Im::PushStyleColor(Im::ImGuiCol_Separator, kEdge);
			Im::Separator();
			Im::PopStyleColor();
			Space();
		}

		bool shareTheme{false};
		void Footer()
		{
			if (dirty) {
				Settings::Submit(draft,shareTheme);
                shareTheme=false;
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
			const bool clicked = Im::SmallButton(Trc("Reset this page"));
			Help(a_hint);
			return clicked;
		}

		// --- Party ---------------------------------------------------------------------

		// a_why: an empty reason means the choice is on offer.
		void RosterCombo(const char* a_id, int& a_value, int a_count, const char* (*a_name)(int), const std::function<std::string_view(int)>& a_why,
			bool& a_changed)
		{
			Im::SetNextItemWidth(-1);
			if (ThemeCombo(a_id, a_name(a_value))) {
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
		const char* PositionName(int a_index) { return positions[static_cast<std::size_t>(a_index + kFirstPosition)].name.data(); }

		void Roster(const View::Snapshot& a_view)
		{
			if (a_view.party.empty()) {
				Note(Trc("No companions in reach. Followers nearby, Walk With Me's party and summons appear here once loaded."));
				return;
			}
			if (!Im::BeginTable("roster", 5, Im::ImGuiTableFlags_SizingStretchProp | Im::ImGuiTableFlags_NoSavedSettings | Im::ImGuiTableFlags_PadOuterX |
												 Im::ImGuiTableFlags_RowBg)) {
				return;
			}
			Im::TableSetupColumn(Trc("Companion"), Im::ImGuiTableColumnFlags_WidthStretch, 1.1f);
			Im::TableSetupColumn(Trc("Role"), Im::ImGuiTableColumnFlags_WidthStretch, 0.9f);
			Im::TableSetupColumn(Trc("Focus"), Im::ImGuiTableColumnFlags_WidthStretch, 1.2f);
			Im::TableSetupColumn(Trc("Position"), Im::ImGuiTableColumnFlags_WidthStretch, 1.0f);
			Im::TableSetupColumn(Trc("Auto"), Im::ImGuiTableColumnFlags_WidthFixed, E(2.4f));
			Im::TableHeadersRow();
			std::string summons;
			for (const auto& m : a_view.party) {
				if (!m.CanTakeRole()) {
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
				Im::TextDisabled("%s", Trf("Lv {}", m.level).c_str());
				Orders orders = m.orders;
				bool   changed = false;
				int    role = static_cast<int>(orders.role), focus = static_cast<int>(orders.focus), position = static_cast<int>(Live(orders.position)) - kFirstPosition;
				Im::TableNextColumn();
				const int before = role;
				// Roles are settled out of a fight, here as in the battle plan.
				const bool fight = a_view.inCombat || !a_view.enemies.empty();
				Im::BeginDisabled(fight);
				RosterCombo("##role", role, static_cast<int>(Role::kCount), RoleName,
					[&](int i) { return m.arsenalKnown ? Tr(Why(LackFor(static_cast<Role>(i), m.arsenal))) : std::string_view{}; }, changed);
				Im::EndDisabled();
				Help(fight ? Trc("Roles are set out of a fight.") : roles[static_cast<std::size_t>(role)].summary.data());
				Im::TableNextColumn();
				RosterCombo("##focus", focus, static_cast<int>(Focus::kCount), FocusName, nullptr, changed);
				Help(focuses[static_cast<std::size_t>(focus)].summary.data());
				Im::TableNextColumn();
				RosterCombo("##position", position, kPositionChoices, PositionName, nullptr, changed);
				Help(positions[static_cast<std::size_t>(position + kFirstPosition)].summary.data());
				Im::TableNextColumn();
				const bool ability = orders.role == Role::kTank || orders.role == Role::kHealer;
				Im::BeginDisabled(!ability);
				changed |= ThemeCheck("##auto", &orders.autoAbility);
				Im::EndDisabled();
				Help(ability ? (orders.role == Role::kTank ? Trc("Taunt without being told.") : Trc("Heal without being told.")) :
							   Trc("Only tanks and healers have an ability of their own."));
				if (changed) {
					if (role != before) {
						orders = ForRole(static_cast<Role>(role), orders);  // a new role brings its focus and position
					} else {
						orders.focus = static_cast<Focus>(focus);
						orders.position = static_cast<Position>(position + kFirstPosition);
					}
					SetOrders(m, orders);
				}
				Im::PopID();
			}
			Im::EndTable();
			if (!summons.empty()) {
				Im::TextDisabled("%s", Trf("Also with you: {}. Creatures and summons use their own combat AI and can receive party healing.", summons).c_str());
			}
		}

		void __stdcall RenderParty()
		{
			Prepare();
			SettingsPage page("FightWithMeParty");
			Header(Trc("PARTY"));
			if (Rows("enabled")) {
				Toggle(Trc("Enable Fight With Me"), draft.enabled, Trc("Let Fight With Me direct companions in battle. Off hands every fight back to their own judgement at once."));
				Im::EndTable();
			}
			Heading(Trc("COMPANIONS"), 0xF0C0);
			Battle::RefreshView();
			const auto view = Battle::ReadView();
			if (view) {
				Im::BeginDisabled(!draft.enabled);
				Roster(*view);
				Im::EndDisabled();
			}
			Help(Trc("Orders change at once and are kept in your save. The battle plan shows the same choices with the fight in view."));
			Space();
			if (ThemeButton(Trc("Open the battle plan"))) {
				Open();
			}
			Help(Trc("Slows the world and shows every companion, their target and the enemies around you."));
			if (Group(Trc("Whole party"), 0xF0C0)) {
				Im::BeginDisabled(!draft.enabled);
				if (ThemeButton(Trc("Everyone back to their role"))) {
					Preset(Battle::Preset::kRoles);
				}
				Help(Trc("Sets every companion's stance and targets back to what their role suggests."));
				Im::EndDisabled();
				Help(Trc("Party orders (Attack!, To me!, Hold back, As you were) replace everyone's own orders at once. They are on the numpad by default; see Controls."));
			}
			if (Group(Trc("Roles"), 0xF3ED)) {
				if (Rows("roles")) {
					Percent(Trc("Role strength"), draft.styleWeight, 0, 100,
						Trc("How far a role reshapes a companion's own way of fighting. 0% changes only whom they fight; 100% replaces their style with the role's."));
					Im::EndTable();
				}
				if (Checks("roleOptions")) {
					Check(Trc("Guess roles for new companions"), draft.guessRoles,
						Trc("The first time a companion joins: a shield makes a Tank, a two-hander a Bruiser, a bow an Archer, a staff or spells a Mage or Healer. "
						"An NFF tank or healer role is kept. Off: everyone starts as a Fighter."));
					Im::EndTable();
				}
			}
			if (Group(Trc("Before a fight"), 0xF06E)) {
				if (Checks("sight")) {
					Check(Trc("Weapons out when an enemy is in view"), draft.drawOnSight,
						Trc("Out of a fight, an enemy that a companion or you can see brings their weapons out. They put them away a few seconds after the last one is "
						"out of view, unless yours are out."));
					Check(Trc("Auto battle"), draft.autoBattle,
						Trc("A companion who sees an enemy starts the fight instead of waiting to be attacked. Companions on Hold back wait, and nobody starts one "
						"while you sneak. Also a switch at the top right of the battle plan."));
					Im::EndTable();
				}
				Im::BeginDisabled(!draft.drawOnSight && !draft.autoBattle);
				if (Rows("sightRange")) {
					Slider(Trc("Sight range"), draft.sightRange, 1000.0f, 5000.0f, "%.0f", Trc("How far away an enemy can be and still count as in view. 2500 is about a long bowshot."));
					Im::EndTable();
				}
				Im::EndDisabled();
			}
			if (Group(Trc("When the fight is over"), 0xF252)) {
				if (Checks("emptyFights")) {
					Check(Trc("Stand down when no enemy is left"), draft.leaveEmptyFights,
						Trc("With weapons out and nobody left to fight, companions leave combat and fall back into Walk With Me's formation instead of searching."));
					Im::EndTable();
				}
				Im::BeginDisabled(!draft.leaveEmptyFights);
				if (Rows("emptyFightDelay")) {
					Slider(Trc("After"), draft.leaveAfter, 0.5f, 10.0f, "%.1f s", Trc("How long the fight must have nobody in it."));
					Im::EndTable();
				}
				Im::EndDisabled();
			}
			if (ResetButton(Trc("Role strength, role guessing, what happens before a fight and standing down back to their defaults. Companions' orders are not touched."))) {
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

		// --- Targeting -------------------------------------------------------------------

		void __stdcall RenderTargeting()
		{
			Prepare();
			SettingsPage page("FightWithMeTargeting");
			Header(Trc("TARGETING"));
			auto& t = draft.tuning;
			if (Group(Trc("Choosing a target"), 0xF140)) {
				if (Rows("targetChoice")) {
					Slider(Trc("Nearer counts up to"), t.engageRange, 500.0f, 6000.0f, Trc("%.0f units"),
						Trc("Between two enemies that fit the focus, the nearer is preferred up to this distance. Distance never rules anyone out: an ordered target is chased wherever it is."));
					Slider(Trc("Stick to a target"), t.stickiness, 0.0f, 100.0f, "%.0f", Trc("How much a companion prefers the enemy they are already fighting."));
					Slider(Trc("Spread out"), t.spreadPenalty, 0.0f, 60.0f, "%.0f", Trc("How much they avoid an enemy others are already on, per ally."));
					Slider(Trc("Distance weight"), t.distanceWeight, 0.0f, 0.15f, "%.3f", Trc("How much nearer enemies are preferred."));
					Slider(Trc("Rethink every"), draft.planInterval, 0.1f, 1.5f, "%.2f s", Trc("How often targets are reconsidered."));
					Im::EndTable();
				}
			}
			if (Group(Trc("Positions"), 0xF505)) {
				if (Rows("targetPositions")) {
					Slider(Trc("Guard you within"), t.guardRadius, 200.0f, 2000.0f, Trc("%.0f units"), Trc("Guard you: companions fight enemies this close to you, or attacking you."));
					Slider(Trc("Hold back within"), t.holdBackRadius, 150.0f, 1500.0f, Trc("%.0f units"),
						Trc("Hold back: melee companions only meet enemies this close to themselves. Ranged ones keep shooting from where they stand."));
					Slider(Trc("With me within"), t.withMeRadius, 200.0f, 1000.0f, Trc("%.0f units"),
						Trc("With me (what To me! gives everyone): they keep this close to you and fight from there. Melee companions take anything that comes near you; archers and casters shoot the nearest enemy, however far."));
					Slider(Trc("Fight back within"), t.defendRange, 100.0f, 1500.0f, Trc("%.0f units"),
						Trc("An enemy attacking a companion from this close is dealt with first, whatever their focus. Your target and a marked enemy are orders and still come first."));
					Toggle(Trc("Keep them near you"), draft.guardLeash,
						Trc("Companions guarding you, holding back or with you keep inside their distance, the way a guard keeps to their post: they break off a chase and come back."));
					Im::BeginDisabled(!draft.guardLeash);
					Slider(Trc("Hold back leash"), draft.holdBackLeash, 300.0f, 3000.0f, Trc("%.0f units"), Trc("How far from you a companion holding back may go."));
					Im::EndDisabled();
					Im::EndTable();
				}
			}
			if (Group(Trc("Sending someone after an enemy"), 0xF024)) {
				if (Checks("marks")) {
					Check(Trc("Outline marked enemies"), draft.markGlow,
						Trc("While the battle plan is open, an enemy you send someone after is outlined, through walls. The outline goes when the plan closes."));
					Im::EndTable();
				}
			}
			if (ResetButton(Trc("Every targeting setting on this page back to its default."))) {
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
			Header(Trc("BATTLE PLAN"));
			if (Group(Trc("Time"), 0xF252)) {
				if (Rows("time")) {
					Toggle(Trc("Stop time"), draft.stopTime, Trc("Pause the game while planning instead of slowing it."));
					Im::BeginDisabled(draft.stopTime);
					Slider(Trc("Slow motion"), draft.timeScale, 0.02f, 1.0f, Trc("%.2fx"), Trc("How fast the world runs while the plan is open."));
					Slider(Trc("Ease in and out"), draft.easeSeconds, 0.0f, 1.0f, "%.2f s", Trc("Seconds for time to slow down and speed back up."));
					Im::EndDisabled();
					Im::EndTable();
				}
				if (Checks("timeOptions")) {
					Im::BeginDisabled(draft.stopTime);
					Check(Trc("Slow Time screen effect"), draft.timeEffect, Trc("The Slow Time shout's look while planning. It washes the picture out and blurs it; off keeps the battlefield clear."));
					Im::EndDisabled();
					Check(Trc("Hide the HUD"), draft.hideHud, Trc("Hide the HUD and other interface while the plan is open; everything comes back when it closes."));
					Im::EndTable();
				}
			}
			if (Group(Trc("Camera"), 0xF030)) {
				if (Rows("camera")) {
					Toggle(Trc("Glide to the companion"), draft.cameraGlide,
						Trc("While the plan is open the camera eases over to the companion you are commanding, follows as you switch between them, and eases back when you close it."));
					Im::BeginDisabled(!draft.cameraGlide);
					Slider(Trc("Distance"), draft.cameraDistance, 120.0f, 700.0f, Trc("%.0f units"), Trc("How far behind the companion the camera settles."));
					Slider(Trc("Height"), draft.cameraHeight, 0.0f, 300.0f, Trc("%.0f units"), Trc("How far above their eyes."));
					Slider(Trc("Turning speed"), draft.orbitSpeed, 0.2f, 3.0f, Trc("%.1fx"),
						Trc("How fast the right stick, or dragging with the right mouse button, turns the view around the companion or the fight."));
					Slider(Trc("Overhead tilt"), draft.overheadPitch, 35.0f, 88.0f, Trc("%.0f deg"),
						Trc("How steeply the overhead view looks down when it first opens. 88 is straight down; lower shows more of the room's shape. Tilt it in the plan with the right stick."));
					Im::EndDisabled();
					Im::EndTable();
				}
				if (Checks("cutaway")) {
					Im::BeginDisabled(!draft.cameraGlide);
					Check(Trc("Cut the ceiling away overhead"), draft.cutaway,
						Trc("When something blocks the view from above (a cave roof, a ceiling, an overhang), the overhead view leaves out everything above the tallest fighter's head, so nobody is hidden. In the open nothing is cut. Off: the camera comes down under the cover instead."));
					Im::EndDisabled();
					Im::EndTable();
				}
				if (draft.cutaway && draft.cameraGlide && Rows("cutawayRows")) {
					Slider(Trc("Cut above heads"), draft.cutawayMargin, 0.0f, 400.0f, Trc("%.0f units"),
						Trc("How far above the tallest head the cut goes. Raise it to keep more of the walls; lower it if a low ceiling still shows."));
					Im::EndTable();
				}
			}
			if (Group(Trc("Interface"), 0xF06E)) {
				if (Rows("planLook")) {
					Im::TableNextRow();
					Im::TableNextColumn();
					Im::AlignTextToFramePadding();
					Im::TextUnformatted(Trc("Theme"));
					Im::TableNextColumn();
					Im::SetNextItemWidth(-1);
					if (ThemeCombo("##uiTheme", Trc(UITheme::Name(draft.uiTheme)))) {
						for (int i = 0; i < UITheme::kCount; ++i) {
							if (Im::Selectable(Trc(UITheme::Name(i)), draft.uiTheme == i) && draft.uiTheme != i) {
								draft.uiTheme = i;
								dirty = true;
							}
						}
						Im::EndCombo();
					}
					Help(Trc("Vanilla: charcoal and silver. Oxblood: the original red. Nordic: engraved stone, beveled metal and inset highlights. All use the same windows and controls. Applies to the battle plan, Paths, notices and these settings."));
					Slider(Trc("Menu scale"), draft.uiScale, 0.6f, 2.0f, Trc("%.2fx"), Trc("Size of the battle plan on top of the screen-height scaling."));
					Im::EndTable();
				}
				if (Checks("notices")) {
                    Check(Trc("Notices"), draft.notices, Trc("Short lines on the left of the screen when a companion cannot do what they were told, or runs out of arrows."));
					Check(Trc("Menu sounds"), draft.sounds, Trc("The game's own menu sounds when you point at and choose orders in the battle plan."));
					Check(Trc("Tooltips"), draft.tooltips, Trc("Describe each control when you point at it or move to it with the pad. Turn off once you know them."));
					Im::EndTable();
				}
			}
			if (Group(Trc("Troubleshooting"), 0xF188, false)) {
				if (Checks("debug")) {
					Check(Trc("Log target decisions"), draft.logDecisions, "Write every target change and role style to FightWithMe.log.");
					Im::EndTable();
				}
			}
			Space();
			if (ThemeButton(Trc("Open the battle plan"))) {
				Open();
			}
			if (ResetButton(Trc("Every setting on this page back to its default."))) {
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
			{ 0, Ntr("Unbound") },
			{ kPadDown, Ntr("LB + D-pad down") },
			{ kPadUp, Ntr("LB + D-pad up") },
			{ kPadY, Ntr("LB + Y") },
			{ 0x80, Ntr("LB + Right-stick click") },
		} };

		constexpr std::array<PadChoice, 4> overheadPadChoices{ {
			{ 0, Ntr("Unbound") },
			{ 0x80, Ntr("Right-stick click") },
			{ 0x40, Ntr("Left-stick click") },
			{ 0x20, Ntr("Back / View") },
		} };

		void __stdcall RenderControls()
		{
			Prepare();
			SettingsPage page("FightWithMeControls");
			Header(Trc("CONTROLS"));
			Heading(Trc("KEY BINDINGS"), 0xF11C);
			Help(Trc("Select the binding, then press a key, holding Shift, Ctrl or Alt with it for a combination. Esc cancels; Delete clears."));
			Space();
			if (Im::BeginTable("bindings", 4, Im::ImGuiTableFlags_SizingStretchProp)) {
				Im::TableSetupColumn(Trc("Action"), Im::ImGuiTableColumnFlags_WidthStretch, 1.4f);
				Im::TableSetupColumn(Trc("Modifier"), Im::ImGuiTableColumnFlags_WidthStretch, 0.6f);
				Im::TableSetupColumn(Trc("Key"), Im::ImGuiTableColumnFlags_WidthStretch, 1.0f);
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
					if (ThemeCombo("##modifier", ModifierName(a_hotkey.modifier))) {
						for (const std::uint32_t modifier : { 0u, kShift, kCtrl, kAlt }) {
							if (Im::Selectable(ModifierName(modifier), modifier == a_hotkey.modifier) && modifier != a_hotkey.modifier) {
								a_hotkey.modifier = modifier;
								dirty = true;
							}
						}
						Im::EndCombo();
					}
					Im::EndDisabled();
					Help(Trc("Held with the key. A key with no modifier works whatever else is held, so sprinting does not get in the way."));
					Im::TableNextColumn();
					const bool waiting = capturing.load() && captureSlot == a_slot;
					const auto held = captureModifier.load();
					const auto name = !waiting ? KeyName(a_hotkey.key) : (held ? std::format("{} + ...", ModifierName(held)) : std::string(Trc("Press a key...")));
					if (ThemeButton(name.c_str(), { -1, 0 })) {
						if (waiting) {
							CancelCapture();
						} else {
							StartCapture(a_slot);
						}
					}
					Im::TableNextColumn();
					if (ThemeButton(Trc("Clear"), { -1, 0 })) {
						a_hotkey = {};
						dirty = true;
						CancelCapture();
					}
					Im::PopID();
				};
				binding(0, Trc("Open the battle plan"), draft.planKey);
				binding(1, Trc("Attack! (everyone on your target)"), draft.orderKeys[0]);
				binding(2, Trc("To me! (everyone guards you)"), draft.orderKeys[1]);
				binding(3, Trc("Hold back (everyone stays back)"), draft.orderKeys[2]);
				binding(4, Trc("As you were"), draft.orderKeys[3]);
				binding(5, Trc("Overhead view (in the plan)"), draft.overheadKey);
				Im::EndTable();
			}
			// Two actions on one binding: only the first of them would ever happen.
			const std::array<std::pair<const char*, const Hotkey*>, 6> bound{ {
				{ Trc("Attack!"), &draft.orderKeys[0] },
				{ Trc("To me!"), &draft.orderKeys[1] },
				{ Trc("Hold back"), &draft.orderKeys[2] },
				{ Trc("As you were"), &draft.orderKeys[3] },
				{ Trc("Open the battle plan"), &draft.planKey },
				{ Trc("Overhead view"), &draft.overheadKey },
			} };
			std::string clash;
			for (std::size_t i = 0; i < bound.size() && clash.empty(); ++i) {
				for (std::size_t j = i + 1; j < bound.size() && clash.empty(); ++j) {
					if (bound[i].second->key && *bound[i].second == *bound[j].second) {
						clash = Trf("{} and {} are both on {}; only {} will happen.", bound[i].first, bound[j].first, HotkeyName(*bound[i].second), bound[i].first);
					}
				}
			}
			if (!clash.empty()) {
				Note(clash.c_str());
			}
			if (Group(Trc("Gamepad"), 0xF11B)) {
				if (Rows("gamepad")) {
					Im::TableNextRow();
					Im::TableNextColumn();
					Im::AlignTextToFramePadding();
					Im::TextUnformatted(Trc("Open the battle plan"));
					Im::TableNextColumn();
					int current = 0;
					for (int i = 0; i < static_cast<int>(padChoices.size()); ++i) {
						if (padChoices[static_cast<std::size_t>(i)].button == draft.planGamepadKey) {
							current = i;
						}
					}
					Im::SetNextItemWidth(-1);
					if (ThemeCombo("##pad", Trc(padChoices[static_cast<std::size_t>(current)].name))) {
						for (int i = 0; i < static_cast<int>(padChoices.size()); ++i) {
							if (Im::Selectable(Trc(padChoices[static_cast<std::size_t>(i)].name), i == current)) {
								draft.planGamepadKey = padChoices[static_cast<std::size_t>(i)].button;
								dirty = true;
							}
						}
						Im::EndCombo();
					}
					Help(Trc("Hold LB and press the chosen button. Walk With Me's wheel uses LB with the right stick, Y or D-pad up; pick something it does not."));
					Im::TableNextRow();
					Im::TableNextColumn();
					Im::AlignTextToFramePadding();
					Im::TextUnformatted(Trc("Overhead view (in the plan)"));
					Im::TableNextColumn();
					int overhead = 0;
					for (int i = 0; i < static_cast<int>(overheadPadChoices.size()); ++i) {
						if (overheadPadChoices[static_cast<std::size_t>(i)].button == draft.overheadGamepadKey) {
							overhead = i;
						}
					}
					Im::SetNextItemWidth(-1);
					if (ThemeCombo("##overheadPad", Trc(overheadPadChoices[static_cast<std::size_t>(overhead)].name))) {
						for (int i = 0; i < static_cast<int>(overheadPadChoices.size()); ++i) {
							if (Im::Selectable(Trc(overheadPadChoices[static_cast<std::size_t>(i)].name), i == overhead)) {
								draft.overheadGamepadKey = overheadPadChoices[static_cast<std::size_t>(i)].button;
								dirty = true;
							}
						}
						Im::EndCombo();
					}
					Help(Trc("Switches between the companion and the whole fight seen from above while the plan is open. The right stick turns the view either way."));
					Im::EndTable();
				}
			}
			if (Group(Trc("In the battle plan"), 0xF05A)) {
				if (Im::BeginTable("planControls", 3, Im::ImGuiTableFlags_SizingStretchProp | Im::ImGuiTableFlags_RowBg)) {
					Im::TableSetupColumn(Trc("What"), Im::ImGuiTableColumnFlags_WidthStretch, 1.2f);
					Im::TableSetupColumn(Trc("Keyboard and mouse"), Im::ImGuiTableColumnFlags_WidthStretch, 1.0f);
					Im::TableSetupColumn(Trc("Gamepad"), Im::ImGuiTableColumnFlags_WidthStretch, 1.0f);
					Im::TableHeadersRow();
					const std::array<std::array<const char*, 3>, 10> rows{ {
						{ Trc("Choose a companion"), Trc("Click a crest, Tab"), Trc("LB / RB") },
						{ Trc("Move between rows"), Trc("Up / Down"), Trc("D-pad up / down") },
						{ Trc("Pick or step a value"), Trc("Click, Left / Right"), Trc("D-pad left / right") },
						{ Trc("Give a party order"), Trc("Click it, or its key"), Trc("Party orders row, A") },
						{ Trc("Send someone after an enemy"), Trc("Click the enemy"), Trc("Battlefield row, A") },
						{ Trc("Ability now / switch"), Trc("Click"), "X / Y" },
						{ Trc("Turn the view"), Trc("Drag with the right button"), Trc("Right stick") },
						{ Trc("Zoom"), Trc("Mouse wheel"), Trc("LT / RT") },
						{ Trc("Overhead view"), Trc("Its key, or the button top left"), Trc("The button set above") },
						{ Trc("Close"), Trc("Esc or the hotkey"), "B" },
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
			if (ThemeButton(Trc("Open the battle plan"))) {
				Open();
			}
			Help(Trc("Commands pause in game menus. Avoid keys used by other mods or by quicksave and quickload."));
			if (ResetButton(Trc("The hotkey and the gamepad button back to their defaults."))) {
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

		// --- Path of the Companions --------------------------------------------
		// Paths settings, including the panel controls and shared theme.

		PathsPage  pathsPage;
		PathsRules pathsDraft;
		bool       havePathsDraft{ false };
		bool       pathsSaveFailed{ false };

		void __stdcall RenderPathsSettings()
		{
			Prepare();
			SettingsPage page("PathsSettings");
			Im::PushStyleColor(Im::ImGuiCol_Text, kEdge);
			Im::TextUnformatted(Upper(Tr("Path of the Companions")).c_str());
			Im::PopStyleColor();
			Im::PushStyleColor(Im::ImGuiCol_Separator, kEdge);
			Im::Separator();
			Im::PopStyleColor();
			Space();

			if (!havePathsDraft && pathsPage.read) {
				pathsDraft = pathsPage.read();
				havePathsDraft = true;
			}
			// The point rules are Paths' own, saved on their own; the page's shared flag stays as it was.
			const bool       wasDirty = dirty;
			const PathsRules before = pathsDraft;
			if (Group(Trc("Perk points"), 0xF005)) {
				if (Rows("pathsPoints")) {
					IntSlider(Trc("A point for every level past"), pathsDraft.above, 1, 50, "%d",
						Trc("10: the first point at level 11, forty by level 50. Points come from the highest level a companion has reached, so "
							"someone met late arrives with points to spend."));
					IntSlider(Trc("Bonus points"), pathsDraft.extra, 0, 50, "%d", Trc("Points every companion has on top, whatever their level."));
					Toggle(Trc("Give points back"), pathsDraft.respec,
						Trc("Ranks can be given back out of a fight, and a whole path started over, free. Off: every point spent is spent for good."));
					Toggle(Trc("Debug Mode button"), pathsDraft.unlimited,
						Trc("Test any ability rank and learn perks freely. Turning Debug Mode off restores earned progression."));
					Im::EndTable();
				}
			}
			dirty = wasDirty;
			if (pathsDraft.above != before.above || pathsDraft.extra != before.extra || pathsDraft.respec != before.respec ||
				pathsDraft.unlimited != before.unlimited) {
				pathsSaveFailed = pathsPage.write && !pathsPage.write(pathsDraft);
			}
			if (pathsSaveFailed) {
				Note(Trc("Could not save FightWithMePaths_user.ini. Check file access, then change the setting again."));
			}

            if (Group(Trc("Party balance"), 0xF24E)) {
                if (Rows("partyBalance")) {
                    Toggle(Trc("Scale follower damage"), draft.partyBalance,
                        Trc("Reduce follower weapon, spell and ability damage as the fighting party grows. Outnumbering enemies adds a further reduction. Player damage is unchanged."));
                    Im::BeginDisabled(!draft.partyBalance);
                    Slider(Trc("Per extra follower"), draft.partyExtraPenalty, 0.0f, 25.0f, "%.0f%%",
                        Trc("Damage reduction for each fighting follower after the first."));
                    Slider(Trc("Outnumbering enemies"), draft.partyAdvantagePenalty, 0.0f, 25.0f, "%.0f%%",
                        Trc("Additional reduction for each follower beyond the number of nearby enemies."));
                    Slider(Trc("Minimum damage"), draft.partyMinimumDamage, 10.0f, 100.0f, "%.0f%%",
                        Trc("Follower damage never falls below this percentage. One follower always deals full damage."));
                    Im::EndDisabled();
                    Im::EndTable();
                }
            }

			if (Group(Trc("Leveling"), 0xF0AA)) {
				if (Checks("leveling")) {
					Check(Trc("Companions level up by fighting"), draft.leveling,
						Trc("When a fight is over, each companion who was in it earns XP for every enemy the party brought down, whoever struck last. Each level costs more than the "
							"last, and each brings health, magicka or stamina and skill by their role. Off: everyone goes back to the level their own mod gave them."));
					Im::BeginDisabled(!draft.leveling);
					Check(Trc("Never above your level"), draft.levelCap,
						Trc("A companion at your level gains no XP until you level up. One who was already above it keeps their level and waits for you."));
					Im::EndDisabled();
					Im::EndTable();
				}
				Im::BeginDisabled(!draft.leveling);
				if (Rows("levelingXP")) {
					Slider(Trc("XP per enemy level"), draft.xpPerEnemyLevel, 1.0f, 20.0f, "%.1f",
						Trc("An even enemy's worth: this times the companion's level. Each level an enemy is above them adds 5%, up to 1.75 times; each level below takes 5% off, "
							"down to a quarter."));
					Slider(Trc("Boss worth"), draft.xpBossMult, 1.0f, 6.0f, "x%.1f", Trc("How many times more a boss is worth."));
					Slider(Trc("Crowd"), draft.xpCrowd, 0.3f, 1.0f, "%.2f",
						Trc("How much a crowd adds: the enemy worth the most counts in full and each after it this share of the one before, so at 0.75 a fight is never worth more "
							"than four of its best enemy. At 1 every enemy counts in full."));
					Slider(Trc("Climb"), draft.xpClimb, 0.0f, 0.2f, "%.2f",
						Trc("How much harder each level gets: a level costs this share more than one of yours would for every level they already have. At 0.05, with fights of "
							"four even enemies, a level takes about 6 fights at level 10, 8 at 30 and 11 at 50. At 0 it is 3 or 4 however high they are. Fewer levels means fewer "
							"path points too."));
					Im::EndTable();
				}
				Im::EndDisabled();
			}

			if (Group(Trc("Panel"), 0xF11C)) {
				if (Rows("pathsPanel")) {
					Im::TableNextRow();
					Im::TableNextColumn();
					Im::AlignTextToFramePadding();
					Im::TextUnformatted(Trc("Open the panel"));
					Im::TableNextColumn();
					const bool waiting = capturing.load() && captureSlot == 0;
					const auto held = captureModifier.load();
					const auto name = !waiting ? HotkeyName(draft.planKey) : (held ? std::format("{} + ...", ModifierName(held)) : std::string(Trc("Press a key...")));
					if (ThemeButton(name.c_str(), { -1, 0 })) {
						if (waiting) {
							CancelCapture();
						} else {
							StartCapture(0);
						}
					}
					Help(Trc("Select it, then press a key, holding Shift, Ctrl or Alt with it for a combination. Esc cancels; Delete clears."));

					Im::TableNextRow();
					Im::TableNextColumn();
					Im::AlignTextToFramePadding();
					Im::TextUnformatted(Trc("Gamepad"));
					Im::TableNextColumn();
					int current = 0;
					for (int i = 0; i < static_cast<int>(padChoices.size()); ++i) {
						if (padChoices[static_cast<std::size_t>(i)].button == draft.planGamepadKey) {
							current = i;
						}
					}
					Im::SetNextItemWidth(-1);
					if (ThemeCombo("##pathsPad", Trc(padChoices[static_cast<std::size_t>(current)].name))) {
						for (int i = 0; i < static_cast<int>(padChoices.size()); ++i) {
							if (Im::Selectable(Trc(padChoices[static_cast<std::size_t>(i)].name), i == current)) {
								draft.planGamepadKey = padChoices[static_cast<std::size_t>(i)].button;
								dirty = true;
							}
						}
						Im::EndCombo();
					}
					Help(Trc("Hold LB and press the chosen button."));

					// Both settings pages edit the same live theme when paired.
					{
                        const int theme=Settings::Theme();
						Im::TableNextRow();
						Im::TableNextColumn();
						Im::AlignTextToFramePadding();
						Im::TextUnformatted(Trc("Theme"));
						Im::TableNextColumn();
						Im::SetNextItemWidth(-1);
						if (ThemeCombo("##pathsTheme", Trc(UITheme::Name(theme)))) {
							for (int i = 0; i < UITheme::kCount; ++i) {
								if (Im::Selectable(Trc(UITheme::Name(i)), theme == i) && theme != i) {
									draft.uiTheme = i;
                                    shareTheme = true;
									dirty = true;
								}
							}
							Im::EndCombo();
						}
						Help(Trc("Vanilla: charcoal and silver. Oxblood: the original red. Nordic: engraved stone, beveled metal and inset highlights."));
					}
					Im::EndTable();
				}
				if (Checks("pathsLook")) {
					Check(Trc("Ability effects"), draft.abilityEffects,
						Trc("What the abilities look like in the world: spirit beasts, circles, bursts and weapon flames. Off keeps what they do and drops how they look."));
					Check(Trc("Compact summons"), draft.compactEffects, Trc("Smaller spirit heads, Dragon's Breath, Azura's apparition and Last Bastion for tighter spaces. Off restores their original size. Applies to new casts."));
                    Check(Trc("Notices"), draft.notices, Trc("Short lines on the left of the screen when a companion uses an ability or has points to spend."));
					Check(Trc("Tooltips"), draft.tooltips, Trc("Describe each control when you point at it or move to it with the pad. Turn off once you know them."));
					Check(Trc("Menu sounds"), draft.sounds, Trc("The game's own menu sounds when you point at and choose in the panel."));
					Im::EndTable();
				}
			}
			Space();
			if (ThemeButton(Trc("Open the panel"))) {
				Open();
			}
			Help(Trc("The companion under your crosshair comes first; Tab moves to the next."));
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
			// Our tree, drawn over Fight With Me's plan, reads the pad as if our panel were open.
			const bool open = (plan && plan->IsOpen) || ::GetTickCount64() < treeInPlanUntil.load();
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
				if (key == kEscape && roleListOpen) {
					roleListBack = true;
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
		MF::AddSectionItem(std::string(Tr("Party")), RenderParty);
		MF::AddSectionItem(std::string(Tr("Targeting")), RenderTargeting);
		MF::AddSectionItem(std::string(Tr("Battle plan")), RenderBattlePlan);
		MF::AddSectionItem(std::string(Tr("Controls")), RenderControls);
		plan = MF::AddWindow(RenderPlan, true);
		inputRegistration.reset(MF::AddInputEvent(OnInput));
		hudRegistration.reset(MF::AddHudElement(RenderHUD));
		Log::Info("[Menu] Battle plan and four settings pages registered (Menu Framework {:.2f})."sv, version());
	}

	void DrawInPlan(FightWithMePathsAPI::Plan& a_plan)
	{
		DrawPathsInPlan(a_plan);
	}

	void RegisterPaths(PathsPage a_page)
	{
		using VersionFunction = float (*)();
		const auto version = GetMenuFrameworkFunction<VersionFunction>("GetMenuFrameworkVersion");
		if (!::GetModuleHandleW(L"SKSEMenuFramework.dll") || !version || version() < 3.0f) {
			Log::Warn("[Menu] SKSE Menu Framework 3 not found ({}): no panel and no settings page. The INI and the save still apply."sv,
				!::GetModuleHandleW(L"SKSEMenuFramework.dll") ? "DLL not loaded"sv : (!version ? "no version export"sv : "older than 3"sv));
			return;
		}
		pathsPage = std::move(a_page);
		MF::SetSection("Path of the Companions");
		MF::AddSectionItem(std::string(Tr("Settings")), RenderPathsSettings);
		plan = MF::AddWindow(RenderPaths, true);
		inputRegistration.reset(MF::AddInputEvent(OnInput));
		hudRegistration.reset(MF::AddHudElement(RenderHUD));
		Log::Info("[Menu] Panel and settings page registered (Menu Framework {:.2f})."sv, version());
	}

	bool Available() noexcept
	{
		return plan != nullptr;
	}

	void Open()
	{
		if (!plan) {
			RE::SendHUDMessage::ShowHUDMessage(Host::Abilities() ? Trc("Paths: the panel needs SKSE Menu Framework 3.") :
			                                                        Trc("Fight With Me: the battle plan needs SKSE Menu Framework 3."));
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

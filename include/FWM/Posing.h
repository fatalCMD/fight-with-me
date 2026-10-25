#pragma once

// Helpers for Fight With Me's own effect meshes, the temporary effects whose nodes the plugin moves
// every frame (Elemental Burst's blasts, Magnus's orb): putting a node where it belongs in the world
// under whatever transform the effect gave its root, colouring and fading its glow, drifting its noise,
// and a real point light that goes with it. Main thread.

namespace FWM::Posing
{
	// The effect's root while it is in the scene, else null.
	[[nodiscard]] RE::NiAVObject* RootOf(RE::BSTempEffectParticle* a_effect);

	// A child of the root put at a_at in the world, turned by a_turn and a_scale big there.
	void PlaceInWorld(RE::NiAVObject* a_root, RE::NiAVObject* a_node, const RE::NiPoint3& a_at, const RE::NiMatrix3& a_turn, float a_scale);

	// After moving it: its world transform and bounds brought up to date.
	void Refresh(RE::NiAVObject* a_node);

	// A glow given a material of its own. The game hands every shape with the same settings one shared
	// material, across every copy of a mesh too, so colouring one shape would colour them all.
	void Own(RE::BSShaderProperty* a_shader);

	// Every shader drawing a node's shapes, each glow owning its material, and whether one draws a tiling
	// texture that drifts (flame, cloud, frost cloud, swirls, embers).
	[[nodiscard]] std::vector<RE::BSShaderProperty*> ShadersOf(RE::NiAVObject* a_node);
	[[nodiscard]] bool                               Noisy(RE::BSShaderProperty* a_shader);

	// The glow's colour and strength. A glow that adds its light fades by its brightness, its alpha held
	// whole: the game copies the base colour's alpha into the shader's own a frame late and multiplies
	// the two, so fading by alpha came out dark on the first frame and squared after.
	void Paint(const std::vector<RE::BSShaderProperty*>& a_shaders, const std::array<float, 3>& a_color, float a_alpha);

	// And one laid over the scene (dust), which can only fade by its alpha.
	void PaintBlended(const std::vector<RE::BSShaderProperty*>& a_shaders, const std::array<float, 3>& a_color, float a_alpha);

	// Whether every shape under a node adds its light rather than laying over the scene.
	[[nodiscard]] bool AddsLight(RE::NiAVObject* a_node);

	// Its texture moved a_v along its length: the noise drifting.
	void Drift(const std::vector<RE::BSShaderProperty*>& a_shaders, float a_v);

	// One real point light under an effect's root, made on first use, moved and dimmed every frame
	// (a_fade: a torch is about 1), and put out before the effect is gone.
	struct Lamp
	{
		RE::NiPointer<RE::NiPointLight> light;
	};
	void Shine(Lamp& a_lamp, RE::NiAVObject* a_root, const RE::NiPoint3& a_at, const std::array<float, 3>& a_color, float a_fade, float a_reach);
	void Out(Lamp& a_lamp);

	// A sound once at a point.
	void PlayAt(RE::BGSSoundDescriptorForm* a_sound, const RE::NiPoint3& a_at);
}

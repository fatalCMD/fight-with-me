#include "FWM/Posing.h"

#include "FWM/Log.h"

namespace FWM::Posing
{
	RE::NiAVObject* RootOf(RE::BSTempEffectParticle* a_effect)
	{
		auto* root = a_effect ? a_effect->particleObject.get() : nullptr;
		return root && root->parent ? root : nullptr;
	}

	void PlaceInWorld(RE::NiAVObject* a_root, RE::NiAVObject* a_node, const RE::NiPoint3& a_at, const RE::NiMatrix3& a_turn, float a_scale)
	{
		const auto& parent = a_root->world;
		const auto  inverse = parent.rotate.Transpose();
		a_node->local.rotate = inverse * a_turn;
		a_node->local.translate = inverse * (a_at - parent.translate) * (1.0f / parent.scale);
		a_node->local.scale = a_scale / parent.scale;
	}

	void Refresh(RE::NiAVObject* a_node)
	{
		RE::NiUpdateData update{ 0.0f, RE::NiUpdateData::Flag::kNone };
		a_node->Update(update);
	}

	void Own(RE::BSShaderProperty* a_shader)
	{
		auto* glow = netimmerse_cast<RE::BSEffectShaderProperty*>(a_shader);
		auto* shared = glow ? glow->material : nullptr;
		auto* copy = shared ? shared->Create() : nullptr;
		if (!copy) {
			return;
		}
		copy->CopyMembers(shared);
		glow->SetMaterial(copy, true);
		// Asked for a unique one, the shader makes its own from ours, which was only the pattern.
		if (glow->material != copy) {
			copy->~BSShaderMaterial();
			RE::free(copy);
		}
	}

	std::vector<RE::BSShaderProperty*> ShadersOf(RE::NiAVObject* a_node)
	{
		std::vector<RE::BSShaderProperty*> out;
		if (a_node) {
			RE::BSVisit::TraverseScenegraphGeometries(a_node, [&](RE::BSGeometry* a_geometry) {
				if (auto* shader = a_geometry->GetGeometryRuntimeData().shaderProperty.get()) {
					Own(shader);
					out.push_back(shader);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}
		return out;
	}

	bool Noisy(RE::BSShaderProperty* a_shader)
	{
		auto* glow = netimmerse_cast<RE::BSEffectShaderProperty*>(a_shader);
		auto* material = glow ? static_cast<RE::BSEffectShaderMaterial*>(glow->material) : nullptr;
		std::string path = material && material->sourceTexturePath.c_str() ? material->sourceTexturePath.c_str() : "";
		std::ranges::transform(path, path.begin(), [](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
		// The game's tiling effect textures our meshes drift: flame, cloud, frost cloud, swirls, embers.
		constexpr std::array<std::string_view, 6> kDrifting{ "fxfirescrolltilegrey", "cloudtile.dds", "cloudtilefrostsmall", "darkswirls", "darkswirlsstrip", "fxembers" };
		return std::ranges::any_of(kDrifting, [&](std::string_view a_name) { return path.find(a_name) != std::string::npos; });
	}

	void Paint(const std::vector<RE::BSShaderProperty*>& a_shaders, const std::array<float, 3>& a_color, float a_alpha)
	{
		for (auto* shader : a_shaders) {
			if (auto* glow = netimmerse_cast<RE::BSEffectShaderProperty*>(shader); glow && glow->material) {
				static_cast<RE::BSEffectShaderMaterial*>(glow->material)->baseColor = { a_color[0] * a_alpha, a_color[1] * a_alpha, a_color[2] * a_alpha, 1.0f };
			}
		}
	}

	void PaintBlended(const std::vector<RE::BSShaderProperty*>& a_shaders, const std::array<float, 3>& a_color, float a_alpha)
	{
		for (auto* shader : a_shaders) {
			if (auto* glow = netimmerse_cast<RE::BSEffectShaderProperty*>(shader); glow && glow->material) {
				static_cast<RE::BSEffectShaderMaterial*>(glow->material)->baseColor = { a_color[0], a_color[1], a_color[2], a_alpha };
			}
		}
	}

	bool AddsLight(RE::NiAVObject* a_node)
	{
		bool adds = true;
		if (a_node) {
			RE::BSVisit::TraverseScenegraphGeometries(a_node, [&](RE::BSGeometry* a_geometry) {
				// Blended onto the scene unless its far side is taken whole (NiAlphaProperty's destination ONE).
				const auto& blend = a_geometry->GetGeometryRuntimeData().alphaProperty;
				if (blend && ((blend->alphaFlags >> 5) & 0xF) != 0) {
					adds = false;
					return RE::BSVisit::BSVisitControl::kStop;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}
		return adds;
	}

	void Drift(const std::vector<RE::BSShaderProperty*>& a_shaders, float a_v)
	{
		for (auto* shader : a_shaders) {
			if (auto* glow = netimmerse_cast<RE::BSEffectShaderProperty*>(shader); glow && glow->material) {
				for (auto& offset : glow->material->texCoordOffset) {
					offset.y = a_v;
				}
			}
		}
	}

	void Shine(Lamp& a_lamp, RE::NiAVObject* a_root, const RE::NiPoint3& a_at, const std::array<float, 3>& a_color, float a_fade, float a_reach)
	{
		auto* scene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		if (!a_root || !scene) {
			return;
		}
		if (!a_lamp.light) {
			auto* node = a_root->AsNode();
			if (!node || a_fade <= 0.0f) {
				return;
			}
			a_lamp.light.reset(RE::NiPointLight::Create());
			if (!a_lamp.light) {
				return;
			}
			a_lamp.light->GetLightRuntimeData().ambient = {};
			node->AttachChild(a_lamp.light.get(), true);
			RE::ShadowSceneNode::LIGHT_CREATE_PARAMS params{};
			params.dynamic = true;
			params.affectLand = true;
			params.affectWater = true;
			params.neverFades = true;
			params.fov = 1.0f;
			params.falloff = 1.0f;
			params.nearDistance = 5.0f;
			scene->AddLight(a_lamp.light.get(), params);
			static bool told = false;
			if (!told) {
				told = true;
				Log::Info("[Effects] Fight With Me's own effects throw their own light (a dynamic point light while each lasts)."sv);
			}
		}
		auto& data = a_lamp.light->GetLightRuntimeData();
		if (std::abs(data.radius.x - a_reach) > 1.0f) {
			data.radius = { a_reach, a_reach, a_reach };
			a_lamp.light->SetLightAttenuation(a_reach);
		}
		data.diffuse = { a_color[0], a_color[1], a_color[2] };
		data.fade = a_fade;
		PlaceInWorld(a_root, a_lamp.light.get(), a_at, {}, 1.0f);
		Refresh(a_lamp.light.get());
	}

	void Out(Lamp& a_lamp)
	{
		if (!a_lamp.light) {
			return;
		}
		if (auto* scene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0]) {
			scene->RemoveLight(a_lamp.light.get());
		}
		if (auto* parent = a_lamp.light->parent) {
			parent->DetachChild(a_lamp.light.get());
		}
		a_lamp.light.reset();
	}

	void PlayAt(RE::BGSSoundDescriptorForm* a_sound, const RE::NiPoint3& a_at)
	{
		RE::BSSoundHandle sound;
		if (auto* audio = RE::BSAudioManager::GetSingleton(); audio && a_sound && audio->GetSoundHandle(sound, a_sound)) {
			sound.SetPosition(a_at);
			sound.Play();
		}
	}
}

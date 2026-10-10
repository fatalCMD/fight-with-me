#include "FWM/Enemies.h"

#include "FWM/Game.h"
#include "FWM/Party.h"
#include "FWM/Settings.h"

namespace FWM::Enemies
{
	namespace
	{
		std::vector<Profile> enemies;

		constexpr float kRefreshSeconds = 3.0f;

		float WeaponDamage(RE::Actor* a_actor)
		{
			float best = 0.0f;
			for (bool left : { false, true }) {
				if (auto* form = a_actor->GetEquippedObject(left)) {
					if (auto* weapon = form->As<RE::TESObjectWEAP>()) {
						best = std::max(best, static_cast<float>(weapon->GetAttackDamage()));
					}
				}
			}
			return best;
		}

		void Describe(Profile& a_profile, RE::Actor* a_actor)
		{
			a_profile.name = Game::Name(a_actor);
			a_profile.level = a_actor->GetLevel();
			a_profile.boss = Game::IsBoss(a_actor);

			const auto loadout = Game::LoadoutOf(a_actor);
			const auto magic = Game::MagicOf(a_actor);
			a_profile.archer = loadout == Game::Loadout::kBow;
			a_profile.caster = loadout == Game::Loadout::kStaff || loadout == Game::Loadout::kSpells || magic.offensive;
			a_profile.healer = magic.healOther;

			// A rough, comparable figure: what the player would call "the big one".
			const float health = Game::Maximum(a_actor, RE::ActorValue::kHealth);
			const float armor = Game::Current(a_actor, RE::ActorValue::kDamageResist);
			a_profile.threat = static_cast<float>(a_profile.level) * 3.0f + health / 12.0f + armor / 25.0f +
			                   WeaponDamage(a_actor) * 1.5f + (a_profile.caster ? 10.0f : 0.0f) + (a_profile.boss ? 25.0f : 0.0f);
			a_profile.refreshIn = kRefreshSeconds;
		}

		bool Hostile(RE::Actor* a_actor, RE::PlayerCharacter* a_player)
		{
			if (!Game::Usable(a_actor) || Party::IsPartyActor(a_actor)) {
				return false;
			}
			return a_actor->IsHostileToActor(a_player) || a_actor->IsInCombat();
		}

		void Collect(RE::Actor* a_fighter, std::vector<RE::ActorHandle>& a_out)
		{
			auto* group = a_fighter ? a_fighter->GetCombatGroup() : nullptr;
			if (!group) {
				return;
			}
			RE::BSReadLockGuard lock(group->lock);
			for (const auto& target : group->targets) {
				if (std::find(a_out.begin(), a_out.end(), target.targetHandle) == a_out.end()) {
					a_out.push_back(target.targetHandle);
				}
			}
		}
	}

	void Update(float a_dt)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			enemies.clear();
			return;
		}

		std::vector<RE::ActorHandle> handles;
		Collect(player, handles);
		for (auto& member : Party::Members()) {
			if (auto actor = member.handle.get(); actor && actor->IsInCombat()) {
				Collect(actor.get(), handles);
			}
		}

		for (auto& profile : enemies) {
			profile.seen = false;
		}
		for (const auto& handle : handles) {
			auto actor = handle.get();
			if (!actor || !Hostile(actor.get(), player)) {
				continue;
			}
			auto* profile = Find(actor->GetFormID());
			if (!profile) {
				Profile fresh;
				fresh.handle = handle;
				fresh.id = actor->GetFormID();
				Describe(fresh, actor.get());
				enemies.push_back(std::move(fresh));
				profile = &enemies.back();
			} else {
				profile->refreshIn -= a_dt;
				if (profile->refreshIn <= 0.0f) {
					Describe(*profile, actor.get());
				}
			}
			profile->seen = true;
			profile->health = Game::Fraction(actor.get(), RE::ActorValue::kHealth);
			if (auto* controller = Game::Controller(actor.get())) {
				profile->target = controller->targetHandle;
			} else {
				profile->target = {};
			}
		}
		std::erase_if(enemies, [](const Profile& a_profile) { return !a_profile.seen; });
	}

	void Clear()
	{
		enemies.clear();
	}

	std::vector<Profile>& All()
	{
		return enemies;
	}

	Profile* Find(RE::FormID a_id)
	{
		for (auto& profile : enemies) {
			if (profile.id == a_id) {
				return &profile;
			}
		}
		return nullptr;
	}

	Profile* Find(const RE::ActorHandle& a_handle)
	{
		for (auto& profile : enemies) {
			if (profile.handle == a_handle) {
				return &profile;
			}
		}
		return nullptr;
	}
}

#include "FWM/EffectAudio.h"
#include "FWM/Log.h"
#include "FWM/Settings.h"

namespace FWM::EffectAudio
{
	namespace
	{
		struct Voice { Soundscape::Cue cue; RE::BSSoundHandle handle; bool started{}, done{}; };
		struct Group {
			ID id{}; RE::ActorHandle source; RE::TESObjectCELL* cell{}; RE::TESWorldSpace* world{};
			RE::NiPoint3 at; float age{}, seconds{}; bool follow{}, ending{}; std::vector<Voice> voices;
		};
		std::vector<Group> groups;
		ID next=1;
		std::unordered_map<RE::FormID,RE::BGSSoundDescriptorForm*> descriptors;
		RE::BGSSoundDescriptorForm* Descriptor(RE::FormID id)
		{
			if (const auto found=descriptors.find(id); found!=descriptors.end()) return found->second;
			auto* data=RE::TESDataHandler::GetSingleton();
			auto* form=data?data->LookupForm<RE::BGSSoundDescriptorForm>(id,"Skyrim.esm"):nullptr;
			descriptors.emplace(id,form);
			if (!form) Log::Warn("[Audio] Skyrim sound {:08X} missing; skipping cue."sv,id);
			return form;
		}
		void Stop(Group& group) { for(auto& voice:group.voices) if(voice.handle.IsValid()) voice.handle.Stop(); }
	}
	ID Begin(Soundscape::Kind kind, RE::Actor* source, const RE::NiPoint3& at, float seconds, int rank, bool follow, int element)
	{
		if (!source || source->IsDead() || !source->Is3DLoaded() || !Settings::Get().enabled || !Settings::Get().abilityEffects ||
			!std::isfinite(seconds) || seconds<=0 || !std::isfinite(at.x) || !std::isfinite(at.y) || !std::isfinite(at.z)) return 0;
		if(groups.size()>=32) return 0;
		Group group; group.id=next++; group.source=source->GetHandle(); group.cell=source->GetParentCell(); group.world=source->GetWorldspace();
		group.at=at; group.seconds=std::min(seconds,120.0f); group.follow=follow;
		for(const auto& cue:Soundscape::Plan(kind,rank,element)) group.voices.push_back({cue,{},{},{}});
		groups.push_back(std::move(group)); return groups.back().id;
	}
	void End(ID id) { for(auto& group:groups)if(group.id==id){group.seconds=std::min(group.seconds,group.age+.25f);group.ending=true;} }
	void Move(ID id,const RE::NiPoint3& at) { for(auto& group:groups)if(group.id==id)group.at=at; }
	void Clear() { for(auto& group:groups)Stop(group);groups.clear(); }
	void Tick(float dt)
	{
		if (!Settings::Get().enabled || !Settings::Get().abilityEffects) { Clear(); return; }
		if (!std::isfinite(dt) || dt<=0) return;
		auto* player=RE::PlayerCharacter::GetSingleton();
		std::erase_if(groups,[&](Group& group){
			group.age+=dt; auto source=group.source.get();
			const bool sameWorld=player&&group.cell&&(player->GetParentCell()==group.cell||(!group.cell->IsInteriorCell()&&player->GetWorldspace()==group.world));
			if(group.age>=group.seconds||!source||source->IsDead()||source->IsDisabled()||!source->Is3DLoaded()||
				!group.cell||!group.cell->IsAttached()||source->GetParentCell()!=group.cell||!sameWorld){Stop(group);return true;}
			if(group.follow)group.at=source->GetPosition();
			return false;
		});
		if(!player)return;
		// Several circles blend down instead of stacking into a loud party-wide drone.
		float beds=0;
		for(const auto& group:groups)for(const auto& voice:group.voices)
			if(voice.cue.loop&&!voice.cue.channel&&Soundscape::Gain(voice.cue,group.age,group.seconds)>0&&Soundscape::DistanceGain(player->GetPosition().GetDistance(group.at),true)>.01f)beds+=1;
		const float bedGain=1.0f/std::max(1.0f,beds);
		auto* manager=RE::BSAudioManager::GetSingleton();
		// A verified mono spatial output model also localizes the game's stereo/2D magic loops.
		auto* spatial=RE::TESForm::LookupByID<RE::BGSSoundOutput>(0x0F1B12);
		for(auto& group:groups)for(auto& voice:group.voices){
			const auto& cue=voice.cue;
			if(voice.done||group.age<Soundscape::Start(cue,group.seconds))continue;
			if(group.age>=Soundscape::End(cue,group.seconds)){if(voice.handle.IsValid())voice.handle.Stop();voice.done=true;continue;}
			const float gain=Soundscape::Gain(cue,group.age,group.seconds)*Soundscape::DistanceGain(player->GetPosition().GetDistance(group.at),cue.loop&&!cue.channel)*(cue.loop&&!cue.channel?bedGain:1);
			if(!voice.started){
				if(group.ending){voice.done=true;continue;}
				voice.started=true;
				auto* descriptor=Descriptor(cue.form);
				if(!manager||!descriptor||!manager->GetSoundHandle(voice.handle,descriptor)){voice.done=true;continue;}
				if(spatial)voice.handle.SetOutputModel(spatial);
				voice.handle.SetPosition(group.at); voice.handle.SetVolume(gain); voice.handle.SetFrequency(cue.pitch); voice.handle.Play();
			}else{voice.handle.SetPosition(group.at);voice.handle.SetVolume(gain);}
		}
	}
}

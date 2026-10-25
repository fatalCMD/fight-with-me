#include "FWM/Soundscape.h"
#include <algorithm>
#include <cmath>

namespace FWM::Soundscape
{
	std::vector<Cue> Plan(Kind kind, int rank, int element)
	{
		const bool grand = rank >= 3;
		auto cues = [&]() -> std::vector<Cue> { switch (kind) {
		case Kind::Circle: return { {0x056623,.35f,0,.055f,true}, {0x04FA3B,-.65f,.65f,.10f} };
		case Kind::Rejuvenation: return { {0x03D117,.45f,0,.045f,true}, {0x04FA3B,-.65f,.65f,.10f} };
		case Kind::Renewal: return { {0x0DCE89,.25f,0,.045f,true}, {0x03F363,.4f,0,.025f,true} };
		case Kind::WeakWill: return { {grand?0x0FF782u:0x0571B2u,.08f,1.8f,grand?.42f:.32f},
			{0x056AC3,.7f,0,.025f,true}, {0x04FA3B,1.9f,1.3f,.15f} };
		case Kind::Chaos: return { {0x04E792,.06f,1.8f,grand?.40f:.30f}, {0x0ADE24,.65f,1.6f,grand?.30f:.22f},
			{0x056AC3,.7f,0,.04f,true}, {0x03F25A,-.6f,.6f,.12f} };
		case Kind::Sunder: return { {0x0F69C3,0,.9f,grand?.65f:.48f}, {0x03C8A2,.18f,1.25f,grand?.18f:.12f} };
		case Kind::Rain: return { {0x053A5E,0,.8f,.35f}, {0x07D014,.45f,1.0f,grand?.25f:.18f} };
		case Kind::Arrow: return { {0x03DAE8,0,.6f,.25f} };
		// Skyrim's short Hrothgar wind rush: a gust, without restoration tones.
		case Kind::SecondWind: return { {0x0A0D90,.12f,2.3f,grand?.45f:.32f,false,grand?.92f:1.05f} };
		case Kind::Riposte: return { {0x03C73C,0,.8f,grand?.60f:.46f,false,grand?.84f:1.f} };
        case Kind::RiposteImpact: return { {0x0DAB80,0,.9f,grand?.42f:.26f,false,grand?.78f:.95f} };
		case Kind::Deadeye: return { {0x07BBAD,0,1.25f,.24f} };
		case Kind::Volley: return { {0x0A0D90,0,.32f,.12f,false,1.15f} };
        case Kind::ExplosiveImpact: return { {0x02518E,0,1.2f,grand?.6f:.44f,false,grand?.78f:1.f} };
		case Kind::Windstep: return { {0x0A0D90,0,1.1f,grand?.35f:.25f,false,1.18f} };
        case Kind::WindstepBurst: return { {0x0A0D90,0,.9f,.38f,false,.78f} };
        case Kind::Pin: return { {0x03D11B,0,.75f,.30f} };
		case Kind::Surge: return { {0x07D014,0,1.8f,.35f} };
		case Kind::Berserk: return { {0x0A28C4,0,2.2f,.44f} };
		case Kind::Ward: return { {0x04E792,.1f,1.8f,.22f,false,.85f}, {0x0ADE24,1.4f,1.6f,.28f,false,.9f} };
		case Kind::DragonBreath: return {{0x10EE59u,0,3.f,grand?.58f:.45f,false,grand?.8f:1.f},
            {element==1?0x0DAB87u:element==2?0x03F205u:0x03EF56u,0,0,grand?.48f:.36f,true,1.f,true}};
		case Kind::WarCry: return { {0x0FB9A1u,0,3.0f,grand ? .70f : .60f,false,grand ? .88f : .95f} };
		case Kind::Magnus: return { {0x101E65,0,2.1f,.48f}, {0x101E63,.25f,0,.055f,true}, {0x10F852,-.8f,.8f,.20f} };
		case Kind::Wolf: return { {0x01706F,0,1.35f,grand?.60f:.48f,false,grand?.72f:.84f},
			{grand?0x0F93EEu:0x0E7521u,.60f,1.3f,grand?.62f:.50f,false,grand?.78f:.88f} };
		case Kind::Taunt: return { {0x0F95BC,0,3.0f,grand?.78f:.65f,false,grand?.82f:1.0f} };
		case Kind::Impact: return { {0x101F41,0,1.8f,.22f,false,.85f} };
		case Kind::Disarm: return { {0x03C73C,0,.75f,.25f} };
		default: return {};
		} }();
		// Final ranks have an additional, short signature. Sustained beds stay quiet.
		if (grand) {
			for (auto& cue : cues) if (!cue.loop && cue.at >= 0) cue.volume = std::min(.85f, cue.volume * 1.12f);
			switch (kind) {
			case Kind::SecondWind:
				cues.push_back({0x0A0D90,.49f,1.8f,.22f,false,.78f}); break;
			case Kind::Circle: case Kind::Renewal:
				cues.push_back({0x0CD671,.18f,1.6f,.24f,false,.88f}); break;
			case Kind::Rejuvenation: case Kind::Ward:
				cues.push_back({0x0571B2,.16f,1.7f,.22f,false,.86f}); break;
			case Kind::DragonBreath: case Kind::WeakWill: case Kind::Chaos: case Kind::Magnus: case Kind::Surge:
				cues.push_back({0x04E792,.15f,1.75f,.24f,false,.78f}); break;
			case Kind::Sunder: case Kind::Disarm:
				cues.push_back({0x0F69C3,.16f,.9f,.24f,false,.76f}); break;
			case Kind::ExplosiveImpact: cues.push_back({0x0F69C3,.04f,.85f,.32f,false,.78f});break;
            case Kind::Rain: case Kind::WarCry:
				cues.push_back({0x0A0F52,.12f,1.2f,.24f,false,.82f}); break;
			case Kind::Deadeye: case Kind::Pin:
				cues.push_back({0x0ADE24,.12f,1.2f,.20f,false,.85f}); break;
			case Kind::Berserk: case Kind::Wolf: case Kind::Taunt:
				cues.push_back({0x01706F,.12f,1.6f,.24f,false,.65f}); break;
			default: break;
			}
		}
		return cues;
	}
	float Start(const Cue& cue, float duration) { return cue.at < 0 ? std::max(0.0f, duration + cue.at) : cue.at; }
	float End(const Cue& cue, float duration) { return std::min(duration, cue.seconds > 0 ? Start(cue,duration)+cue.seconds : duration); }
	float Gain(const Cue& cue, float age, float duration)
	{
		if (!std::isfinite(age) || !std::isfinite(duration) || duration <= 0) return 0;
		const float since=age-Start(cue,duration), left=End(cue,duration)-age;
		if (since < 0 || left <= 0) return 0;
		const float in=cue.loop ? std::clamp(since/(cue.channel?.10f:.8f),0.0f,1.0f) : 1;
		const float out=std::clamp(left/(cue.channel?.25f:cue.loop?2.4f:.2f),0.0f,1.0f);
		return cue.volume*in*out;
	}
	float DistanceGain(float distance, bool loop)
	{
		const float reach=loop?1200.0f:3000.0f;
		if (!std::isfinite(distance) || distance<0 || distance>=reach) return 0;
		const float t=std::clamp((distance-100.0f)/(reach-100.0f),0.0f,1.0f);
		return 1-t*t*(3-2*t);
	}
}

#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace FWM::SupportArt
{
    enum class Kind { Guard, Vitality, Focus, Mark, Binding, Surge, Rally, Wind, Impact, LevelUp, Fire, Steel, Spirit };
    inline constexpr bool Weapon(Kind kind) { return kind >= Kind::Fire; }
    inline constexpr std::size_t MoteCount(Kind kind) { return kind == Kind::Fire ? 24 : (Weapon(kind) ? 0 : (kind == Kind::LevelUp ? 26 : 12)); }
    inline float Smooth(float x) { x = std::clamp(x, 0.0f, 1.0f); return x*x*(3.0f-2.0f*x); }
    struct Frame {
        float alpha{}, turn{}, rise{}, drift{}, scale{1};
        float coreTurn{}, coreScale{1}, coreDrift{};
    };
    inline Frame At(float age, float remaining, Kind kind)
    {
        if (!std::isfinite(age) || !std::isfinite(remaining) || age < 0 || remaining <= 0) return {};
        const bool impact = kind == Kind::Impact;
        const float envelope = Smooth(age/(impact ? .045f : .18f))*Smooth(remaining/(impact ? .22f : .55f));
        Frame f;
        f.alpha = envelope*(.88f+.12f*std::sin(age*2.4f));
        f.turn = age*.34f;
        f.drift = age*.22f;
        f.coreScale = 1.0f+.025f*std::sin(age*2.2f);
        switch (kind) {
        case Kind::Guard: f.turn=age*.32f; break;
        case Kind::Vitality: f.coreTurn=-age*.14f; f.drift=age*.35f; break;
        case Kind::Focus: f.coreTurn=-age*.28f; f.turn=age*.45f; break;
        case Kind::Mark: f.coreScale=1+.055f*std::sin(age*3.0f); break;
        case Kind::Binding: f.coreScale=1+.26f*(1-Smooth(age/.65f))+.018f*std::sin(age*2); f.turn=-age*.22f; break;
        case Kind::Surge: f.coreTurn=-age*.32f; f.turn=age*.58f; f.coreDrift=age*.28f; break;
        case Kind::Rally: f.coreTurn=age*.12f; f.coreScale=1+.045f*std::sin(age*2.8f); break;
        case Kind::LevelUp: f.coreTurn=-age*.24f; f.drift=age*.45f; break;
        case Kind::Wind:
            f.scale=.65f+1.8f*(1-std::exp(-age*2)); f.coreScale=f.scale;
            f.alpha*=std::exp(-age*.45f); f.turn=age*.65f; break;
        case Kind::Impact:
            f.scale=.65f+1.1f*(1-std::exp(-age*6)); f.coreScale=f.scale;
            f.alpha*=std::exp(-age*3.5f); f.turn=age*.8f; break;
        case Kind::Fire: case Kind::Steel: case Kind::Spirit:
            // Animate energy along the blade, never turn/scale the trail off its weapon.
            f.turn=0; f.coreScale=1;
            f.drift=age*(kind==Kind::Spirit ? -.24f : .65f);
            f.coreDrift=age*(kind==Kind::Steel ? -.65f : .45f);
            f.alpha*=.90f+.10f*std::sin(age*(kind==Kind::Steel ? 8.0f : 6.0f));
            break;
        }
        return f;
    }
    struct Mote { float turn{}, rise{}, alpha{}, scale{}; };
    inline Mote Particle(float age, float remaining, Kind kind, std::size_t index)
    {
        if (index >= MoteCount(kind) || !std::isfinite(age) || !std::isfinite(remaining) || age < 0 || remaining <= 0) return {};
        const float i=static_cast<float>(index);
        const float period=(kind==Kind::Fire ? 1.4f : (kind==Kind::LevelUp ? 2.2f : 3.2f))+.23f*static_cast<float>(index%5);
        const float phase=std::fmod(age/period+i*.381966f,1.0f);
        const float fade=Smooth(phase/.18f)*Smooth((1-phase)/.24f);
        const float height=kind==Kind::Fire ? (index>=12 ? 24.0f : 40.0f) : (kind==Kind::Binding ? 13.0f : (kind==Kind::LevelUp ? 52.0f : 36.0f));
        return { .16f*std::sin(age*.85f+i*1.4f), height*phase,
            At(age,remaining,kind).alpha*fade, kind==Kind::Fire&&index>=12 ? .65f+.75f*phase : .7f+.5f*std::sin(phase*3.14159265f) };
    }
}

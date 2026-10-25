#include "FWM/ChaosPulse.h"

#include <algorithm>
#include <cmath>

namespace FWM::ChaosPulse
{
	namespace
	{
		using V = std::array<float, 3>;
		using M = std::array<float, 9>;
		V Add(V a, V b) { return { a[0]+b[0], a[1]+b[1], a[2]+b[2] }; }
		V Mul(V a, float s) { return { a[0]*s, a[1]*s, a[2]*s }; }
		V Sub(V a, V b) { return Add(a, Mul(b, -1)); }
		float Dot(V a, V b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
		V Cross(V a, V b) { return { a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0] }; }
		V Unit(V a) { return Mul(a, 1.0f / std::max(std::sqrt(Dot(a,a)), 0.00001f)); }
		V Mix(V a, V b, float u) { return Add(Mul(a, 1-u), Mul(b, u)); }
		struct Key { float time; V value; };
		template<std::size_t N> V Track(float time, const std::array<Key,N>& keys)
		{
			if(time<=keys.front().time)return keys.front().value;
			if(time>=keys.back().time)return keys.back().value;
			std::size_t i=0;while(i+1<N&&time>keys[i+1].time)++i;
			const auto& a=keys[i];const auto& b=keys[i+1];
			const float span=b.time-a.time,u=(time-a.time)/span,u2=u*u,u3=u2*u;
			const V va=i?Mul(Sub(b.value,keys[i-1].value),1.0f/(b.time-keys[i-1].time)):V{};
			const V vb=i+2<N?Mul(Sub(keys[i+2].value,a.value),1.0f/(keys[i+2].time-a.time)):V{};
			return Add(Add(Mul(a.value,2*u3-3*u2+1),Mul(va,span*(u3-2*u2+u))),
				Add(Mul(b.value,-2*u3+3*u2),Mul(vb,span*(u3-u2))));
		}
		V Transform(const M& m, V v)
		{
			return { m[0]*v[0]+m[1]*v[1]+m[2]*v[2], m[3]*v[0]+m[4]*v[1]+m[5]*v[2], m[6]*v[0]+m[7]*v[1]+m[8]*v[2] };
		}
		M Product(const M& a, const M& b)
		{
			M m{};
			for (std::size_t i=0;i<3;++i) for (std::size_t j=0;j<3;++j)
				for (std::size_t k=0;k<3;++k) m[i*3+j] += a[i*3+k]*b[k*3+j];
			return m;
		}
		M Rotate(V axis, float angle)
		{
			axis=Unit(axis);
			const float x=axis[0],y=axis[1],z=axis[2],c=std::cos(angle),s=std::sin(angle),d=1-c;
			return { c+x*x*d,x*y*d-z*s,x*z*d+y*s, y*x*d+z*s,c+y*y*d,y*z*d-x*s, z*x*d-y*s,z*y*d+x*s,c+z*z*d };
		}
		M Aim(V from, V to)
		{
			from = Unit(from); to = Unit(to);
			const V v = Cross(from, to);
			const float c = Dot(from, to), f = 1.0f / std::max(1.0f+c, 0.00001f);
			return { c+v[0]*v[0]*f, v[0]*v[1]*f-v[2], v[0]*v[2]*f+v[1],
				v[1]*v[0]*f+v[2], c+v[1]*v[1]*f, v[1]*v[2]*f-v[0],
				v[2]*v[0]*f-v[1], v[2]*v[1]*f+v[0], c+v[2]*v[2]*f };
		}
		void OpenHand(Spirit& s, bool right)
		{
			// Keep the relaxed source hand intact. Every phalanx follows the wrist;
			// the rune is controlled by the whole body's reach and draw, not a fist.
			static constexpr std::array<V,15> rest{{
				{32.01965f,14.06086f,75.88984f},{30.60591f,16.32295f,73.43610f},{29.67221f,19.60163f,70.18919f},
				{35.47557f,18.87782f,72.61374f},{36.56783f,20.86068f,69.54532f},{37.42193f,22.63915f,66.72895f},
				{37.07774f,17.14478f,71.57420f},{38.64350f,19.55600f,68.00918f},{39.49519f,21.48319f,64.77428f},
				{37.55102f,15.13074f,70.78649f},{38.99796f,16.99563f,67.44615f},{39.69174f,18.51247f,64.47179f},
				{37.63294f,13.00288f,69.98985f},{39.16827f,14.66216f,66.68065f},{39.74031f,15.84758f,64.22732f}
			}};
			const float side=right?1.0f:-1.0f;
			const auto mirrored=[&](V p){p[0]*=side;return p;};
			const V wrist{side*32.7617f,10.3022f,79.2642f};
			const auto& hand=s.bones[right?6:3];
			const std::size_t first=right?22:7;
			for (std::size_t i=0;i<rest.size();++i)
				s.bones[first+i]={Add(hand.at,Transform(hand.turn,Sub(mirrored(rest[i]),wrist))),hand.turn};
		}
		void Arm(Spirit& s, bool right, V target)
		{
			const float side = right ? 1.0f : -1.0f;
			const V shoulder{ side*14.4053f, -5.1951f, 119.0647f };
			const V elbow{ side*24.5235f, -2.3375f, 95.7518f };
			const V wrist{ side*32.7617f, 10.3022f, 79.2642f };
			const V upper = Sub(elbow,shoulder), lower = Sub(wrist,elbow), delta = Sub(target,shoulder);
			const float length1=std::sqrt(Dot(upper,upper)), length2=std::sqrt(Dot(lower,lower));
			const float distance=std::clamp(std::sqrt(Dot(delta,delta)), 1.0f, length1+length2-0.1f);
			const V direction=Unit(delta), hint{side, -0.2f, -0.35f};
			const V outward=Unit(Sub(hint,Mul(direction,Dot(hint,direction))));
			const float along=(length1*length1-length2*length2+distance*distance)/(2*distance);
			const float bend=std::sqrt(std::max(length1*length1-along*along,0.0f));
			const V posedElbow=Add(shoulder,Add(Mul(direction,along),Mul(outward,bend)));
			const std::size_t start=right?4:1;
			const V origin{0,0,82};
			s.bones[start] = { Sub(shoulder,origin), Aim(upper,Sub(posedElbow,shoulder)) };
			const V forearm=Unit(Sub(target,posedElbow));
			M turn=Aim(lower,forearm);
			const V restPalm=Mul(Cross({-side*2.157f,5.875f,2.624f},{side*6.733f,11.181f,-14.49f}),side);
			const auto projected=[&](V v){return Unit(Sub(v,Mul(forearm,Dot(v,forearm))));};
			const V now=projected(Transform(turn,restPalm)), down=projected({0,0,-1});
			const float roll=std::atan2(Dot(forearm,Cross(now,down)),Dot(now,down));
			turn=Product(Rotate(forearm,roll),turn);
			s.bones[start+1] = { Sub(posedElbow,origin), turn };
			// The palm is a continuation of the forearm, with the same rotation.
			s.bones[start+2] = { Sub(target,origin), turn };
			OpenHand(s,right);
		}
		float Ease(float t, float start, float end)
		{
			const float u = std::clamp((t - start) / (end - start), 0.0f, 1.0f);
			return u * u * (3.0f - 2.0f * u);
		}
		float Glow(float t, float start, float peak, float fade, float end)
		{
			return Ease(t, start, peak) * (1.0f - Ease(t, fade, end));
		}
	}
	Binding BindAt(float age, float left, bool grand)
	{
		Binding result;
		if (!std::isfinite(age) || !std::isfinite(left) || age < 0 || left <= 0) return result;
		const float envelope = Ease(age, 0, 0.32f) * Ease(left, 0, 0.55f);
		const float breath = 0.90f + 0.10f * std::sin(age * 2.0f + 0.5f);
		for (auto& part : result.parts) part.color = grand ? V{ 0.34f, 0.035f, 0.64f } : V{ 0.60f, 0.25f, 1.0f };
		auto& seal = result.parts[0];
		seal.turn = 0.035f * age;
		seal.alpha = (grand ? 0.30f : 0.23f) * envelope * breath;
		if (!grand) seal.color = { 0.68f, 0.35f, 1.0f };
		auto& mist = result.parts[1];
		mist.scale = 0.93f + 0.035f * std::sin(age * 1.45f);
		mist.turn = -0.15f * age;
		mist.height = 1.5f * std::sin(age * 1.8f);
		mist.alpha = (grand ? 0.40f : 0.29f) * envelope * breath;
		for (std::size_t i = 0; i < 2; ++i) {
			auto& thread = result.parts[2 + i];
			const float phase = age * 2.1f + static_cast<float>(i) * 3.14159265f;
			const float draw = (1.0f - std::cos(phase)) * 0.5f;
			thread.scale = 1.0f - 0.14f * draw;
			thread.turn = -0.22f * age + static_cast<float>(i) * 0.5f;
			thread.height = 2.0f + 5.5f * draw;
			thread.alpha = (grand ? 0.47f : 0.34f) * envelope * (0.76f + 0.24f * draw);
		}
		auto& flecks = result.parts[4];
		flecks.turn = -0.11f * age;
		flecks.height = 2.5f + 2.5f * std::sin(age * 1.4f);
		flecks.alpha = (grand ? 0.40f : 0.22f) * envelope * breath;
		flecks.color = grand ? V{ 0.44f, 0.08f, 0.76f } : V{ 0.78f, 0.50f, 1.0f };
		return result;
	}

	Frame At(float seconds, bool grand, float duration)
	{
		Frame p;
		if (!std::isfinite(seconds) || seconds < 0.0f || !std::isfinite(duration) || duration <= 0 || seconds >= duration) return p;
		const float t = seconds * 2.4f / kGone;
		auto& out = p.parts[0];
		out.scale = 0.10f + 0.90f * Ease(t, 0.0f, 0.52f);
		out.alpha = 1.1f * Glow(t, 0.0f, 0.10f, 0.42f, 0.9f);
		out.color = { 0.72f, 0.40f, 1.0f };
		auto& in = p.parts[1];
		in.scale = 1.0f - 0.82f * Ease(seconds, 1.45f, 3.30f);
		in.alpha = 0.72f * Glow(seconds, 1.15f, 1.60f, 3.0f, 3.75f);
		auto& seal = p.parts[2];
		seal.turn = 0.08f * t;
		seal.alpha = 0.75f * Glow(t, 0.15f, 0.48f, 1.25f, 2.25f);
		seal.color = { 0.68f, 0.35f, 1.0f };
		auto& mist = p.parts[3];
		mist.scale = 0.70f + 0.30f * Ease(t, 0.0f, 0.60f) - 0.28f * Ease(t, 0.9f, 2.4f);
		mist.turn = -0.18f * t;
		mist.alpha = 0.25f * Glow(t, 0.08f, 0.50f, 1.1f, 2.4f);
		auto& threads = p.parts[4];
		threads.scale = 1.0f - 0.68f * Ease(t, 0.75f, 2.4f);
		threads.turn = -0.4f * t;
		threads.alpha = 0.64f * Glow(t, 0.24f, 0.65f, 1.4f, 2.4f);
		auto& core = p.parts[5];
		core.scale = 0.4f + 0.6f * Ease(t, 0.0f, 0.45f);
		core.alpha = 0.45f * Glow(t, 0.0f, 0.09f, 0.16f, 0.75f);
		core.color = { 0.80f, 0.62f, 1.0f };
		auto& flecks = p.parts[6];
		flecks.scale = 1.0f - 0.55f * Ease(t, 0.7f, 2.4f);
		flecks.turn = -0.3f * t;
		flecks.height = 22.0f * Ease(t, 0.35f, 2.4f);
		flecks.alpha = 0.65f * Glow(t, 0.28f, 0.70f, 1.55f, 2.4f);
		flecks.color = { 0.78f, 0.50f, 1.0f };
		p.light = 0.6f * Glow(t, 0.0f, 0.18f, 0.55f, 1.5f);
		// Overlapping arcs: weight shifts first, the leading arm arrives before the
		// other, and the head settles last. Continuous tangents avoid stop/start beats.
		const V body=Track(seconds,std::array<Key,6>{{
			{0,{76,-145,-0.08f}}, {0.55f,{80,-154,-0.045f}}, {1.5f,{61,-119,-0.23f}},
			{2.4f,{45,-77,-0.44f}}, {3.15f,{43,-84,-0.40f}}, {kGone,{45,-86,-0.39f}}
		}});
		const V balance=Track(seconds,std::array<Key,6>{{
			{0,{0,-0.035f,-0.035f}}, {0.7f,{0.018f,-0.06f,-0.015f}}, {1.6f,{-0.022f,0.025f,-0.10f}},
			{2.5f,{-0.012f,0.045f,-0.16f}}, {3.2f,{0.009f,0.012f,-0.11f}}, {kGone,{0,0.01f,-0.12f}}
		}});
		p.spirit.scale = grand ? 7.0f : 5.5f;
		p.spirit.height = body[0]; p.spirit.forward = body[1]; p.spirit.lean = body[2];
		p.spirit.roll=balance[0];p.spirit.twist=balance[1];
		p.spirit.alpha = 0.85f * Glow(seconds, 0.03f, 0.65f, 3.2f, kGone);
		p.spirit.bones[0] = { {-0.01585f,-2.40295f,52.30057f},
			Product(Rotate({0,0,1},-0.65f*balance[1]),Product(Rotate({0,1,0},-0.5f*balance[0]),Rotate({1,0,0},balance[2]))) };
		Arm(p.spirit, true, Track(seconds,std::array<Key,6>{{
			{0,{28,3,98}}, {0.55f,{34,5,99}}, {1.45f,{31,20,111}},
			{2.35f,{23,28,108}}, {3.35f,{12,22,101}}, {kGone,{13,22.5f,101.5f}}
		}}));
		Arm(p.spirit, false, Track(seconds,std::array<Key,6>{{
			{0,{-28,1,98}}, {0.4f,{-33,7,101}}, {1.1f,{-30,24,112}},
			{2.05f,{-24,28,107}}, {3.05f,{-15,20,100}}, {kGone,{-16,21,101}}
		}}));
		if (grand) {
			p.spirit.color = { 0.42f, 0.08f, 0.74f };
			for (auto& part : p.parts) part.color = { 0.34f, 0.035f, 0.64f };
			p.parts[5].color = { 0.58f, 0.18f, 0.88f };
			// Three overlapping waves rise from the rim and narrow toward the center.
			for (std::size_t i = 0; i < 3; ++i) {
				auto& rise = p.parts[7 + i];
				const float age = seconds - 0.4f - static_cast<float>(i) * 0.48f;
				const float phase = std::clamp(age / 1.9f, 0.0f, 1.0f);
				rise.scale = 1.0f - 0.67f * phase;
				rise.height = 165.0f * phase;
				rise.turn = -0.3f * age;
				rise.alpha = 0.75f * std::sin(phase * 3.14159265f) * (1.0f - Ease(seconds, 3.1f, kGone));
				rise.alpha = std::max(0.0f, rise.alpha);
			}
		}
		if (duration > kGone) {
			const float held = Glow(seconds, .12f, .65f, duration - 2.4f, duration);
			const float settle = Ease(seconds, 1.2f, 2.4f);
			for (std::size_t index : {2u,3u,4u,6u}) {
				auto& part=p.parts[index];
				const float strength=index==2?.75f:index==3?.24f:index==4?.52f:.5f;
				part.alpha=held*strength;
				part.scale += ((index==2?1.0f:.80f+.07f*std::sin(seconds*1.4f+static_cast<float>(index)))-part.scale)*settle;
			}
			p.light=std::max(p.light,held*.18f);
			if (grand) for (std::size_t i=0;i<3;++i) {
				auto& wave=p.parts[7+i];
				const float phase=seconds/2.8f+static_cast<float>(i)/3.0f;
				const float u=phase-std::floor(phase);
				wave.scale=1.0f-.60f*u; wave.height=195.0f*u;
				wave.turn=-.18f*seconds;
				wave.alpha=.78f*held*std::sin(u*3.14159265f);
			}
		}
		return p;
	}
}

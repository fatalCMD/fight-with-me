#include "FWM/ChaosPulse.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace
{
	void Check(bool value, const char* message)
	{
		if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
	}
}
int main()
{
	using namespace FWM::ChaosPulse;
	const float stretch = kGone / 2.4f;
	for (bool grand : { false, true }) {
	for (float t : { -1.0f, 0.0f, kGone, 100.0f, std::numeric_limits<float>::quiet_NaN() }) {
		const auto p = At(t, grand);
		for (const auto& part : p.parts) Check(part.alpha == 0.0f, "No flashes before or after the pulse");
		Check(p.light == 0.0f, "No light after cleanup");
		Check(p.spirit.alpha == 0.0f, "No specter before or after the animation");
	}
	Check(std::abs(At(0.52f * stretch, grand).parts[0].scale - 1.0f) < 0.0001f, "The outward edge must reach the gameplay radius");
	float previousOut = 0.0f, previousIn = 1.0f;
	for (int i = 0; i <= 240; ++i) {
		const float t = static_cast<float>(i) / 100.0f;
		const auto p = At(t * stretch, grand);
		for (const auto& part : p.parts) {
			Check(std::isfinite(part.alpha) && part.alpha >= 0.0f && part.alpha <= 1.1f, "Finite, bounded glow");
			Check(part.scale > 0.0f && part.scale <= 1.0f, "The pulse must stay inside the slow's radius");
		}
		if (t < 0.53f) { Check(p.parts[0].scale >= previousOut, "Release expands smoothly"); previousOut = p.parts[0].scale; }
		if (t >= 0.68f && t < 2.4f) { Check(p.parts[1].scale <= previousIn, "Binding wave contracts smoothly"); previousIn = p.parts[1].scale; }
	}
	Check(At(0.7f * stretch, grand).parts[2].alpha > 0.5f, "Seal is readable when the wave catches enemies");
	Check(At(kGone - 0.01f, grand).parts[4].alpha < 0.001f, "Threads fade before deletion");
	Check(At(1.8f, grand).spirit.bones[6].at[1] > At(0.2f, grand).spirit.bones[6].at[1], "The hand reaches over the circle");
	Check(At(1.8f, grand).spirit.height < At(0.8f, grand).spirit.height, "The specter lowers over the circle");
	Check(At(1.8f, grand).spirit.lean < -0.25f, "The torso hunches forward");
	Check(At(2.4f,grand).spirit.forward>At(0.2f,grand).spirit.forward+60.0f,"The whole torso reaches forward over the rune");
	Check(std::abs(At(1.4f,grand).spirit.bones[3].at[1]-At(1.4f,grand).spirit.bones[6].at[1])>3.0f,"The arms overlap their timing instead of moving in lockstep");
	for (const auto [hand, fingertip] : { std::pair{3u,15u}, std::pair{6u,30u} }) {
		const auto reach=[&](float t) {
			const auto pose=At(t,grand).spirit;
			float sum=0;for(std::size_t i=0;i<3;++i){const float d=pose.bones[fingertip].at[i]-pose.bones[hand].at[i];sum+=d*d;}return std::sqrt(sum);
		};
		Check(std::abs(reach(2.4f)-reach(1.0f))<0.01f,"The hand stays relaxed and open throughout the movement");
	}
	Check(At(2.95f,grand).spirit.bones[6].at[2]<At(1.7f,grand).spirit.bones[6].at[2]-5.0f,"Both arms draw downward over the rune");
	for (int frame=1; frame<static_cast<int>(kGone*100); ++frame) {
		const auto pose=At(static_cast<float>(frame)/100.0f,grand).spirit;
		const auto before=At(static_cast<float>(frame-1)/100.0f,grand).spirit;
		for(std::size_t j=0;j<pose.bones.size();++j){
			const auto& bone=pose.bones[j];
			for(std::size_t k=0;k<3;++k){
				Check(std::isfinite(bone.at[k])&&std::abs(bone.at[k]-before.bones[j].at[k])<0.6f,"No joint jumps during the reach or draw");
				float length=0;for(std::size_t c=0;c<3;++c)length+=bone.turn[k*3+c]*bone.turn[k*3+c];
				Check(std::isfinite(length)&&std::abs(length-1.0f)<0.001f,"Joint rotation preserves the mesh's shape");
			}
		}
		for (std::size_t side : {1u,4u}) {
			for(std::size_t k=0;k<9;++k)
				Check(std::abs(pose.bones[side+1].turn[k]-pose.bones[side+2].turn[k])<0.00001f,"The wrist follows the forearm without bending independently");
			const auto distance=[&](std::size_t a,std::size_t b) {
				float sum=0; for(std::size_t i=0;i<3;++i){const float d=pose.bones[a].at[i]-pose.bones[b].at[i];sum+=d*d;} return std::sqrt(sum);
			};
			Check(std::abs(distance(side,side+1)-25.576f)<0.05f,"The upper arm keeps its natural length");
			Check(std::abs(distance(side+1,side+2)-22.354f)<0.10f,"The forearm stays connected to the wrist");
		}
	}
	}
	Check(At(1.5f).parts[7].alpha == 0.0f, "Upward pulses are unique to rank III");
	Check(At(1.5f, true).parts[7].alpha > 0.5f, "The crowned draugr sends up a purple pulse");
	Check(At(1.5f, true).parts[7].height > At(0.8f, true).parts[7].height, "Rank III's glow rises");
	Check(At(1.5f, true).parts[7].scale < At(0.8f, true).parts[7].scale, "Rank III's glow moves inward");
	const auto purple = At(1.5f, true).parts[0].color;
	Check(purple[0] > purple[1] * 5.0f && purple[2] > purple[0], "Rank III's circle is saturated purple");
	Check(purple[2] < At(1.5f).parts[0].color[2], "Rank III uses a deeper glow than ranks I and II");
	for (bool grand : { false, true }) {
		for (const auto [age, left] : { std::pair{ -1.0f, 8.0f }, std::pair{ 0.0f, 8.0f }, std::pair{ 8.0f, 0.0f },
			std::pair{ 8.0f, -1.0f }, std::pair{ std::numeric_limits<float>::quiet_NaN(), 8.0f }, std::pair{ 1.0f, std::numeric_limits<float>::quiet_NaN() } }) {
			for (const auto& part : BindAt(age, left, grand).parts) Check(part.alpha == 0, "No target restraint before its reveal, after the actual slow, or from invalid clocks");
		}
		for (int frame = 1; frame < 1200; ++frame) {
			const float age = static_cast<float>(frame) / 144;
			for (const auto& part : BindAt(age, 8.0f - age, grand).parts) {
				Check(std::isfinite(part.scale) && part.scale > 0 && part.scale <= 1.01f, "Target restraint stays compact around the enemy");
				Check(std::isfinite(part.alpha) && part.alpha >= 0 && part.alpha <= .5f, "Target restraint stays translucent and bounded");
				Check(std::isfinite(part.turn) && std::isfinite(part.height), "Target motion remains finite throughout the slow");
			}
		}
		Check(BindAt(.9f, 7.1f, grand).parts[2].scale < BindAt(.4f, 7.6f, grand).parts[2].scale, "Following restraint curls inward with the casting effect");
		Check(BindAt(7.8f, .2f, grand).parts[0].alpha < BindAt(7.0f, 1.0f, grand).parts[0].alpha, "Target seal fades with remaining debuff time");
		const auto before = BindAt(3.0f, 2.0f, grand), refreshed = BindAt(3.0f, 8.0f, grand);
		for (std::size_t i = 0; i < before.parts.size(); ++i)
			Check(before.parts[i].scale == refreshed.parts[i].scale && before.parts[i].turn == refreshed.parts[i].turn && before.parts[i].height == refreshed.parts[i].height,
				"Refreshing the slow does not restart or jump the restraint's motion");
	}
	Check(BindAt(1, 7, true).parts[0].color == At(1, true).parts[2].color, "Rank III target seal matches the main circle's deep purple exactly");
	Check(BindAt(1, 7, false).parts[0].color == At(1, false).parts[2].color, "Rank I-II target seal matches the main circle's lavender exactly");
	std::puts("Chaos Down visual timing and bounds pass.");
}

#include "FWM/WindstepFx.h"
namespace FWM::WindstepFx {
Frame At(float t,int rank){
 Frame f;if(!std::isfinite(t)||t<0||t>VisualSeconds(rank))return f;
 const bool grand=Rank(rank)==3;const float fade=Ease(t/.16f)*(1-Ease((t-EchoHold(rank))/kFade));
 // One stock-armoured bow-bearing echo, then two offset echoes at the final rank.
 for(int i=0;i<3;++i){auto& p=f.parts[static_cast<std::size_t>(i)];p.at={static_cast<float>(i-1)*8,-static_cast<float>(i)*17,1.2f*std::sin(t*1.8f)};p.alpha=(i==0?1.f:grand?.35f:0)*fade*.58f;p.drift=t*.3f;p.color=grand?std::array<float,3>{.65f,1,.82f}:std::array<float,3>{.38f,.8f,1};}
 for(int i=3;i<15;++i){auto& p=f.parts[static_cast<std::size_t>(i)];const float n=static_cast<float>(i-3),delay=n*.019f,u=t-delay;
  p.at={std::sin(n*2.4f)*23,-Reach(rank)*Travel(std::max(0.f,u))*.85f,8+n*6+u*16};p.turn=n*.52f+u*1.4f;
  p.scale=.65f+.025f*n+.3f*Ease(u/.3f);p.alpha=Ease(u/.12f)*(1-Ease((u-.3f)/.7f))*(grand?.65f:.42f);p.drift=u*.8f;
 }
 for(int i=15;i<51;++i){auto& p=f.parts[static_cast<std::size_t>(i)];const float n=static_cast<float>(i-15),u=t-.04f*n/36;const float a=n*2.399963f;
  const float spread=grand?1.5f:1.f;p.at={std::cos(a)*(18+u*35)*spread,-Reach(rank)*Travel(u)*(n/36.f),12+std::fmod(n*19,110.f)+u*30};
  p.scale=.45f+.5f*std::fmod(n*.37f,1.f);p.alpha=Ease(u/.1f)*(1-Ease((u-.4f)/1.1f))*(grand?.8f:.5f);p.color={.75f,.95f,1};
 }
 auto& gust=f.parts[51];const float u=t-EchoHold(rank);gust.at={0,0,5};gust.scale=.1f+Ease(u/.6f)*4.2f;gust.alpha=grand?Ease(u/.08f)*(1-Ease(u/.6f))*.75f:0;gust.turn=t*.25f;gust.drift=t;gust.color={.65f,1,.85f};
 return f;
}
}

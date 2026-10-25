# Arcane Surge following seal

The existing violet body aura remains. The new ground seal follows the caster's
world position every frame, rather than a hand/pelvis bone, so walking and casting
do not tilt the circle. Its timer is captured from the actual Surge activation
(currently 12 seconds), with a 0.6-second arrival and a 1-second exit fade.

Ranks I and II use a generated rotating seal with white-lilac inscriptions,
textured rising rim and twelve motes. Rank II is 12% wider. Rank III is 35% wider,
replaces it with a denser generated master seal and adds a second rim, doubles the motes and slows
rotation/pulsing for a heavier presence. The upper edge of both rim meshes reaches
zero vertex opacity; they are smooth 256-segment surfaces, not billboard cards.

`BuildArcaneSurge.js` maps the two generated transparent images to flat decals and
authors the soft rim geometry. It references our `ArcaneSurgeSeal.dds`,
`ArcaneSurgeGrandSeal.dds`, `Glow.dds` and `HealingPulse.dds`, plus Skyrim's soft
glow textures. PNG masters and exact built-in image-generation prompts live in
`art/`. The DDS files use BC3 with full mipmaps, preserving the generated alpha.
No additional art dependency.
Four separately named NIFs allow simultaneous independent casters. The required
Effects download is now 1.0.20; both plugin packages must accompany it.

`ArcaneSurge.h` is the shared animation used by `Surges.cpp` and the browser frame
sampler. `Arts.cpp` starts/ends it, and the ability host ticks and clears it through
`Battle.cpp`. Death, disabling, missing 3D and cell changes retire the live model;
an active remaining timer can respawn art after reloading 3D. Effects are excluded
from saves through the existing owned-particle registry. Rank is captured at cast
time, including debug casts. The effect changes no ability balance.

Build mesh: `node tools/arcanesurge/BuildArcaneSurge.js`.
Build the plugins and `FightWithMeArcaneSurgeLook` with the existing CMake build.
Export preview: `node tools/wolfhead/preview/build.js --surge-only --stock-textures`.
Open `/surge.html?rank=3` on the effect preview server. The moving-caster control
exercises the same per-part offsets over a moving origin; the retained aura is
also shown using the existing support sampler.

Validation: animation tests cover full-duration persistence, smooth fade,
rank distinctions, stationary offsets and invisible mote resets. The asset audit
checks all animation nodes, NIF roundtrips and transparent upper rim vertices.
Still verify in Skyrim: moving/strafing on uneven terrain, simultaneous mages,
effects toggling and the final fade under the installed lighting setup.

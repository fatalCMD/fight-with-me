# Berserk rage mantle

Original Fight With Me geometry, using Skyrim's `FXFireScrollTileGrey.dds` and `FXGlowSpotLinearAlpha.dds`. No external mod assets or distributed vanilla textures are needed.

Ranks I/II use eight amber flame plumes and 24/36 embers. Rank III uses twelve larger crimson plumes, 48 embers and a stronger second shockwave. The burst remains at the activation point; the mantle follows the follower. Existing cosmetic weapon fire and ranked activation audio are retained. This is visual only and adds no elemental damage.

`Berserk::At` drives both the plugin and the browser preview. Bloodlust extends the deadline without replaying the entrance. Battle-end cancellation fades the mantle over 1.1 seconds. Death, disabled effects, unloading and save/load release the temporary models and light. Four separate NIF names allow four independent active followers; missing/busy models retain the existing weapon-fire fallback.

Build geometry with `node tools/berserk/BuildBerserk.js`, build the plugin and `FightWithMeBerserkLook`, then run `node tools/wolfhead/preview/build.js --berserk-only --stock-textures`. Preview at `/berserk.html?rank=3`. The mannequin is a scale reference, not a follower model.

Validation: `FightWithMeBerserkTests` covers finite/bounded frames, appearance, sustained duration, Bloodlust, cancellation, expiry, and rank III distinction. The builder round-trips all four NIFs and the preview builder verifies each animated node and texture. Actual lighting and rendering still need an in-game playtest.

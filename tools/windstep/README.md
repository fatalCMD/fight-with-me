# Windstep assets

The echo uses Skyrim's male/female hide light armour, body, head, helmet, hands,
boots, iron bow and iron arrow. It is a generic human archer, selected using the
caster NPC base sex. It is not a clone of the caster's face, race or equipment.

The pose is frame 0 of Skyrim's `bow_idledrawn.hkx`, applied through the complete
99-bone human skeleton. The bow uses the corresponding 7-bone weapon animation,
attached to `Shield`; the nocked arrow follows `Weapon`. This includes the stock
finger, shoulder and torso rotations and the drawn bowstring. No manually posed
arms or third-party animation assets are used.

Build prerequisites: Node, Python, stock Skyrim Data, and an installed PyNifly HKX
reader. PyNifly is a build tool only; no PyNifly source or raw HKX is packaged.

1. Set `FWM_SKYRIM_DATA` to the stock Data directory if not using this machine's default.
2. Run `node tools/windstep/ExtractPose.js`.
3. Run `python tools/windstep/ExtractPose.py --pynifly <io_scene_nifly-directory> --input build/windstep-stock --output build/windstep-stock/pose.json`.
4. Run `node tools/windstep/BuildWindstep.js`.
5. Build `FightWithMeWindstepLook`, then run `node tools/wolfhead/preview/build.js --windstep-only --stock-textures`.

`StockArcher.js` bakes skin weights into rigid spell geometry and validates complete
meshes and bounds. Both sexes keep the same 52 animated parts. Stock texture
references resolve through Skyrim; they are not copied into the Effects package.

## Runtime distraction

The image holds for 4/5/6 seconds, then fades for 0.9 seconds. Rank III's gust is
at the start of the final fade. Speed/protection remain 2/3/4 seconds plus Tailwind.

`WindstepDecoys` creates an invisible, invulnerable, non-acting temporary target
under the image after the caster clears it. Its isolated templates derive from
stock EncBandit01MissileNordM/F (0x37C00/0x37BFF), with factions, inventories, spells,
outfits and AI packages stripped. They join only PlayerFaction, not the follower
faction. The body is hidden; the exported spectral mesh supplies the appearance.

Only enemies already targeting the caster within 20 m and line of sight are
captured. StartCombat registers the decoy with their combat groups, then the
existing hold/target-selector mechanism keeps their attention through the fade.
A later tank taunt can take precedence. Debug previews never create combat targets.
The core orders plugin queries the Paths decoy registry so it never lists or targets
an echo as an enemy or companion. Decoys neither attack nor leave loot.

Expiry, role/ability removal, caster death/unload, cell changes, disabling the mod,
saving and resetting clean up target holds and disable/delete the temporary actor.
The two reusable NPC templates are marked temporary. No vanilla NPC base is edited.

## Validation still needed in Skyrim

Automated tests cover duration, fade, movement, terrain rejection and asset integrity.
They cannot confirm Havok/AI behaviour. Test a melee enemy and a ranged enemy already
attacking the archer, then cast Windstep: both should aim at the departure image,
resume ordinary targeting after it disappears, and never attack the hidden target
after saving/loading or changing cells. Test a later Taunt and simultaneous archers.
`[Windstep]` logs record target creation and observed attacker target samples; use
these to verify acquisition rather than assuming a successful StartCombat call means
the enemy actually stayed on the decoy.

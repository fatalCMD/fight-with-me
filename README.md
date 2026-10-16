# Fight With Me

Battle orders for your companions in Skyrim. Give each follower a role, a focus and a place in
the fight; the game's own combat AI carries it out. Tanks taunt, healers heal, and opening the
battle plan slows the world while you decide.

Pairs with [Walk With Me](https://www.nexusmods.com/skyrimspecialedition/mods/191283): Walk With
Me moves the party, Fight With Me fights with it. Either works without the other.

## Requirements

- SKSE and Address Library for SKSE Plugins
- SKSE Menu Framework 3 (for the battle plan; without it, saved orders and the INI still apply)
- [Triumvirate - Mage Archetypes](https://www.nexusmods.com/skyrimspecialedition/mods/39170), by Enai Siaion
- [Elemental Destruction Magic Redux](https://www.nexusmods.com/skyrimspecialedition/mods/37211), by reaperix, Redux maintained by phenderix
- [Vibrant Weapons - Fire Frost Shock](https://www.nexusmods.com/skyrimspecialedition/mods/40087), by jg1

Install those three FX sources with their own requirements and enable their plugins. For
Vibrant Weapons, use the download for your Skyrim runtime; this integration uses the original
Fire Frost Shock mod, not Vibrant Weapons EAE. Fight With Me reads their installed visual
assets; it does not bundle them, cast their spells, or require that followers learn them.
If a source is missing, its visuals fall back to the simpler vanilla effects and the missing
plugin or mesh is reported in FightWithMe.log.

## Roles

| Role | Plays like | Ability |
|---|---|---|
| Fighter | Balanced melee that flanks | |
| Tank | Blocks, bashes and pulls enemies off the party; shields against archers shooting at them | Taunt: holds enemies up to the tank's level plus a quarter of it, for 10 s (15 s from level 30, 30 s from 60), and braces the tank with extra armor while it holds |
| Healer | Stays back and keeps the whole party standing | Heal: you and everyone near them at once, more as they level, no spell or magicka needed. Rejuvenate: your magicka and every companion's back twice as fast for 30 s (1 min from level 30, 3 min from 60) |
| Bruiser | Power attacks and staggers the big ones | |
| Archer | Keeps range, picks off casters first; fights as a Fighter when out of arrows | |
| Mage | Spell pressure, finishes the weakest; never conjures a bound weapon | |

Anyone can be a Healer. Mages and healers can be given a spell for each hand on their sheet in
the party view (out of a fight): one they know, or one of yours they are then taught. Mages
hold any spell but a heal; healers any spell, with attack spells capped by level. Whatever is
chosen gets used: a spell the combat AI never casts on its own (one cast on oneself to hurt
those around, say) is cast for them when their hands have been idle a few seconds. Mages and
healers never fight with a blade or their fists; out of magicka, they fall back and wait for it.

Roles are set out of a fight. In one, the role box shows the role they fight as, with their
armor, damage and resistances beside it.

Each role also sets a default focus (their own choice, your target, whoever attacks you or the
party, casters and archers, strongest, weakest, or one marked enemy) and position (formation,
free, guard you, hold back, front line, with me). Change either in the battle plan. Whatever
the focus, an enemy attacking a companion up close is dealt with first.

Click an enemy in the battle plan to send a companion (or everyone) after them. A companion
sent after someone goes, whatever their position: no guard radius or leash holds them back,
and melee companions press in like the front line. When that enemy falls they go back to
the focus they had.

## Before a fight

Out of a fight, an enemy that you or a companion can see within 2500 units brings their
weapons out; they put them away a few seconds after it is gone, unless yours are out. With
**Auto battle** on (a switch at the top right of the battle plan, off by default), the
companion who sees an enemy starts the fight instead of waiting to be attacked. Companions on
Hold back wait for it to come to them, and nobody starts one while you sneak.

## Abilities

Every role has a special, with a button in the battle plan and an "on their own" switch:
Bodyguard (tank: runs to you when you are in trouble and taunts what is on you), Volley
(archer: every archer and mage fires at one enemy), War cry (bruiser: staggers everything close
around them), Ward (mage and healer: Oakflesh up to Ebonyflesh on you, by their level) and
Disarm (fighter: the Disarm shout's effect at their opponent). Tanks keep their taunt and
healers their heal and Rejuvenate.

Each ability shows in the world: enemies a taunt holds glow red and the braced tank carries
a protective aura. Healing blooms around its recipients, Rejuvenate brings a water swirl,
and Ward raises a barrier. War cry sends out a wind burst; a disarmed weapon throws sparks.
With Paths, Earthshaker throws up rocks at the impact and aftershock, Sunder sheds fragments
from its target's armor, and Berserk, Cleave and Riposte light the weapons with fire,
fire-and-lightning and frost respectively. Renewal sustains a healing aura, Pinning Shot
binds its target, and Magnus surrounds the mage with Triumvirate's resurrection art.
Weapon visuals follow the equipped weapon type and hand and end on sheathing or swapping.
These added visuals can be switched off with **Ability effects** in the settings.

In the battle plan, icons over each companion's crest show what is on
them (braced, rejuvenated, guarding you, out of magicka) with a ring for the time left; point
at one for the details. Enemy markers show health, magicka and stamina as they are.

Fight With Me leaves potions alone: when companions drink is up to the game, or to a mod made
for it such as [NPCs Use Potions](https://www.nexusmods.com/skyrimspecialedition/mods/67489).

## Paths - Follower Ability Skilltree (optional add-on)

With Paths installed, every role has a perk tree in the battle plan. A companion earns a perk point for every level past 10 (those met above that have their points
waiting) and spends them on new abilities, ranks and branches in the ones they know, and
traits. Each carries two abilities into a fight, chosen on their path, and the level
milestones above give way to ranks. Without the add-on, everything here works as described.

Every point spent on a path is also a path bonus for a companion of that role, 1% per point
by default (`[PathBonus]` in the INI), shown beside the path's name: attack speed for a
fighter, less damage taken for a tank (from blows, arrows and harmful spells; 75% at most),
all the healing a healer does (abilities, spells they cast,
potions they drink), weapon damage for a bruiser, the chance an arrow is not used up for an
archer (90% at most), and less magicka for every spell for a mage (75% off at most). Only
the path of the role they play now counts. Once a path is mastered, a companion's leftover
points can go straight into its bonus, and come back out again or with Start over. Every
companion has their own points, from their own level.

Ability cooldowns run on game time, in a fight or out of it (an hour's wait ends them), and
are kept in the save.

With Paths installed, the Tanks and Healers settings pages are replaced by a cover, *Your
Followers Are Guided by Their Path*, and can no longer be changed: the paths set a taunt's
hold, cooldown and bracing armor, a heal's amount and cooldown and how long Rejuvenate
lasts, and the rest of those pages stays at the defaults in FightWithMe.ini. Your own
changes there are kept in FightWithMe_user.ini and return if Paths is removed.

## Controls

Choose **Oxblood** (the original red, still the default), **Vanilla**, or **Nordic** under
**Battle plan > Interface > Theme** in the Mod Control Panel. Vanilla uses translucent
charcoal panels, silver borders, white selection marks and pale blue light on Paths.
Nordic reskins the same windows with dark stone surfaces, engraved corners, beveled metal
emblems and inset selection highlights. Window placement, dimensions, typography metrics,
navigation and controls stay the same in every theme. The choice applies to
the battle plan, Paths, HUD notices and settings together, and is saved without a restart.
It can also be set with `iTheme` in the INI's `[Interface]` section: `0` for Vanilla, `1` for
Oxblood, or `2` for Nordic. Health, magicka, stamina and warning colors keep their meaning
in every theme.

Numpad 7 opens the battle plan (LB + D-pad down on a pad). Walk With Me's party actions grid has
a Battle plan tile as well. Party orders work without opening it: Numpad 8 Attack!, Numpad 6
To me!, Numpad 4 Hold back, Numpad 2 As you were. Any of these can be rebound on the Controls
page, alone or with Shift, Ctrl or Alt held. Settings are in the Mod Control Panel and in
`SKSE/Plugins/FightWithMe.ini`. What you change in game is saved to `FightWithMe_user.ini` next
to it, which updates never replace (under MO2 it appears in overwrite). Each companion's orders
are saved with your save game.

In the battle plan, the right stick (or dragging with the right mouse button) turns the view
around the chosen companion, and the triggers or the mouse wheel zoom. Numpad 5, the right
stick click or the switch top left shows the whole fight from above: the party in blue rings,
enemies in red, you in white. Drag with the middle mouse button (or the left stick, or WASD)
to move the view about; a middle click brings it back over the fight. Where a roof or a cave
ceiling hides the fight, it is cut away above everyone's heads; in the open, nothing is.

## Nether's Follower Framework

NFF has its own tank and healer roles and combat styles. Set its combat role to Standard and its
combat style to Default for companions you command here.

## Building

Build with Visual Studio 2022 and vcpkg:

```powershell
$env:VCPKG_ROOT = 'C:\vcpkg'
cmake --preset skyrim
cmake --build build/skyrim --config Release
```

`tools/BuildPackage.ps1 -VcpkgRoot <path>` builds, runs the tests and makes the mod-manager ZIP.

`tools/VerifyEffectAssets.py --mods <MO2 mods directory> --plugins <profile plugins.txt>`
checks the FX catalog against the installed sources before an in-game visual test. See
[ability FX validation](docs/ability-fx.md) for mappings and the runtime checks still needed.

## FX credits

Enai Siaion created Triumvirate's support, targeting and magical aura visuals. reaperix
created the earth, water and wind effects used from Elemental Destruction Magic Redux;
phenderix maintains the Redux edition. jg1 created Vibrant Weapons' weapon particle models.
Please download and endorse their mods at the links above. Their assets remain in their
original mods and are not redistributed in Fight With Me.

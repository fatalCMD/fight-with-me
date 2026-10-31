# Fight With Me 0.6.0

Battle orders for your companions in Skyrim. Give each follower a role, a focus and a place in
the fight; the game's own combat AI carries it out. Fight With Me decides whom they fight and
where they stand, never what their attacks do: abilities, unique traits and levels earned by
fighting are [Path of the Companions](#path-of-the-companions-optional)'s. Opening the battle plan
slows the world while you decide.

Pairs with [Walk With Me](https://www.nexusmods.com/skyrimspecialedition/mods/191283): Walk With
Me moves the party, Fight With Me fights with it. Once companions are in a fight, Walk With Me
lets go of the party until it is over (Walk With Me 0.3.1 or later). Either works without the
other.

## Requirements

- SKSE and Address Library for SKSE Plugins
- SKSE Menu Framework 3 (for the battle plan; without it, saved orders and the INI still apply)

No ESP, and no art: the POTC Effects pack is Path of the Companions' now.

## Roles

A role is where a companion stands, whom they go for and how they move. What they attack with,
and how, stays their own (or what you chose for them, below).

| Role | Plays like | Goes for | Stands |
|---|---|---|---|
| Fighter | Melee that works round to the side of whoever the fight puts in front of them | Their own choice | Free |
| Tank | Holds the line before you, close in, without circling away | Whoever attacks the party | Guarding you |
| Healer | Stays back out of reach, falls back from anyone who closes | Whoever attacks the party | Hold back |
| Bruiser | Into the front line, giving no ground (needs a two-handed melee weapon) | The strongest enemy | Front line |
| Archer | Keeps range and falls back from melee; fights as a Fighter when out of arrows | Casters and archers | Hold back |
| Mage | Keeps range; out of magicka, falls back and waits for it | The weakest enemy | Hold back |
| Skald | Support bard with slowing lute notes, four interruptible Ljodhs and low enemy targeting priority; requires a lute and a 10-point unlock in POTC | Whoever attacks the party | Hold back |

The Skald preview adds four songs, eight upgrade branches and three traits. See
[Skald mechanics and validation](docs/SKALD.md). [Follower damage](docs/PARTY-BALANCE.md)
is controlled by one 0–100% slider, including when automatic party scaling is off.

Each role's focus (their own choice, your target, whoever attacks you or the party, casters and
archers, strongest, weakest, or one marked enemy) and position (free, guard you, hold back,
front line, with me) are only where it starts: change either in the battle plan. Whatever the
focus, an enemy attacking a companion up close is dealt with first. **Role strength** on the
Party page sets how far a role and a position move a companion; at 0 they only change whom
they fight.

Roles are set out of a fight. In one, the role box shows the role they fight as, with their
armor, damage and resistances beside it. Creature companions and summons cannot take a role;
they stay in the party display and use their own combat AI. NPC races are eligible unless
tagged as creatures, animals or dragons.

Click an enemy in the battle plan to send a companion (or everyone) after them. A companion
sent after someone goes, whatever their position: no guard radius or leash holds them back,
and melee companions press in like the front line. When that enemy falls they go back to
the focus they had.

## Before a fight

With **Auto battle** off (the default; a switch at the top right of the battle plan),
companions wait for a fight with their weapons away: an enemy in view brings nothing out
until it attacks, or you do. With it on, the companion who sees an enemy within 2500 units
starts the fight instead of waiting to be attacked, and the sight of one brings everyone's
weapons out (not while you sneak); they put them away a few seconds after it is gone, unless
yours are out. They draw once per sighting, so if the game puts their weapons away, they stay
away. Companions on Hold back wait for it to come to them, and nobody starts a fight while you
sneak. Wildlife that keeps to itself (a fox, an elk) is not an enemy until it is in a fight.

## Weapons and spells

Outside combat, open a companion's Loadout and choose what they fight with. Weapon pools list
the weapons they carry; choosing some limits the combat AI to those, without removing anything
from their inventory. Mages and healers also have a spell pool for each hand: spells they know,
or yours, which they are then taught for good. Chosen spells keep their hands on them, and one
the combat AI never casts on its own is cast for them when their hands have been idle a few
seconds. A two-handed ritual goes in both hand pools. Empty pools leave the choice to them.
Pools are kept in the save.

A Bruiser needs a carried greatsword, battleaxe or warhammer, and fights only with two-handed
melee weapons while they are one.

**Role weapons** (Settings > Battle plan > Roles, or `bRoleWeapons` in the INI's `[Loadout]`
section, on by default) keeps each role to its own weapons: a Bruiser to a two-hander, a Mage
or Healer to a staff beside their spells, and with Path of the Companions every role to its
path's weapons. Switched off, any role may use any weapon or shield the companion carries, a
Bruiser no longer needs a two-hander, and nothing is greyed out in the pools. Pools you choose
still apply, and the Skald keeps its lute.

## Path of the Companions (optional)

Path of the Companions is a mod of its own: a perk tree for each role, the abilities on it,
unique traits from a companion's own mod, and levels earned by fighting. With both installed,
the role you give a companion here is the path that counts there, the enemy you send them
after is the one their abilities go for, and Fight With Me carries out what the abilities ask
of a fight (a volley's target, a healer's circle to walk into, enemies a taunt holds). Its
panel opens on Numpad 9 (LB + D-pad right on a pad), beside the battle plan. Update both
together: Fight With Me 0.6.0 pairs with Path of the Companions 1.2, and neither connects to
the other's older versions.

### From Fight With Me 0.3

Saves carry over. Ability lifts still on when the game was saved come off. With Path of the
Companion installed, it takes up the levels companions earned by fighting, their cooldowns
and every ability switch. Without it, those levels go back to what the companions' own mods
give them.

## Controls

Choose **Oxblood** (the original red, still the default), **Vanilla**, or **Nordic** under
**Battle plan > Interface > Theme** in the Mod Control Panel. Vanilla uses translucent
charcoal panels, silver borders and white selection marks.
Nordic reskins the same windows with dark stone surfaces, engraved corners, beveled metal
emblems and inset selection highlights. Window placement, dimensions, typography metrics,
navigation and controls stay the same in every theme. The choice applies to
the battle plan, HUD notices and settings together, Path of the Companions' panel, and Walk With Me's wheel and settings. Changing the theme in any of the three menus updates the others and saves without a restart. Update all three DLLs together.
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

The battle plan and standalone Path of the Companions panel use a pointed metal
cursor matching the selected theme. It disappears on gamepad button, stick or
trigger input and returns on actual mouse movement, clicks or scrolling. Neutral
controller events and small stick drift do not change input mode. Closing these
panels leaves the framework's ordinary cursor handling intact.

## Translating

Everything the battle plan, the notices and the settings pages show can be translated, without
touching the DLL. The package has `SKSE/Plugins/FightWithMe/Translations/FightWithMe_ENGLISH.txt`:
one line per text, the English, a tab, then the English again. Copy it to the game's language
name (`FightWithMe_FRENCH.txt`, `FightWithMe_GERMAN.txt`, ...), translate the right-hand side,
and ship that file in the same folder. A `{}` is a value filled in as the game runs (`{0}`, `{1}`
put them in another order), `\n` starts a new line, and a line left untranslated stays English.
The language follows the game's own (`sLanguage` in Skyrim.ini); `sLanguage` under
`[Interface]` in FightWithMe.ini picks another. The log stays English. For Cyrillic, Chinese,
Japanese, Korean or Thai, switch the matching `Enable...` on under `[Fonts]` in
SKSEMenuFramework.ini so the menus have the letters.

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

`tools/BuildPackage.ps1 -VcpkgRoot <path>` builds, runs all registered tests, and creates
`FightWithMe-<version>.zip` and `POTC Effects-<effects-version>.zip` in `package/`.
Use `-EffectsVersion` when art changes; it can stay fixed for code-only updates. The Effects
archive includes a versioned file/hash manifest. Neither archive ships debugging symbols.

`tools/VerifyEffectAssets.py --skyrim <stock Skyrim Data directory>` validates every packaged
NIF and resolves its textures against only our assets and stock Skyrim archives. It also
checks all runtime model selections and rejects old external effect paths. See
[ability FX validation](docs/ability-fx.md) for mappings and remaining in-game checks.

## Split maintenance

After the split, update **both plugins and their shipped INIs**, preserving the
`*_user.ini` overrides. Paths owns the ability panel on **Numpad 9 / LB + D-pad right**;
this battle plan stays on **Numpad 7 / LB + D-pad down**. Abilities are intentionally
absent from the battle-orders panel. The full review, fixes, and latest effect changes
are summarized in [the release notes](docs/RELEASE-NOTES.md).

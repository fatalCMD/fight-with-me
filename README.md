# Fight With Me

Battle orders for your companions in Skyrim. Give each follower a role, a focus and a place in
the fight; the game's own combat AI carries it out. Tanks taunt, healers heal, and opening the
battle plan slows the world while you decide.

Pairs with [Walk With Me](https://www.nexusmods.com/skyrimspecialedition/mods/191283): Walk With
Me moves the party, Fight With Me fights with it. Either works without the other.

## Requirements

- SKSE and Address Library for SKSE Plugins
- SKSE Menu Framework 3 (for the battle plan; without it, saved orders and the INI still apply)

## Roles

| Role | Plays like | Ability |
|---|---|---|
| Fighter | Balanced melee that flanks | |
| Tank | Blocks, bashes and pulls enemies off the party; shields against archers shooting at them | Taunt: holds enemies up to the tank's level plus a quarter of it |
| Healer | Stays back and keeps everyone up, you first; walks over to anyone who is down | Heal: their own heal-other spell |
| Bruiser | Power attacks and staggers the big ones | |
| Archer | Keeps range, picks off casters first; fights as a Fighter when out of arrows | |
| Mage | Spell pressure, finishes the weakest; never conjures a bound weapon | |

Each role also sets a default focus (their own choice, your target, whoever attacks you or the
party, casters and archers, strongest, weakest, or one marked enemy) and position (formation,
free, guard you, hold back, front line, with me). Change either in the battle plan. Whatever
the focus, an enemy attacking a companion up close is dealt with first.

## Controls

Numpad 7 opens the battle plan (LB + D-pad down on a pad). Walk With Me's party actions grid has
a Battle plan tile as well. Party orders work without opening it: Numpad 8 Attack!, Numpad 6
To me!, Numpad 4 Hold back, Numpad 2 As you were. Settings are in the Mod Control Panel and in
`SKSE/Plugins/FightWithMe.ini`; each companion's orders are saved with your save game.

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

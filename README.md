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

Click an enemy in the battle plan to send a companion (or everyone) after them. A companion
sent after someone goes, whatever their position: no guard radius or leash holds them back,
and melee companions press in like the front line. When that enemy falls they go back to
the focus they had.

## Abilities

Every role has a special, with a button in the battle plan and an "on their own" switch:
Bodyguard (tank: runs to you when you are in trouble and taunts what is on you), Volley
(archer: every archer and mage fires at one enemy), War cry (bruiser: staggers everything close
around them), Ward (mage and healer: Oakflesh up to Ebonyflesh on you, by their level) and
Disarm (fighter: the Disarm shout's effect at their opponent). Tanks keep their taunt and
healers their heal.

Each companion's Potions switch lets them drink their own potions in a fight and brew what
they are short of when the party rests, better grades and rarer kinds as they rise in level.

## Controls

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

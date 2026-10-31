#pragma once
#include <array>
#include <windows.h>

// Mirrored in Walk With Me and Fight With Me. No import library or required peer.
// Getters return LOCAL snapshots; setters change LOCAL preferences on the game
// thread. Only an explicit menu edit broadcasts, so peers never call each other
// recursively and stale settings drafts cannot undo another menu's choice.
namespace CompanionThemeLink
{
    enum class Mod { Fight, Paths, Walk };
    struct Peer { Mod mod; const wchar_t* dll; const char* get; const char* set; };
    inline constexpr std::array peers{
        Peer{Mod::Fight, L"FightWithMe.dll", "FightWithMe_GetTheme", "FightWithMe_SetTheme"},
        Peer{Mod::Paths, L"FightWithMePaths.dll", "FightWithMePaths_GetTheme", "FightWithMePaths_SetTheme"},
        Peer{Mod::Walk, L"Wayfarer.dll", "WalkWithMe_GetTheme", "WalkWithMe_SetTheme"}};
    constexpr int Normalize(int theme) { return theme >= 0 && theme < 3 ? theme : 0; }

    // Deterministic startup precedence: Fight, then Paths, then Walk. Keep the
    // established Fight/Paths choice when adding Walk to an existing install.
    inline int Current(Mod self, int local)
    {
        for (const auto& peer : peers) {
            if (peer.mod == self) return Normalize(local);
            if (const auto module = ::GetModuleHandleW(peer.dll)) {
                using Getter = int (*)();
                if (const auto get = reinterpret_cast<Getter>(::GetProcAddress(module, peer.get))) {
                    const int theme = get();
                    if (theme >= 0) return Normalize(theme); // -1 means not loaded yet
                }
            }
        }
        return Normalize(local);
    }

    // Attempt every installed, theme-aware peer even if one cannot save.
    inline bool Share(Mod self, int theme)
    {
        bool saved = true;
        for (const auto& peer : peers) {
            if (peer.mod == self) continue;
            if (const auto module = ::GetModuleHandleW(peer.dll)) {
                if (!::GetProcAddress(module, peer.get)) continue; // older, unaware peer
                using Setter = bool (*)(int);
                const auto set = reinterpret_cast<Setter>(::GetProcAddress(module, peer.set));
                if (!set || !set(Normalize(theme))) saved = false;
            }
        }
        return saved;
    }
}

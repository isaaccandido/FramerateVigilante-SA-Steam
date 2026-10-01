# Framerate Vigilante SA Steam

Play **GTA San Andreas** on Windows 11 at 60 FPS, stable, and without the physics, swimming, door and mission bugs that high frame rates cause.

This repository has two parts:

1. **A setup guide** for getting the classic PC version of GTA San Andreas running well on Windows 11.
2. **Framerate Vigilante SA Steam**, a port of [Framerate Vigilante](https://github.com/GTAmodding/FramerateVigilante) by [Junior_Djjr](https://github.com/JuniorDjjr) to the Steam version of the game. The original only supports the 1.0 US executable, and on the Steam version it can break save loading.

> **GTA San Andreas only.** GTA III, Vice City and the Definitive Edition are not supported.

## Contents
- [Setup guide for Windows 11](#setup-guide-for-windows-11)
- [What the mod fixes](#what-the-mod-fixes)
- [Configuration](#configuration)
- [Troubleshooting](#troubleshooting)
- [Differences from the original](#differences-from-the-original)
- [For developers](#for-developers)
- [Credits](#credits)

## Setup guide for Windows 11

### 1. Check which version you have
Open the game folder (in Steam: right-click the game → **Manage → Browse local files**) and look at the executable's name.

| You have | Executable | Use |
|---|---|---|
| Steam | `gta-sa.exe` (hyphen) | SilentPatch + **this mod** |
| Rockstar Games Launcher | `gta-sa.exe` (hyphen) | SilentPatch + **this mod** (not tested yet, see note) |
| Original disc, or a 1.0 US downgrade | `gta_sa.exe` (underscore), about 14 MB | SilentPatch + the **[original Framerate Vigilante](https://github.com/GTAmodding/FramerateVigilante)** |
| Definitive Edition (2021 remaster) | — | Not covered by this guide |

> **Note:** this mod was mapped and tested against the Steam executable (md5 `5bfd4dd83989a8264de4b8e771f237fd`). If the Rockstar Games Launcher build differs, the mod skips every fix it can't verify instead of crashing, and its log shows which ones.

> **On 1.0 US?** Don't use this mod. Install SilentPatch (step 4), then the original Framerate Vigilante, which is distributed through [MixMods](https://www.mixmods.com.br) (its GitHub repository is source-only).

### 2. Back up your saves
Copy `Documents\GTA San Andreas User Files` somewhere safe before installing anything.

If your Documents folder is synced by OneDrive, the game can crash when saving or loading. Pause OneDrive while playing, or exclude that folder from sync.

### 3. Install the DirectX runtime (if needed)
The game needs `xinput1_3.dll`, which Windows 11 doesn't include. Steam usually installs it on the game's first launch. If the game complains that `xinput1_3.dll` is missing, install the [DirectX End-User Runtimes (June 2010)](https://www.microsoft.com/en-us/download/details.aspx?id=8109). It only adds legacy libraries next to the system DirectX and doesn't replace anything.

### 4. Install an ASI loader and SilentPatch
[SilentPatch](https://silentsblog.com/mods/gta-sa/) fixes dozens of engine bugs that cause random freezes and crashes on modern systems. It's the single most important fix.

1. Install an **ASI loader** (SilentPatch doesn't include one). Extract it into the game folder, next to the executable:
   - Steam or 1.0: [Silent's ASI Loader](https://silent.rockstarvision.com/uploads/silents_asi_loader_13.zip)
   - Rockstar Games Launcher: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest/download/Ultimate-ASI-Loader.zip)
2. Download [SilentPatch for San Andreas](https://github.com/CookiePLMonster/SilentPatch/releases/latest/download/SilentPatchSA.zip) and extract it into the game folder.

### 5. Install Framerate Vigilante SA Steam
1. Download the latest `FramerateVigilanteSteam-vX.Y.Z.zip` from [Releases](https://github.com/isaaccandido/FramerateVigilante-SA-Steam/releases).
2. Copy `FramerateVigilanteSteam.asi` and `FramerateVigilanteSteam.ini` into the game folder.

Remove any other Framerate Vigilante files (`FramerateVigilante.asi` / `.ini`) first. That's the 1.0 version, and it breaks the Steam build.

### 6. Adjust the game and Windows
- **In-game Frame Limiter: ON** (Options → Display Setup). The mod raises its cap from 30 to `FPSlimit` (default 60). With the limiter off, the frame rate is uncapped and the physics break above about 60 FPS.
- **Fullscreen optimizations:** right-click `gta-sa.exe` → **Properties → Compatibility** → check **Disable fullscreen optimizations**. This helps with alt-tab and display mode issues. No compatibility mode is needed.
- **Custom radio (User Tracks):** a large or unusual music library in `GTA San Andreas User Files\User Tracks` can crash the game during the audio scan. If the game crashes at startup, empty that folder to test.

### 7. Check that it works
Launch the game, load a save, then open `FramerateVigilanteSteam.log` in the game folder. Every line should end in `OK`. Use the Steam overlay's FPS counter (or RTSS) to confirm 60 FPS.

## What the mod fixes
- **Frame rate:** raises the frame limiter's cap to `FPSlimit` and removes the Steam version's 10 ms minimum frame delay.
- **Vehicles:** wheel spin on rails, burnout force, car slowdown, helicopter rotor speed-up (MixSets-compatible), Skimmer water resistance.
- **Doors and swinging parts:** bonnets, boots and the fire truck ladder (force, damping, angle).
- **Sirens:** tap the horn to toggle the siren, hold it to honk. Time-based instead of frame-based, and player 2 can toggle too.
- **Swimming:** swim speed, diving, sprinting back to the surface, player buoyancy.
- **On foot:** aiming while walking, pushing cars.
- **Automatic FPS limits** where the game still misbehaves at high frame rates (see [Configuration](#configuration)).

## Configuration
`FramerateVigilanteSteam.ini`:

| Setting | Default | Meaning |
|---|---|---|
| `[Settings] FPSlimit` | `60` | Frame limiter cap. The original mod is designed for 60 FPS max. |
| `[AutoLimitFPS] ForMinigames` | `1` | 30 FPS during pool and the date minigame |
| `[AutoLimitFPS] ForMissions` | `1` | 50 FPS in the DRUGS1 interior (prevents Big Smoke from getting stuck) |
| `[AutoLimitFPS] ForCutscenes` | `1` | 60 FPS during cutscenes |
| `[AutoLimitFPS] ForScriptedCutscenes` | `1` | 80 FPS during scripted scenes (black bars) |
| `[AutoLimitFPS] ForSchools` | `1` | 80 FPS in the driving, boat and bike schools |

Automatic limits only ever lower your cap, never raise it. At 60 FPS, only the minigame and DRUGS1 limits take effect.

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| The game hangs when loading a save after installing Framerate Vigilante | You're using the 1.0 version of Framerate Vigilante on the Steam build. Remove `FramerateVigilante.asi` and use this mod. |
| No `FramerateVigilanteSteam.log` appears | The ASI loader isn't installed, or the `.asi` isn't in the game folder (step 4). |
| The log says `SKIPPED` for some fixes | Your executable differs from the tested build, or another mod patches the same code. The game is safe; those fixes are just off. Please report it with the log and your executable's md5. |
| The log says `Executable does not match the mapped Steam build` | Wrong game version. See step 1. |
| Random freezes or crashes | Make sure SilentPatch and its ASI loader are installed (step 4). |
| Crash when saving or loading | OneDrive syncing the Documents folder (step 2). |
| Crash at startup | User Tracks folder (step 6), or `xinput1_3.dll` missing (step 3). |
| Physics glitches, can't swim or aim properly | Frame Limiter is off or `FPSlimit` is above 60. Turn the limiter on (step 6). |

## Differences from the original
- **Not ported:** the pause-menu limit (no effect at `FPSlimit` 60 or lower) and the optional `RefreshRate` setting (SilentPatch handles refresh rates).
- **Door force** uses `timestep × 0.6`, as the original's comments describe (its code multiplies by `× 1.667`). Behavior at 30 FPS is unchanged.
- **Car slowdown** scales the game's own handling value instead of a hardcoded 0.9, so handling mods still apply.
- **Buoyancy** keeps the Steam build's own timestep clamp (minimum 0.7) for everything except the player.
- **Implementation:** standalone C with no plugin-sdk dependency. Hooks are small machine-code stubs generated at runtime, and every address is rebased because the Steam executable uses ASLR.

## For developers

### Layout
| Path | Contents |
|---|---|
| `src/main.c` | The plugin, including every hook address and what it patches |
| `test/` | Native test harness and a Win32 shim to run it on Linux |
| `tools/make_test_image.py` | Builds the relocated game image the tests run against |
| `package/INSTALL.txt` | Install notes shipped in the release zip |
| `original/` | Upstream Framerate Vigilante source, reduced to its GTA San Andreas code paths. The complete upstream source, including GTA III and Vice City, is in this repository's history (commit `ed60ae8`) and [upstream](https://github.com/GTAmodding/FramerateVigilante). |

### Building
Requires [Zig](https://ziglang.org/download/) (or `pip install ziglang`).
```
build.cmd
```
This produces `build\FramerateVigilanteSteam.asi`.

### Testing
The harness compiles the same `src/main.c` for 32-bit Linux, loads a relocated copy of the Steam executable, applies every patch, then executes each generated stub natively and checks the results (88 checks at 30 and 60 FPS timesteps). Game files aren't included; generate the image from your own copy:
```
pip install pefile
python tools/make_test_image.py "<path to gta-sa.exe>" build/steam_reloc.bin
bash test/run.sh       # Linux or WSL2
```

### Releasing
GitHub Actions builds the plugin on every push. To release, set `VERSION_TAG` in `src/main.c`, commit, then push a matching tag:
```
git tag v1.0.2
git push origin v1.0.2
```
The workflow checks that the tag matches `VERSION_TAG`, then publishes a release with `FramerateVigilanteSteam-vX.Y.Z.zip` attached.

### How the addresses were found
Each hook site in the original was matched to the Steam executable through byte patterns, normalized instruction context, call-graph pairs, constant values and reference-count fingerprints of globals, then disassembled side by side to rewrite the hooks wherever the Steam compiler produced different code.

## Credits
- **[Framerate Vigilante](https://github.com/GTAmodding/FramerateVigilante):** Junior_Djjr / GTA modding (MIT). All fixes and their logic come from it.
- **[SilentPatch](https://silentsblog.com/mods/gta-sa/):** Silent (CookiePLMonster). Recommended, not included.
- **[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader):** ThirteenAG. Recommended for the Rockstar Games Launcher version, not included.

## License
MIT. See [LICENSE](LICENSE).

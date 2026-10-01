# Framerate Vigilante SA Steam

Play the **Steam / Rockstar Launcher** version of GTA San Andreas at 60 FPS without the physics, swimming, door and mission bugs caused by high frame rates.

This is a port of **[Framerate Vigilante](https://github.com/GTAmodding/FramerateVigilante)** by [Junior_Djjr](https://github.com/JuniorDjjr) (MixMods). The original only supports the 1.0 US executable, and on the Steam build it can break save loading. All the fixes and their logic come from the original; this project re-targets them to the Steam executable. The original source is kept unchanged in [`original/`](original/) for reference.

## Download
Download `FramerateVigilanteSteam-vX.Y.Z.zip` from the [Releases page](https://github.com/isaaccandido/FramerateVigilante-SA-Steam/releases). It contains the `.asi`, the `.ini` and install notes.

## Compatibility
- **Supported:** `gta-sa.exe` from Steam / Rockstar Launcher (tested build md5 `5bfd4dd83989a8264de4b8e771f237fd`).
- **Not supported:** 1.0 US `gta_sa.exe` (use the original mod) or the Definitive Edition.
- Works alongside SilentPatch.

## Install
1. Install an ASI loader (SilentPatch already ships one).
2. Copy `FramerateVigilanteSteam.asi` and `FramerateVigilanteSteam.ini` into the game folder.
3. Keep the in-game **Frame Limiter ON**. The cap is set by `FPSlimit` in the `.ini` (default 60).

Each patch checks the game's original bytes before writing and is skipped on mismatch, so an unexpected executable or a conflicting mod results in a skipped fix, not a crash. Results are written to `FramerateVigilanteSteam.log` in the game folder.

## What it fixes
- **Frame rate:** the cap is raised to `FPSlimit`; Steam's 10 ms minimum frame delay is removed.
- **Vehicles:** wheel spin on rails, burnout force, car slowdown, heli rotor speed-up (MixSets-compatible), skimmer water resistance.
- **Doors and swinging parts:** bonnets, boots and the fire truck ladder (force, damping, angle).
- **Sirens:** tap the horn to toggle the siren, hold it to honk (time-based; player 2 supported).
- **Swimming:** swim speed, diving, sprinting back to the surface, player buoyancy.
- **On foot:** aiming while walking, pushing cars.
- **Automatic FPS limits** for pool and the date minigame (30), the DRUGS1 interior (50), cutscenes (60), scripted scenes and schools (80). Each can be toggled in the `.ini`.

## Differences from the original
- **Not ported:** the pause-menu limit (no effect at `FPSlimit` 60 or lower) and the optional `RefreshRate` setting.
- **Door force** uses `timestep × 0.6`, as the original's comments describe (its code multiplies by `× 1.667`). Behavior at 30 FPS is unchanged.
- **Car slowdown** scales the game's own handling value instead of a hardcoded 0.9, so handling mods still apply.
- **Buoyancy** keeps the Steam build's own timestep clamp (minimum 0.7) for everything except the player.
- **Implementation:** standalone C with no plugin-sdk dependency. Hooks are small machine-code stubs generated at runtime, and every address is rebased because the Steam executable uses ASLR.

## Building
Requires [Zig](https://ziglang.org/download/) (or `pip install ziglang`).
```
build.cmd
```
This produces `build\FramerateVigilanteSteam.asi`.

GitHub Actions builds the same binary on every push. To release, set `VERSION_TAG` in `src/main.c`, commit, then push a matching tag (e.g. `git tag v1.0.2 && git push origin v1.0.2`). The workflow checks that the tag matches `VERSION_TAG`, then publishes a release with the zip attached.

## Testing
The harness compiles the same `src/main.c` for 32-bit Linux, loads a relocated copy of your Steam executable, applies every patch, then executes each generated stub natively and checks the results (88 checks at 30 and 60 FPS timesteps). Game files are not included; generate the image from your own copy:
```
pip install pefile
python tools/make_test_image.py "<path to gta-sa.exe>" build/steam_reloc.bin
bash test/run.sh       # Linux or WSL2
```

## How the addresses were found
Each hook site in the original was matched to the Steam executable through byte patterns, normalized instruction context, call-graph pairs, constant values and reference-count fingerprints of globals, then disassembled side by side to rewrite the hooks wherever the Steam compiler produced different code. Address notes are in [`src/main.c`](src/main.c).

## Credits
- **Framerate Vigilante:** Junior_Djjr / GTA modding, MIT license.

## License
MIT. See [LICENSE](LICENSE).

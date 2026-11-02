# SWROTS UNOFFICIAL CO-OP MOD

**Unofficial two-player couch co-op for the story campaign of *Star Wars: Episode III – Revenge of the Sith* on the original Xbox (NTSC-U).**

**How To Play**
With player 1 in **port 1**, plug a second controller into **port 2**. In any single-player story mission, after letting the level load for a few seconds, **hold Back on controller 2 for about a second** to join in; hold it again to leave. Player 2 starts every level as the game's normal computer companion, or spawns in a character if necessary and takes over from there. On the boss duels, player 2 takes over the boss.

**Fully Functional Player 2**
Player 2 is a fully functioning character with matching power set to Player 1 (where applicable, level 15 is a suprise!). Player 2 has his own health, force, and combo meter, and at the end of the level all experience/kills go to Player 1's Single Player save. Player 2 can also interact with consumables, secret objects, buttons, and story objects like doors and panels.

Player 2 is attached to player 1 by a tether and will be teleported back to him if he gets too far. Only player 1 can progress the level.

Player 2 can interact with some "force jump" circles but this can sometimes cause a softlock in the level so while it does work, you should have player 1 use most force jumps. Some levels may require both characters to jump, or just pull along player 2 with the tether.

**Full Drop-In Drop-Out Support**
On companion levels, drop out returns control of the companion to the level ai and returns their power set to normal.
On spawn in levels, drop out removes the spawned character from play but health and other values are stored.
On boss levels, player 2 can drop in and out as all boss characters including both Serra and Cin in Mission 12.

**Co-Op Camera**
When the second player drops in, the camera pans and pulls back to try to keep both characters in frame. This can sometimes result in the camera being behind level geometry.

This is a binary patch of the game's own executable (`default.xbe`) plus a replacement start-up notice screen. **It contains no game files**: you need your own copy of the game, and the patcher refuses anything but the exact NTSC-U release.

> Unofficial fan project. Not affiliated with, endorsed by, or connected to LucasArts, Lucasfilm Ltd. or Disney. *Star Wars* and *Revenge of the Sith* are trademarks of their owners.
> It was made with the help of documentation from jedijosh920's [SWROTS-PC](https://github.com/jedijosh920/swrots-pc) port, and with Claude AI.

## What you need

* Your own NTSC-U ISO of the game (or the extracted game folder). `default.xbe` must have MD5 `6460ef37862de3af364a0563a0a18c4e`.
* **Python 3.8 or newer** (no extra packages).
* [`extract-xiso`](https://github.com/XboxDev/extract-xiso), only for the ISO route.
* A way to run unsigned executables: a softmodded Xbox, or the [xemu](https://xemu.app) emulator. The patch does not recompute the executable's RSA signature, so a stock console will not run it.

## Installing

### 1. Patch an ISO (easiest)

Open a terminal in the `patcher` folder and run:

```
python couchcoop_patch.py "Star Wars Episode III.iso"
```

This unpacks the ISO next to the output, patches it and packs `Star Wars Episode III_CouchCoOpRedux.iso`. You need as much free disk space as the game takes. Put `extract-xiso` on your `PATH` or next to the script, or pass `--extract-xiso <path>`. Your original ISO is never changed.

### 2. Patch an extracted game folder

```
python couchcoop_patch.py --folder "C:\Games\SWEP3"
```

Patches the folder in place. The two original files are first saved in `C:\Games\SWEP3_original_files`. Then build the ISO the way you usually do (`extract-xiso -c`).

### 3. Only the executable (`default.xbe`), for people who know what they are doing

The `xdelta/default.xbe.xdelta` patch contains every byte this mod changes in the executable and nothing else. Apply it to the original `default.xbe` with any xdelta tool:

```
xdelta3 -d -s default_original.xbe default.xbe.xdelta default.xbe
```

Expected MD5s: original `6460ef37862de3af364a0563a0a18c4e`, patched `8ef778d829352457dad05d7abe785fa8`.
`python couchcoop_patch.py --xbe default.xbe -o patched.xbe` gives the same file without xdelta.

The start-up screen is optional and separate: it only needs a replacement of `movies/Splash_Movies/Legal_XBOX.sfd`, which the patcher builds from your own file's headers. Without it the mod works the same, you just see the original legal screen.

## Playing

* **Join / leave:** hold **Back** on controller 2 for about 1 second, any time in a story level. Each level starts with the game's own companion, and nothing is remembered after a restart.
* **Boss levels:** player 2 takes over the boss (levels 4, 9, 10, 12, 16, 17). The boss gets all his powers, and control goes back to the computer when player 2 leaves.
* **Levels with no companion** (5, 7, 8, 11, 13, 14) get a second character beside player 1.
* Joining is disabled in cutscenes and in the few scripted sections of gameplay that need one player.
* Level by level details and status: [source/CHANGES.md](source/CHANGES.md) ("Co-op rule per level").

### Status

All 17 Story Missions can now be played with a fully functional player 2 and each level has been initially run, but a full playthrough is still in progress. The author tested in xemu 0.8.139 (Windows x86_64) and on a softmodded original Xbox (levels 1, 6 and 14 on the console). If a story moment does not seem to progress, player 2 can try dropping out (hold Back) to let the game continue; please report it as an issue. Known limits:

* Level 15 (Yoda as the companion) has no saber throw; his black-button Force power works.
* On level 12, handing a boss back to the computer in the middle of phase 2 might leave the computer without some Force powers for the rest of that fight (not observed).
* The mod keeps a debug and script-recording block in memory on purpose (see `source/CHANGES.md`, "Left in on purpose"); it is used with `tools/xbwatch` (xemu only) to find scripted moments that need fixes.

## Reporting problems

Please open an issue (there is a form) and tell us:

* the mod version (the patcher prints it, and it is in the release name),
* where you played: xemu (which version) or a softmodded console,
* the level and the part of it (room, boss phase, cutscene),
* what player 2 was doing (companion, boss, second character, joining or leaving),
* what happened and what you expected. A short video is the best report.

Please do not attach game files, ISOs or a patched `default.xbe`.

## What exactly is changed

* `default.xbe`: a new 36 KB `COOP` section is appended, the game's per-frame function gets a jump into it, and a list of hooks and call replacements is applied. Every site's original bytes are checked before patching. The complete list with the original bytes and the reason for each is in [source/CHANGES.md](source/CHANGES.md); each patch has a header comment in [source/coop_m1.c](source/coop_m1.c).
* `movies/Splash_Movies/Legal_XBOX.sfd`: the first screen, replaced by a notice (10 s: 1 s fade in, 1 s fade out). The game does not care about a movie's length.
* Nothing else on the disc is touched.

## Building from source

```
cd source
./build.sh 2 _s0 0          # needs gcc and binutils with 32-bit x86 support (gcc -m32, ld -m elf_i386)
python3 patch_xbe.py default_original.xbe default.xbe --blob coop_s0.bin --sym coop_s0.sym
```

The result is byte-for-byte the file the xdelta patch produces. `patcher/coop.bin` and `patcher/coop.sym` are the build output of this source.
To redraw the start-up screen: `movie/legal_screen.py` (picture) and `movie/make_movie.py` (needs numpy, opencv-python, pillow and ffmpeg). The movie is plain MPEG-1 video; `patcher/sfdmux.py` wraps it in the game's Sofdec (`.sfd`) container.

## Tools

`tools/xbwatch` is a small GDB-stub client for xemu (started with `-s`) that reads the mod's debug block to record where scripted moments occur. It does not work on real hardware.

## Credits and licence

* Many ideas, offsets and the behaviour of the companion (tether, respawn, how a companion is taken over) come from **SWROTS-PC** by jedijosh920 (GPLv3), the unofficial PC port of the same game and executable. This mod is a separate implementation for the Xbox.
* Written with the help of Claude AI (Anthropic).
* The mod's code, patcher and tools are licensed **GPL-3.0-or-later** (see `LICENSE`).
* Fonts used for the start-up screen picture: [Cinzel](https://github.com/NDISCOVER/Cinzel) and [Inter](https://github.com/rsms/inter), both SIL Open Font License 1.1 (`movie/fonts`).
* [extract-xiso](https://github.com/XboxDev/extract-xiso) and [xdelta](https://github.com/jmacd/xdelta) are separate programs and are not included.

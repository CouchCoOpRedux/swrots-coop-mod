# SWROTS Xbox co-op: change list

Target: `default.xbe`, NTSC-U, MD5 `6460ef37862de3af364a0563a0a18c4e`. Everything below is applied by
`patch_xbe.py` to that file; the mod code is `coop_m1.c` (freestanding C), linked at VA `0x96E000` as a new `COOP`
section, with its debug block `g_dbg` at `0x976000` (section `.coopdbg`). The signature is not checked by softmods or
xemu; the section digests of touched sections are recomputed.

Build: `./build.sh 2 _s0 0` (the last argument: 1 = diagnostics build), check that it exits 0 (the linker reports an
overlap when the diagnostics blob outgrows its slot at `0x96E000..0x976000`), then
`python3 patch_xbe.py <original.xbe> out/default.xbe --blob coop_s0.bin --sym coop_s0.sym`.
The patcher refuses to patch if the original bytes at a site are not the expected ones or if the blob lacks any of the
hook / call symbols (a stale blob used to be skipped silently).

Version 36 changes no behaviour compared with version 35: it is the cleanup release (comments, the header's patch index, this file). The compiled code is byte-identical to version 35 apart from the version number in the debug block.

Player-two skins (P39 to P41, below) add three code hooks, two data words and a small config block (`g_skincfg`, `0x977800`). The block's layout is listed under P40.

Source tags: **PC port** = idea or addresses from SWROTS-PC (jedijosh920/swrots-pc, GPLv3);
**this project** = found in the original game's code and confirmed with xemu test dumps.

## Where it stands (version 36)

Every story level (1 to 17) has a co-op rule and has been played through with a second controller (level 1's bridge hand-off and level 6 confirmed last). What is left is the full-playthrough
testing with a friend, which will turn up scripted moments that need a small fix each (a row in `k_events`).

1. **Player two as a full player**: every ability and interaction a player has (Force powers, saber throw, pickups, XP, triggers, hazards, HUD, camera, tether, respawn) (P1 to P13, P21, P22). Done.
2. **Drop in and out**: each level starts with the computer's companion; holding Back on pad 2 (about 1 s) takes it over or gives it back; levels with no companion get one of ours, parked while player two is out (P12 to P20). Done.
3. **The shared camera** (P13, P22). Done.
4. **Level by level**: every level is recognised from its cutscene ids and has a co-op rule (P24 to P36); the bosses of the duel levels (4, 9, 10, 12, 16, 17) are taken over with all their powers (P14, P15, P35, P38); levels with a scripted moment that locks the game up get a small scripted fix (levels 1 and 5 so far). Done for every level the user has played through; the full playthrough is the open part.

## Code patches (what the original bytes were, what they do now)

| ID | VA | Original bytes | Replaced by | Purpose | Source |
|----|----|----------------|-------------|---------|--------|
| P1 | 0x504810 (D3DDevice_Swap) | `56 8b 35 68 fe 50 00` | `jmp coop_detour` (+2 NOP) | once-per-frame entry: save flags, regs, x87; `coop_tick`; replay the 7 bytes; continue at 0x504817 | this project |
| P2 | 0x151500 (health change) | `56 8b f1 8a 86 d6 05 00 00` | `jmp coop_hook_health` | a fatal hit on player 2 becomes health 1 + respawn | PC port |
| P3 | 0x152D30 (instant kill) | `55 8b ec 83 e4 f0 83 ec 58` | `jmp coop_hook_kill` | a void / kill-zone fall on player 2 becomes a rescue | PC port |
| P4 | 0x1A20C8, 0x1A2258 | `e8 23 10 f0 ff`, `e8 93 0e f0 ff` (call GetPlayer 0xA30F0) | call `coop_gp_trigger` | "Player Only" triggers accept player 2 | PC port / this project |
| P5 | 0x178070 (button query) | `a1 cc 43 69 00 56 8b 70 68` | `jmp coop_hook_pressed` | on the first query for player 2, put the Force script's instance into the player state (Obi-Wan 0x459A70..0x459B30, Anakin 0x324BD0..0x324D10); flags restored on drop-out. The boss rules P15, P35 and P38 and the recorder P37 run in the same hook | this project |
| P6 | 0x11881B, 0x11DE82, 0x2B8B11, 0x2B8B9E, 0x2B8C6D, 0x2B9524, 0x2B9532, 0x2B937F | `e8 .. .. .. ..` (call GetPlayer) | call `coop_pk_a` .. `coop_pk_g` | bacta tanks and power-ups accept player 2 | this project |
| P7 | 0x269325 | `e8 06 ff ff ff` (call 0x269230, add a kill's XP) | call `coop_xp_stub` | player 2's kills are credited to player 1 | this project |
| P8 | 0xBDA1B (IBreakable), 0x29481A (IPowerConduit) | `e8 d0 56 fe ff`, `e8 d1 e8 e0 ff` (call GetPlayer) | call `coop_dmg_brk`, `coop_dmg_cnd` | "Damaged Only By Player" accepts player 2's saber / hits | this project |
| P13 | 0x129990 (master camera SetTransform), 0x211C30 (renderer field of view) | `55 8b ec 83 e4 f0 83 ec 4c`, `a1 f4 93 61 00 85 c0` | `jmp coop_hook_cam`, `jmp coop_hook_fov` | shared camera: with player 2 in control the camera shifts towards the middle of the two players and pulls back (max 300 aside, 450 back, eased); the game's matrix is never edited (a bit-exact copy with the shifted position is passed on); only a placement within 2000 units of player 1 is treated as the game's camera; a placement identical to the one we gave is not shifted again; the shift is held off for 60 frames after a cutscene (the game moves the companion then and the tether needs a moment); a cutscene drops the shift at once instead of easing it away; the field of view is only read; the float code is built with `optimize("O0")` | PC port (SharedCamera, freecam.cpp) |
| P39 | 0x14F7E0 (mesh texture-name callback), 0x14F8A2 (just after its "is it declared" check), 0x527F0 (the packer's resource read); data words 0x648F0C (Obi-Wan's costume list) and 0x648B1C (Anakin's), both 0 | `a1 a0 41 69 00` (probe), `84 db 74 0c 5f`, `81 ec 90 00 00 00`; the two words 0 | `jmp coop_cb_probe`, `jmp coop_cb_force`, `jmp coop_read_hook`; both words = `0x57B6F0` (the game's own string `_var01`) | **Player two's skin.** Texture set 1 of the two costume lists gets the suffix `_var01`; the callback at 0x14F8A2 declares `<folder>\<name>_var01.stx` in the level's resource scene when the level does not ship it (the declare routine is the PC port's, ported); the packer read hook hands the game's texture decoder a recoloured copy from memory (see P40). Everything else runs the game's code | PC port (`resources.cpp` Declare) + this project |

## Data and logic changes (no code patched)

| ID | Where | What | Source |
|----|-------|------|--------|
| P9 | character +0xB5F (mastery on), +0xAFD (player-2 slot flag) | set to 1 while player 2 is in control, restored on drop-out: gives the mastery ring and kill ratings | this project |
| P10 | character +0x1024..+0x105F (two arrays of 7 dwords) | copied from player 1 on take-over, restored on drop-out: the companion has player 1's unlocks | this project |
| P11 | options object [0x68F720] +0xCC..+0xCE | non-zero = the game's own co-op / Versus mode: the mod does nothing | this project |
| P12 | `coop_tick` | each level starts with the companion under the game's control; holding Back on pad 2 (about 1 s) takes it, holding again hands it back | this project |
| P14 | `coop_tick`, `register_boss`, `release_boss` | a mission whose second player is a boss (slot 2 = the game's boss, targets player 1, e.g. Dooku): holding Back on pad 2 sets the boss's control mode to player and binds pad 2; holding again gives him back; none of the companion extras apply; if he dies he cannot be taken again, but the next boss the game names can be (P36) | PC port (boss path) + this project |
| P15 | Dooku's Force script instance (script at 0x370000..0x37AC00), reached through the P5 button-query hook | while player 2 plays Dooku the last phase's flags of the game's own phase handler (0x378710) are kept on: 0x2D9 0x2DB 0x2DC 0x2DD 0x2DF 0x2E2 0x2E3 0x2E4 0x2E6 on, 0x2E7 off; the instance's bytes are saved and given back on release (no code patched) | the game's own code |
| P16 | Every object's "active" state: byte +0x119 and zone slot +0xD8 (the game's deactivate routine at 0xF29F0 sets 0 and -1, clears +0xD4 and sets +0x1C2; the reactivation at 0xF2A50 sets +0x119 again) | a switched-off companion is not taken (on AUTO levels the search waits for him, no spawned copy); player two's character that is switched off for 10 frames is given back to the game and taken again once active and out of a cutscene (Back while waiting drops out). Level 5: Obi-Wan is hidden during Anakin's solo section; the take-over used to keep a hidden body following player one (no code patched) | level 5 memory dumps before / after he vanished |
| P17 | Class names (every character the sweep meets, player 1, player 2) | read from the code at vtable slot 3 (`mov eax,<name>; ret`, true of all 302 named classes) instead of calling it; an object whose slot 3 is anything else (being built or torn down by the loader) has no name and is skipped; the sweep loop checks every page it enters (no data change) | crash dump at the start of level 5 (EIP in the sweep, ESP 8 above the frame's usual, loop counter overwritten) |
| P18 | The game's own deactivate (0xF29F0) and activate (0xF2A50) routines, both thiscall with no stack arguments | a spawned hero can be **parked**: deactivated (hidden, no updates) when player two drops out, activated, placed beside player one and taken again when Back is held again. (The first design, swapping between the level's own companion and a spawned substitute as the level switched him on and off, fought with the level's scripts and was dropped for P19.) Back is also the manual way to take the spawned Obi-Wan out of a scripted spot such as level 5's turret until the act-1 scripting pass does it (no code patched) | level 5 dumps; the routines' disassembly |
| P19 | The same two routines plus the "M<dd>_" histogram of the sweep (every build) | one strategy per level, chosen at the end of the first Back sweep from the level key (`k_strategy[]`): AUTO (levels 1 to 4, 6 and unknown levels) hijacks the mission's companion, spawning one only when none is found; SPAWN (level 5 = M04) always uses our own spawned hero and never touches the mission's Obi-Wan. (Logic only) Since P28 the strategy is chosen when the start-of-level scan names the level, not at the end of the first Back sweep, and levels 7 to 14 have rows too (P31). | level 5 tests: two Obi-Wans on screen are fine; the dropped-out spawned companion must not stay as a computer ally |
| P20 | none (logic; the tether constants are scaled) | the tether (leash and far distances) is scaled per level key by `k_leash[]` (percent of normal): level 5 (M04) 60 %, level 3 (key 16) 70 %, others 100 %. Applied when the first Back sweep has identified the level Since P28 level 3's key is 3 (the user's level number). | level 5 and 3 cameras are close; user request |
| P21 | Four calls to GetPlayer 0xA30F0 in the IPain hazard zone class (original `E8` + rel32 to 0xA30F0 at each): 0xFF0AA and 0xFF0C1 (touch handler 0xFF070: "affects player" / "affects others" flags 0x1E0 / 0x1E1), 0xFF106 (same handler), 0xFF2AC ("can this object be hurt" 0xFF280, used by IPain and IElectricalDischargeZone) | each call goes to `coop_pain` (coop_pk with the object in ESI): while player two is taken over he counts as "the player" in those checks, so zones set to hurt only the player, or whose "affects others" flag is off, hurt him too. Found on level 5: the electrical discharge hurt Anakin but not the spawned Obi-Wan (dump: teams 0xFFFFFFFF / 0x8000, flags differ only in 0x08000000; IObiwan's team-mask virtual 0x6AA60 always returns true, so it was not the teams) | disassembly of 0xFF070 / 0xFF280 / 0x24BE00 (the discharge zone's touch callback) |
| P22 | none (change inside coop_cam, P13) | the "game placed the camera where it already is" test (paused) compares only the position words (matrix words 12..14) instead of all 16: in the pause menu the game hands back the position we gave it but other words differ, so the whole-matrix test failed and our shift was applied again on every placement (the pause backdrop crept away, in bursts). Diagnosed from the placement log: squared distance to our last output was exactly 0 on every logged placement | pause menu dump (8 placements, all 0.0) |
| P23 | none (level key; the histogram of P19) | the level key tells user level 3 (mission M02B) from level 1: M02B borrows M01's strings, so key 16 (`KEY_M02B`) is used when `"M02B_"` strings outnumber `"M02A_"` (at least 8), or when the M01 count is below 120 (level 1 counted ~500, level 3 ~26). Gives level 3 its 70 % tether. **Heuristic, to be checked on level 3** (`sub[9]` = 0x10) **Replaced by P27 / P28 (cutscene ids); the histogram no longer exists.** | level 1 / 3 histograms |
| P24 | none (logic only; reads the cinematic cell `0x695C88`) | **Level 5 ends with the turbolaser room, which player 2 cannot join.** When the cutscene before it (ICutscene id `0x1F8FB7`, found with xbwatch) has ended on level key 4, player 2 is let go like a drop-out (the spawned Obi-Wan is switched off; the mission's own Obi-Wan is never touched) and Back on pad 2 does nothing until the level ends or player 1 has been dead ~20 frames (a restart from a checkpoint). If the key is not yet known (Back not yet held on this level) the cutscene is remembered and the lock applies when the first Back sweep decides the key is 4. `FINAL_CUT_PARK_AT_START` = 1 parks at the cutscene's start instead | level 5 recordings |
| P25 | none (layout and logic) | **Room for more scripting.** The code slot is now 32 KB (`0x96E000`-`0x976000`; was 16 KB), the debug block `g_dbg` moved to **`0x976000`** (every debug address above is `+0x4000` compared with versions 17/18) and the object scan starts at `0x977000`. The lock of P24 is now a table row (`k_events`: cutscene id, level key, action; actions `EV_LOCK_AFTER`, `EV_LOCK_START`), so a new scripted event is one row (about 8 bytes) plus, for a new kind of action, a case in `event_watch` / `end_coop`. The `COOP_DIAG=1` build fits again | version 18 size check |
| P26 | none (logic only; reads two flag words per frame) | **Level 1's scripted walk.** The story orders the computer-controlled Obi-Wan to a spot and only then plays a cutscene; with P2 playing him he never goes. When trigger `0x205ED3` (ITriggerMulti, word `+0x9C` bit 31) fires and cutscene `0x205EFB` has not played, P2 is dropped out automatically, Back does nothing, and after cutscene `0x1F0EB0` has ended (or 2400 frames after the first started) P2 takes the companion again by itself. If P2 was already out, nothing is taken. The two objects are found once per level by class + id (a pass over memory of about 130 frames after the level settles), then one word of each is read per frame. A restart (P1 dead ~20 frames, or a new P1 object) resets it | graph dump of level 1, level 1 recordings |
| P27 | none (logic only) | **Level identification by object ids, and the bridge cutscene.** (1) One scan of memory per level (about 130 frames after the level settles) remembers every cutscene (ICutscene) and multi-trigger (ITriggerMulti) object by id. The cutscene ids are the level's signature (`k_sigs`: level 1 has 17 ids, level 5 has 2): two or more ids of one level and no equal rival give the level key at once, before anyone presses Back, and beat the old string counts (which stay as the fallback for levels without ids). The P26 window takes its objects from the same lists. (2) New event action `EV_AI_DURING`: cutscene `0x1EC91C` (level 1: Obi-Wan crosses the bridge as it connects) is played with the companion under the computer's control: P2 is dropped when it starts, Back does nothing, and P2 takes the companion again about 10 frames after it ended | recordings of level 1 |
| P28 | none (logic only; removes code) | **The string histogram is gone.** The level is named only by the ids of its cutscenes (levels 1 to 6 so far; keys are now the user's level numbers: level 3 is 3, level 5 is 5). It is named by the scan about 3 seconds after the level starts, so the first Back needs no 64 MB sweep to learn the level, and Back is ignored until that scan is done. A level without ids gets no key and the backup action is nothing: ordinary take-over, normal tether, no scripted events. Level 5's spawned-companion strategy is chosen as soon as the level is named (the same result as at the end of the Back sweep before) | levelid.bat logs of levels 1 to 6 |
| P29 | none (table only) | **Levels 7 to 17 are named.** `k_sigs` holds the cutscene ids of every story level (the user's level numbers 1 to 16, from `levelid.bat` logs). Levels 16 and 17 are one level file (the same five ids), so both get key 16 and only player 1's character tells them apart (Obi-Wan on 16, Anakin on 17) | levelids.txt, levels 2 to 17 |
| P30 | none (reads only; **removed in version 29**, see P33) | **Costume probe** (versions 25 to 28). At the end of the start-of-level scan every resource scene found (vtable `0x563808`) was asked the game's own "is this resource declared" (`SceneIsDeclared` 0x4BDA0, mesh type 4) for the 68 costume meshes of the XBE's class costume lists, plus a control path nobody has; `levelid.bat` printed the answers by class and costume. Result: 61 to 146 objects share that vtable, the first four by address were placeholders or empty, and the level's real scene was never among them, so it could not say what a level has loaded | levels 7, 8, 11, 13, 14 |
| P31 | none (logic and tables) | **Who player two is on a spawn level** is a row of `k_body[]` (next to `k_strategy[]`): BODY_OPPOSITE (the other hero), BODY_COPY (a copy of player 1's class; costume / texture set -1 = player 1's own, else a number from the class's costume list) or BODY_CLASS (a named class and costume). Level 5 is BODY_OPPOSITE (Obi-Wan beside Anakin); levels 7, 8, 11, 13 and 14 are BODY_COPY with player 1's costume. After trying to find out what models could be given to player two through scans and tests (the probe above, since removed, then spawning the plain, cloaked and hood-down costumes of the same hero on levels 7, 8, 11 and 13), only the body player 1 wears turned out to be loaded: the other costumes did nothing, and level 14, where the costume matched player 1's, was the one that spawned. So player two is a copy of player 1's model on those levels. The other hero (BODY_OPPOSITE) was not tried there; characters a level did not declare could only be had by porting the PC port's declare-from-other-PAKs loader, which was judged too large for now | tests of v27 on levels 7, 8, 11, 13, 14 |
| P32 | none (tables only) | **Every level has a co-op rule** (`k_strategy[]` names levels 1 to 16; 17 uses 16's). AUTO: take over the mission's companion (1 to 4, 6, 15 = Yoda) or the game's own boss slot on the duel levels (4, 9, 10, 12, 16, 17: the game manager says it has a second player, as for Dooku); SPAWN: our own character beside player one (5, 7, 8, 11, 13, 14). The table of rules per level is in *Co-op rule per level* below | spawn tests; first tries of 9, 10, 12, 15, 16, 17 |
| P33 | none (removes code) | **The costume probe is gone.** On level 7 the game froze (EIP `0x55BF4`, inside the resource map lookup; the map's "count" was a pointer into free heap memory, CR2 `0x05582250`): the probe called the game's lookup on objects that only looked like scenes, and a stale one has a freed table. Nothing used its answers once the decision for P31 was made, so the scene scan, the probe and its tables were deleted (the cutscene / trigger scan stays) | level 7 freeze report |
| P34 | none (logic only) | **The second HUD empties on spawn levels again.** The handlers of slot 2 (the vitals' fade and the portrait) used to be found only by the Back sweep; since P28 picks a spawn level's strategy at the start-of-level scan, that sweep never ran there and the HUD icon and Force bar stayed after player two dropped out (levels 5, 11, 13; level 2, which takes the mission's companion, was fine). The scan now collects the same handlers (`s_hud`), and `hide_p2_hud` finds them on every level | user tests of v28 on levels 2, 5, 11, 13 |
| P35 | none (data writes through the P5 hook) | **Anakin and Obi-Wan as the duel boss get all their powers.** On level 16 the boss Anakin had no saber throw at the start; level 17's boss Obi-Wan may have lacked the mind trick. Their scripts switch powers on phase by phase for the computer, and the Force-script fix only knew companions (P5) and Dooku (P15). While player two plays a hero as the boss, the flag set of a player-controlled hero (P5's) is kept on at every query, the marker byte `0x1D6` is left alone, and `release_boss` hands the saved bytes back | user test of v30 on levels 16 and 17 |
| P36 | none (logic) | **A second boss after the first one died.** On level 12 Serra is fought first and Cin second. Once the boss player two had was dead, the boss take-over was switched off for the level, so Cin could not be taken. It now remembers the id of the boss that died and switches back on when the game names a different character as its second player | user test of v31 on level 12; confirmed in v32 |
| P37 | none (the button-query hook only records) | **Script recorder, left in on purpose.** Each query for player two's buttons notes which stretch of code asked, the button ids and the script instance's bytes `0x1D0..0x2EF`, per character (3 characters, 16 code ranges); nothing in the game is changed. Read with xbwatch `scriptid` (`scriptid.bat`, needs mod version 34 or later). It found the Force flags of Serra and Cin (P38) and showed that Yoda's script already has the saber throw flag on. The first copy is taken before the fix writes (v35), so it shows what the script itself holds. Versions 32 and 33 had 4 entries of 668 bytes and Serra alone filled them, so Cin was not recorded; version 34 split characters and code ranges. About 650 bytes of code and 2 KB of the debug block | user tests of v31 to v34 on levels 12 and 15 |
| P38 | none (data writes through the P5 hook) | **Serra and Cin as the boss get saber throw and mind trick.** The recorder showed Serra's second phase turns on the Force-flag bytes `0x2D9 0x2DE 0x2DF 0x2E3 0x2E4` of her script instance (global `0x6964C0`, shared by her four scripts around `0x3C2FD1`, `0x47926F`, `0x47C8FB`, `0x47E68B`; before: `01 00 00 00 01 00 00 00 00 00 01 00 00 01 01 01`, after: `01 01 00 00 01 00 01 01 00 00 01 01 01 01 01 01` for `0x2D8..0x2E7`). While player two plays Serra or Cin those five are kept on, whichever code asks. Cin's script instance is another object (`0x1604A20`) with the same layout; the same five give him both powers. On release the bytes the script held when player two took the boss are put back (v33 and v34 left them on, and Cin used the mind trick on player one in his first phase) | recorder output of v32 to v34 on level 12; confirmed in v35 |
| P40 | `g_skincfg` at `0x977800` (section `.coopcfg`, 248 bytes), the disc, the DXT blocks of the textures | **Recolour at load.** On the first request of a skin texture the mod reads the original `.stx` from the player's own level PAK on the disc (`NtCreateFile` / `NtReadFile`, sector aligned; `\??\D:\pak\...`, then `\Device\CdRom0\...`), checks name, format and size, and shifts the two RGB565 endpoints of every DXT block (hue / saturation / value, integer HSV; the indices stay; optionally only inside a rectangle). DXT1 four-colour blocks keep c0 > c1 (equal ends are nudged, a flipped order swaps the ends and XORs the indices with `0x55555555`); three-colour blocks swap and toggle the indices whose high bit is 0; DXT5 colour blocks (at +8) ignore the order and leave alpha alone. The result is named `<name>_var01` and served from memory. **The mod contains no game pixels.** Layout of `g_skincfg`: magic `SCKF`, version, flags (bits 0-4 part unreadable, bits 8-12 wrong texture), last NTSTATUS, then 5 x 10 words (enable, hue, sat, val, x0, y0, x1, y1, made, served), then 8 debug words (loader calls, level key, part, stage 1 off / 2 no source / 3 disc read failed / 4 wrong texture / 5 ok, callback calls, folder, name). Parts: 0 `obi_02` (Obi-Wan tunic), 1 `obiCloak`, 2 `anakin_01` (DXT5, the tunic), 3 `anakinCloak`, 4 `anakin_cloth` (256x128, the trousers). Defaults: Obi tunic hue 0 sat 330 val 58, Obi cloak hue +15 sat 28 val 200, Anakin tunic sat 25 val 100, Anakin cloak sat 15 val 120, Anakin trousers sat 12 val 90. The numbers are read when a level asks for the texture, so they can be changed with xbwatch `skin` / `skin.bat` while the game runs and take effect when the level is loaded again | this project; user tests on levels 7, 8, 13 (Obi-Wan), 11 and 14 (Anakin) |
| P41 | `k_skinsrc[]`, `k_part[]`, `k_body[]` (tables) | **Where the originals are** (the level key, the PAK, the offset of the `.stx` inside it): level 7 `res_m06a_uta_separatisthq.pak` (obi_02 8811990, obiCloak 8845228); level 8 `res_m06b_uta_controlroom.pak` (obi_02 8407534; plain Obi-Wan, no cloak texture); level 13 `res_m11a_uta_order66.pak` (obi_02 8561555); level 11 `res_m09a_cor_temple.pak` (anakin_01 9025048, anakinCloak 9093577, anakin_cloth 9008190); level 14 `res_m12b_mus_assassin.pak` (8032660, 8101189, 8015802). `k_body` rows of those levels use texture set 1 for player two. Level 5 (Obi-Wan beside Anakin) has no skin: he is already a different hero | PAK listings of the user's disc files |

Also in `coop_tick` (all from the PC port's `coop.cpp`, re-done in C): finding or spawning the companion, tether and
respawn near player 1, shield after a respawn, HUD slot for player 2, controller binding.

## Debug block (read from the xemu monitor with `x/Nwx <address>`)

`g_dbg` is at `0x976000` (section `.coopdbg`); its layout is the same in every build, only the (DIAG) parts need the
`COOP_DIAG=1` build. Nothing in the game reads it. Addresses (version 36):

| Address | Field | What |
|---------|-------|------|
| `0x976008` / `0x97600C` | `frames` / `state` | frame counter / what the mod is doing (`ST_*` in the source: 0 idle, 1 settling, 2 no pad 2, 3 no companion, 4 registered, 5 game has its own P2, 6 lost, 7 cutscene, 8 scanning, 9 dropped out, 10 spawned, 11 level 5 over (P24), 12 scripted walk (P26)) |
| `0x976010` | `player1`, `player1Id`, `player2` | addresses / id |
| `0x976440` | `lvl[48]` | P27 start-of-level scan: [0] level key the cutscene ids gave (0xFF none), [1] cutscenes found, [2] multi triggers found, [3] scans done, [4..27] cutscene ids, [28..47] per level key how many signature ids matched |
| `0x97603C` | `spare` | P26 scripted-walk window: bits 0-3 state (0 searching, 1 waiting for the order, 2 companion walking, 3 first cutscene seen, 4 over), bit 4 Back locked, bit 5 P2 goes back in afterwards |
| `0x976050` | `misc[4]` | [0] page-table state (0x600D0001 good), [1] CR3, [2] page-table base / last spawn, [3] Back button state |
| `0x976060` | `rows[8]` | characters the last sweep found (32 bytes each: ptr, id, name, control, flags) |
| `0x976160` | counters | leashes, respawns, rescues, replaced, last distance, hooks |
| `0x976180` | `fx[8]` | Force script fix: [0] times applied, [1] script instance, [2..6] bytes before, [7] last caller |
| `0x9761A0` | `st[64]` (DIAG) | shared camera and spawn counters: [3] why no camera target (1 no guard, 2 characters, 3 position, 4 no fov, 5 fov range, 6 tan, 7 bad pointer, 8 not story, 9 non-finite, 10 waiting after a cutscene, 0 ok), [40] `0x976240` player 2 came from (1 spawned, 0 existing), [44] `0x976250` Force fix mode, [46] `0x976258` marker written back, [56] `0x976280` placements skipped, [57..59] `0x976284` last placement the game gave, [60..62] `0x976290` our shift, [63] `0x97629C` repeated placements skipped |
| `0x9762A0` | `lv[20]` | reserved, always 0 since version 29 (was the P30 costume probe: `[3*i..3*i+2]` one bit per mesh of `k_meshes` that scene i declares (i < 4), `[12+i]` its address, `[16]` bits 0-3 scene i says yes to a path nobody has, bits 8-15 meshes asked, `[17]` scenes found, `[18]` / `[19]` P1's costume / texture set) |
| `0x9762F0` | `lvx[4]` | [0],[1] unused; [2] `0x9762F8` id of the last cutscene seen (P24/P27); [3] `0x9762FC` flags: bit0 an event's cutscene was seen, bit1 its trigger point is reached, bit2 player 2 locked out, bits 8-15 level key, bits 16-23 the event's row in `k_events` + 1 |
| `0x976300` | `sub[12]` | spawned companion: [4] address, [5] flags (1 parked, 2 registered, 4 alive, 8 active), [6] the level's own companion, [7] its flags, **[9] `0x976324` = the level key (the user's level number; `0xFF` = not known) the last Back sweep decided (0xFF = not known)**, [10] spawn attempts failed, [11] unpark placements; [0..3] counters (DIAG) |
| `0x976330` | `lvp[20]`, `0x976380` `lvi[10]` | reserved, always 0 since version 22 (the DIAG level probe was removed) |
| `0x9763A8` | `hz[12]` (DIAG) | hazard comparison, three characters x {AI teams +0x214, flags +0x0C, zone +0xD4, AI side | invincible<<8 | control<<16 | active<<24}: player one, spawned companion, mission companion |
| `0x9763D8` | `cut[26]` (DIAG) | cutscene log: [0] number started, then a ring of the last 5 (entry (number - 1) % 5 at [1 + k*5]): {start frame, end frame (0 while it plays), player one x, y, z bits} |
| `0x976500` | `spc[3]` (P37) | script recorder, one per character player two played, 592 bytes each: name, script instance, query count, instance bytes `0x1D0..0x2EF` at the first query and every 64th after; read with xbwatch `scriptid` |
| `0x976BF0`..`0x976D70` | `spr[16]` (P37) | the stretches of code that asked for a character's buttons, 24 bytes each: `lo`, `hi`, queries, button id bit mask (ids 0..63), character entry + 1 (version 32 and 33 had 4 entries of 668 bytes, all filled by Serra alone, so Cin was never recorded) |

The debug block's own size is 0xD70 bytes (its slot is 0x1000). The earlier watchers (button taps, Force gates, combo add-hit, input history,
character snapshots, the mastery gauge scan, pickup and XP counters, the turret object scan and the changing-word ring)
answered their questions and were removed in the version 17 cleanup.

## Level identification (the level selector)

How the mod knows which level it is in, so a level can have its own rule.

* **Method.** Every level file has its own cutscene objects with fixed ids. About 3 seconds after a level starts (60 settled frames, then a scan of
  memory in 0.5 MB slices, ~130 frames, no per-frame cost afterwards) the mod remembers every cutscene (ICutscene) and multi-trigger (ITriggerMulti)
  object by class and id. The level is the one in `k_sigs` with two or more of those ids and more than any other level. The key is the user's level
  number (1 to 16; level 17 shares 16's file and key). The same scan hands the scripted events their objects (P26, P27).
* **When it runs.** At the start of every level, on its own. Back on pad 2 is ignored until it is done, so the first Back never needs a sweep to learn the level.
* **Not recognised** (a mod level, an unlisted level, a scan that found nothing): key 0xFF and a backup action of **nothing**: the ordinary take-over, the
  normal tether and no scripted events, so nothing level-specific can fire where it should not.
* **Ids per level** (from `levelid.bat` on the real game): 1 (17), 2 (15), 3 (11), 4 (5), 5 (8), 6 (4), 7 (14), 8 (14), 9 (4), 10 (4), 11 (12), 12 (4),
  13 (12), 14 (21), 15 (10), 16 and 17 (5, identical). No id occurs in two levels except 16 / 17.
* **Replaced idea.** The first version counted "M<dd>_" mission strings left in memory (a histogram). It named levels wrongly now and then (level 3 borrows
  level 1's strings, leftovers of other missions) and needed a 64 MB sweep; the cutscene ids replaced it (P27, P28).
* **To check a level**: wait a few seconds after it has started, then `levelid.bat` (or `x/1wx 0x976324` for the key).

## Co-op rule per level

| Level | Rule for player two | Status |
|-------|---------------------|--------|
| 1 | AUTO: take over Obi-Wan; his scripted walk (P26) and the bridge cutscene are handled automatically (P27) | confirmed: the walk and the bridge cutscene hand-off both work (softmodded console and xemu) |
| 2, 3 | AUTO: take over the companion (level 3 tether 70 %) | playable |
| 4 | AUTO: Dooku duel, boss take-over (P14, P15) | confirmed |
| 5 | SPAWN, the other hero (Obi-Wan beside Anakin); tether 60 %; locked out after the cutscene before the turbolaser room (P24) | confirmed |
| 6 | AUTO | confirmed: played through (three rooms of enemies, no scripting needed) |
| 7, 8, 11, 13, 14 | SPAWN, a copy of player 1 with texture set 1, a recoloured skin (P39 to P41): Obi-Wan brown tunic and sandy-grey cloak (7, 8, 13), Anakin near-black tunic, grey cloak and grey trousers (11, 14); the HUD empties on drop-out (P34) | confirmed: the skin shows on 7, 8, 13 (Obi-Wan), 11 and 14 (Anakin, first numbers; trousers added in v36r7 are not yet reported) |
| 9 | AUTO: Grievous boss take-over; all his powers work, the fight's triggers follow his health | confirmed |
| 10 | AUTO: Mace Windu boss take-over | confirmed |
| 12 | AUTO: boss take-over, two opponents in turn (Serra, then Cin: the one in the game's second slot, P36); both get saber throw and mind trick (P38) and give them up again on hand-back | confirmed (v35) |
| 15 | AUTO: take over the companion Yoda. He has no saber throw to give (his script already has the flag on and forcing more changed nothing; accepted); his black-button Force power is a mind trick of his own. His class lacks a saber duelist's reaction animations (the PC port patches the blocks), so he may float when blocking a saber, and the computer's own commands may still run for a moment | plays; to be tested further in the full playthrough |
| 16, 17 | AUTO: boss take-over (16: P1 Obi-Wan, boss Anakin; 17: P1 Anakin, boss Obi-Wan); the boss hero gets every power (P35) | confirmed: all powers present and control hands back |

## Player-two skins: why the textures are recoloured while loading

The aim was for player two to look different from player one on the spawn levels. Short history of what we tried:

1. **Costume lists / other bodies** (P31): only the costume player one wears is loaded; a different costume or hero is simply missing from the level's resource scene.
2. **The engine's texture sets.** Each costume list has suffix slots; set 1 asks for `<name>_var01`. The callback at 0x14F7E0 only picks a set whose textures the level already declared, and no PAK has `_var01` files. A forced-name test (v36f) made player two's Obi-Wan the engine's green default texture: so a texture can be bound per instance, and what is missing is the texture itself.
3. **Supplying the texture**: declare it in the scene ourselves (PC port's Declare, ported) and serve it from the packer's read hook (0x527F0) through the engine's memory file. Works.
4. **Where the pixels come from.** v36k carried repainted textures inside the mod and worked, but that puts game-derived pixels into a public release. Replaced (v36r) by reading the original from the player's own disc and shifting the colours of its DXT blocks when it is requested: nothing from the game is shipped, and the numbers are tunable. Fully painted skins can still be made as a private build.
5. **Things that went wrong on the way**: m11a (level 13) is an Obi-Wan level, not Anakin's (Anakin's are m09a / level 11 and m12b / level 14), and the first recolour build had it under key 11 so the loader never ran (the debug line in `skin.bat` showed "made 0"); DXT1 four-colour blocks must keep c0 > c1 or the picture inverts; the boots and face share Obi-Wan's `obi_01` / Anakin's `anakin_01` with the cloth, so only the parts that are their own textures are recoloured (Obi-Wan's tunic, both cloaks, Anakin's trousers `anakin_cloth`) or a rectangle is used; Anakin's tunic was already nearly black, so dark numbers hardly showed (defaults now sat 25 val 100).
6. **Tuning while the game runs** (xbwatch `skin`, `skin.bat`): the table at 0x977800 is written through the GDB stub. xemu is paused while a connection is open, so the first interactive version froze the game; connections are now opened per command. Changes apply when a level is loaded again.

## Tried and removed

* Embedded repainted skin textures (v36k): replaced by recolouring from the disc (see above). An in-place test that overwrote a loaded texture (v36 early builds) was dropped with it.
* Yoda's saber throw (v34): `0x2DD 0x2DE 0x2E1` forced on while player two plays him, on top of the `0x2DF` his script already has; nothing happened. Removed in v35.

* Saber-throw AI-object bypass at 0x2B6DBC and a probe of the saber-throw target handler at 0x2B6CF0: neither ran for
  player 2's throws (counters stayed 0); the real cause was P8.
* A renderer-view hook at 0x2116A0 (shifting the view matrix the renderer gets instead of the game's camera placement):
  the game picks what to draw from its own camera, so only a small cut-out of the level showed; removed. A float copy
  of the matrix also blacked the screen out (the unused fourth column must be copied bit for bit).
* Shifting every SetTransform call: other cameras (one at x = 0) were shifted too and the view pointed at the background.
* The camera's float code at -Os: the x87 register allocation gave values that did not match the source on the console
  (a shift that was zero on two axes); the two camera functions are built with `optimize("O0")`.

## Confirmed working (xemu, and levels 1 and 14 on a softmodded console)

Tested with xemu 0.8.139 (Windows x86_64) and a softmodded original Xbox. The start-up screen replacement plays in xemu (console test pending).

Every level with both players: drop in / out with Back on pad 2 (about 1 s, each level starts with the companion under the game's control),
both heroes' Force powers (push, lightning, saber throw, heal, mind trick), breakables and power conduits that need the saber throw, "Player Only"
triggers, kills credited to player 1's XP (shown correctly on the end-of-level screen), tether and respawn, the shared camera (toggles with drop in /
out, held off for 60 frames after a cutscene), power-ups and secrets picked up by player 2 (a saber crystal fills player 2's combo meter), the bacta
tanks, the CPU companion getting his own powers back on drop-out.

* Boss duels (levels 4, 9, 10, 12, 16, 17): the boss is taken over and handed back, all his powers work, the fight's triggers and sections follow his
  health, the game over when player one dies. Level 12 has two bosses in turn.
* Spawn levels (5, 7, 8, 11, 13, 14): a copy of player 1 (the other hero on level 5) beside player one, parked and its HUD emptied on drop-out.
* Level 5 is locked out of the turbolaser room (P24); level 1's scripted walk is handled automatically and Back is ignored during it (P26).
* Level ids are read correctly on every level (P27, P29).

## Known issues / to do

* **Full playthrough with a friend** is the next test. Soft locks and scripted moments found there become `k_events` rows (a cutscene id, a level key, an action).
* Level 1: xemu / the game once died about 55 frames after a cutscene (the first recording); no cause in the data, not seen since. Level 6: xemu crashed once
  when a level was restarted after dying (cause unknown). Level 17: xemu crashed once while the user was clicking in and out of the window (treated as xemu
  behaviour; it worked on the retest).
* Serra / Cin (P38): handing a boss back in his second phase puts the first-phase flag bytes back too, so the computer may not use those powers for the rest of
  the fight. Not seen yet; if it is, the script has a phase marker to look for with the recorder.
* Level 15: Yoda has no saber throw (accepted). The computer's own commands may still run for a moment after the take-over (see below).
* Controllers in the wrong ports: player 1 can be on a port other than 1 (hardware); player 2 is assumed to be port 2 (`PLAYER2_PORT`) and player 1 the
  primary record's character. Needs port detection (pad 2 = the other connected pad).
* The Obi-Wan force-jump circles can leave player 2 stuck (soft lock): plan a scripted teleport.
* Story telekinesis icons for player 2 (player 1 can do them).
* Bacta flash plays on player 1's bar when player 2 heals; pause-on-pad-2 icons.
* Level 5: the mission's Obi-Wan is never touched (P19), so his turret script cannot trap him; switching the spawned Obi-Wan off automatically in the
  turret section is still open (pressing Back parks him). A spawned character's Force script starts in player state (marker 0x1D6 = 0); the restore on
  release writes the companion defaults for it. Check `st[40]` / `st[44]` / `st[46]` (0x976240, 0x976250, 0x976258) in the diag build.
* Level 3's existing Obi-Wan starts with marker 0 and the powers on (native to the level; the restore finds nothing to save).
* A character that is taken over (or spawned) may do one more action of the computer's before standing still (a saber throw, a run back to Anakin): the
  AI finishes the command it had. Noted, not changed.
* Cutscene ids (P24 to P29) are from the retail NTSC-U game; after a restart check `lvx[2]` (0x9762F8) shows a level's id during its cutscene.
* Not done, on purpose: characters a level did not declare (other costumes, the other hero, Yoda outside level 15). They need the PC port's
  declare-from-other-PAKs loader (about 1,800 lines of C++ there); only bodies a level already loads can be used for now (the costume attempt of v27 showed
  that only the costume a level actually uses is loaded). The skins (P39) use a small part of that loader (declare one texture and serve it), not the whole.
* Skins: the recolour buffers (about 150 KB) are allocated at the first request and never freed (fine for the few levels involved); the debug words of
  `g_skincfg` and the free-memory diagnostic (`hr_tick`) are still in the skin test builds and should be stripped from a release; the faces and Obi-Wan's boots
  are not recoloured (shared texture); the light-saber colour is the next step; the live writes of `skin.bat` were reported not to stick in v36r5 (the cause was
  not found; v36r7 opens a connection per command; not yet confirmed); the DXT5 path (Anakin's tunic) was tested on the host and on the console
  (levels 11 and 14 show it). The skin code is in the working build, not yet merged into `source/`.
* Camera: assumes a 4:3 picture; some cutscenes were slow to settle (now the shift is dropped at once when one starts; not re-tested on all of them);
  leash distances are 600 / 900 scaled per level (P20); level 1 is open and keeps 100 %.
* Dooku (P15): the flags are set as the game's last phase sets them; the R-attack hold-and-throw was seen working in phase 2 before the fix and Force
  grip / saber throw after it. Not yet compared against duel mode move by move.
* Ideas, not started: replacing the loading screens (the textures named by each level's `loadScreenPrefix`, three DXT pieces of a 640x256 picture, files on the
  disc); a disc-file edit, not a patch to `default.xbe`.
* Start-up screen (disc-file edit, plays in xemu; console test pending): the first screen is `movies\Splash_Movies\Legal_XBOX.sfd` (played by the function at `0x215340`;
  the `.xmv` code at `0x6E000` is left over and unused). It is a CRI Sofdec file: MPEG-1 video 640x480 at 29.97 fps, no audio, in 2048-byte sectors with two Sofdec header
  sectors (frame count and duration are patched as binary and as ASCII tags). The replacement runs 300 frames (10 s, 1 s fade in, 1 s fade out); the game expects no
  particular length or size. Built with `sfdmux.py` (each picture starts a new PES packet, timestamps follow the retail file; remuxing the retail stream gives the same
  decoded frames and timestamps within 1 tick). The released screen is original artwork (starfield, Cinzel and Inter type, `movie/` in the release); a private version
  that keeps the game's logo and cityscape exists but is not released. The screen says "press Back": the code needs Back held for about a second (README says so).
* Release (SWROTS (UNOFFICIAL) Co-Op Mod v0.36; the mod build number is 36): `patcher/swrots-coop-mod_patch.py` (ISO mode through extract-xiso, folder mode, xbe-only mode; checks the MD5 of default.xbe and
  Legal_XBOX.sfd, refuses anything else, safe to run twice), `xdelta/swrots-coop-mod.xdelta` (13 KB, applies to the retail xbe), README, GPL-3.0-or-later. No game data and no
  patched default.xbe are shipped. Tested here on a synthetic ISO built with extract-xiso 2.7.1: unpacked, patched, packed again, files and checksums match; not yet run on
  the real game ISO.

## Left in on purpose

These are in the shipped build although the game does not need them. Each is marked "left in on purpose" in the source.

* **The script recorder (P37)** and xbwatch's `scriptid`: the way to find the Force flags of any character player two plays (about 650 bytes of code).
* **The debug block `g_dbg`** at `0x976000` (state, level key, characters found, the cutscene ids of the level) with xbwatch's `report` and `levelid`.
  Nothing in the game reads it.
* **`COOP_DIAG=1`** (a second build, never shipped): the camera, spawn, hazard and cutscene counters. The block's layout is the same either way.
* **`STAGE`** (`build.sh`'s first argument): a bring-up switch (0 counts frames, 1 adds the player lookup, 2 is everything). The shipped build is always 2.

## Tools (xbwatch, outside the mod)

`xbwatch.py` talks to xemu's GDB stub (`-s`): `run` (record a profile), `report`, `peek`, `graph`, `levelid` (the level key and cutscene ids after a level has
loaded), `scriptid` (the script recorder), `skin` (show and change the player-two recolour numbers; `skin.bat` starts an interactive prompt), `resume`. The `.bat` files next to it wrap the common ones. See its README.

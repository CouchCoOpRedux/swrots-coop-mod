# xbwatch: a memory flight recorder for Xbox games running in xemu

xbwatch attaches to xemu's built-in GDB server, reads the pieces of guest memory a **profile** names, and writes every
change to a log (one JSON object per line) with the time, the game's frame counter and the field offset. You play,
press a **mark** key when something interesting happens ("Obi-Wan climbs into the turret"), and the report shows what
the game changed around that moment. Nothing is written into the game and no game files are touched.

It is not specific to one game: a profile is just a list of addresses. `profiles/swrots_coop.json` is the one for
Star Wars Episode III with the co-op patch; copy it and change the addresses for another game or mod.

## Quick start (Windows)

1. **Python 3.8 or newer** (python.org, tick "Add python.exe to PATH"). No other packages are needed.
2. **Start xemu with `-s`** (it opens a debug server on `localhost:1234`):
   right-click `xemu.exe` -> *Create shortcut* -> right-click the shortcut -> *Properties* -> in **Target** add ` -s`
   after the closing quote, e.g. `"C:\Games\xemu\xemu.exe" -s` -> OK. Always start xemu from this shortcut when you want to record.
   (`-S` as well would start the game paused; you do not want that.)
3. Start the game in xemu, then double-click **`run_swrots.bat`** (or `python xbwatch.py run profiles\swrots_coop.json`).
   The console prints `connected`, the mark keys and, every 5 seconds, a status line.
4. Play. Press a mark key at the moments you want to find. **Ctrl+C** in the console stops the recorder; the game keeps running.
5. `python xbwatch.py report logs\trace_<date>.jsonl` summarises the log (see below).

If the game ever stays frozen after the recorder stopped, run `resume_game.bat`. xemu's own Machine -> Resume works too.

## Level 1 test (what stops the first cutscene while player 2 is in)

`run_level1.bat` (= `python xbwatch.py run profiles\swrots_level1.json`) watches the player objects, Obi-Wan, cutscenes and every trigger / door class
of the level. Start the recorder AFTER level 1 has loaded and press **NUMPAD0** after every level restart (it finds the objects again). Second pass (profile version 2) watches every trigger and cutscene object and the whole debug block. Marks:
1 player 2 in, going to the spot; 2 at the spot, nothing happens; 3 Back held (dropping out); 4 cutscene started; 5 cutscene ended; 6 player 2 in again.
Afterwards `python xbwatch.py report logs\trace_<date>.jsonl`. The debug block word `0x976008` (frame) and `0x9762F8` (id of the last cutscene) are in the log too.

## scriptid: which script plays a character (mod version 32+)

(Needs mod version 34 or later.) Play Serra, Cin or Yoda as player two, try the powers that do not work (saber throw, mind trick), then run `scriptid.bat` (= `python xbwatch.py scriptid "Serra, level 12"`). It prints and appends to `logs\scripts.txt`: the code range that asked for the character's buttons, the button ids, and the script instance bytes `0x1D0..0x2EF` at the first query and later. Send me `logs\scripts.txt`. Run it once per character, in the same session.

## levelid: what the mod found at the start of the level

`levelid.bat` (= `python xbwatch.py levelid "level 2"`) prints the cutscene ids and the level key the co-op mod found when the level started
and appends them to `logs\levelids.txt`. (Mod versions 25 to 28 also printed a costume probe; it was removed in 29.) Run it ~10 s after any level has loaded; the ids are how the mod will recognise that level.

## graph: dump objects and what they point to

`python xbwatch.py graph --classes 0x58B3F0 --ids 206329,2063AA --depth 3` scans RAM for objects of those classes (vtable addresses), keeps the ones
whose id (word at +4) is in the list, and writes them with every object their pointers lead to (breadth first, `--depth`, siblings of the same class
skipped) to `logs\graph_<date>.txt`. No addresses needed; the game freezes about 10 s. `graph_level1.bat` does it for the level 1 triggers.

## Marks, scans

* **Mark keys** are global hotkeys (they work while the game has the focus; Windows only). In the SWROTS profile:
  NUMPAD1..NUMPAD6 with the labels in the profile. Rename them in the profile at any time. Each mark takes a full
  snapshot of every region, so two marks can be compared.
* Other ways to mark: type a line in the console and press Enter, or append a line to `profiles/marks_in.txt`.
* **Scans** find objects by class: every place in RAM where a given vtable pointer stands is an object of that class.
  A scan runs once at the start if it has `"auto": true`, again on the **scan key** (NUMPAD0, or the line `!scan` in
  `marks_in.txt`), and every `every_s` seconds if set. One pass looks for all the classes of a scan at once. It runs in
  slices between samples (the game runs at about 75 % speed meanwhile) and takes some tens of seconds for all 64 MB.
  Regions with `"scan": "<class name>"` then watch the objects found, one sub-region per object, named `name@ADDRESS`.

## What gets logged (events)

| `ev` | meaning |
|------|---------|
| `chg` | words that changed in a region: `c` = list of `[offset, old, new, float]` (the float only when the value looks like one). Offsets are from the object's start, so `0x119` is the byte in the word at `0x118`. |
| `mute` / `unmute` | a word that changes in more than about a quarter of the samples (positions, timers) is silenced automatically so it does not bury the rare changes; it is logged again when it settles |
| `rebase` | a pointer-based region moved to another object (new level, respawn) |
| `unreadable` / `readable` | the region's address is unmapped / is mapped again |
| `snap` | full hex of every region (at the start and at each mark) |
| `mark` | a mark: `n`, `label` |
| `scan` | the addresses a scan found, per class (`found`) |
| `last_state` | written only when the run ends abnormally (xemu closed / crashed / connection lost): the last full snapshot of every region, taken at most `crash_snap_s` (default 2) seconds before the end |
| `end` | the last line; `reason` says why (`stopped`, Ctrl+C, or the error when the game was lost; the console prints the same) |
| `region`, `profile`, `start`, `stat`, `guest_stop` | bookkeeping |

`python xbwatch.py report <log>` prints the scan / rebase events, then every word that changed only a few times
(`--max-changes`, default 6) with the time relative to the mark before it, then the busiest words. Rare changes right
after a mark are usually the trigger you are looking for. The log is plain JSON lines, easy to load into Python.

## The profile (JSON; re-read whenever you save it, about every half second)

```json
{
  "rate_hz": 15,                 samples per second
  "frame_addr": "0x976008",      optional: a frame counter in guest memory, logged as "fr"
  "ram_top": "0x04000000",       pointers outside 0x10000..ram_top are treated as invalid (0x08000000 for 128 MB)
  "scan_budget_ms": 80,          how long one sample may spend scanning
  "scan_run_ratio": 3,           while a scan runs the guest runs this many times as long as it was stopped
  "min_run_ratio": 1.0,          the guest runs at least this many times as long as it was stopped (1.0: at most half the time stopped)
  "snap_on_mark": true,
  "scan_key": "NUMPAD0",
  "marks": { "NUMPAD1": "label", "F9": "another label" },
  "auto_mute": { "on": true, "hi": 8, "lo": 2, "decay": 0.97 },
  "regions": [
    { "name": "dbg",  "addr": "0x976000", "len": "0x40", "mask": ["0x8-0xB"] },
    { "name": "p1",   "ptr": "0x976010", "off": "0x0", "len": "0x400" },
    { "name": "p1ai", "ptr": ["0x976010", "0xA00"], "len": "0x2A0" },
    { "name": "ai",    "scan": "obiwan", "index": "all", "max": 3, "chain": ["0xA00"], "len": "0x2A0" }
  ],
  "scans": [ { "name": "classes", "auto": true, "every_s": 0, "lo": "0x977000", "hi": "0x4000000",
               "find": { "obiwan": "0x5BE570", "gun": "0x5A6728" } } ]
}
```

* A region is **where** (one of): `addr` (fixed address); `ptr` (the address of a 32-bit pointer cell whose value is the
  object, so it follows the object when it moves; a list is a chain: read the pointer at the first address, add the next
  number, read the pointer there, and so on); `scan` + `index` (one of the objects a scan found: a class name from the
  scan's `find`; `"all"` watches every object, `max` of them; `chain` follows pointers from the object, e.g. `["0xA00"]`
  watches what the pointer at object+0xA00 points to). Then `off` (added to the object address) and `len` (at most
  0x2000, multiples of 4).
* `mask`: byte offsets (`"0x8-0xB"` or `"0x44"`, object-relative like the log) that are never logged.
* `disabled: true` switches a region off without deleting it. Any extra keys (like `notes`) are ignored.
* Numbers can be JSON numbers or strings like `"0x1F"`.
* Several regions of the same object are cheaper than one huge one only if the middle is unneeded; each region costs
  one or two reads per sample.

## Cost, and what to expect

GDB's stub in xemu answers only while the guest is stopped, so each sample stops the guest (Ctrl+C), reads, and
continues. The status lines show the cost ("% of the time stopped"). The guest's own clock is stopped meanwhile, so the
game's timing does not change; it only runs a bit slower in real time. If it stutters, lower `rate_hz` or the
sizes of the regions. Memory is read through the guest's page tables, so use the addresses the game itself uses
(the same as xemu's monitor `x` command). A scan of all of RAM reads about 64 MB and takes some seconds, spread out
over many samples.

## Troubleshooting

* `cannot connect to 127.0.0.1:1234`: xemu is not running, or was not started with `-s`.
* A region shows `unreadable`: its pointer is 0 or points outside RAM (for example before the level has loaded).
  It is logged again as `readable` when it works.
* Hotkeys do nothing: only on Windows, and the key name must be one of F1..F12, NUMPAD0..9, 0..9, A..Z, INSERT, DELETE, HOME, END, PGUP, PGDN, PAUSE.
  Typing a line in the console always works.
* `python` is not found: reinstall Python with "Add python.exe to PATH", or use `py xbwatch.py ...`.

## Files

* `xbwatch.py`: the tool (one file, standard library only): `run`, `report`, `peek ADDR [LEN]`, `resume`.
* `profiles/swrots_coop.json`: the SWROTS Xbox co-op profile. `run_swrots.bat`, `resume_game.bat`: launchers.
* `tests/fake_stub.py`, `tests/run_test.sh`: a fake xemu debug server and a test run that needs no emulator.

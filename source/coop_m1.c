// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * SWROTS co-op for the ORIGINAL XBOX: a binary patch of default.xbe that lets a second controller play in the
 * story campaign of Star Wars: Episode III - Revenge of the Sith. Freestanding C (no CRT, no STL, no OS calls),
 * linked at VA 0x96E000 and appended to the XBE as the COOP section by patch_xbe.py.
 *
 * Valid only for default.xbe, MD5 6460ef37862de3af364a0563a0a18c4e (NTSC-U).
 *
 * HOW IT PLAYS (couch co-op, all 17 story levels). Every level starts as the game made it: the companion is the
 * game's computer character. Holding Back on pad 2 for about a second lets player two in, holding it again lets
 * go; everything is forgotten when a level restarts, and all experience goes to player one's save. Who player two
 * is depends on the level (the table is k_strategy / k_body, "Co-op rule per level" in CHANGES.md):
 *   - the mission's own companion, taken over (levels 1 to 4, 6, 15);
 *   - the boss of a duel, taken over, with the boss's health bar, camera and win / lose rules left to the game
 *     (levels 4, 9, 10, 12, 16, 17; on 12 the second boss can be taken after the first has died);
 *   - a character of our own made beside player one, parked while player two is out (levels 5, 7, 8, 11, 13, 14).
 * Which level it is comes from the ids of its cutscenes, found by one scan of memory early in each level.
 *
 * CREDIT. Many ideas, offsets and the behaviour of the companion (tether, respawn, shield, mastery, the way a
 * companion is taken over) come from the unofficial PC port SWROTS-PC (github.com/jedijosh920/swrots-pc, GPLv3):
 * coop.cpp, characters.cpp, customchars.cpp, game.h, sdk_symbols.inc. Where a patch below is marked
 * "Source: PC port" its idea or addresses come from there; "Source: this project" means it was found in the original
 * game's code and confirmed by xemu test dumps made for this mod.
 *
 * WHAT IS CHANGED, AND WHERE (the same list, with the original bytes, is in CHANGES.md). Each item has a header
 * comment ("Where / Was / Now / Needed / Source") at the code that does it.
 *   Code patches (jumps or call replacements written into default.xbe by patch_xbe.py):
 *   P1  per-frame entry          D3DDevice_Swap 0x504810               coop_detour -> coop_tick
 *   P2  health change            ICharacter 0x151500                   coop_hook_health
 *   P3  instant kill / void      ICharacter 0x152D30                   coop_hook_kill
 *   P4  "Player Only" triggers   calls at 0x1A20C8, 0x1A2258           coop_gp_trigger
 *   P5  Force-script flags       button query 0x178070                 coop_hook_pressed -> script_probe, fix_force_script
 *                                (P15 Dooku, P35 Anakin / Obi-Wan as bosses and P38 Serra / Cin are further rules of the same fix)
 *   P6  pickups                  calls at 8 sites                      coop_pk_a .. coop_pk_g
 *   P7  experience               call at 0x269325                      coop_xp_stub
 *   P8  "Damaged Only By Player" calls at 0xBDA1B, 0x29481A            coop_dmg_brk, coop_dmg_cnd
 *   P13 shared camera            0x129990, 0x211C30                    coop_hook_cam, coop_hook_fov
 *   P21 hazard zones (IPain)     calls at 0xFF0AA, 0xFF0C1, 0xFF106, 0xFF2AC   coop_pain
 *   Logic and data changes (nothing in the game's code is touched):
 *   P9 mastery gauge flags, P10 upgrade levels, P11 story-mode guard, P12 companion-first start (drop in / out),
 *   P14 boss take-over, P16 switched-off companions, P17 class names without a call into the game, P18 parking a
 *   spawned companion, P19 one strategy per level, P20 tether length per level, P22 the pause-menu camera,
 *   P24 / P25 / P26 scripted moments (level 5's turbolaser room, level 1's walk; the table k_events and the windows),
 *   P27 / P28 / P29 the level named by its cutscene ids (k_sigs), P31 who player two is on a spawn level (k_body),
 *   P32 a rule for every level, P34 the second HUD emptied on spawn levels, P36 a second boss after the first has died.
 *   (P23, P30 and P33 were tried and removed; they are described in CHANGES.md only.)
 *
 * LEFT IN ON PURPOSE (not needed to play, kept for the next round of testing):
 *   - the script recorder (P37, script_probe and g_dbg.spc / spr): when player two plays a character it notes which
 *     code asked for his buttons and the bytes of his script instance; read with xbwatch "scriptid". Cheap (about
 *     650 bytes of code, 2 KB of debug block) and the way to find what a character's powers are made of;
 *   - the debug block g_dbg (state, level key, characters found, cutscene ids of the level): read with xbwatch
 *     "report" / "levelid" or the xemu monitor. Nothing in the game reads it;
 *   - COOP_DIAG=1 (a second build, never shipped): camera, spawn, hazard and cutscene counters. g_dbg keeps its
 *     layout either way.
 *   - STAGE (build.sh's first argument): a bring-up switch (0 count frames, 1 + player lookup, 2 everything); the
 *     shipped build is always 2.
 *
 * Entered from a detour on D3DDevice_Swap (0x504810), once per rendered frame:
 *   coop_detour: pushfd; pushad; fnsave; call coop_tick; frstor; popad; popfd; <the 7 displaced bytes>; push 0x504817; ret
 */
#include <stdint.h>

typedef uint8_t  u8;
typedef uint32_t u32;
typedef uint16_t u16;
typedef int32_t  i32;

#ifndef STAGE
#define STAGE 2 /* 0: count frames only; 1: + player lookup; 2: full M1 */
#endif

/* COOP_DIAG: 0 (default) = the shipping build; 1 = also the test counters and watchers whose results are dumped from the
 * xemu monitor (see CHANGES.md, "Debug block"). The debug block g_dbg keeps its layout either way. */
#ifndef COOP_DIAG
#define COOP_DIAG 0
#endif
#if COOP_DIAG
#define DIAG(...) __VA_ARGS__
#else
#define DIAG(...)
#endif

#define CDECL    __attribute__((cdecl))
#define STDCALL  __attribute__((stdcall))
#define FASTCALL __attribute__((fastcall)) /* ecx, edx, rest on the stack: a thiscall with a dummy edx */

/* Calls into the game. All are the game's own functions (addresses from the PC port or found in this XBE). ObjectById (0xA31F0)
 * is NOT used: it is unchecked and walks lists the loader may still be building (see sweep_step). */
typedef void        (FASTCALL *BindControllerFn)(u8* c, void* edx, i32 port);     /* 0x150580 thiscall, ret 4 */
typedef i32         (FASTCALL *IsAFn)(u8* c, void* edx, u32 typeKey);             /* vtable slot 1 */
typedef i32         (CDECL *InCutsceneFn)(void);                                  /* 0xA3720 */

#define BIND_CONTROLLER ((BindControllerFn)0x00150580)

/* XDK input (XPP section): used to read pad 2's Back button. The game already holds a handle on each pad, so the
 * game's own handle is looked up (XPORT_RECORD below) and only XInputGetState is called on it; XInputGetState raises
 * the IRQL around its copy, so it is safe to call. */
typedef u32   (STDCALL *XInputGetStateFn)(void* handle, void* state);
#define XINPUT_GET_STATE ((XInputGetStateFn)0x00558E58)
/* XDK's own table of open pads: the game already holds a handle on each pad, so XInputOpen fails (ERROR_SHARING_
 * VIOLATION) when asked again. This finds the port's record (the XDK helper at 0x55927C, ecx = device-type
 * descriptor, edx = port); the handle it holds is at +0x12. */
typedef u8* (FASTCALL *XPortRecFn)(const void* descriptor, u32 port);
#define XPORT_RECORD ((XPortRecFn)0x0055927C)
#define G_DESC_GAMEPAD ((const void*)0x00557F4C)
/* Spawning a character (characters.cpp CreateCharacter / PlaceCharacter / SetCharacterSide; addresses are the
 * game's own, as the port has them). */
typedef void* (__attribute__((cdecl)) *ClassLookupFn)(const char* name);
typedef u8*   (__attribute__((cdecl)) *CreateInstanceFn)(void* info);
typedef int   (__attribute__((cdecl)) *SpawnInstanceFn)(u8* object, void* owner, const float* m, int one);
typedef void  (FASTCALL *ActivateFn)(u8* object, void* edx);
#define CLASS_LOOKUP    ((ClassLookupFn)0x000E9B60)
#define CREATE_INSTANCE ((CreateInstanceFn)0x000E2350)
#define SPAWN_INSTANCE  ((SpawnInstanceFn)0x000A2DE0)
#define VSLOT_ACTIVATE  (0xB4 / 4)   /* vtable slot: activate a freshly spawned object */
#define VSLOT_PLACE     (0x1F4 / 4)  /* vtable slot, thiscall (const float m[16]): puts a character somewhere */
typedef void (FASTCALL *PlaceFn)(u8* c, void* edx, const float* m);
/* The game's own deactivate / activate of an object (thiscall, no stack arguments): "parking" an object hides it and stops its updates (P18). */
typedef void (FASTCALL *ObjFn)(u8* object);
#define OBJ_DEACTIVATE  ((ObjFn)0x000F29F0)
#define OBJ_ACTIVATE    ((ObjFn)0x000F2A50)

/* ---- Offsets inside the game's structures (all found in this XBE; the PC port's game.h names most of them) ---- */
/* object (any instance): */
#define C_ID            0x004   /* unique id: tells a character from a new object at the same address */
#define C_FLAGS         0x00C
#define FLAG_INACTIVE   0x08000000u /* cleared on a spawned character before it is spawned (as the PC port does) */
#define C_DEAD          0x12C
#define C_HEALTH        0x130   /* float; the sign / zero test is done on the bits (no FPU) */
#define C_MAXHEALTH     0x134
#define C_TRANSFORM     0x150   /* 4x4 matrix; the position is at +0x180 (C_TRANSFORM + 48) */
#define C_CONTROL       0x390   /* K_PLAYER (2) or K_COMPANION (16): who drives the character */
#define C_CONTROLLER    0x43C   /* port the character listens to */
#define C_INVINCIBLE    0x5D6
#define C_POWER         0xA40   /* Force energy and its maximum */
#define C_MAXPOWER      0xA44
#define C_AI            0xA00   /* -> AI data (its own vtable AI_MARKER; +0x29C points back at the character) */
#define C_COSTUME       0x1E8
#define C_TEXSET        0x1EC
/* AI data: */
#define AI_TEAMS        0x214   /* "Team Setting": a bit per team, read to tell friend from foe */
#define AI_HASSIDE      0x1C
#define AI_TARGET_PLAYER 0x50   /* non-zero: this character attacks the player */
#define AI_IGNORED      0x52
#define PORT_PLAYER_TEAM 0x8000u /* the port's team bit for the player and its allies */
#define K_PLAYER        2
#define K_COMPANION     16
#define JEDI_TYPE_KEY   0x00249950u /* class-info key for IsA(Jedi) */
#define PLAYER2_PORT    1       /* pad 2 (port numbers start at 0) */

/* ---- Spawning and the second HUD ---- */
#define SPAWN_DISTANCE 120.0f   /* in front of player one, in game units (a character is about 70 tall) */
#define SPAWN_AFTER_FAILS 3     /* searches without a companion before one is made (levels using the "auto" strategy) */
#define SPAWN_WAIT_FRAMES 150   /* how long to wait for a spawned character to come alive */
#define HUD_VITALS_FADE 0x005A80A0u /* the "EnemyVitals" fade handler: +4 item, +8 group, +0x10 fade time */
#define HUD_PORTRAIT    0x005A82B0u /* HudVitals: +0xC gave up, +0xD picked, +8 group (+0x84 names the slot) */

/* ---- The tether (coop.cpp; 30 frames a second assumed). The two distances are scaled per level (k_leash). ---- */
#define LEASH_DIST2   (600.0f * 600.0f) /* player two further than this for LEASH_FRAMES is put beside player one */
#define FAR_DIST2     (900.0f * 900.0f) /* ... further than this at once */
#define LEASH_FRAMES  15
#define SHIELD_FRAMES 60                /* invincibility after a respawn */
#define DEATH_FRAMES  90                /* a companion that died anyway is replaced after this */
#define AFTER_CUTSCENE_FRAMES 30        /* the tether waits this long after a cutscene */
#define TRAIL_SPACING2 (40.0f * 40.0f)  /* player one's trail: a point every 40 units, the last 32 kept */
#define TRAIL_LEN 32
#define BEHIND 70.0f                    /* player two is put this far behind player one */
#define BEHIND2 (BEHIND * BEHIND)
#define TRAIL_REACH2 (400.0f * 400.0f)
#define FALL_PER_FRAME 8.3f             /* 250 units a second at 30 frames a second: faster is a fall, the trail stops */
#define XINPUT_BACK 0x0020u             /* XINPUT_GAMEPAD wButtons (state +4) */
#define IN_CUTSCENE     ((InCutsceneFn)0x000A3720)

/* ---- Game globals ---- */
#define G_GAME_MANAGER   0x007EB964u  /* -> manager: +0x1E4 player count, +0x2A4/+0x2A8 slot ids */
#define G_PRIMARY_RECORD 0x0068DCECu  /* -> record: +0xC = player 1 */
#define G_INPUT_MANAGER  0x0068D4F4u  /* -> +0xC flags (0x40000 live), +0x1D8 + port*4 -> entry, entry+0x7C = character */
#define G_LETTERBOX      0x0069226Cu
#define G_CINEMATIC      0x00695C88u
/* P24 / P25: scripted events keyed by cutscene. The cell above holds the playing cutscene's object while one plays (0 otherwise); its first word is the
 * ICutscene vtable and +4 is the cutscene's id from the level file (found with the xbwatch recorder). k_events (below) says what to do for which id. */
#define VT_CUTSCENE      0x00599478u
#define YODA_MAX_X 1.5f   /* P43: Yoda's maximum Force as a multiple of player one's */
#define VT_YODA          0x005C79F8u   /* IYoda's vtable (class table 0x5C79F8, found through its name function 0x2A0700) */
#define EVENT_QUIET_FRAMES 10           /* frames without a cutscene before it counts as ended */
#define P1_DEAD_FRAMES   20             /* player one dead this long: player 2 may come back (a restart from a checkpoint) */
#define KEY_L1           1              /* level keys are the user's level numbers */
#define KEY_L5           5
#define KEY_ANY          0xFEu          /* an event that applies in every level */
/* P26: a scripted walk of the companion. Level 1 has a place where the story sends the (computer-controlled) companion to a spot by itself and only then
 * starts a cutscene; while player 2 plays him he never goes. Found with the xbwatch recorder and graph command (all ids are from the level file):
 *   trigger (ITriggerMulti) 0x205ED3 fires (word +0x9C, bit 31) when the walk is ordered -> the window opens: player 2 goes out and cannot come back,
 *   cutscene (ICutscene) 0x205EFB plays (its +0x9C bit 31 stays set afterwards), then cutscene 0x1F0EB0 plays -> the window closes, player 2 is back. */
#define VT_TRIGGER_MULTI 0x0058B3F0u
#define WIN_TRIG_ID      0x00205ED3u
#define WIN_CUT1_ID      0x00205EFBu
#define WIN_CUT2_ID      0x001F0EB0u
#define WIN_TIMEOUT      2400u          /* frames after the first cutscene started: the window closes by itself if the second never plays */
#define WIN_SCAN_CHUNK   0x80000u       /* bytes of memory looked through per frame while the objects are searched */
#define OBJ_FIRED        0x80000000u    /* bit 31 of +0x9C: a trigger has fired / a cutscene has played */
#define G_SCRIPTED_CAMS  0x0068DA44u
/* g_DeviceType_Gamepad (sdk_symbols.inc): first dword = bitmask of connected pads. Read directly:
 * XGetDevices() also resets the game's own insertion/removal tracking. */
#define G_PAD_MASK       0x00557F14u

/* ---- Timing (frames; Swap runs once per rendered frame, about 30 a second) ---- */
#define SETTLE_FRAMES   60   /* after a level starts, nothing happens for this long */
#define RETRY_FRAMES    120  /* between searches for a companion */
#define REASSERT_FRAMES 30   /* between re-binding player two's controls */
#define BACK_HOLD_FRAMES 30  /* hold Back on pad 2 this long (about a second) to drop out / drop in */
#define MATCH_FRAMES    30   /* keep player 2's maxima equal to player 1's */

/* What the mod is doing (g_dbg.state): */
enum { ST_IDLE, ST_WAIT_SETTLE, ST_WAIT_PAD, ST_NO_COMPANION, ST_REGISTERED, ST_SKIP_OWN_P2, ST_LOST, ST_WAIT_CUTSCENE, ST_SCANNING /* 8 */, ST_DROPPED /* 9: player 2 let go with Back */, ST_SPAWNED /* 10: waiting for a spawned ally to come alive */, ST_OVER /* 11: P24, level 5's last room: player 2 is out until the level ends or player 1 dies */, ST_WINDOW /* 12: P26, a scripted walk of the companion: player 2 is out until it is over */ };

struct CoopRow { u32 ptr, id; char name[8]; i32 control; u32 flags; u32 pad[2]; }; /* 32 bytes */
/* P37, one entry per character player two played (spc) and one per stretch of code that asked for his buttons (spr).
 * SpChar: name, inst the script instance (global 0x6964C0) at the last query, hits, first / last the instance bytes 0x1D0..0x2EF at the first query and a little later (refreshed every 64 queries). 592 bytes.
 * SpReg:  lo..hi the code addresses (return addresses) merged within 0x1000, hits, ids a bit mask of the button ids seen (0..63), chr the character's entry + 1 (0 = unused). 24 bytes. */
struct SpChar { char name[8]; u32 inst, hits; u8 first[0x120]; u8 last[0x120]; };
struct SpReg { u32 lo, hi, hits, ids[2]; u32 chr; };
/* The debug block: read from the xemu monitor (x/Nwx <address>) or a Xbox debugger; nothing in the game reads it. It sits at 0x976000 and its layout is the
 * same in every build (CHANGES.md, "Debug block", has the addresses). Parts marked (DIAG) are only filled by the COOP_DIAG=1 build. */
struct CoopDbg {
    volatile u32 magic;       /* +0x00 'COOP' */
    volatile u32 version;     /* +0x04 */
    volatile u32 frames;      /* +0x08 frames since boot (one per rendered frame) */
    volatile u32 state;       /* +0x0C ST_* */
    volatile u32 player1, player1Id, player2; /* +0x10 addresses / id of the characters */
    volatile u32 reasserts;   /* +0x1C times player two's controls were set again */
    volatile u32 objectsSeen; /* +0x20 mapped pages swept in the last sweep */
    volatile u32 jedisSeen;   /* +0x24 characters for which IsA(Jedi) was true */
    volatile u32 maxIndex;    /* +0x28 sweep cursor (address; reaches 0x04000000 when done) */
    volatile u32 padMask;     /* +0x2C connected pads (bit 0 = pad 1) */
    volatile u32 scans;       /* +0x30 sweeps finished */
    volatile u32 p1IsJedi;    /* +0x34 IsA(Jedi) on player 1 (must be nonzero: Anakin) */
    volatile u32 charsSeen;   /* +0x38 characters (AI-data marker test) in the last sweep */
    volatile u32 spare;       /* +0x3C (P26) scripted-walk window: bits 0-3 state (0 searching, 1 waiting for the order, 2 companion walking, 3 first cutscene seen, 4 over), bit 4 Back locked, bit 5 player 2 goes back in afterwards */
    char p1Name[8];           /* +0x40 class name of player 1 */
    char candName[8];         /* +0x48 class name of the chosen companion */
    volatile u32 misc[4];     /* +0x50 [0] page-table access: 0x600D0001 works, 0xDEAD0001 not (the code then does nothing), [1] CR3, [2] page-table base, later the last spawned object,
                                 [3] Back button state: backHold<<24 | down<<16 | xinput-seen<<8 | dropped */
    struct CoopRow rows[8];   /* +0x60 characters with control 16 or IsA(Jedi): ptr,id,name[8],control,flags (1 alive, 2 targets player, 4 isJedi) */
    volatile u32 leashes;     /* +0x160 brought back for being too far */
    volatile u32 respawns;    /* +0x164 would-be deaths turned into a respawn */
    volatile u32 rescues;     /* +0x168 void falls / kill zones turned into a respawn */
    volatile u32 replaced;    /* +0x16C died anyway: a new character made */
    volatile u32 lastDist;    /* +0x170 last distance squared to player 1 (as float bits) */
    volatile u32 hooks;       /* +0x174 bit0 health hook seen, bit1 kill hook seen, bit2 (DIAG) force-script fix seen */
    char spawnName[8];        /* +0x178 class used for the last spawn */
    volatile u32 fx[8];       /* +0x180 Force script fix (Obi-Wan, Anakin; the boss rules P15 / P35 / P38 set [0], [1] and [7] only): [0] times applied, [1] script instance, [2] its bytes 0x1D6..0x1D9 before,
                                 [3] bytes 0x2D8..0x2DB before, [4] 0x2DC..0x2DF before, [5] 0x2E0..0x2E3 before, [6] 0x2E4..0x2E7 before, [7] last caller */
    volatile u32 st[64];      /* +0x1A0 shared camera and spawn counters (DIAG): [0] camera calls, [1] calls returned shifted, [2] game fov bits, [3] why no target (1 no p2 guard, 2 chars,
                                 3 position/cutscene, 4 no fov, 5 fov range, 6 tan, 7 bad matrix pointer, 8 not story mode, 9 bad floats, 10 waiting after a cutscene, 0 ok),
                                 [4] shift x bits, [5] fov-hook calls, [6] back bits, [7] shift length bits, [32] frames since last call, [33] ease,
                                 [40] player 2 came from: 1 spawned by this code, 0 an existing companion, [41] spawned registrations, [42] existing ones,
                                 [43] script marker byte 0x1D6 at first sight (0xFF not seen yet), [44] Force fix mode (1 originals saved, 2 spawned defaults, 3 Dooku's flags saved),
                                 [45] script restores done, [46] marker written back, [56] placements skipped as another camera, [57..59] the last placement the game gave (x y z),
                                 [60..62] our current shift, [63] repeated placements skipped (the pause menu) */
    volatile u32 lv[20];      /* +0x2A0 reserved, always 0 (the costume probe of versions 25 to 28 was removed: P33) */
    volatile u32 lvx[4];      /* +0x2F0 [0],[1] unused; [2] (P24) id of the last cutscene seen (level 5's cutscene before the turbolaser room is 0x1F8FB7),
                                 [3] (P24/P25) flags: bit0 an event's cutscene was seen, bit1 its trigger point is reached, bit2 player 2 is locked out,
                                 bits 8-15 level key (0xFF not known), bits 16-23 the event's row in k_events + 1 */
    volatile u32 sub[12];     /* +0x300 spawned companion (P18/P19): [0] (DIAG) spawned, [1] parked, [2] unparked, [3] switched back to the level's own companion, [4] its address,
                                 [5] its flags: 1 parked, 2 registered as player two, 4 alive, 8 active, [6] the level's own companion's address, [7] its flags: 1 active, 2 alive,
                                 [9] level key (the user's level number; 0xFF not known: see scan_done), [10] spawn attempts that failed, [11] unpark placements */
    volatile u32 lvp[20];     /* +0x330 reserved, always 0 (unused since version 22) */
    volatile u32 lvi[10];     /* +0x380 reserved, always 0 (unused since version 22) */
    volatile u32 hz[12];      /* +0x3A8 (DIAG) hazard comparison, per character {AI teams +0x214, object flags +0x0C, zone +0xD4, AI side byte | invincible<<8 | control<<16 | active<<24}:
                                 [0..3] player one, [4..7] the spawned companion, [8..11] the mission's own companion (or the one taken over) */
    volatile u32 cut[26];     /* +0x3D8 (DIAG) cutscenes: [0] number started so far, then a ring of the last 5, entry k = (number - 1) % 5 at [1 + k*5]: {start frame, end frame (0 while it plays), player one's x, y, z at the start (float bits)} */
    volatile u32 lvl[48];     /* +0x440 (P27) object scan at the start of a level: [0] level key the cutscene ids gave (0xFF none), [1] cutscenes found, [2] multi triggers found, [3] scans done,
                                 [4..27] ids of the cutscenes found (the first 24), [28..47] for each level key how many of its signature ids were found */
    struct SpChar spc[3];     /* +0x500 (P37, left in on purpose: read with xbwatch "scriptid") the script recorder: the characters player two played (3 x 592 bytes), */
    struct SpReg spr[16];     /* +0xBF0 and the stretches of code that asked for their buttons (16 x 24 bytes); ends at 0xD70 */
};
_Static_assert(sizeof(struct CoopDbg) <= 0x1000, "the debug block outgrew its 4 KB slot");
__attribute__((section(".coopdbg"))) struct CoopDbg g_dbg = { .magic = 0x504F4F43u, .version = 36 };

static u32 s_seen, s_nextTry, s_nextReassert;
static u8* s_p1;
static u8* s_p2;
static u32 s_p2Id;

/* what player 2's character was before co-op took it (given back when it leaves) */
static i32 s_svControl, s_svCount;
/* boss duel (P14) */
static u32 s_boss;                 /* 1: player two is playing the mission's boss */
static u32 s_bossMode;             /* the sweep is looking for the boss (the game's slot 2) */
static u32 s_bossSlotId, s_bossId, s_bossDone, s_bossFails, s_bossDeadId;
static u8 s_bossKind;             /* 1: the boss is Serra or Cin (P38) */
static u8* s_bossObj;
static u8* s_bossCand;
static u32 s_svSlot;
static u8  s_svInvincible;
static float s_svMaxHealth, s_svMaxPower;
/* drop-in / drop-out */
static void* s_xh;            /* our XInput handle for pad 2 (0: none) */
static u32 s_xdiag;
static u32 s_backHold, s_backLatch;
static u32 s_dropped;         /* 1: player 2 chose to drop out; stays out until Back is held again */
static u8* s_last;            /* the character let go of (taken again without a new search) */
static u8* s_yoda; static u32 s_yodaId;   /* P43: Yoda, as the start-of-level scan found him, and the object id that already has its Force doubled */
static u32 s_lastId;
static u32 s_nextMatch;
static u32 s_fails, s_spawnTried, s_spawnFrame;
static u8* s_spawnObj;
/* the tether */
struct Pt { float x, y, z; };
static struct Pt s_trail[TRAIL_LEN];
static u32 s_ntrail;
static float s_lastY;
static u32 s_farSince, s_shieldUntil, s_deadSince, s_lastCutscene;
static volatile u32 s_guard, s_respawnPending, s_rescuePending; /* set by the game hooks */
/* what player 2 is, to make another of the same after a death */
static char s_p2Name[24];
static i32 s_p2Costume, s_p2TexSet;

/* Is this virtual address backed by a present page? We run in ring 0 (called from the game's Swap), and
 * a read of an unmapped page is a fatal fault, so every pointer the sweep follows goes through this first.
 *
 * The page tables are read through the kernel's self-map: one page-directory entry points back at the
 * directory itself, so all page tables appear as one array at s_ptebase and the directory at s_pdebase.
 * (An earlier build read tables through the 0x80000000 alias of physical RAM; that alias is NOT complete --
 * the table page at physical 0xE4F000 has no alias -- and the read crashed the kernel.) The self-map is
 * looked for once, in the directory read through the alias of its own page (CR3, always mapped). If there
 * is none, page_ok() answers "no" for everything and the co-op code stays inert; g_dbg.misc[0] says
 * which (0x600D0001 found, 0xDEAD0001 not found; [1] = CR3, [2] = PTE base). */
static u32 s_ptebase, s_pdebase;
static int s_pwstate; /* 0 not looked, 1 usable, -1 unusable */

static void pagewalk_init(void)
{
    u32 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    cr3 &= 0xFFFFF000u;
    g_dbg.misc[1] = cr3;
    s_pwstate = -1;
    g_dbg.misc[0] = 0xDEAD0001u;
    if (cr3 == 0 || cr3 >= 0x04000000u) return;
    const volatile u32* pd = (const volatile u32*)(0x80000000u + cr3); /* the directory's own page: mapped */
    for (u32 i = 0; i < 1024; ++i) {
        if ((pd[i] & 0xFFFFF003u) == (cr3 | 3u)) { /* present, writable, and it is the directory itself */
            s_ptebase = i << 22;
            s_pdebase = s_ptebase + (i << 12);
            s_pwstate = 1;
            g_dbg.misc[0] = 0x600D0001u;
            g_dbg.misc[2] = s_ptebase;
            return;
        }
    }
}

static int page_ok(u32 va)
{
    if (s_pwstate == 0) pagewalk_init();
    if (s_pwstate < 0) return 0;
    u32 pde = *(const volatile u32*)(s_pdebase + ((va >> 22) << 2));
    if (!(pde & 1u)) return 0;
    if (pde & 0x80u) return 1; /* 4 MB page */
    return (*(const volatile u32*)(s_ptebase + ((va >> 12) << 2)) & 1u) != 0;
}

static inline int vptr(u32 p)
{
    return ((p >= 0x10000u && p < 0x08000000u) || (p >= 0x80000000u && p < 0x88000000u)) && page_ok(p) && page_ok(p + 3);
}
static inline void* vfn(u8* o, u32 s) { return (*(void***)o)[s]; }
/* ---- P17 class name without a call into the game (safe sweep)
 * Where:  every place that reads a character's class name (consider, sweep_begin, register_p2, ...): type_name()
 * Was:    the name was got by calling the object's virtual slot 3. The sweep also meets objects the loader is still building or already
 *         tearing down; their table is a base class's, whose slot 3 is not a name function, and the call (made with no arguments) can
 *         pop stack words that are not its own. In a crash at the start of level 5 the scan's own loop counter, kept at the bottom of
 *         the frame, was overwritten that way, the scan ran on past the end of its page and read an unmapped one (fatal).
 * Now:    slot 3 of every real class is `mov eax, <name>; ret` (B8 imm32 C3, checked for all 302 named classes in the XBE): the code
 *         at the slot is checked for exactly that and the name is read from the immediate; anything else is "no name" (not a
 *         companion), and the game's function is never called. The page loop also checks every page it enters (see sweep_step).
 * Needed: Taking Obi-Wan at the start of level 5 while the level is still streaming in.
 * Source: this project (register dump of the crash, the XBE's class tables) */
static const char* type_name(u8* c)
{
    const u8* fn = (const u8*)vfn(c, 3);
    if (!vptr((u32)fn) || !vptr((u32)fn + 5) || fn[0] != 0xB8 || fn[5] != 0xC3) return 0;
    const char* n = *(const char* const*)(fn + 1);
    return vptr((u32)n) ? n : 0;
}

static inline u8 f8(u8* c, u32 o) { return *(c + o); }
static inline i32 fi(u8* c, u32 o) { return *(i32*)(c + o); }
static inline float ff(u8* c, u32 o) { return *(float*)(c + o); }

static int is_jedi(u8* c) { return ((IsAFn)vfn(c, 1))(c, 0, JEDI_TYPE_KEY); }

static int alive(u8* c)
{
    return c && vptr((u32)c) && vptr((u32)c + C_HEALTH) && !f8(c, C_DEAD) && fi(c, C_HEALTH) > 0; /* positive float bits */
}

/* ---- P16 inactive companions (logic, no code patched)
 * Where:  consider() / the sweep's end (who can be taken), the registered-player-two branch of coop_tick (what to do when they go), is_active()
 * Was:    A level can switch its companion off and on again (level 5: Obi-Wan retreats from the first fight, is hidden until the
 *         regroup cutscene). The take-over picked him anyway; the hidden body then followed player one (tether) and could not be moved.
 * Now:    The game's own "active" state of an object (byte +0x119 with the zone slot +0xD8; its deactivate routine at 0xF29F0 zeroes +0x119, clears the zone link at
 *         +0xD4 and sets +0xD8 = -1; the reactivation at 0xF2A50 sets +0x119 again) is read. A companion that is switched off is not
 *         taken, and if only a switched-off hero is there the search waits for him (STRAT_AUTO levels). Player two's character that is
 *         switched off for 10 frames is given back to the game (no tether, no controls) and taken again when it is on and no cutscene
 *         plays. Holding Back while waiting drops out as usual.
 * Needed: Levels whose companion is hidden for a while. Seen in dumps of the level 5 Obi-Wan before (+0x119 = 1,
 *         +0xD4 = a pointer, +0xD8 = 2) and after (0, 0, -1, +0x1C2 = 1) he vanished.
 * Source: this project (level 5 memory dumps; the routines at 0xF29F0 / 0xF2A50) */
#define C_ACTIVE 0x119u
#define INACTIVE_FRAMES 10
#define C_ZONE_SLOT 0xD8u
/* switched off = +0x119 is 0 and the zone slot is -1 (both as the deactivate routine leaves them): the second test keeps a
 * character that merely has the byte at 0 from being taken for a hidden one */
static int is_active(u8* c)
{
    if (!vptr((u32)c + C_ZONE_SLOT) || !vptr((u32)c + C_ACTIVE)) return 1;
    return c[C_ACTIVE] != 0 || *(i32*)(c + C_ZONE_SLOT) != -1;
}
static u32 s_inactive, s_waitActive, s_hiddenHero;
static u8* s_hiddenObj;
/* ---- P18 spawned companion (logic + two calls into the game, no code patched)
 * Where:  use_substitute(); the wantSub block of coop_tick; the game's own routines 0xF29F0 (deactivate: leaves the zone, +0x119 = 0, +0xD8 = -1,
 *         +0x1C2 = 1) and 0xF2A50 (activate again), both thiscall with no stack arguments (OBJ_DEACTIVATE / OBJ_ACTIVATE above)
 * Was:    A level that switches its companion off for a long stretch (level 5: Obi-Wan is gone from the first fight to the regroup) left player
 *         two with nobody to play, and the first design (swap between the level's companion and a spawned one as the level switched him on and off)
 *         fought with the level's scripts.
 * Now:    On STRAT_SPAWN levels (P19) player two is a spawned opposite hero beside player one (the level's own companion is never touched). When
 *         player two drops out the spawned hero is parked with the game's deactivate (hidden, no updates, kept for next time); when Back is held
 *         again he is activated, put beside player one and taken again. This is also what the Back button does to turn the spawned hero off at a
 *         scripted spot (level 5's turret) until the act-1 scripting pass does it by itself.
 * Needed: Levels whose companion the story moves around (level 5 first; the same for levels 7 to 16, which may have none at all).
 * Source: this project (level 5 dumps; the game's activate / deactivate routines) */
static u8* s_scr; static u32 s_scrId;     /* the level's own companion, once seen */
static u8* s_sub; static u32 s_subId;      /* our substitute, once made */
static u8 s_subParked, s_placeSub;
static u8 s_key = 0xFF;                  /* level key the last Back sweep decided (0xFF: not known yet) */
struct Event { u32 cutId; u8 key; u8 action; };         /* P25: a scripted event: when cutscene cutId plays in level key, do action */
enum { EV_LOCK_AFTER = 1 /* player 2 is out for the rest of the level once the cutscene has ended */, EV_LOCK_START = 2 /* the same, from the cutscene's start */,
       EV_AI_DURING = 3 /* P27: the computer plays the companion during the cutscene (player 2 gets him back afterwards) */ };
static const struct Event k_events[] = {
    { 0x001F8FB7u, KEY_L5, EV_LOCK_AFTER },   /* level 5: the cutscene before the turbolaser room (P24) */
    { 0x001EC91Cu, KEY_L1, EV_AI_DURING },    /* level 1: Obi-Wan crosses the bridge as it connects (P27) */
};
#define N_EVENTS ((u32)(sizeof k_events / sizeof k_events[0]))
static const struct Event* s_ev;         /* the event whose cutscene was seen in this level (0: none) */
static u8 s_cutDone, s_coopOver;         /* its trigger point is reached / player 2 is locked out (EV_LOCK_*) */
static u32 s_endCutQuiet, s_p1Dead;
enum { W_SCAN, W_ARMED, W_OPEN, W_CUT1, W_DONE };    /* P26 window: searching the objects / waiting for the order / companion walking / first cutscene seen / over */
#define SCAN_CUTS        24             /* P27: cutscene / multi-trigger objects the start-of-level scan remembers */
#define SCAN_TRIGS       192
static u32 s_scCutA[SCAN_CUTS], s_scCutId[SCAN_CUTS], s_scTrigA[SCAN_TRIGS], s_scTrigId[SCAN_TRIGS];
static u32 s_nCuts, s_nTrigs, s_scCur;
static u8 s_scState, s_sigKey = 0xFF, s_aiOn, s_aiResume; /* scan: 0 not started, 1 running, 2 done / the level key its ids gave / the companion is the computer's during a cutscene */
static u32 s_aiQuiet;
static u8 s_wState, s_wLock, s_wResume, s_wSeen2;    /* lock: Back does nothing; resume: player 2 goes back in when it is over */
static u32 s_wTrig, s_wCut, s_wCur, s_wT1, s_wQuiet, s_wDead; /* the objects, scan cursor, frame the first cutscene started */
static float s_leashSc2 = 1.0f;          /* squared scale of the tether distances for this level (see k_leash) */
static u8 s_strat, s_wantSub;           /* the level's strategy (see k_strategy) / player two is to be the spawned companion as soon as no cutscene plays */
static u32 s_subNext;           /* frame before which no spawn / unpark is tried */

static int targets_player(u8* c)
{
    u8* ai = *(u8**)(c + C_AI);
    return !vptr((u32)ai) || !vptr((u32)ai + AI_TARGET_PLAYER) || ai[AI_TARGET_PLAYER] != 0;
}

static int ieq(const char* a, const char* b)
{
    for (; *a && *b; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}

static int is_character(u8* c);
/* ---- P11 story-mode guard (data read, no code patched)
 * Where:  coop_tick: p1 = story_mode() ? player_object() : 0
 * Was:    The mod ran in every mode the game can be in, including the game's own co-op missions and Versus.
 * Now:    The options object [0x68F720] has flag bytes +0xCC..+0xCE that are non-zero in the game's own co-op / Versus modes; then the mod sees "no level" and does nothing (hooks fall through to the original behaviour).
 * Needed: The game's own co-op bonus mission crashed with the mod's hooks active.
 * Source: this project
 */
/* The game's own co-op / Versus modes (options object [0x68F720], flag bytes +0xCC..+0xCE): the mod stays out of them. */
#define G_MODE_OBJ 0x0068F720u
static int story_mode(void)
{
    u8* m = *(u8**)G_MODE_OBJ;
    if (!vptr((u32)m) || !vptr((u32)m + 0xCC) || !vptr((u32)m + 0xCF)) return 0;
    return !(m[0xCC] | m[0xCD] | m[0xCE]);
}

static u8* player_object(void)
{
    u8* rec = *(u8**)G_PRIMARY_RECORD;
    if (!vptr((u32)rec) || !vptr((u32)rec + 0xC)) return 0;
    u8* p = *(u8**)(rec + 0xC);
    if (!vptr((u32)p) || !is_character(p)) return 0;
    return p;
}

/* The engine's "a cutscene is playing" as coop.cpp asks it (0xA3720 reads the letterbox unchecked). */
static int in_cutscene(void)
{
    u8* lb = *(u8**)G_LETTERBOX;
    if (vptr((u32)lb) && (*(u32*)(lb + 0xC) & 0x40000u))
        return IN_CUTSCENE() != 0;
    return *(u32*)G_CINEMATIC != 0 || *(i32*)G_SCRIPTED_CAMS > 0;
}

static int pad2_present(void)
{
    g_dbg.padMask = *(volatile u32*)G_PAD_MASK;
    return (g_dbg.padMask & (1u << PLAYER2_PORT)) != 0;
}

#define VTABLE_LO 0x0055F660u /* .rdata range (game.h): real objects have a vtable in it */
#define VTABLE_HI 0x00612BC8u
#define AI_MARKER 0x00591C38u /* AI data vtable; a character's +0xA00 points to one that points back at +0x29C */

static int starts_with(const char* s, const char* p)
{
    for (; *p; ++s, ++p) {
        char x = *s, y = *p;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return 1;
}

static void copy_name(char* dst, const char* src)
{
    int i = 0;
    if (vptr((u32)src))
        for (; i < 7 && src[i]; ++i) dst[i] = src[i];
    for (; i < 8; ++i) dst[i] = 0;
}

static int hero_name(const char* n)
{
    static const char* const heroes[] = { "IObiwan", "IAnakin", "IJediKnight", "ICinDrallig", "ISerra", "IMace", "IYoda" };
    for (unsigned i = 0; i < sizeof heroes / sizeof heroes[0]; ++i)
        if (starts_with(n, heroes[i])) return 1;
    return 0;
}

static int is_character(u8* c)
{
    u32 vt = *(u32*)c;
    if (vt < VTABLE_LO || vt >= VTABLE_HI) return 0;
    if (!vptr((u32)c + C_AI)) return 0; /* small objects end before +0xA00; that page may be unmapped */
    u8* ai = *(u8**)(c + C_AI);
    return vptr((u32)ai) && *(u32*)ai == AI_MARKER && vptr((u32)ai + 0x29C) && *(u32*)(ai + 0x29C) == (u32)c;
}

#if COOP_DIAG
/* ---- Diagnostics (COOP_DIAG=1): filled by the tick, read from the monitor ---- */
/* The cutscenes of a level with player one's position at each start (cut[]): a way to find where a script switches things on or off. */
static u8* cw_p1; static u32 cw_prev;
static void cut_watch(u8* p1, u32 now)
{
    if (p1 != cw_p1) { cw_p1 = p1; for (int i = 0; i < 26; ++i) g_dbg.cut[i] = 0; cw_prev = 0; }
    u32 c = in_cutscene() != 0;
    if (c && !cw_prev && vptr((u32)p1 + C_TRANSFORM + 60)) {
        u32 n = g_dbg.cut[0];
        volatile u32* e = &g_dbg.cut[1 + (n % 5) * 5];
        const u32* q = (const u32*)(p1 + C_TRANSFORM + 48);
        e[0] = now; e[1] = 0; e[2] = q[0]; e[3] = q[1]; e[4] = q[2];
        g_dbg.cut[0] = n + 1;
    }
    if (!c && cw_prev) { u32 n = g_dbg.cut[0]; if (n) g_dbg.cut[1 + ((n - 1) % 5) * 5 + 1] = now; }
    cw_prev = c;
}
static void hz_fill(u32 at, u8* c)
{
    for (u32 i = 0; i < 4; ++i) g_dbg.hz[at + i] = 0;
    if (!c || !vptr((u32)c) || !is_character(c)) return;
    u8* ai = *(u8**)(c + C_AI);
    if (!vptr((u32)ai) || !vptr((u32)ai + AI_TEAMS + 3)) return;
    g_dbg.hz[at] = *(u32*)(ai + AI_TEAMS);
    g_dbg.hz[at + 1] = *(u32*)(c + C_FLAGS);
    g_dbg.hz[at + 2] = *(u32*)(c + 0xD4);
    g_dbg.hz[at + 3] = (u32)ai[AI_HASSIDE] | ((u32)c[C_INVINCIBLE] << 8) | (((u32)fi(c, C_CONTROL) & 0xFF) << 16) | ((u32)c[C_ACTIVE] << 24);
}
static void sub_status(void)
{
    g_dbg.sub[4] = (u32)s_sub; g_dbg.sub[6] = (u32)s_scr;
    u32 f = 0;
    if (s_subParked) f |= 1;
    if (s_p2 && s_p2 == s_sub) f |= 2;
    if (s_sub && vptr((u32)s_sub) && is_character(s_sub)) { if (alive(s_sub)) f |= 4; if (is_active(s_sub)) f |= 8; }
    g_dbg.sub[5] = f;
    f = 0;
    if (s_scr && vptr((u32)s_scr) && is_character(s_scr)) { if (is_active(s_scr)) f |= 1; if (alive(s_scr)) f |= 2; }
    g_dbg.sub[7] = f;
    hz_fill(0, s_p1);
    hz_fill(4, s_sub);
    hz_fill(8, s_scr);
}
#endif

/* The mission's companion: a living character the story moves (control 16) that is not the player's
 * enemy and is a Jedi (IsA) or a hero class by name, nearest to player 1. Like coop.cpp, characters are
 * found by sweeping memory (every aligned word whose value is a .rdata vtable, then the strict
 * is_character test). The sweep covers the game's heap in 1 MB slices, one slice per frame, and every
 * page is checked with page_ok first, so it cannot fault. */
#define SWEEP_LO    0x00977000u /* just past the XBE image */
#define SWEEP_HI    0x04000000u /* 64 MB ceiling; pages past the installed RAM are simply not present */
#define SWEEP_CHUNK 0x00200000u

static u32 s_cur, s_chars, s_jedis, s_rows, s_pages;
static u8* s_best;
static float s_bestd;
static int s_sweep;
static u32 s_hud[12], s_nhud;

/* ---- P19 / P27 one strategy per level (logic only)
 * Where:  scan_done() names the level from the ids of its cutscenes (k_sigs, below); k_strategy[] picks, per level key, how player two gets a body; k_leash[]
 *         how long the tether is; the scripted events (k_events) and windows look at the key too.
 * Was:    every level used the companion the mission has (taken over), or a spawned one when none was found; level 5 has its Obi-Wan switched off or
 *         scripted for most of the level (retreat, regroup, turbolaser), which broke the take-over. The level was guessed from "M<dd>_" strings in memory,
 *         which also hold other missions' leftovers: wrong now and then.
 * Now:    STRAT_AUTO (levels 1 to 4, 6, 9, 10, 12, 15, 16 and 17): the mission's companion is taken over (or the game's boss slot, on the duel levels), or one is made
 *         when there is none. STRAT_SPAWN (levels 5, 7, 8, 11, 13, 14): the mission's own companion is never touched; player two is always our own spawned
 *         character. A spawned player two is parked (the game's deactivate: hidden, no updates) when player two drops out and activated and placed beside
 *         player one when Back is held again.
 *         The key is the user's level number (1 .. 16; level 17 shares level 16's file and so its key). A level that k_sigs does not know has key 0xFF:
 *         nothing level-specific happens there (ordinary take-over, the normal tether, no scripted events).
 * Needed: Level 5, and the levels whose companion the story moves around or that have none.
 * Source: this project (the cutscene ids of levels 1 to 6, read with the recorder / levelid command). */
enum { STRAT_AUTO = 0, STRAT_SPAWN = 1 };
#define KEYS       20
/* The co-op rule of every level (the key is the user's level number; 17 is 16's key):
 *   AUTO:   the mission's companion is taken over (levels 1 to 4, 6, 15 = Yoda); the game's own boss slot is taken on the duel levels (9, 10, 12, 16/17, as Dooku on 3),
 *           see the sweep (the game manager says it has a second player); none of these two needs a row here
 *   SPAWN:  the level has no companion to take: a character of our own beside player one (5, 7, 8, 11, 13, 14; who: k_body) */
static const u8 k_strategy[KEYS] = {
    [1] = STRAT_AUTO,  [2] = STRAT_AUTO,  [3] = STRAT_AUTO,  [4] = STRAT_AUTO,  [5] = STRAT_SPAWN, [6] = STRAT_AUTO,
    [7] = STRAT_SPAWN, [8] = STRAT_SPAWN, [9] = STRAT_AUTO,  [10] = STRAT_AUTO, [11] = STRAT_SPAWN, [12] = STRAT_AUTO,
    [13] = STRAT_SPAWN, [14] = STRAT_SPAWN, [15] = STRAT_AUTO, [16] = STRAT_AUTO,
};
/* the tether's length as a percentage of the normal one: level 5's camera is close (60 %), level 3's is tighter than level 1's (70 %) */
static const u8 k_leash[KEYS]    = { 100, 100, 100, 70, 100, 60, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100 };
/* P31: who player two is on a STRAT_SPAWN level. One row per level key; to try another character or costume on a level, change its row only.
 *   BODY_OPPOSITE: the other hero (Anakin beside Obi-Wan and the other way round); costume / texture set -1 = the same number as player 1's (Obiwan and Anakin
 *                  list their plain, Duel and Cloak bodies as 0, 1, 2, so a cloaked Obi-Wan gets a cloaked Anakin); else the number given (level 5: 0, as before)
 *   BODY_COPY:     a copy of player 1's class; costume / texture set -1 = the same as player 1's, else the number from the class's costume list
 *                  (Obiwan: 0 plain, 1 Duel, 2 Cloak, 3 Dirty, 4-7 the NPC versions; Anakin: 0 plain, 1 Duel, 2 Cloak, 3 Hood_Down, 4-7 NPC, 8 Cloak_NoAnim)
 *   BODY_CLASS:    the named class ("IAnakin", "IObiwan", "IMace", "IYoda", ...) with the costume / texture set given (-1: 0) */
enum { BODY_OPPOSITE = 0, BODY_COPY = 1, BODY_CLASS = 2 };
struct Body { u8 mode; signed char costume; signed char texset; const char* cls; };
static const struct Body k_body[KEYS] = {
    [5]  = { BODY_OPPOSITE, 0, 0, 0 },     /* P1 Anakin: P2 Obi-Wan */
    [7]  = { BODY_COPY, -1, 1, 0 },       /* a copy of P1's class; texture set 1 = the recoloured skin (P39 to P41); the other costumes did not load (v27 tests) */
    [8]  = { BODY_COPY, -1, 1, 0 },
    [11] = { BODY_COPY, -1, 1, 0 },
    [13] = { BODY_COPY, -1, 1, 0 },
    [14] = { BODY_COPY, -1, 1, 0 },
};
static void apply_level(u32 key)
{
    u32 pct = key < KEYS ? k_leash[key] : 100;
    s_leashSc2 = (float)(pct * pct) / 10000.0f;
}

static void sweep_begin(u8* player)
{
    for (u32 row = 0; row < 8; ++row) g_dbg.rows[row].ptr = 0;
    g_dbg.p1IsJedi = (u32)is_jedi(player);
    copy_name((char*)g_dbg.p1Name, type_name(player));
    s_cur = SWEEP_LO; s_chars = s_jedis = s_rows = s_pages = 0;
    s_best = 0; s_bestd = 0.0f; s_nhud = 0; s_bossCand = 0; s_hiddenHero = 0; s_hiddenObj = 0;
    s_sweep = 1;
}

static void consider(u8* c, u8* player)
{
    if (s_bossMode) { if (*(u32*)(c + C_ID) == s_bossSlotId) s_bossCand = c; return; }
    const float* p = (const float*)(player + C_TRANSFORM + 48);
    s_chars++;
    i32 ctrl = fi(c, C_CONTROL);
    int jedi = is_jedi(c) != 0;
    if (jedi) s_jedis++;
    const char* name = type_name(c);
    int al = alive(c), tp = targets_player(c);
    int cand = al && ctrl == K_COMPANION && !tp && vptr((u32)name) &&
               (jedi || hero_name(name)) && !ieq(name, "IPalpatine");
    if (cand && !is_active(c)) { s_hiddenHero = 1; if (!s_hiddenObj) s_hiddenObj = c; cand = 0; } /* P16: switched off by the level: not now */
    if ((ctrl == K_COMPANION || jedi) && s_rows < 8) {
        struct CoopRow* r = &g_dbg.rows[s_rows++];
        r->ptr = (u32)c; r->id = *(u32*)(c + C_ID); r->control = ctrl;
        r->flags = (al ? 1u : 0) | (tp ? 2u : 0) | (jedi ? 4u : 0);
        copy_name(r->name, name);
    }
    if (!cand) return;
    const float* q = (const float*)(c + C_TRANSFORM + 48);
    float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];
    float d = dx * dx + dy * dy + dz * dz;
    if (!s_best || d < s_bestd) { s_best = c; s_bestd = d; }
}

/* One slice; returns 1 when the whole range has been swept. */
static int sweep_step(u8* player)
{
    u32 end = s_cur + SWEEP_CHUNK;
    if (end > SWEEP_HI) end = SWEEP_HI;
    u32 a = s_cur;
    /* One loop over addresses: the page check is made again every time a page boundary is reached, so nothing (a loop counter that
     * was overwritten, a page the loader unmapped meanwhile) can take the scan into a page that was not checked. */
    while (a < end) {
        if ((a & 0xFFFu) == 0 && !page_ok(a)) { a += 0x1000; continue; }
        if ((a & 0xFFFu) == 0) s_pages++;
        u32 pgend = (a | 0xFFFu) + 1;
        u32 v = *(u32*)a;
        u32 here = a;
        a += 4;
        if (v - VTABLE_LO >= VTABLE_HI - VTABLE_LO) continue;
        if ((v == HUD_VITALS_FADE || v == HUD_PORTRAIT) && s_nhud < 12 && here + 0x14 <= pgend) { s_hud[s_nhud++] = here; continue; }
        u8* c = (u8*)here;
        if (c == player || !is_character(c)) continue;
        consider(c, player);
    }
    s_cur = end;
    g_dbg.maxIndex = s_cur;
    return s_cur >= SWEEP_HI;
}


static int still_char(u8* c, u32 id)
{
    return vptr((u32)c) && is_character(c) && *(u32*)(c + C_ID) == id;
}

/* A cutscene can leave the last element of an AI character's transform (+0x18C, copy +0x75C) not a
 * number; a player-controlled character cannot move with it (coop.cpp RepairTransform). */
static void repair_transform(u8* c)
{
    static const u32 offs[2] = { C_TRANSFORM + 0x3C, 0x720 + 0x3C };
    for (int i = 0; i < 2; ++i) {
        u32* w = (u32*)(c + offs[i]);
        if ((*w & 0x7F800000u) == 0x7F800000u) *(float*)w = 1.0f;
    }
}

static u8* port_entry(int port)
{
    u8* im = *(u8**)G_INPUT_MANAGER;
    if (!vptr((u32)im) || !(*(u32*)(im + 0xC) & 0x40000u)) return 0;
    u8* e = *(u8**)(im + 0x1D8 + port * 4);
    return vptr((u32)e) ? e : 0;
}

static int controls_intact(u8* c)
{
    u8* e = port_entry(PLAYER2_PORT);
    return fi(c, C_CONTROL) == K_PLAYER && fi(c, C_CONTROLLER) == PLAYER2_PORT && e && *(u8**)(e + 0x7C) == c;
}



/* A maximum changed to `m`, the current value kept in proportion (coop.cpp SetMaximum). */
static void set_max(u8* c, u32 cur, u32 mx, float m)
{
    if (!vptr((u32)c + mx) || !vptr((u32)c + cur)) return;
    float* v = (float*)(c + cur);
    float* o = (float*)(c + mx);
    if (!(m > 0.0f) || *o == m) return;
    *v = (*o > 0.0f) ? (*v / *o * m) : m;
    *o = m;
}

/* Player 2 as strong as player 1: the same maximum health and Force (not linked: each keeps its own
 * current values). Force only between Jedi, as coop.cpp MatchPlayer1. */
static void match_p1(u8* c, u8* p1)
{
    if (!vptr((u32)p1 + C_MAXHEALTH)) return;
    set_max(c, C_HEALTH, C_MAXHEALTH, *(float*)(p1 + C_MAXHEALTH));
    if (vptr((u32)p1 + C_MAXPOWER) && vptr((u32)c + C_MAXPOWER) && is_jedi(p1) && is_jedi(c))
        set_max(c, C_POWER, C_MAXPOWER, *(float*)(p1 + C_MAXPOWER));
}

/* ---- P9 mastery gauge for player two (data write)
 * Where:  register_p2 / release_p2: character bytes +0xB5F and +0xAFD
 * Was:    Only a character that is already a player when created has the mastery gauge on (+0xB5F), and the kill-rating popup uses +0xAFD to pick the second slot, so a taken-over companion earned no mastery and no rating popups.
 * Now:    Both bytes are set to 1 while player two is in control and put back on drop-out.
 * Needed: Mastery ring and kill ratings for player two in the story levels.
 * Source: this project (found with the Test D/E/F dumps); the game's own two-player set-up at 0x2AAE8E sets the same bytes
 */
/* The mastery (combo) gauge. The game turns it on only for a character that is already a player when it is
 * created (character +0xB5F, set by the code at 0x2673B2 using 0x27ABE0), so a companion that player two takes
 * over has it off and its hits earn nothing (0x269E40 starts with that byte). Byte +0xAFD names the second player
 * for the kill-rating popup (0x26938A passes it as the slot to 0x2A7AF0); the game's own two-player set-up
 * (0x2AAE8E) sets both bytes on the second player. */
#define C_MASTERY_ON   0xB5Fu
#define C_PLAYER2_FLAG 0xAFDu
static u8 s_svMasteryOn, s_svPlayer2Flag;
static volatile u8* s_fixInst; /* the Force script instance the fix changed, and what it held before */
static u8 s_fixSave[0x11];
static u8 s_spawnedNext, s_p2Spawned;   /* the next registration is of a character this code spawned / the current player 2 is */
static u32 s_svUpg[14]; static u8 s_svUpgOk;

/* coop.cpp Register(), the non-boss path. */
static void register_p2(u8* c, u8* p1)
{
    u8* mgr = *(u8**)G_GAME_MANAGER;
    if (!vptr((u32)mgr) || !vptr((u32)mgr + 0x2A8)) return;
    /* what to give back */
    s_p2Spawned = (s_spawnedNext || c == s_sub) ? 1 : 0; s_spawnedNext = 0;
    if (!s_p2Spawned) { s_scr = c; s_scrId = *(u32*)(c + C_ID); }
    DIAG(g_dbg.st[40] = s_p2Spawned; g_dbg.st[s_p2Spawned ? 41 : 42]++; g_dbg.st[43] = 0xFF; g_dbg.st[44] = 0;)
    s_svControl = fi(c, C_CONTROL);
    s_svInvincible = f8(c, C_INVINCIBLE);
    s_svCount = *(i32*)(mgr + 0x1E4);
    s_svSlot = *(u32*)(mgr + 0x2A8);
    s_svMaxHealth = *(float*)(c + C_MAXHEALTH);
    s_svMaxPower = vptr((u32)c + C_MAXPOWER) ? *(float*)(c + C_MAXPOWER) : 0.0f;
    repair_transform(c);
    match_p1(c, p1);
    { const char* yn = type_name(c); if (yn && starts_with(yn, "IYoda") && vptr((u32)c + C_MAXPOWER)) set_max(c, C_POWER, C_MAXPOWER, *(float*)(c + C_MAXPOWER) * YODA_MAX_X); }   /* P43 */
    *(i32*)(c + C_CONTROL) = K_PLAYER;
    BIND_CONTROLLER(c, 0, PLAYER2_PORT);
    *(c + C_INVINCIBLE) = 0;
    /* the second HUD: slot 2 names player 2 while there are two players */
    *(u32*)(mgr + 0x2A8) = *(u32*)(c + C_ID);
    *(i32*)(mgr + 0x1E4) = 2;
    if (vptr((u32)c + C_MASTERY_ON) && vptr((u32)c + C_PLAYER2_FLAG)) {
        s_svMasteryOn = c[C_MASTERY_ON]; s_svPlayer2Flag = c[C_PLAYER2_FLAG];
        c[C_MASTERY_ON] = 1; c[C_PLAYER2_FLAG] = 1;
    }
    /* ---- P10 upgrade levels (data write)
     * Where:  register_p2 (copy) and release_p2 (restore): character +0x1024..+0x105F, two arrays of seven dwords
     * Was:    each hero has his own unlocks; the companion has the story-level default.
     * Now:    player two's arrays are player one's while player two is in control; the companion's own are put back
     *         on drop-out.
     * Needed: "the companion's unlocks match player one's". Index meaning between the two heroes is assumed equal.
     * Source: this project (arrays read by 0x280BD0 / 0x280BA0) */
    /* the unlocks: player 2's upgrade levels (two arrays of seven, read by 0x280BD0 / 0x280BA0) are player 1's */
    if (c != p1 && vptr((u32)c + 0x1024) && vptr((u32)c + 0x105C) && vptr((u32)p1 + 0x1024) && vptr((u32)p1 + 0x105C)) {
        volatile u32* d = (volatile u32*)(c + 0x1024); volatile u32* sr = (volatile u32*)(p1 + 0x1024);
        for (int i = 0; i < 14; ++i) { s_svUpg[i] = d[i]; d[i] = sr[i]; }
        s_svUpgOk = 1;
    }
    s_p2 = c;
    s_p2Id = *(u32*)(c + C_ID);
    {
        const char* nm = type_name(c);
        int i = 0;
        if (vptr((u32)nm)) for (; i < 23 && vptr((u32)nm + i) && nm[i]; ++i) s_p2Name[i] = nm[i];
        s_p2Name[i] = 0;
        s_p2Costume = *(i32*)(c + C_COSTUME);
        s_p2TexSet = *(i32*)(c + C_TEXSET);
    }
    s_guard = 1; s_respawnPending = s_rescuePending = 0;
    s_deadSince = 0; s_farSince = 0; s_shieldUntil = 0;
    s_dropped = 0;
    s_nextMatch = g_dbg.frames + MATCH_FRAMES;
    g_dbg.player2 = (u32)c;
    g_dbg.state = ST_REGISTERED;
}

/* coop.cpp ResetPlayer2Hud(true): the second HUD (found in the sweep) is emptied: the vitals' item goes
 * invisible and their fade restarts, and the portrait is to be picked again when someone joins. Only the
 * handlers whose group names slot 2 (+0x84 == 1). */
static void hide_p2_hud(void)
{
    for (u32 i = 0; i < s_nhud; ++i) {
        u8* h = (u8*)s_hud[i];
        if (!vptr((u32)h + 0x14)) continue;
        u32 vt = *(u32*)h;
        if (vt != HUD_VITALS_FADE && vt != HUD_PORTRAIT) continue;
        u8* group = *(u8**)(h + 8);
        if (!vptr((u32)group) || !vptr((u32)group + 0x84 + 3) || *(i32*)(group + 0x84) != 1) continue;
        if (vt == HUD_PORTRAIT) {
            *(h + 0xC) = 0;
            *(h + 0xD) = 0;
        } else {
            u8* item = *(u8**)(h + 4);
            if (vptr((u32)item) && vptr((u32)item + 0xC)) *(item + 0xB) = 0;
            *(float*)(h + 0x10) = 0.0f;
        }
    }
}

/* A character of class `name` (costume/texture set given), placed in front of player 1 and set on player 1's
 * side. Returns the object or 0. */
static u8* spawn_char(u8* p1, const char* name, i32 costume, i32 texset)
{
    if (!vptr((u32)name)) return 0;
    void* info = CLASS_LOOKUP(name);
    if (!info) return 0;
    u8* o = CREATE_INSTANCE(info);
    if (!vptr((u32)o) || !vptr((u32)o + C_AI)) return 0;
    *(u32*)(o + C_FLAGS) &= ~FLAG_INACTIVE;
    *(i32*)(o + C_COSTUME) = costume;
    *(i32*)(o + C_TEXSET) = texset;
    float m[16];
    const float* t = (const float*)(p1 + C_TRANSFORM);
    for (int i = 0; i < 16; ++i) m[i] = t[i];
    for (int i = 0; i < 3; ++i) m[12 + i] += m[8 + i] * SPAWN_DISTANCE;
    for (int i = 0; i < 3; ++i) { m[i] = -m[i]; m[8 + i] = -m[8 + i]; }
    if (!SPAWN_INSTANCE(o, 0, m, 1)) return 0;
    ((ActivateFn)vfn(o, VSLOT_ACTIVATE))(o, 0);
    /* an ally: player 1 and its allies share the port's team bit; it does not target the player */
    u8* pai = *(u8**)(p1 + C_AI);
    u8* ai = *(u8**)(o + C_AI);
    if (vptr((u32)pai + AI_TEAMS) && vptr((u32)ai + AI_TEAMS)) {
        *(u32*)(pai + AI_TEAMS) |= PORT_PLAYER_TEAM;
        *(u32*)(ai + AI_TEAMS) = PORT_PLAYER_TEAM;
        *(ai + AI_HASSIDE) = 1;
        *(ai + AI_TARGET_PLAYER) = 0;
        *(ai + AI_IGNORED) = 0;
    }
    int i = 0;
    for (; i < 7 && name[i]; ++i) g_dbg.spawnName[i] = name[i];
    for (; i < 8; ++i) g_dbg.spawnName[i] = 0;
    return o;
}

/* Who player 2 is where the mission has no companion: the row of the level's key in k_body (P31). A level without a row, or the "no companion found"
 * fallback on an AUTO level, gets the old default: the other hero, and a copy of their own class for any other player 1. */
static u8* spawn_body(u8* p1)
{
    const char* n = type_name(p1);
    if (!vptr((u32)n)) return 0;
    i32 pc = *(i32*)(p1 + C_COSTUME), pt = *(i32*)(p1 + C_TEXSET);
    const struct Body* b = (s_key < KEYS && k_strategy[s_key] == STRAT_SPAWN) ? &k_body[s_key] : 0;
    if (b && b->mode == BODY_COPY) return spawn_char(p1, n, b->costume < 0 ? pc : b->costume, b->texset < 0 ? pt : b->texset);
    if (b && b->mode == BODY_CLASS && b->cls) return spawn_char(p1, b->cls, b->costume < 0 ? 0 : b->costume, b->texset < 0 ? 0 : b->texset);
    i32 oc = (b && b->costume < 0) ? pc : (b ? b->costume : 0), ot = (b && b->texset < 0) ? pt : (b ? b->texset : 0);
    if (starts_with(n, "IObiwan")) return spawn_char(p1, "IAnakin", oc, ot);
    if (starts_with(n, "IAnakin")) return spawn_char(p1, "IObiwan", oc, ot);
    return spawn_char(p1, n, pc, pt);
}

/* --- the tether (coop.cpp FollowPlayer1 / PlaceSafely / Leash / Respawn) --- */

/* Where player 1 stood a moment ago, while not falling: spots to put player 2 on. */
static void follow_p1(u8* p1)
{
    const float* p = (const float*)(p1 + C_TRANSFORM + 48);
    int falling = (s_lastY - p[1]) > FALL_PER_FRAME;
    s_lastY = p[1];
    if (falling) return;
    if (s_ntrail) {
        const struct Pt* l = &s_trail[s_ntrail - 1];
        float dx = p[0] - l->x, dy = p[1] - l->y, dz = p[2] - l->z;
        if (dx * dx + dy * dy + dz * dz < TRAIL_SPACING2) return;
    }
    if (s_ntrail == TRAIL_LEN) {
        for (u32 i = 1; i < TRAIL_LEN; ++i) s_trail[i - 1] = s_trail[i];
        --s_ntrail;
    }
    s_trail[s_ntrail].x = p[0]; s_trail[s_ntrail].y = p[1]; s_trail[s_ntrail].z = p[2];
    ++s_ntrail;
}

/* Player 2 where player 1 stood a moment ago (a little behind them, on ground they walked on), facing as
 * player 1 does; just behind player 1 when they have not moved yet. */
static void place_safely(u8* p1, u8* c)
{
    float m[16];
    const float* t = (const float*)(p1 + C_TRANSFORM);
    for (int i = 0; i < 16; ++i) m[i] = t[i];
    const float px = m[12], py = m[13], pz = m[14];
    m[12] -= m[8] * BEHIND;
    m[14] -= m[10] * BEHIND;
    for (u32 k = s_ntrail; k > 0; --k) {
        const struct Pt* q = &s_trail[k - 1];
        float dx = q->x - px, dy = q->y - py, dz = q->z - pz;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 >= BEHIND2 && d2 <= TRAIL_REACH2) { m[12] = q->x; m[13] = q->y; m[14] = q->z; break; }
    }
    ((PlaceFn)vfn(c, VSLOT_PLACE))(c, 0, m);
}

/* Player 2 back on their feet by player 1, with full health and a moment's shield. */
static void respawn_p2(u8* p1, u32 now)
{
    place_safely(p1, s_p2);
    *(float*)(s_p2 + C_HEALTH) = *(float*)(s_p2 + C_MAXHEALTH);
    *(s_p2 + C_INVINCIBLE) = 1;
    s_shieldUntil = now + SHIELD_FRAMES;
}

/* Player 2 too far from player 1 for a moment (at once when very far) is brought back. */
static void leash(u8* p1, u32 now)
{
    const float* a = (const float*)(p1 + C_TRANSFORM + 48);
    const float* b = (const float*)(s_p2 + C_TRANSFORM + 48);
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    float d2 = dx * dx + dy * dy + dz * dz;
    union { float f; u32 u; } cv; cv.f = d2; g_dbg.lastDist = cv.u;
    if (d2 <= LEASH_DIST2 * s_leashSc2) { s_farSince = 0; return; }
    if (!s_farSince) s_farSince = now;
    u32 wait = d2 > FAR_DIST2 * s_leashSc2 ? 0 : LEASH_FRAMES;
    if (now - s_farSince >= wait) {
        place_safely(p1, s_p2);
        s_farSince = 0;
        g_dbg.leashes++;
    }
}

/* coop.cpp Unregister(), the companion path: the character goes back to the game as it was. The port's own
 * input entry is cleared here first: BindController(-1) alone leaves it driving the character (it would
 * stand still with an idle controller). */
static void release_p2(void)
{
    u8* c = s_p2;
    s_p2 = 0;
    s_guard = 0;
    g_dbg.player2 = 0;
    if (!c || !still_char(c, s_p2Id)) { g_dbg.state = ST_LOST; return; }
    /* the companion goes back to its own powers: its upgrade levels and its Force script's flags */
    if (s_svUpgOk && vptr((u32)c + 0x1024) && vptr((u32)c + 0x105C)) {
        volatile u32* d = (volatile u32*)(c + 0x1024);
        for (int i = 0; i < 14; ++i) d[i] = s_svUpg[i];
    }
    s_svUpgOk = 0;
    if (s_fixInst && vptr((u32)s_fixInst + 0x1D6) && vptr((u32)s_fixInst + 0x2E8)) {
        for (int i = 0; i < 16; ++i) s_fixInst[0x2D8 + i] = s_fixSave[i];
        s_fixInst[0x1D6] = s_fixSave[16];
        DIAG(g_dbg.st[45]++; g_dbg.st[46] = s_fixSave[16];)
    }
    s_fixInst = 0;
    u8* e = port_entry(PLAYER2_PORT);
    if (e && vptr((u32)e + 0x7C) && *(u8**)(e + 0x7C) == c) *(u8**)(e + 0x7C) = 0;
    BIND_CONTROLLER(c, 0, -1);
    *(i32*)(c + C_CONTROL) = s_svControl;
    *(c + C_INVINCIBLE) = s_svInvincible;
    if (vptr((u32)c + C_MASTERY_ON) && vptr((u32)c + C_PLAYER2_FLAG)) {
        c[C_MASTERY_ON] = s_svMasteryOn; c[C_PLAYER2_FLAG] = s_svPlayer2Flag;
    }
    u8* mgr = *(u8**)G_GAME_MANAGER;
    if (vptr((u32)mgr) && vptr((u32)mgr + 0x2A8)) {
        *(u32*)(mgr + 0x2A8) = s_svSlot;
        *(i32*)(mgr + 0x1E4) = s_svCount;
    }
    set_max(c, C_HEALTH, C_MAXHEALTH, s_svMaxHealth);
    if (s_svMaxPower > 0.0f) set_max(c, C_POWER, C_MAXPOWER, s_svMaxPower);
    s_last = c; s_lastId = s_p2Id;
    hide_p2_hud();
    g_dbg.state = ST_DROPPED;
}

/* ---- P14 boss duel (player two plays the mission's boss)
 * Where:  coop_tick (detection in the sweep, tick_boss), register_boss / release_boss; no game code is patched.
 * Was:    missions where the game has a second player of its own (Dooku in the throne room: the boss is slot 2, with
 *         his health bar and the duel camera, and targets player one) were skipped.
 * Now:    holding Back on pad 2 finds the boss (the character the game names as slot 2), sets his control mode to
 *         player and binds pad 2 to him: nothing else changes (his health, health bar, camera, side and the
 *         mission's win / lose rules stay the game's). Holding Back again gives him back. If he dies, player two
 *         cannot take him again, but can take the next boss the game names in the same slot (P36, level 12). None of the companion extras apply (s_guard stays 0: no tether, respawn, pickups,
 *         shared camera, mastery or upgrade copy).
 * Needed: Dooku (and later bosses) played by the second controller; the companion Obi-Wan stays the game's.
 * Source: PC port (coop.cpp Register() boss path, "Player 2 As Boss"); drop-in / drop-out is this project's. */
static void register_boss(u8* c)
{
    /* what the boss goes back to: his control mode and flags, as the game had them (taken before anything is changed) */
    s_svControl = fi(c, C_CONTROL);
    s_svInvincible = f8(c, C_INVINCIBLE);
    s_svMasteryOn = s_svPlayer2Flag = 0;
    if (vptr((u32)c + C_MASTERY_ON) && vptr((u32)c + C_PLAYER2_FLAG)) { s_svMasteryOn = c[C_MASTERY_ON]; s_svPlayer2Flag = c[C_PLAYER2_FLAG]; }
    s_p2Spawned = 0; s_spawnedNext = 0;
    repair_transform(c);
    *(i32*)(c + C_CONTROL) = K_PLAYER;
    BIND_CONTROLLER(c, 0, PLAYER2_PORT);
    s_p2 = c; s_p2Id = *(u32*)(c + C_ID);
    s_boss = 1; s_bossObj = c; s_bossId = s_p2Id;
    { const char* nm = type_name(c); s_bossKind = (nm && (starts_with(nm, "ISerra") || starts_with(nm, "ICinDrallig"))) ? 1 : 0; } /* P38 */
    s_guard = 0; s_dropped = 0; s_bossFails = 0;
    s_nextReassert = g_dbg.frames + REASSERT_FRAMES;
    g_dbg.player2 = (u32)c;
    g_dbg.state = ST_REGISTERED;
}

static void release_boss(void)
{
    u8* c = s_p2;
    s_p2 = 0; s_boss = 0; s_guard = 0;
    g_dbg.player2 = 0;
    if (!c || !still_char(c, s_p2Id)) { g_dbg.state = ST_LOST; return; }
    u8* e = port_entry(PLAYER2_PORT);
    if (e && vptr((u32)e + 0x7C) && *(u8**)(e + 0x7C) == c) *(u8**)(e + 0x7C) = 0;
    BIND_CONTROLLER(c, 0, -1);
    *(i32*)(c + C_CONTROL) = s_svControl;
    *(c + C_INVINCIBLE) = s_svInvincible;
    if (vptr((u32)c + C_MASTERY_ON) && vptr((u32)c + C_PLAYER2_FLAG)) { c[C_MASTERY_ON] = s_svMasteryOn; c[C_PLAYER2_FLAG] = s_svPlayer2Flag; }
    if (s_fixInst && vptr((u32)s_fixInst + 0x1D6) && vptr((u32)s_fixInst + 0x2E8)) {
        for (int i = 0; i < 16; ++i) s_fixInst[0x2D8 + i] = s_fixSave[i];
        s_fixInst[0x1D6] = s_fixSave[16];
    }
    s_fixInst = 0; s_bossKind = 0;
    g_dbg.state = ST_DROPPED;
}

static void xh_drop(void)
{
    s_xh = 0;
}

/* Is Back held on pad 2? (0 whenever anything about the pad is not as expected.) */
/* Never opens a pad handle of its own: the XDK allows one handle per pad, and when ours held pad 2 the game's own
 * XInputOpen later failed (null handle) and its next XInputGetState crashed. The game's handle for pad 2 is looked
 * up on every call (it can be opened or closed at any time) and only read from, once it checks out. */
static int back_down(u32 now)
{
    (void)now;
    s_xdiag = 0;
    s_xh = 0;
    if (!pad2_present()) return 0;
    u8* rec = XPORT_RECORD(G_DESC_GAMEPAD, PLAYER2_PORT);
    if (!rec || !vptr((u32)rec + 0x12) || !vptr((u32)rec + 0x12 + 3)) return 0;
    s_xdiag = 1;
    u32 h = *(u32*)(rec + 0x12);
    /* what XInputGetState touches: handle +0, +4, +8, +0x14.. (the packet), +0xA2/+0xA3, and the record at [+0xA3] +0x28 */
    if (!h || !vptr(h) || !vptr(h + 0x40) || !vptr(h + 0xA2) || !vptr(h + 0xA3 + 3)) return 0;
    u32 hrec = *(u32*)(h + 0xA3);
    if (!vptr(hrec + 0x28)) return 0;
    u32 hp = *(u32*)h;
    if (hp && !vptr(hp + 4)) return 0;
    s_xdiag = 3;
    s_xh = (void*)h;
    u32 st[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }; /* XINPUT_STATE: packet number, then the gamepad (wButtons at +4) */
    if (XINPUT_GET_STATE(s_xh, st) != 0) return 0;
    s_xdiag = 7;
    return ((st[1] & 0xFFFFu) & XINPUT_BACK) != 0;
}

/* P18 / P19: player two's body is the spawned companion: the parked one activated again, or a new one spawned. */
static void use_substitute(u8* p1, u32 now)
{
    s_subNext = now + 120;
    if (s_sub && still_char(s_sub, s_subId) && alive(s_sub)) {
        if (s_subParked) { OBJ_ACTIVATE(s_sub); s_subParked = 0; s_placeSub = 1; DIAG(g_dbg.sub[2]++;) }
        s_spawnObj = s_sub; s_spawnFrame = now;
        s_waitActive = 0; s_wantSub = 0; g_dbg.state = ST_SPAWNED;
        return;
    }
    u8* o = spawn_body(p1);
    if (!o) { DIAG(g_dbg.sub[10]++;) return; }
    s_sub = o; s_subId = *(u32*)(o + C_ID); s_subParked = 0;
    DIAG(g_dbg.sub[0]++;)
    s_spawnObj = o; s_spawnFrame = now;
    s_waitActive = 0; s_wantSub = 0; g_dbg.state = ST_SPAWNED;
}

/* Everything the code remembers about a level, forgotten: called when player one is gone (menus, loading) and when a level (re)starts. */
static void forget_level(u32 now)
{
    s_boss = s_bossMode = s_bossDone = s_bossFails = s_bossDeadId = 0; s_bossObj = 0; s_bossKind = 0;
    s_fixInst = 0; s_svUpgOk = 0;
    s_p2 = 0; s_inactive = s_waitActive = 0; s_last = 0; s_guard = 0; s_ntrail = 0;
    s_scr = s_sub = 0; s_subParked = s_placeSub = 0; s_strat = s_wantSub = 0;
    s_key = 0xFF; s_ev = 0; s_cutDone = s_coopOver = 0; s_endCutQuiet = s_p1Dead = 0;
    s_yoda = 0; s_yodaId = 0;
    s_scState = 0; s_scCur = 0; s_nCuts = s_nTrigs = 0; s_sigKey = 0xFF; s_aiOn = s_aiResume = 0; s_aiQuiet = 0;
    s_wState = W_SCAN; s_wLock = s_wResume = s_wSeen2 = 0; s_wTrig = s_wCut = 0; s_wCur = 0; s_wT1 = s_wQuiet = s_wDead = 0; s_subNext = 0; s_leashSc2 = 1.0f;
    s_sweep = 0; s_nhud = 0; s_dropped = 1; s_seen = now; s_nextTry = 0; s_fails = 0; s_spawnTried = 0; s_spawnObj = 0; s_lastCutscene = 0;
}

/* P24: level 5's last room (the turbolaser room) is Anakin's alone. Once the cutscene before it has ended player 2 is let go like a drop-out (the spawned
 * Obi-Wan is switched off, the mission's own is never touched) and Back does nothing until the level ends or player 1 dies (a restart must not lock player 2 out).
 * Needed: the turbolaser section cannot be shared and the spawned Obi-Wan would stand next to the mission's own there.
 * Source: this project (xbwatch recording of level 5: the cutscene's id in the cinematic cell) */
static void end_coop(void)
{
    if (s_p2 && s_boss) release_boss();
    else if (s_p2) release_p2();
    if (s_sub && still_char(s_sub, s_subId) && alive(s_sub) && !s_subParked) { OBJ_DEACTIVATE(s_sub); s_subParked = 1; }
    s_spawnObj = 0; s_wantSub = 0; s_waitActive = 0; s_sweep = 0; s_bossMode = 0;
    s_dropped = 1; s_coopOver = 1; s_backHold = s_backLatch = 0;
    g_dbg.player2 = 0; g_dbg.state = ST_OVER;
}

/* Player 2 lets go of the companion (Back, or a scripted event): the character goes back to the game; a spawned one is switched off. */
static void drop_out(void)
{
    if (s_p2 && s_boss) {
        release_boss();
    } else if (s_p2) {
        u8* x = s_p2;
        int sp = s_p2Spawned && x == s_sub;
        release_p2();
        if (sp && still_char(x, s_subId) && alive(x)) { /* P19: a spawned companion is hidden while player two is out */
            OBJ_DEACTIVATE(x);
            s_subParked = 1;
            DIAG(g_dbg.sub[1]++;)
        }
        s_wantSub = 0;
    } else if (s_waitActive) { /* waiting for a switched-off companion: drop out */
        s_waitActive = 0;
    }
    s_dropped = 1;
}

/* Player 2 takes the same character again, or looks for one again if it is gone (the second half of the Back toggle). */
static void drop_in(u8* p1)
{
    s_dropped = 0;
    if (s_strat == STRAT_SPAWN || (s_sub && s_last == s_sub)) { /* P19: the spawned companion: activated (if parked) and placed beside player one */
        s_wantSub = 1; s_subNext = 0;
    } else if (s_last && still_char(s_last, s_lastId) && alive(s_last)) {
        if (is_active(s_last)) register_p2(s_last, p1);
        else { s_waitActive = 1; s_subNext = 0; } /* P16: switched off by the level at the moment: taken when he is back */
    }
    else { s_last = 0; g_dbg.state = ST_WAIT_SETTLE; s_nextTry = 0; }
}

static u32 cut_id(void) /* id of the cutscene that is playing now (0: none) */
{
    u32 cell = *(volatile u32*)G_CINEMATIC;
    if (cell && vptr(cell) && vptr(cell + 4) && *(u32*)cell == VT_CUTSCENE) return *(u32*)(cell + 4);
    return 0;
}

static int event_key_ok(const struct Event* e) { return e->key == KEY_ANY || e->key == s_key; }

static void event_watch(u8* p1)
{
    u32 cell = *(volatile u32*)G_CINEMATIC;
    u32 id = cut_id();
    if (id) {
        g_dbg.lvx[2] = id;
        for (u32 i = 0; i < N_EVENTS; ++i) {
            const struct Event* e = &k_events[i];
            if (e->cutId != id) continue;
            if (e->action == EV_AI_DURING) { /* P27: the companion is the computer's for this cutscene */
                if (!s_aiOn && (s_key == 0xFF || event_key_ok(e))) {
                    s_aiOn = 1; s_aiQuiet = 0; s_aiResume = (s_p2 != 0);
                    if (s_p2 || s_waitActive) drop_out();
                    g_dbg.state = ST_WINDOW;
                }
            } else { s_ev = e; s_endCutQuiet = 0; }
        }
    }
    if (s_aiOn) { /* ... and given back (to player 2, if they had him) when it is over */
        if (cell == 0 && !in_cutscene()) {
            if (++s_aiQuiet >= EVENT_QUIET_FRAMES) { s_aiOn = 0; if (s_aiResume && !s_coopOver) s_wResume = 1; s_aiResume = 0; }
        } else s_aiQuiet = 0;
    }
    if (s_ev && s_key != 0xFF && !event_key_ok(s_ev)) { s_ev = 0; s_cutDone = 0; } /* the same id in another level: not ours */
    if (s_ev && !s_cutDone) {
        if (s_ev->action == EV_LOCK_START) s_cutDone = 1;
        else {
            if (cell == 0 && !in_cutscene()) s_endCutQuiet++; else s_endCutQuiet = 0;
            if (s_endCutQuiet >= EVENT_QUIET_FRAMES) s_cutDone = 1;
        }
    }
    if (s_coopOver) { /* player one died: a restart from a checkpoint, player 2 may drop in again */
        if (!alive(p1)) { if (++s_p1Dead >= P1_DEAD_FRAMES) { s_coopOver = s_cutDone = 0; s_ev = 0; s_p1Dead = 0; g_dbg.state = ST_DROPPED; } }
        else s_p1Dead = 0;
    } else if (s_cutDone && s_key != 0xFF && event_key_ok(s_ev)) end_coop(); /* (a key not known yet waits for the first Back sweep) */
    g_dbg.lvx[3] = (u32)(s_ev != 0) | ((u32)s_cutDone << 1) | ((u32)s_coopOver << 2) | ((u32)s_key << 8) | (s_ev ? (u32)(s_ev - k_events + 1) << 16 : 0);
}

/* P26: looks through memory once per level for the trigger and the cutscene (by class and id), then reads one flag word of each per frame. */
static int win_obj_ok(u32 o, u32 vt, u32 id)
{
    return o && vptr(o) && vptr(o + 0x9C) && *(u32*)o == vt && *(u32*)(o + 4) == id;
}

/* P27: the start-of-level scan. One pass over memory (WIN_SCAN_CHUNK bytes a frame) remembers every cutscene and multi-trigger object (class and id). The ids tell
 * which level this is (each level file has its own ids, k_sigs), and the scripted events look their objects up in the lists. */
struct Sig { u32 cutId; u8 key; };
static const struct Sig k_sigs[] = {
    /* level 1: 17 cutscenes */
    { 0x00206CF4u, 1 }, { 0x001F1947u, 1 }, { 0x001EDAF6u, 1 }, { 0x0020651Bu, 1 }, { 0x001EEAC5u, 1 }, { 0x001F0EB0u, 1 },
    { 0x001EEA74u, 1 }, { 0x001F10BBu, 1 }, { 0x001F1129u, 1 }, { 0x001EEA81u, 1 }, { 0x00205EFBu, 1 }, { 0x0020654Cu, 1 },
    { 0x00206A7Du, 1 }, { 0x002063C9u, 1 }, { 0x001ED7B4u, 1 }, { 0x001EC91Cu, 1 }, { 0x00206702u, 1 },
    /* level 2: 15 cutscenes */
    { 0x001FFA23u, 2 }, { 0x001D05D4u, 2 }, { 0x002067E3u, 2 }, { 0x001FF9D1u, 2 }, { 0x001CF426u, 2 }, { 0x001FA0B3u, 2 },
    { 0x001D6636u, 2 }, { 0x001CF432u, 2 }, { 0x001CE79Du, 2 }, { 0x001F9ECBu, 2 }, { 0x001F9CD2u, 2 }, { 0x001F9F3Du, 2 },
    { 0x001D04E4u, 2 }, { 0x001D678Bu, 2 }, { 0x001F9C2Du, 2 },
    /* level 3: 11 cutscenes */
    { 0x001D6945u, 3 }, { 0x001D46F7u, 3 }, { 0x001D470Fu, 3 }, { 0x001D58D5u, 3 }, { 0x001D1A91u, 3 }, { 0x0020667Fu, 3 },
    { 0x001BE3B5u, 3 }, { 0x001BE42Au, 3 }, { 0x001B00EEu, 3 }, { 0x001D46E5u, 3 }, { 0x001BE36Cu, 3 },
    /* level 4: 5 cutscenes */
    { 0x00206B0Eu, 4 }, { 0x001AEF09u, 4 }, { 0x001AEF76u, 4 }, { 0x001AEE60u, 4 }, { 0x001BFD79u, 4 },
    /* level 5: 8 cutscenes */
    { 0x001F9068u, 5 }, { 0x001F58CAu, 5 }, { 0x001F8EE0u, 5 }, { 0x001F8FB7u, 5 }, { 0x001F8F57u, 5 }, { 0x00207501u, 5 },
    { 0x001F75D2u, 5 }, { 0x001FA090u, 5 },
    /* level 6: 4 cutscenes */
    { 0x001D5717u, 6 }, { 0x001B49B4u, 6 }, { 0x001C97E7u, 6 }, { 0x001D0549u, 6 },
    /* level 7: 14 cutscenes */
    { 0x001D3338u, 7 }, { 0x001F7B8Bu, 7 }, { 0x001F7B8Cu, 7 }, { 0x001F7B8Eu, 7 }, { 0x001F7B8Fu, 7 }, { 0x001EE805u, 7 },
    { 0x001F2EA6u, 7 }, { 0x001F7615u, 7 }, { 0x001F2409u, 7 }, { 0x001F09B0u, 7 }, { 0x001F7B56u, 7 }, { 0x001F7376u, 7 },
    { 0x001F2751u, 7 }, { 0x00206FF7u, 7 },
    /* level 8: 14 cutscenes */
    { 0x001ABE50u, 8 }, { 0x001BB8D8u, 8 }, { 0x001ABE53u, 8 }, { 0x001ABE02u, 8 }, { 0x001BB7C4u, 8 }, { 0x001ABE07u, 8 },
    { 0x001C5BEAu, 8 }, { 0x001C24B7u, 8 }, { 0x001C22C9u, 8 }, { 0x001F197Eu, 8 }, { 0x001F1981u, 8 }, { 0x001F1983u, 8 },
    { 0x001C22D8u, 8 }, { 0x001C15ECu, 8 },
    /* level 9: 4 cutscenes */
    { 0x001CE910u, 9 }, { 0x001D3284u, 9 }, { 0x001D3285u, 9 }, { 0x00209D4Eu, 9 },
    /* level 10: 4 cutscenes */
    { 0x002085D6u, 10 }, { 0x001D1FE5u, 10 }, { 0x001D1FE7u, 10 }, { 0x001D1FD9u, 10 },
    /* level 11: 12 cutscenes */
    { 0x001BE56Au, 11 }, { 0x001C6CC4u, 11 }, { 0x001D1150u, 11 }, { 0x001D2782u, 11 }, { 0x001E08D0u, 11 }, { 0x00207511u, 11 },
    { 0x001DDD15u, 11 }, { 0x001C1696u, 11 }, { 0x001DEE0Fu, 11 }, { 0x001D118Au, 11 }, { 0x001EC584u, 11 }, { 0x001CFEA3u, 11 },
    /* level 12: 4 cutscenes */
    { 0x00207DAAu, 12 }, { 0x001F4ACDu, 12 }, { 0x001F4AEFu, 12 }, { 0x001F4AF1u, 12 },
    /* level 13: 12 cutscenes */
    { 0x00205B32u, 13 }, { 0x001F720Cu, 13 }, { 0x001F720Eu, 13 }, { 0x002058DFu, 13 }, { 0x00208BC7u, 13 }, { 0x002054FAu, 13 },
    { 0x00203BC2u, 13 }, { 0x00206A63u, 13 }, { 0x0020A79Bu, 13 }, { 0x0020A7A3u, 13 }, { 0x00205D18u, 13 }, { 0x00205B2Du, 13 },
    /* level 14: 21 cutscenes */
    { 0x001B274Fu, 14 }, { 0x001D7390u, 14 }, { 0x00206650u, 14 }, { 0x001FFE29u, 14 }, { 0x001FFE2Cu, 14 }, { 0x0019E9C0u, 14 },
    { 0x002062CCu, 14 }, { 0x001F073Cu, 14 }, { 0x001AF284u, 14 }, { 0x0020858Bu, 14 }, { 0x001E2DDDu, 14 }, { 0x001E2D13u, 14 },
    { 0x001F0616u, 14 }, { 0x001E2D18u, 14 }, { 0x001D6906u, 14 }, { 0x002064DEu, 14 }, { 0x001E2D88u, 14 }, { 0x001BED24u, 14 },
    { 0x001ECC8Du, 14 }, { 0x001F0433u, 14 }, { 0x0020861Du, 14 },
    /* level 15: 10 cutscenes */
    { 0x00206574u, 15 }, { 0x001CFF1Eu, 15 }, { 0x001CFF25u, 15 }, { 0x001EFF4Au, 15 }, { 0x001B28EDu, 15 }, { 0x001D65FDu, 15 },
    { 0x001D6665u, 15 }, { 0x002071A1u, 15 }, { 0x001B1120u, 15 }, { 0x001D04A0u, 15 },
    /* levels 16 and 17: the same level file, so the same 5 cutscenes and one key (16); P1's class tells them apart (Obi-Wan on 16, Anakin on 17) */
    { 0x001EE341u, 16 }, { 0x001A9FA1u, 16 }, { 0x001A9F64u, 16 }, { 0x001F2CEFu, 16 }, { 0x001F2CF1u, 16 },
};
#define N_SIGS ((u32)(sizeof k_sigs / sizeof k_sigs[0]))

static u32 scan_find(const u32* ids, const u32* addrs, u32 n, u32 id)
{
    for (u32 i = 0; i < n; ++i) if (ids[i] == id) return addrs[i];
    return 0;
}

static void scan_done(void)
{
    u32 hits[KEYS];
    for (u32 i = 0; i < KEYS; ++i) hits[i] = 0;
    for (u32 i = 0; i < s_nCuts; ++i)
        for (u32 j = 0; j < N_SIGS; ++j)
            if (k_sigs[j].cutId == s_scCutId[i] && k_sigs[j].key < KEYS) hits[k_sigs[j].key]++;
    u32 best = 0, nb = 0, n2 = 0;
    for (u32 i = 0; i < KEYS; ++i) {
        if (hits[i] > nb) { n2 = nb; nb = hits[i]; best = i; }
        else if (hits[i] > n2) n2 = hits[i];
    }
    s_sigKey = (nb >= 2 && nb > n2) ? (u8)best : 0xFF;     /* two or more ids of one level, and no other level as strong; else the level is not known and nothing level-specific is done */
    if (s_sigKey != 0xFF) {
        s_key = s_sigKey; apply_level(s_sigKey);
        if (k_strategy[s_sigKey] == STRAT_SPAWN) s_strat = STRAT_SPAWN; /* P19: this level gets its own companion */
    }
    g_dbg.sub[9] = s_key;
    g_dbg.lvl[0] = s_sigKey; g_dbg.lvl[1] = s_nCuts; g_dbg.lvl[2] = s_nTrigs; g_dbg.lvl[3]++;
    for (u32 i = 0; i < SCAN_CUTS; ++i) g_dbg.lvl[4 + i] = i < s_nCuts ? s_scCutId[i] : 0;
    for (u32 i = 0; i < KEYS; ++i) g_dbg.lvl[28 + i] = hits[i];
}

/* ---- P34: the second HUD is found by the start-of-level scan
 * Where:  scan_watch (the scan of P27), read by hide_p2_hud on every drop-out / release.
 * Was:    the second HUD's handlers (the vitals fade and the portrait of slot 2) were found only by the Back sweep. A spawn level (STRAT_SPAWN: 5, 7, 8, 11,
 *         13, 14) never runs that sweep since P28 chose its strategy at the scan, so when player two dropped out the HUD icon and Force bar stayed
 *         (levels 5, 11, 13); the levels that take the mission's companion ran the sweep and were fine.
 * Now:    the start-of-level scan collects the same handlers, so the list is there before the first Back; the sweep still refills it on AUTO levels.
 * Needed: the second HUD empties when player two leaves, on every level.
 * Source: this project (user tests of v28: level 2 fine, levels 5, 11 and 13 not) */
static void scan_watch(u32 now)
{
    if (s_scState == 2) return;
    if (s_scState == 0) {
        if (now - s_seen < SETTLE_FRAMES) return;
        s_scState = 1; s_scCur = SWEEP_LO; s_nCuts = s_nTrigs = 0; s_nhud = 0; s_yoda = 0;
    }
    u32 end = s_scCur + WIN_SCAN_CHUNK;
    if (end > SWEEP_HI) end = SWEEP_HI;
    u32 a = s_scCur;
    while (a < end) {
        if ((a & 0xFFFu) == 0 && !page_ok(a)) { a += 0x1000; continue; }
        u32 v = *(u32*)a;
        u32 here = a;
        a += 4;
        if ((here & 0xFFFu) == 0xFFCu) continue;           /* the id word is in the next page */
        if (v == VT_TRIGGER_MULTI) { if (s_nTrigs < SCAN_TRIGS) { s_scTrigA[s_nTrigs] = here; s_scTrigId[s_nTrigs++] = *(u32*)(here + 4); } }
        else if (v == VT_CUTSCENE) { if (s_nCuts < SCAN_CUTS) { s_scCutA[s_nCuts] = here; s_scCutId[s_nCuts++] = *(u32*)(here + 4); } }
        else if (v == VT_YODA) { if (!s_yoda) s_yoda = (u8*)here; }
        else if ((v == HUD_VITALS_FADE || v == HUD_PORTRAIT) && s_nhud < 12 && here + 0x14 <= ((here | 0xFFFu) + 1)) s_hud[s_nhud++] = here; /* P34 */
    }
    s_scCur = end;
    if (end >= SWEEP_HI) { s_scState = 2; scan_done(); }
}

static void win_close(u8* p1)
{
    s_wLock = 0; s_wState = W_DONE; s_wSeen2 = 0;
    if (s_wResume && s_coopOver) s_wResume = 0;
    (void)p1;
}

static void win_watch(u8* p1, u32 now)
{
    if (s_wResume && !s_wLock && !s_aiOn) { /* the walk is over: player 2 goes back in once no cutscene plays */
        if (!in_cutscene() && pad2_present()) { s_wResume = 0; drop_in(p1); }
    }
    if (s_wState == W_DONE) { g_dbg.spare = (u32)s_wState | ((u32)s_wLock << 4) | ((u32)s_wResume << 5); return; }
    if (s_wState == W_SCAN) {
        if (s_scState != 2) return;                      /* the start-of-level scan is still running */
        s_wTrig = scan_find(s_scTrigId, s_scTrigA, s_nTrigs, WIN_TRIG_ID);
        s_wCut = scan_find(s_scCutId, s_scCutA, s_nCuts, WIN_CUT1_ID);
        s_wState = (s_wTrig && s_wCut) ? W_ARMED : W_DONE; /* not in this level: nothing more to do */
        return;
    }
    if (!win_obj_ok(s_wTrig, VT_TRIGGER_MULTI, WIN_TRIG_ID) || !win_obj_ok(s_wCut, VT_CUTSCENE, WIN_CUT1_ID)) {
        if (s_wState >= W_OPEN) win_close(p1);           /* the level took the objects away: do not keep player 2 out */
        s_wState = s_wLock ? W_DONE : W_SCAN; s_scState = 0;
        return;
    }
    u32 trigF = *(u32*)(s_wTrig + 0x9C), cutF = *(u32*)(s_wCut + 0x9C);
    u32 id = cut_id();
    if (s_wState == W_ARMED) {
        if (cutF & OBJ_FIRED) { s_wState = W_DONE; return; }  /* already played (a restart from a checkpoint after it) */
        if (trigF & OBJ_FIRED) { /* the companion is sent off: player 2 gives him back and cannot take him until it is over */
            s_wState = W_OPEN; s_wLock = 1; s_wResume = (s_p2 != 0);
            if (s_p2 || s_waitActive) drop_out();
            g_dbg.state = ST_WINDOW;
        }
    } else if (s_wState == W_OPEN) {
        if (id == WIN_CUT1_ID || (cutF & OBJ_FIRED)) { s_wState = W_CUT1; s_wT1 = now; s_wQuiet = 0; }
    } else { /* W_CUT1 */
        if (id == WIN_CUT2_ID) s_wSeen2 = 1;
        if (s_wSeen2 && *(volatile u32*)G_CINEMATIC == 0 && !in_cutscene()) s_wQuiet++; else s_wQuiet = 0;
        if (s_wQuiet >= EVENT_QUIET_FRAMES || now - s_wT1 > WIN_TIMEOUT) win_close(p1);
    }
    if (!alive(p1)) { /* player one died: a restart; do not keep player 2 out of it */
        if (++s_wDead >= P1_DEAD_FRAMES && s_wLock) { s_wDead = 0; s_wResume = 0; win_close(p1); s_wState = W_SCAN; s_scState = 0; }
    } else s_wDead = 0;
    g_dbg.spare = (u32)s_wState | ((u32)s_wLock << 4) | ((u32)s_wResume << 5);
}

/* ---- P43 Yoda: a bigger Force bar that refills faster
 * Where:  the start-of-level scan (notes the first object with IYoda's vtable, 0x5C79F8), yoda_watch (every frame), register_p2 (a take-over), and a hook at 0x2809E9 inside
 *         the game's per-frame Force regeneration (0x280990: if the character is not using a Force power and has less than the maximum, power += rate * frame time;
 *         the rate is a constant shared by everyone, 0x5B1374; the hook is the `mulss 0xC(%esp),%xmm0` that applies the frame time).
 * Was:    Yoda's own powers (the ones the game gives him) cost a lot, the bar ran dry and refilled at everyone's slow rate.
 * Now:    his maximum Force is 1.5 times player one's (YODA_MAX_X; the filled fraction is kept), and the hook doubles the regeneration rate of a character whose vtable is
 *         IYoda's (YODA_REGEN_X = 2: `addss %xmm0,%xmm0`). When player two takes him, the maximum the match with player one gives him (match_p1) is multiplied by 1.5 as
 *         well, and what is put back on drop-out is the 1.5 x value.
 * Needed: Yoda on level 15 (his only level); user request after console tests.
 * Source: this project (the regeneration code at 0x280990). */
static void yoda_max(u8* y, u8* p1)
{
    if (!p1 || !vptr((u32)p1 + C_MAXPOWER + 3)) return;
    float m = *(float*)(p1 + C_MAXPOWER);
    if (m > 0.0f) set_max(y, C_POWER, C_MAXPOWER, m * YODA_MAX_X);
}
static void yoda_watch(u8* p1)
{
    u8* y = s_yoda;
    if (!y || s_scState != 2) return;
    if (!vptr((u32)y) || !vptr((u32)y + C_MAXPOWER + 3) || *(u32*)y != VT_YODA) { s_yoda = 0; return; }
    u32 id = *(u32*)(y + C_ID);
    if (!id || id == s_yodaId || !is_character(y)) return;
    if (y == s_p2) {                       /* taken before the scan finished: register_p2 already set the maximum; what is given back must be the 1.5 x too */
        if (vptr((u32)p1 + C_MAXPOWER + 3) && *(float*)(p1 + C_MAXPOWER) > 0.0f) s_svMaxPower = *(float*)(p1 + C_MAXPOWER) * YODA_MAX_X;
    } else yoda_max(y, p1);
    s_yodaId = id;
}
__asm__(
    ".text\n"
    ".globl coop_yoda_regen\n"
    "coop_yoda_regen:\n"                 /* 0x2809E9: movss rate -> xmm0, ESI = the character, [esp+0xC] = frame time */
    "    cmpl $0x5C79F8, (%esi)\n"
    "    jne 1f\n"
    "    addss %xmm0, %xmm0\n"            /* Yoda: twice the rate */
    "1:\n"
    "    mulss 0xc(%esp), %xmm0\n"       /* the displaced instruction */
    "    pushl $0x2809EF\n"
    "    ret\n"
    ".text\n");

/* ---- P42 player two's light-saber colour
 * Where:  the spawned character's light saber: the character holds it at +0x1080 (the cinematic events' "saber of character n" is [character + (0x84 + n) * 32]), the
 *         saber (class ILightSaber, vtable 0x5B1FF0 and the Anakin / Obi-Wan / Dooku classes after it) keeps its colour as three floats at +0x814 (red, green, blue; blue
 *         is 0 0 1, Dooku's red 1 0 0), and virtual slot 0x604 (0x286180) stores a new colour there and tells the blade renderer (+0x868). It is what the game's own
 *         cinematic event "Set saber color" (CinEvent_SaberColor, 0x24A680) calls. No game code is patched.
 * Was:    every saber was blue, so on the spawn levels both players had the same blade.
 * Now:    on the levels of k_saber[] the spawned character's saber gets the row's colour (green for Obi-Wan on 7, 8 and 13, red for Anakin on 11 and 14) as soon as it
 *         exists, and again if the game puts another colour back.
 * Source: this project (the cinematic event's code; a RAM dump of two IObiwan characters, both with a saber at +0x1080 and colour 0 0 1). */
#define C_SABER         0x1080u
#define SABER_COLOR_OFF 0x814u
#define SABER_RENDER    0x868u
#define SABER_VSLOT     0x604u
typedef void (FASTCALL *SaberColorFn)(void* saber, void* edx, const float* rgb);
static const float k_sabCol[3][3] = { { 0, 0, 0 }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };   /* none, green, red */
static const u8 k_saber[KEYS] = { [7] = 1, [8] = 1, [13] = 1, [11] = 2, [14] = 2 };
static void saber_watch(void)
{
    if (s_key >= KEYS || !k_saber[s_key] || !s_sub || !still_char(s_sub, s_subId)) return;
    u8* sb = *(u8**)(s_sub + C_SABER);
    if (!sb || !page_ok((u32)sb) || !page_ok((u32)sb + SABER_RENDER + 3)) return;
    u32 vt = *(u32*)sb;
    if (vt < 0x5B1FF0u || vt >= 0x5B3400u) return;                    /* one of the ILightSaber classes */
    u32 rend = *(u32*)(sb + SABER_RENDER);
    if (!rend || !page_ok(rend)) return;                              /* the blade is not built yet */
    const float* want = k_sabCol[k_saber[s_key]];
    const u32* have = (const u32*)(sb + SABER_COLOR_OFF);
    const u32* w = (const u32*)want;
    if (have[0] == w[0] && have[1] == w[1] && have[2] == w[2]) return;
    ((SaberColorFn)*(u32*)(vt + SABER_VSLOT))(sb, 0, want);
}

void coop_tick(void)
{
    g_dbg.frames++;
    u32 now = g_dbg.frames;
#if STAGE == 0
    return;
#endif

    u8* p1 = story_mode() ? player_object() : 0;
    if (!p1) { /* menus, the game's own co-op and Versus modes, loading, Versus, between levels */
        forget_level(now); s_p1 = 0;
        xh_drop();
        g_dbg.state = ST_IDLE; g_dbg.player1 = g_dbg.player1Id = g_dbg.player2 = 0;
        return;
    }
    if (p1 != s_p1) { /* a level (re)started: forget everything */
        forget_level(now); s_p1 = p1;
        s_lastY = *(const float*)(p1 + C_TRANSFORM + 48 + 4);
        g_dbg.player1 = (u32)p1; g_dbg.player1Id = *(u32*)(p1 + C_ID); g_dbg.player2 = 0;
        g_dbg.state = ST_WAIT_SETTLE;
    }

#if STAGE == 1
    return;
#endif
    follow_p1(p1);
    event_watch(p1);
    saber_watch();
    scan_watch(now);
    yoda_watch(p1);
    win_watch(p1, now);
#if COOP_DIAG
    sub_status();
    cut_watch(p1, now);
#endif
    /* ---- P36 a second boss after the first has died
     * Where:  coop_tick, here (s_bossDeadId is set where the boss is lost or dies); no game code is patched.
     * Was:    once the boss player two had was dead the boss take-over stayed off for the level, so Cin (level 12: Serra is fought first, Cin second) could not be taken.
     * Now:    the id of the boss that died is remembered; when the game names a different character in its second-player slot (manager +0x2A8, count 2), the
     *         take-over is allowed again and Back picks him up as before.
     * Needed: the levels with two bosses in turn.
     * Source: this project (user test of v31 on level 12). */
    if (s_bossDone && s_scState == 2) {
        u8* m = *(u8**)G_GAME_MANAGER;
        if (vptr((u32)m) && vptr((u32)m + 0x2AC) && *(i32*)(m + 0x1E4) == 2 && *(u32*)(m + 0x2A8) && *(u32*)(m + 0x2A8) != s_bossDeadId) {
            s_bossDone = 0; s_bossObj = 0; s_bossFails = 0; s_dropped = 1;
        }
    }
    /* ---- P12 companion-first start (logic, no code patched)
     * Each level (a fresh process) starts with s_dropped = 1: the companion stays the game's computer character.
     * Holding Back on pad 2 for ~1 s takes the companion (or finds / spawns one); holding it again hands it back.
     * Needed: drop-in / drop-out couch co-op; the state resets every level. Source: this project. */
    /* Hold Back on pad 2 (about a second, never in a cutscene) to let player 2 go / take them again. */
    if ((s_p2 || s_dropped || s_waitActive) && !s_bossDone && now - s_seen >= SETTLE_FRAMES && s_scState == 2) { /* P27: the level is named first */
        int down = !s_coopOver && !s_wLock && !s_aiOn && back_down(now) && !in_cutscene(); /* P24 / P26: Back does nothing in level 5's last room and during a scripted walk */
        if (!down) { s_backHold = 0; s_backLatch = 0; }
        else if (!s_backLatch && ++s_backHold >= BACK_HOLD_FRAMES) {
            s_backLatch = 1;
            if (s_p2 && s_boss) {
                release_boss();
                s_dropped = 1;
            } else if (s_p2 || s_waitActive) {
                drop_out();
            } else if (s_bossObj) { /* the boss again, or look for him again */
                s_dropped = 0;
                if (still_char(s_bossObj, s_bossId) && alive(s_bossObj)) register_boss(s_bossObj);
                else { s_bossObj = 0; g_dbg.state = ST_WAIT_SETTLE; s_nextTry = 0; }
            } else {
                drop_in(p1);
            }
        }
        g_dbg.misc[3] = (s_backHold << 24) | ((u32)down << 16) | ((s_xh ? 1u : 0u) << 8) | (s_dropped ? 1u : 0u) | (s_xdiag << 4);
    }

    if (s_p2 && s_boss) { /* P14: only the controls are ours */
        if (!still_char(s_p2, s_p2Id)) { s_bossDeadId = s_p2Id; s_p2 = 0; s_boss = 0; s_bossObj = 0; s_bossDone = 1; g_dbg.player2 = 0; g_dbg.state = ST_LOST; return; }
        if (!pad2_present()) { release_boss(); s_dropped = 1; xh_drop(); return; }
        if (!alive(s_p2)) { s_bossDeadId = s_p2Id; release_boss(); s_bossDone = 1; s_dropped = 1; return; } /* the fight is the game's to end */
        if (!controls_intact(s_p2) && now >= s_nextReassert) {
            *(i32*)(s_p2 + C_CONTROL) = K_PLAYER;
            BIND_CONTROLLER(s_p2, 0, PLAYER2_PORT);
            s_nextReassert = now + REASSERT_FRAMES;
            g_dbg.reasserts++;
        }
        return;
    }
    if (s_p2) {
        if (!still_char(s_p2, s_p2Id)) { /* the level took the character away: look again, make one soon */
            s_p2 = 0; s_guard = 0; g_dbg.state = ST_LOST; g_dbg.player2 = 0;
            s_spawnTried = 0; s_fails = SPAWN_AFTER_FAILS - 1; s_nextTry = now;
            return;
        }
        if (!pad2_present()) { /* the controller went: the character goes back to the game (it is taken again when it returns) */
            release_p2();
            xh_drop();
            return;
        }
        if (!s_p2Spawned) { /* P16: the level switched the companion off */
            if (!is_active(s_p2)) {
                if (++s_inactive >= INACTIVE_FRAMES) { s_inactive = 0; release_p2(); s_waitActive = 1; s_subNext = 0; }
                return;
            }
            s_inactive = 0;
        }
        /* a cutscene's end can change the controls (seen in the first mission): give them back */
        if (alive(s_p2) && !controls_intact(s_p2) && now >= s_nextReassert) {
            *(i32*)(s_p2 + C_CONTROL) = K_PLAYER;
            BIND_CONTROLLER(s_p2, 0, PLAYER2_PORT);
            s_nextReassert = now + REASSERT_FRAMES;
            g_dbg.reasserts++;
        }
        if (now >= s_nextMatch) { match_p1(s_p2, p1); s_nextMatch = now + MATCH_FRAMES; }
        int cut = in_cutscene();
        if (cut) s_lastCutscene = now;
        if (s_respawnPending) { /* a hit that would have killed them */
            s_respawnPending = 0;
            respawn_p2(p1, now);
            g_dbg.respawns++;
        } else if (s_rescuePending) { /* a fall into the void / a kill zone */
            s_rescuePending = 0;
            place_safely(p1, s_p2);
            *(s_p2 + C_INVINCIBLE) = 1;
            s_shieldUntil = now + SHIELD_FRAMES;
            g_dbg.rescues++;
        } else if (!alive(s_p2)) { /* died all the same: a new one of the same kind after a moment */
            if (!s_deadSince) s_deadSince = now;
            if (now - s_deadSince >= DEATH_FRAMES) {
                char nm[24];
                for (int i = 0; i < 24; ++i) nm[i] = s_p2Name[i];
                i32 co = s_p2Costume, tx = s_p2TexSet;
                release_p2();
                s_dropped = 0;
                u8* o = spawn_char(p1, nm, co, tx);
                g_dbg.replaced++;
                if (o) { s_spawnObj = o; s_spawnFrame = now; g_dbg.state = ST_SPAWNED; }
                else { g_dbg.state = ST_NO_COMPANION; s_spawnTried = 0; s_fails = SPAWN_AFTER_FAILS - 1; s_nextTry = now; }
            }
            return;
        } else {
            s_deadSince = 0;
        }
        if (s_shieldUntil && now >= s_shieldUntil) { s_shieldUntil = 0; *(s_p2 + C_INVINCIBLE) = 0; }
        if (alive(s_p2) && !cut && now - s_lastCutscene >= AFTER_CUTSCENE_FRAMES) leash(p1, now);
        else s_farSince = 0;
        return;
    }
    if (s_waitActive) { /* P16: taken again as soon as the level switches the companion on and no cutscene plays */
        if (s_dropped || !s_last || !still_char(s_last, s_lastId) || !alive(s_last)) s_waitActive = 0; /* gone: search as usual */
        else {
            if (is_active(s_last) && !in_cutscene() && pad2_present()) { s_waitActive = 0; register_p2(s_last, p1); }
            return;
        }
    }
    if (s_wantSub) { /* P19: player two is the spawned companion: once no cutscene plays (spawned new, or activated and placed again) */
        if (s_dropped) s_wantSub = 0;
        else {
            if (!in_cutscene() && pad2_present() && now >= s_subNext) use_substitute(p1, now);
            if (s_wantSub) return;
        }
    }
    if (s_dropped) return; /* stays out until Back is held again */

    if (g_dbg.state == ST_SKIP_OWN_P2) return;
    if (s_spawnObj) { /* a spawned ally: taken as player 2 as soon as it is a living character */
        if (now - s_spawnFrame > SPAWN_WAIT_FRAMES) { s_spawnObj = 0; g_dbg.state = ST_NO_COMPANION; return; }
        if (now - s_spawnFrame >= 5 && vptr((u32)s_spawnObj) && is_character(s_spawnObj) && alive(s_spawnObj) && pad2_present() &&
            (!s_placeSub || is_active(s_spawnObj))) {
            u8* o = s_spawnObj;
            s_spawnObj = 0;
            copy_name((char*)g_dbg.candName, type_name(o));
            s_spawnedNext = 1;   /* a character of our own making: see fix_force_script */
            s_sub = o; s_subId = *(u32*)(o + C_ID);
            if (s_placeSub) { s_placeSub = 0; place_safely(p1, o); DIAG(g_dbg.sub[11]++;) }
            register_p2(o, p1);
        }
        return;
    }
    if (!s_sweep) {
        if (now - s_seen < SETTLE_FRAMES || now < s_nextTry) return;
        if (s_strat == STRAT_SPAWN) { s_wantSub = 1; s_subNext = 0; g_dbg.state = ST_NO_COMPANION; return; } /* P19: nobody to look for */
        u8* mgr = *(u8**)G_GAME_MANAGER;
        if (!vptr((u32)mgr) || !vptr((u32)mgr + 0x1E4)) return;
        s_bossMode = 0;
        if (*(i32*)(mgr + 0x1E4) != 1) { /* the game has a second player of its own: a boss in slot 2 is player two's to play */
            if (*(i32*)(mgr + 0x1E4) != 2 || s_bossDone || !vptr((u32)mgr + 0x2AC) || !*(u32*)(mgr + 0x2A8)) { g_dbg.state = ST_SKIP_OWN_P2; return; }
            s_bossMode = 1; s_bossSlotId = *(u32*)(mgr + 0x2A8);
        }
        if (!pad2_present()) { g_dbg.state = ST_WAIT_PAD; return; }
        if (in_cutscene()) { g_dbg.state = ST_WAIT_CUTSCENE; return; }
        sweep_begin(p1);
        g_dbg.state = ST_SCANNING;
    }
    if (!sweep_step(p1)) return;

    s_sweep = 0;
    g_dbg.scans++;
    g_dbg.objectsSeen = s_pages; g_dbg.jedisSeen = s_jedis; g_dbg.charsSeen = s_chars;
    if (s_bossMode) {
        u8* b = s_bossCand;
        s_bossMode = 0;
        if (b && still_char(b, s_bossSlotId) && alive(b) && fi(b, C_CONTROL) != K_PLAYER && targets_player(b)) register_boss(b);
        else {
            g_dbg.state = ST_NO_COMPANION; s_nextTry = now + RETRY_FRAMES;
            if (++s_bossFails >= 4) { s_bossFails = 0; s_dropped = 1; } /* not there (yet): press Back again later */
        }
        return;
    }
    {
        u32 lvl = s_key;                                   /* P27: named by scan_done() from the level's cutscene ids */
        g_dbg.sub[9] = lvl;
        apply_level(lvl);
        if (s_ev && !event_key_ok(s_ev)) { s_ev = 0; s_cutDone = 0; }
        else if (s_cutDone) { end_coop(); return; } /* P24: Back was held after the event: not now */
        if (lvl < KEYS && k_strategy[lvl] == STRAT_SPAWN) { /* P19: this level gets its own companion; the mission's is left alone */
            s_strat = STRAT_SPAWN; s_wantSub = 1; s_subNext = 0;
            g_dbg.state = ST_NO_COMPANION;
            return;
        }
    }
    if (s_best && still_char(s_best, *(u32*)(s_best + C_ID)) && alive(s_best)) {
        copy_name((char*)g_dbg.candName, type_name(s_best));
        register_p2(s_best, p1);
    } else if (s_hiddenHero && s_hiddenObj && still_char(s_hiddenObj, *(u32*)(s_hiddenObj + C_ID))) {
        /* P16 / P18: the level's companion is there but switched off: take him when he is back, or a substitute meanwhile */
        s_scr = s_hiddenObj; s_scrId = *(u32*)(s_hiddenObj + C_ID);
        s_last = s_scr; s_lastId = s_scrId;
        s_waitActive = 1; s_subNext = 0;
        g_dbg.state = ST_NO_COMPANION;
    } else {
        g_dbg.state = ST_NO_COMPANION; s_nextTry = now + RETRY_FRAMES;
        /* nobody to take after a few searches: a copy of player 1's class beside them (once per level) */
        if (++s_fails >= SPAWN_AFTER_FAILS && !s_spawnTried && !in_cutscene() && pad2_present()) {
            s_spawnTried = 1;
            u8* o = spawn_body(p1);
            g_dbg.misc[2] = (u32)o;
            if (o) { s_spawnObj = o; s_spawnFrame = now; g_dbg.state = ST_SPAWNED; }
        }
    }
}

/* ---- P2 / P3 health change and instant kill
 * Where:  0x151500 (9 bytes, ICharacter health change) and 0x152D30 (9 bytes, ICharacter instant kill); jump stubs tramp_health / tramp_kill
 * Was:    A fatal hit or a void / kill-zone fall killed player two like any character.
 * Now:    A fatal hit on player two becomes health 1 plus a respawn next frame; a void or kill-zone fall becomes a rescue next to player one.
 * Needed: A companion that can't be lost for good, as in the PC port.
 * Source: PC port (coop.cpp ChangeHealthHook, InstantKillHook); addresses checked in this XBE
 */
/* Game hooks (coop.cpp ChangeHealthHook / InstantKillHook): integer code only, they run inside the game's own
 * calls. After a health change, a fatal one on player 2 is turned into health 1 plus a respawn at the next
 * frame; a fall into the void or a kill zone (the game's instant kill) on player 2 becomes a rescue. */
typedef void (FASTCALL *HealthFn)(u8* c, void* edx, u32 change, u32 a, u32 b);
typedef void (FASTCALL *KillFn)(u8* c, void* edx, u32 a);
extern char tramp_health[], tramp_kill[];

void FASTCALL coop_hook_health(u8* c, void* edx, u32 change, u32 a, u32 b)
{
    (void)edx;
    ((HealthFn)tramp_health)(c, 0, change, a, b);
    g_dbg.hooks |= 1;
    if (!s_guard || c != s_p2) return;
    u32 hb = *(volatile u32*)(c + C_HEALTH); /* health <= 0.0f without the FPU */
    if (((hb & 0x7FFFFFFFu) == 0 || (hb >> 31)) && !f8(c, C_DEAD)) {
        *(volatile u32*)(c + C_HEALTH) = 0x3F800000u; /* 1.0f */
        s_respawnPending = 1;
    }
}

void FASTCALL coop_hook_kill(u8* c, void* edx, u32 a)
{
    (void)edx;
    g_dbg.hooks |= 2;
    if (s_guard && c == s_p2) { s_rescuePending = 1; return; }
    ((KillFn)tramp_kill)(c, 0, a);
}

/* The Force-button queries (diagnostic): 0x178070 (id, 5, 0, 2) is "is this button pressed" for the script's
 * character, 0x1780F0 (id, 0, 0, 10, 1, 0) counts its taps. They are only watched: what the queries return
 * for player 2's character against player 1's tells where the powers are lost. */
typedef u8  (*PressedFn)(u32, u32, u32, u32);
extern char tramp_pressed[];
#define SCRIPT_CTX 0x006943CCu


static u8* script_char(void)
{
    u32 ctx = *(volatile u32*)SCRIPT_CTX;
    return (ctx && vptr(ctx + 0x68)) ? *(u8**)(ctx + 0x68) : 0;
}

/* ---- P5 Force-script flags (hook on the button query, writes script data)
 * Where:  0x178070 (6 bytes) -> coop_hook_pressed; the fix runs when the query is made for player two from the Obi-Wan script (0x459A70..0x459B30) or the Anakin script (0x324BD0..0x324D10); Dooku (P15), Anakin / Obi-Wan as bosses (P35) and Serra / Cin (P38) are further cases below
 * Was:    A companion's Force script sets instance byte 0x1D6 and turns every power off but sprint, so a taken-over companion had no Force powers.
 * Now:    The first time the script asks for a button for player two, the script instance (global 0x6964C0) is put into the state a player-controlled hero gets; the changed bytes are saved and put back when player two drops out (release_p2). Obi-Wan: init branch at 0x45D086. Anakin: init 0x326E90..0x32825A (saber throw is 0x2DF, 0x2DD set, 0x2DE clear).
 * Needed: Push, mind trick, saber throw, heal, lightning and the interactables for player two in levels 1-3.
 * Source: this project (structure of the scripts confirmed against the PC port's custom-character notes on the Jedi rules)
 */
/* Obi-Wan's Force script (the one at 0x459A70, running for the character the script context names) decides once,
 * when the level loads, that a character wearing one of the Obiwan_NPC costumes is a companion: it sets
 * instance byte 0x1D6 and switches every Force power off but sprint. In the story levels the player-two Obi-Wan
 * is that costume. So, the first time that script asks for a button on behalf of player two, the instance is
 * put into the state a player-controlled Obi-Wan gets (the common branch of the same init code, 0x45D086):
 * 0x1D6 = 0 and the power flags 0x2D9 0x2DB 0x2DC 0x2DE 0x2DF 0x2E1 on; 0x2E2..0x2E4 (the upgrade-gated ones) on
 * as well, so player two has the full set. The instance is the global the script itself reads, 0x6964C0. */
#define OBI_CALL_LO 0x00459A70u
#define OBI_CALL_HI 0x00459B30u
#define ANA_CALL_LO 0x00324BD0u
#define ANA_CALL_HI 0x00324D10u
/* ---- P15 Dooku's Force flags (boss duel)
 * Where:  fix_force_script (the same button-query hook as P5); data writes into Dooku's Force script instance
 * Was:    Dooku played by player two had only the flags his duel phase has set (measured: 0x2D9 0x2DC 0x2E2 0x2E5
 *         0x2E6 on, 0x2DB 0x2DD 0x2DF 0x2E3 0x2E4 off): no saber throw, and his R attack only pushed (no hold and throw).
 * Now:    while player two plays him, the flags of the last phase of the game's own phase handler (0x378710: 0x2D9
 *         0x2DB 0x2DC 0x2DD 0x2DF 0x2E2 0x2E3 0x2E4 0x2E6 on, 0x2E7 off) are kept on; his script's phase changes
 *         (health thresholds, which the game handles) go on as before and are overwritten on the next query. What the
 *         instance held is saved and handed back with release_boss.
 * Needed: the boss with all his powers in player-two hands.
 * Source: the game's own code (script at 0x370000..0x37AC00); the idea of the flag fix is P5's. */
#define DOOKU_CALL_LO 0x00370000u
#define DOOKU_CALL_HI 0x0037AC00u
/* ---- P35 Anakin and Obi-Wan as the boss (levels 16 and 17)
 * Where:  fix_force_script (the button-query hook of P5); data writes into the hero's Force script instance, undone by release_boss
 * Was:    the boss Anakin (level 16) had no saber throw at the start of the fight and the boss Obi-Wan (level 17) was thought to lack the mind trick: their scripts
 *         switch powers on phase by phase for the computer, and the fix only knew Dooku (P15) among the bosses, and companions (P5, which needs the companion marker 0x1D6).
 * Now:    while player two plays one of them, the flags of a player-controlled hero's script (the sets of P5) are kept on every query and the marker is left alone;
 *         the bytes the instance held are saved and handed back with release_boss, as for Dooku.
 * Needed: the duel bosses with all their powers in player-two hands.
 * Source: this project (user test of v30 on levels 16 and 17); the flag sets are P5's. */
/* ---- P38 Serra and Cin as the boss (level 12)
 * Where:  fix_force_script (the button-query hook of P5); data writes into the script instance (global 0x6964C0), whichever code asks (the recorder P37 saw Serra's scripts at 0x3C2FD1.., 0x47926F.., 0x47C8FB.. and 0x47E68B)
 * Was:    Serra had no saber throw or mind trick in her first phase; in her second phase the game's script turns on the flag bytes 0x2D9 0x2DE 0x2DF 0x2E3 0x2E4 (measured with the
 *         recorder: before 01 00 00 00 01 00 00 00 00 00 01 00 00 01 01 01, after 01 01 00 00 01 00 01 01 00 00 01 01 01 01 01 01 for 0x2D8..0x2E7)
 * Now:    while player two plays her those five bytes are kept on. Cin's script is another object (instance 0x1604A20 against her 0x1653330) with the same layout: the same five bytes give him both powers (tested).
 *         On release the bytes the script held when player two took the boss are put back (v33 left them on, and Cin used the mind trick on player one in his first phase).
 * Needed: the boss with the powers she has later, in player-two hands.
 * Source: this project (recorder output of v32 on level 12; v35 tested on both). */
static void fix_force_script(u32 ret)
{
    if (s_boss && s_bossKind) { /* P38: Serra and Cin, whichever of their scripts asks (they share one instance) */
        volatile u8* si = *(volatile u8**)0x006964C0u;
        if (!vptr((u32)si) || !vptr((u32)si + 0x1D6) || !vptr((u32)si + 0x2E8)) return;
        if (s_fixInst != si) {
            for (int i = 0; i < 16; ++i) s_fixSave[i] = si[0x2D8 + i];
            s_fixSave[16] = si[0x1D6];
            s_fixInst = si;
        }
        si[0x2D9] = 1; si[0x2DE] = 1; si[0x2DF] = 1; si[0x2E3] = 1; si[0x2E4] = 1;
        g_dbg.fx[0]++; g_dbg.fx[1] = (u32)si; g_dbg.fx[7] = ret;
        return;
    }
    int ana = (ret >= ANA_CALL_LO && ret <= ANA_CALL_HI);
    int dooku = (s_boss && ret >= DOOKU_CALL_LO && ret <= DOOKU_CALL_HI);
    if (!dooku && !ana && (ret < OBI_CALL_LO || ret > OBI_CALL_HI)) return;
    volatile u8* inst = *(volatile u8**)0x006964C0u;
    if (!vptr((u32)inst) || !vptr((u32)inst + 0x1D6) || !vptr((u32)inst + 0x2E8)) return;
    g_dbg.fx[1] = (u32)inst; g_dbg.fx[7] = ret;
    if (dooku) { /* P15: the marker byte is left alone, the flags are what the last phase of his script sets */
        if (s_fixInst != inst) {
            for (int i = 0; i < 16; ++i) s_fixSave[i] = inst[0x2D8 + i];
            s_fixSave[16] = inst[0x1D6];
            s_fixInst = inst;
            DIAG(g_dbg.st[43] = inst[0x1D6]; g_dbg.st[44] = 3;)
        }
        inst[0x2D9] = 1; inst[0x2DB] = 1; inst[0x2DC] = 1; inst[0x2DD] = 1; inst[0x2DF] = 1;
        inst[0x2E2] = 1; inst[0x2E3] = 1; inst[0x2E4] = 1; inst[0x2E6] = 1; inst[0x2E7] = 0;
        g_dbg.fx[0]++;
        return;
    }
    if (s_boss) { /* P35: Anakin or Obi-Wan as the duel's boss: every power a player-controlled hero has, the marker byte left alone */
        if (s_fixInst != inst) {
            for (int i = 0; i < 16; ++i) s_fixSave[i] = inst[0x2D8 + i];
            s_fixSave[16] = inst[0x1D6];
            s_fixInst = inst;
            DIAG(g_dbg.st[43] = inst[0x1D6]; g_dbg.st[44] = 4;)
        }
        if (ana) { inst[0x2DA] = 1; inst[0x2D9] = 1; inst[0x2DB] = 1; inst[0x2DC] = 1; inst[0x2DD] = 1; inst[0x2DE] = 0; inst[0x2DF] = 1;
                   inst[0x2E1] = 1; inst[0x2E2] = 1; inst[0x2E6] = 1; inst[0x2E3] = 1; inst[0x2E4] = 1; inst[0x2E7] = 1; }
        else { inst[0x2D9] = 1; inst[0x2DB] = 1; inst[0x2DC] = 1; inst[0x2DE] = 1; inst[0x2DF] = 1; inst[0x2E1] = 1;
               inst[0x2E2] = 1; inst[0x2E3] = 1; inst[0x2E4] = 1; }
        g_dbg.fx[0]++;
        return;
    }
    if (!inst[0x1D6]) {
        /* A character this code spawned was built the way a player's is (marker 0, Force flags on) and has no companion
         * state of its own to save: what it goes back to is what the game gives a companion (marker 1, the power flags
         * of both scripts off: measured on the level 1 companion, whose 0x2D8..0x2E7 are all 0). */
        DIAG(if (s_fixInst != inst) g_dbg.st[43] = 0;)
        if (s_p2Spawned && s_fixInst != inst) {
            for (int i = 0; i < 16; ++i) s_fixSave[i] = 0;   /* 0x2D8..0x2E7 all off: what a level 1 companion holds (measured) */
            s_fixSave[16] = 1;
            s_fixInst = inst;
            DIAG(g_dbg.st[44] = 2;)
        }
        return;
    }
    DIAG(if (s_fixInst != inst) { g_dbg.st[43] = inst[0x1D6]; g_dbg.st[44] = 1; })
    g_dbg.fx[2] = *(volatile u32*)(inst + 0x1D6);
    g_dbg.fx[3] = *(volatile u32*)(inst + 0x2D8); g_dbg.fx[4] = *(volatile u32*)(inst + 0x2DC);
    g_dbg.fx[5] = *(volatile u32*)(inst + 0x2E0); g_dbg.fx[6] = *(volatile u32*)(inst + 0x2E4);
    if (s_fixInst != inst) {
        for (int i = 0; i < 16; ++i) s_fixSave[i] = inst[0x2D8 + i];
        s_fixSave[16] = inst[0x1D6];
        s_fixInst = inst;
    }
    inst[0x1D6] = 0;
    if (ana) { /* the player-controlled state of Anakin's script (init 0x326E90..0x32825A) */
        inst[0x2DA] = 1; inst[0x2D9] = 1; inst[0x2DB] = 1; inst[0x2DC] = 1; inst[0x2DD] = 1; inst[0x2DE] = 0; inst[0x2DF] = 1;
        inst[0x2E1] = 1; inst[0x2E2] = 1; inst[0x2E6] = 1; inst[0x2E3] = 1; inst[0x2E4] = 1; inst[0x2E7] = 1;
    } else {
        inst[0x2D9] = 1; inst[0x2DB] = 1; inst[0x2DC] = 1; inst[0x2DE] = 1; inst[0x2DF] = 1; inst[0x2E1] = 1;
        inst[0x2E2] = 1; inst[0x2E3] = 1; inst[0x2E4] = 1;
    }
    g_dbg.fx[0]++;
}

/* ---- P37 script recorder (finds the Force scripts of Serra, Cin and Yoda)
 * Where:  coop_hook_pressed, for the character player two plays; the entries are g_dbg.sp[] (read with xbwatch "scriptid")
 * Was:    Serra and Cin as bosses (level 12) and Yoda as the companion (level 15) were missing saber throw / mind trick when player two played them: the flag fix (P5, P15, P35)
 *         only knows three scripts, and where the other ones are and which instance bytes are their power flags was not known.
 * Now:    each time the button query is made for player two, the code address that asked is noted (an entry is a stretch of code, merged within 0x1000 bytes, per character name),
 *         with the button ids seen and the script instance bytes 0x1D0..0x2EF at the first query and again every 64th. Nothing is changed in the game.
 * Needed: the addresses and the flag bytes to give these three the same fix as Obi-Wan, Anakin and Dooku.
 * Source: this project. */
static char s_spName[8]; static u32 s_spId; static u32 s_spChr;
static void script_probe(u32 ret, u32 id)
{
    volatile u8* inst = *(volatile u8**)0x006964C0u;
    if (!vptr((u32)inst + 0x1D0) || !vptr((u32)inst + 0x2F0)) return;
    if (s_spId != s_p2Id) { copy_name(s_spName, type_name(s_p2)); s_spId = s_p2Id; s_spChr = 0xFF; }
    if (s_spChr == 0xFF) { /* this character's entry: the one with his name, or a free one */
        for (u32 i = 0; i < 3 && s_spChr == 0xFF; ++i) {
            struct SpChar* e = &g_dbg.spc[i];
            int same = e->hits != 0;
            for (int k = 0; k < 8; ++k) if (e->name[k] != s_spName[k]) same = 0;
            if (same) s_spChr = i;
        }
        for (u32 i = 0; i < 3 && s_spChr == 0xFF; ++i) if (!g_dbg.spc[i].hits) {
            struct SpChar* e = &g_dbg.spc[i];
            for (int k = 0; k < 8; ++k) e->name[k] = s_spName[k];
            for (int k = 0; k < 0x120; ++k) e->first[k] = inst[0x1D0 + k];
            s_spChr = i;
        }
        if (s_spChr == 0xFF) return;
    }
    struct SpChar* ch = &g_dbg.spc[s_spChr];
    ch->inst = (u32)inst;
    if ((ch->hits++ & 63u) == 0)
        for (int k = 0; k < 0x120; ++k) ch->last[k] = inst[0x1D0 + k];
    struct SpReg* r = 0;
    for (int i = 0; i < 16 && !r; ++i) {
        struct SpReg* e = &g_dbg.spr[i];
        if (e->chr == s_spChr + 1 && ret + 0x1000u >= e->lo && ret <= e->hi + 0x1000u) r = e;
    }
    if (!r) {
        for (int i = 0; i < 16 && !r; ++i) if (!g_dbg.spr[i].chr) r = &g_dbg.spr[i];
        if (!r) return;
        r->lo = r->hi = ret; r->chr = s_spChr + 1;
    }
    if (ret < r->lo) r->lo = ret;
    if (ret > r->hi) r->hi = ret;
    r->hits++;
    if (id < 64) r->ids[id >> 5] |= 1u << (id & 31);
}

u8 coop_hook_pressed(u32 id, u32 a, u32 b, u32 d)
{
    u32 ret = (u32)__builtin_return_address(0);
    if (!s_p1) return ((PressedFn)tramp_pressed)(id, a, b, d);
    u8* who = script_char();
    u8 r = ((PressedFn)tramp_pressed)(id, a, b, d);
    DIAG(g_dbg.hooks |= 4;)
    if (who && s_p2 && who == s_p2) { script_probe(ret, id); fix_force_script(ret); } /* the recorder first: its first copy is then what the script itself holds */
    return r;
}



/* ---- P4 "Player Only" triggers
 * Where:  calls at 0x1A20C8 and 0x1A2258 (replacing their call to GetPlayer 0xA30F0) -> coop_gp_trigger
 * Was:    Only player one could set off triggers marked "Player Only".
 * Now:    Player two counts as the player while taken over.
 * Needed: Doors, levers and scripted triggers touched by player two.
 * Source: PC port (the trigger notes in coop.cpp); sites found in this XBE
 */
/* The two "Player Only" trigger checks (0x1A20C8, 0x1A2258) compare the touching character with the game's
 * "the player" (0xA30F0, player 1): player 2 counts as well. Called in place of 0xA30F0 with the character in EDI. */
u8* coop_gp_helper(u8* cand)
{
    g_dbg.hooks |= 16;
    if (s_guard && s_p2 && cand == s_p2) return cand;
    return ((u8* (*)(void))0x000A30F0)();
}

/* ---- P21 environmental hazards (IPain zones)
 * Where:  calls to GetPlayer 0xA30F0 at 0xFF0AA, 0xFF0C1, 0xFF106 (IPain's touch handler 0xFF070) and 0xFF2AC (IPain "can this object be hurt" 0xFF280, slot 0x278 of
 *         IPain and of IElectricalDischargeZone, which derives from it) -> coop_pain (an asm stub among the others below, which calls coop_pk with the candidate in ESI)
 * Was:    A hazard zone treats "the player" (player one) as one thing and every other character as another: zones set to hurt the player only, or whose
 *         "affects others" flag is off, never touched player two (found on level 5: the electrical discharge hurt Anakin, not the spawned Obi-Wan).
 * Now:    While player two is taken over he counts as the player in those four checks, so a hazard hurts him exactly as it hurts player one.
 * Needed: Hazards that hurt both players.
 * Source: this project (disassembly of 0xFF070 / 0xFF280; the hazard dump on level 5 showed the teams and flags were not the difference: IObiwan's team-mask virtual 0x6AA60 always says yes) */
/* ---- P6 pickups
 * Where:  calls to GetPlayer at 0x11881B, 0x11DE82, 0x2B8B11, 0x2B8B9E, 0x2B8C6D, 0x2B9524, 0x2B9532, 0x2B937F -> coop_pk_a .. coop_pk_g
 * Was:    Bacta tanks and power-ups asked "is this character the player?" by comparing with player one.
 * Now:    Each call is replaced by a stub that answers player two when the character being compared is player two.
 * Needed: Health and power-up pickups for player two.
 * Source: this project (the PC port lists health pickups as a known issue)
 */
/* Pickups. The bacta tanks and the power-ups ask "is this character the player?" by comparing it with the
 * game's "the player" (0xA30F0, player 1), so only player 1 could use them. Each such call is replaced by one to
 * a small stub that hands coop_pk the character that was being compared (found where that code keeps it); when
 * that is player two, the answer is player two. Otherwise it is the game's answer. `site` is only for the counters. */
u32 coop_pk(u32 site, u32 cand)
{
    (void)site; /* which call site it is: not needed any more, kept for the stubs */
    if (s_guard && s_p2 && cand == (u32)s_p2) return cand;
    return ((u32 (*)(void))0x000A30F0)();
}

/* ---- P7 experience
 * Where:  call at 0x269325 (kill routine 0x2692C0 -> 0x269230) -> coop_xp_stub
 * Was:    A kill by player two earned no experience (only the game's "the player" got it).
 * Now:    A kill by player two is credited to player one, so the single save gets everything.
 * Needed: All experience goes to the one save file.
 * Source: this project
 */
/* Experience. A kill's score is added by 0x269230 (called from the kill routine 0x2692C0 at 0x269325, with the
 * killer in ECX) to the character's counter at +0xE58, and only when the killer is the game's "the player"
 * (player 1): a kill by player two earned nothing. It is handed to player 1 instead, so the two share what the
 * level pays out under player 1's character. */
u8* coop_xp_who(u8* c)
{
    if (s_guard && s_p2 && c == s_p2) {
        u8* p = ((u8* (*)(void))0x000A30F0)();
        if (p) return p;
        return c;
    }
    return c;
}

/* ---- P8 "Damaged Only By Player?"
 * Where:  calls at 0xBDA1B (IBreakable) and 0x29481A (IPowerConduit) to GetPlayer 0xA30F0 -> coop_dmg_brk / coop_dmg_cnd
 * Was:    Breakables and power conduits marked "Damaged Only By Player" ignored everything not owned by player one.
 * Now:    When the attacker's owner is player two the answer is player two.
 * Needed: Level 2: the generators and the conduit that need the saber throw.
 * Source: this project (found with the property strings and the registration at 0xBDE90)
 */
/* "Damaged Only By Player?" (IBreakable bit 1 of +0x444 at 0xBDA12, IPowerConduit +0x377 at 0x29481A): the attacker's
 * owner (the attacker's vfunc 0x3D8, or the raw attacker when there is none) is compared with the game's "the
 * player". Called in place of that 0xA30F0: when the owner is player two, the answer is player two. */
typedef u8* (FASTCALL *OwnerFn)(u8* o, void* edx);
u32 coop_dmg_player(u8* att, u8* st)
{
    if (s_guard && s_p2) {
        u8* owner = 0;
        if (att) {
            if (vptr((u32)att) && vptr(*(u32*)att + 0x3D8)) {
                u32 fn = *(u32*)(*(u32*)att + 0x3D8);
                if (vptr(fn)) owner = ((OwnerFn)fn)(att, 0);
            }
        } else if (vptr((u32)st)) owner = *(u8**)st;
        if (owner == s_p2) return (u32)s_p2;
    }
    return ((u32 (*)(void))0x000A30F0)();
}

/* The displaced first bytes of the functions, then on into them. */
__asm__(
    ".text\n"
    ".globl tramp_health\n"
    "tramp_health:\n"
    "    pushl %esi\n"            /* 56 */
    "    movl %ecx, %esi\n"       /* 8B F1 */
    "    movb 0x5d6(%esi), %al\n" /* 8A 86 D6 05 00 00 */
    "    pushl $0x151509\n"
    "    ret\n"
    ".globl coop_dmg_brk\n"
    "coop_dmg_brk:\n"                /* 0xBDA1B: esi = attacker, ebp -> its holder */
    "    pushl %ecx\n    pushl %edx\n"
    "    pushl %ebp\n    pushl %esi\n"
    "    call coop_dmg_player\n    addl $8, %esp\n"
    "    popl %edx\n    popl %ecx\n    ret\n"
    ".globl coop_dmg_cnd\n"
    "coop_dmg_cnd:\n"                /* 0x29481A: edi = attacker, [ebp+8] -> its holder */
    "    pushl %ecx\n    pushl %edx\n"
    "    pushl 8(%ebp)\n    pushl %edi\n"
    "    call coop_dmg_player\n    addl $8, %esp\n"
    "    popl %edx\n    popl %ecx\n    ret\n"
    ".globl coop_pain\n"
    "coop_pain:\n"                  /* 0xFF0AA, 0xFF0C1, 0xFF106, 0xFF2AC: esi = the object being judged */
    "    pushl %ecx\n    pushl %edx\n"
    "    pushl %esi\n"
    "    pushl $8\n    call coop_pk\n    addl $8, %esp\n"
    "    popl %edx\n    popl %ecx\n    ret\n"
    ".globl coop_pk_a\n"
    "coop_pk_a:\n"
    "    pushl %ecx\n    pushl %edx\n"
    "    pushl %ebx\n"
    "    pushl $0\n    call coop_pk\n    addl $8, %esp\n"
    "    popl %edx\n    popl %ecx\n    ret\n"
    ".globl coop_pk_b\n"
    "coop_pk_b:\n"
    "    pushl %ecx\n    pushl %edx\n"
    "    pushl 0x38(%esp)\n"
    "    pushl $1\n    call coop_pk\n    addl $8, %esp\n"
    "    popl %edx\n    popl %ecx\n    ret\n"
    ".globl coop_pk_c\n"
    "coop_pk_c:\n"
    "    pushl %ecx\n    pushl %edx\n"
    "    pushl 0x14(%esp)\n"
    "    pushl $2\n    call coop_pk\n    addl $8, %esp\n"
    "    popl %edx\n    popl %ecx\n    ret\n"
    ".globl coop_pk_d\n"
    "coop_pk_d:\n"
    "    pushl %ecx\n    pushl %edx\n"
    "    pushl 0x10(%esp)\n"
    "    pushl $3\n    call coop_pk\n    addl $8, %esp\n"
    "    popl %edx\n    popl %ecx\n    ret\n"
    ".globl coop_pk_e\n"
    "coop_pk_e:\n"
    "    pushl %ecx\n    pushl %edx\n"
    "    pushl 0x10(%esp)\n"
    "    pushl $4\n    call coop_pk\n    addl $8, %esp\n"
    "    popl %edx\n    popl %ecx\n    ret\n"
    ".globl coop_pk_f1\n"
    "coop_pk_f1:\n"
    "    pushl %ecx\n    pushl %edx\n"
    "    movl 0x18(%esp), %eax\n    pushl 4(%eax)\n"
    "    pushl $5\n    call coop_pk\n    addl $8, %esp\n"
    "    popl %edx\n    popl %ecx\n    ret\n"
    ".globl coop_pk_f2\n"
    "coop_pk_f2:\n"
    "    pushl %ecx\n    pushl %edx\n"
    "    pushl 4(%ecx)\n"
    "    pushl $6\n    call coop_pk\n    addl $8, %esp\n"
    "    popl %edx\n    popl %ecx\n    ret\n"
    ".globl coop_pk_g\n"
    "coop_pk_g:\n"
    "    pushl %ecx\n    pushl %edx\n"
    "    pushl 0x25c(%esi)\n"
    "    pushl $7\n    call coop_pk\n    addl $8, %esp\n"
    "    popl %edx\n    popl %ecx\n    ret\n"
    ".globl coop_xp_stub\n"
    "coop_xp_stub:\n"
    "    pushl %ecx\n"
    "    call coop_xp_who\n"
    "    addl $4, %esp\n"
    "    movl %eax, %ecx\n"
    "    movl $0x269230, %eax\n"
    "    jmp *%eax\n"
    ".globl coop_gp_trigger\n"
    "coop_gp_trigger:\n"
    "    pushl %edi\n"
    "    call coop_gp_helper\n"
    "    addl $4, %esp\n"
    "    ret\n"
    ".globl tramp_pressed\n"
    "tramp_pressed:\n"
    "    movl 0x6943cc, %eax\n"      /* a1 cc 43 69 00 */
    "    pushl %esi\n"               /* 56 */
    "    movl 0x68(%eax), %esi\n"    /* 8b 70 68 */
    "    pushl $0x178079\n"
    "    ret\n"
    ".globl tramp_kill\n"
    "tramp_kill:\n"
    "    pushl %ebp\n"            /* 55 */
    "    movl %esp, %ebp\n"       /* 8B EC */
    "    andl $-16, %esp\n"       /* 83 E4 F0 */
    "    subl $0x58, %esp\n"      /* 83 EC 58 */
    "    pushl $0x152D39\n"
    "    ret\n");


/* ---- P13 shared camera
 * Where:  0x129990 (master camera SetTransform, thiscall (const Matrix*), ret 4; 9 bytes) -> coop_hook_cam
 *         0x211C30 (renderer field of view, thiscall (float), ret 4; 7 bytes) -> coop_hook_fov (read only)
 * Was:    the camera follows player one alone.
 * Now:    while player two is under control and alive (not in a cutscene, not in a boss duel), the placement the game
 *         gives its camera is replaced by a copy moved towards the two players' middle and pulled back along its view
 *         until both are inside the picture (at most 300 aside and 450 back, eased). The game's own matrix is never
 *         written to (a copy is passed on), the game's field of view is only read. Cutscenes and the game's own
 *         focus shots are untouched: without a player two the shift eases back to zero and the matrix passes as it is.
 *         The x87 state is saved around this code (the hook runs inside game code).
 * Needed: the second player stays in view without the companion being dragged back all the time.
 * Source: PC port (coop.cpp SharedCamera, freecam.cpp SetTransformHook / RenderFovHook), constants as the port has them.
 *         The picture is assumed 4:3 (a 16:9 screen then only gets pulled back a little more than needed).
 * Learned the hard way (do not repeat):
 *   - Moving only the renderer's view (0x2116A0) shows a small cut-out of the level: what is drawn is chosen from the
 *     game's own camera, so the master placement must move, not the final view.
 *   - The matrix must be copied bit for bit: a copy through floats alters the bits of its unused fourth column and
 *     the screen went black.
 *   - SetTransform is also called with other cameras (one at x = 0 was seen): only a placement within 2000 units of
 *     player one is treated as the game's camera.
 *   - The float code is compiled without optimisation (optimize("O0")): at -Os its x87 register juggling gave
 *     values that did not match the source on the console (a shift that was zero on two axes).
 *   - The game places the camera where it already is now and then (paused): such a placement is ours already and
 *     is not shifted again (as the port does). */
#define CAM_MAX_SHIFT 300.0f
#define CAM_MAX_BACK  450.0f
#define CAM_MARGIN    0.8f
#define CAM_EASE      3.0f      /* per second */
#define CAM_MIDDLE    40.0f     /* above the character's feet */
#define CAM_ASPECT    (4.0f / 3.0f)
#define CAM_AFTER_CUTSCENE_FRAMES 60   /* the shared camera stays off this long after a cutscene (about 2 s at 30 frames a second) */
volatile u32 coop_cam_fov, coop_cam_fovlock, coop_cam_fovcalls;   /* the game's field of view this frame (float bits), written by coop_hook_fov */
static float s_shown[16];
static float s_camShift[3];
static u32 s_camAdjusted, s_camLast;

static inline float xsqrt(float x) { float r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
static inline float xtan(float x) { float r; __asm__("fptan\n\tfstp %%st(0)" : "=t"(r) : "0"(x)); return r; }
static inline float xabs(float x) { return x < 0.0f ? -x : x; }
static inline int finite_bits(float x) { union { float f; u32 u; } v; v.f = x; return (v.u & 0x7F800000u) != 0x7F800000u; }

__attribute__((optimize("O0")))   /* the camera's float code is kept simple: at -Os its x87 register juggling gave wrong values on the console */
static int cam_target(const float* m, float t[3])
{
    t[0] = t[1] = t[2] = 0.0f;
    DIAG(g_dbg.st[3] = 1;)
    if (!s_guard || !s_p1 || !s_p2) return 0;
    DIAG(g_dbg.st[3] = 2;)
    if (!still_char(s_p2, s_p2Id) || !alive(s_p2) || !is_character(s_p1) || !vptr((u32)s_p1 + C_TRANSFORM + 56)) return 0;
    DIAG(g_dbg.st[3] = 3;)
    if (!vptr((u32)s_p2 + C_TRANSFORM + 56) || in_cutscene()) return 0;
    /* a moment after a cutscene the game moves the companion (the tether brings him back): the camera waits for him */
    if (s_lastCutscene && g_dbg.frames - s_lastCutscene < CAM_AFTER_CUTSCENE_FRAMES) { DIAG(g_dbg.st[3] = 10;) return 0; }
    float fov = *(volatile float*)&coop_cam_fov;
    DIAG(g_dbg.st[3] = 4;)
    if (!finite_bits(fov) || fov <= 0.0f) return 0;
    if (fov > 3.2f) fov *= 3.14159265f / 180.0f;   /* degrees */
    if (fov > 3.0f) return 0;
    DIAG(g_dbg.st[3] = 5;)
    const float* r = m; const float* u = m + 4; const float* f = m + 8; const float* c = m + 12;
    const float* a0 = (const float*)(s_p1 + C_TRANSFORM + 48);
    const float* b0 = (const float*)(s_p2 + C_TRANSFORM + 48);
    float a[3] = { a0[0], a0[1] + CAM_MIDDLE, a0[2] }, b[3] = { b0[0], b0[1] + CAM_MIDDLE, b0[2] };
    for (int i = 0; i < 3; ++i) if (!finite_bits(a[i]) || !finite_bits(b[i])) return 0;
    float half[3] = { (b[0] - a[0]) * 0.5f, (b[1] - a[1]) * 0.5f, (b[2] - a[2]) * 0.5f };
    float sr = half[0] * r[0] + half[1] * r[1] + half[2] * r[2];
    float su = half[0] * u[0] + half[1] * u[1] + half[2] * u[2];
    float shift[3] = { r[0] * sr + u[0] * su, r[1] * sr + u[1] * su, r[2] * sr + u[2] * su };
    float len = xsqrt(shift[0] * shift[0] + shift[1] * shift[1] + shift[2] * shift[2]);
    if (len > CAM_MAX_SHIFT) for (int i = 0; i < 3; ++i) shift[i] *= CAM_MAX_SHIFT / len;
    float tanUp = xtan(fov * 0.5f) * CAM_MARGIN;
    DIAG(g_dbg.st[3] = 6;)
    if (!(tanUp > 0.01f)) return 0;
    float tanSide = tanUp * CAM_ASPECT;
    float back = 0.0f;
    for (int k = 0; k < 2; ++k) {
        const float* q = k ? b : a;
        float d[3] = { q[0] - c[0] - shift[0], q[1] - c[1] - shift[1], q[2] - c[2] - shift[2] };
        float x = d[0] * r[0] + d[1] * r[1] + d[2] * r[2];
        float y = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
        float z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
        float bx = xabs(x) / tanSide - z, by = xabs(y) / tanUp - z;
        if (bx > back) back = bx;
        if (by > back) back = by;
    }
    if (back > CAM_MAX_BACK) back = CAM_MAX_BACK;
    for (int i = 0; i < 3; ++i) t[i] = shift[i] - f[i] * back;
    DIAG(g_dbg.st[3] = 0;)
    return 1;
}

/* Called from coop_hook_cam with the matrix the game is about to place the camera with. Returns the matrix to use: the
 * game's own (nothing to do) or s_shown, a bit-exact copy moved by the eased shift. */
__attribute__((optimize("O0")))
u32 coop_cam(const float* m)
{
    coop_cam_fovlock = 0;   /* a new frame: the next field-of-view call is the game's */
    DIAG(g_dbg.st[0]++; g_dbg.st[2] = coop_cam_fov; g_dbg.st[5] = coop_cam_fovcalls;)
    /* the matrix is a local of the game's caller, on the kernel stack (0xD0000000..): vptr() only admits heap addresses */
    if (!m || (u32)m < 0x10000u || !page_ok((u32)m) || !page_ok((u32)m + 63)) { DIAG(g_dbg.st[3] = 7;) return (u32)m; }
    if (story_mode() == 0) { DIAG(g_dbg.st[3] = 8;) return (u32)m; }
    /* only the twelve used floats are checked: the matrix's last column is not ours to judge */
    for (int i = 0; i < 16; ++i) if ((i & 3) != 3 && !finite_bits(m[i])) { DIAG(g_dbg.st[3] = 9;) return (u32)m; }
    /* SetTransform is also used by cameras that are not the one following player one (one with x = 0 was seen):
     * only a placement near player one is the game's camera */
    if (s_p1 && vptr((u32)s_p1 + C_TRANSFORM + 56)) {
        const float* a0 = (const float*)(s_p1 + C_TRANSFORM + 48);
        float dx = m[12] - a0[0], dy = m[13] - a0[1], dz = m[14] - a0[2];
        float d2 = dx * dx + dy * dy + dz * dz;
        if (!(d2 < 2000.0f * 2000.0f)) { DIAG(g_dbg.st[56]++;) return (u32)m; }
    }
    /* the game sometimes places the camera where it already is (paused): that is our placement already. Only the position is compared: in the pause menu the game
     * hands back the position it was given, but other words of the matrix differ (the log of that showed a squared distance of exactly 0 on every placement
     * while the whole-matrix comparison failed, so the shift was applied again each time and the picture crept away) */
    if (s_camAdjusted) {
        int same = 1;
        for (int i = 12; i < 15; ++i) if (((const volatile u32*)m)[i] != ((volatile u32*)s_shown)[i]) { same = 0; break; }
        if (same) { DIAG(g_dbg.st[63]++;) return (u32)m; }
    }
    /* bit-exact copy (a float copy alters the bits of the unused column and blacked the screen out) */
    for (int i = 0; i < 16; ++i) ((volatile u32*)s_shown)[i] = ((const volatile u32*)m)[i];
    u32 now = g_dbg.frames, dtf = now - s_camLast;
    s_camLast = now;
    if (dtf > 4) dtf = 4;
    float ease = CAM_EASE * (float)dtf / 30.0f;
    if (ease > 1.0f) ease = 1.0f;
    float t[3];
    cam_target(m, t);
    /* a cutscene has its own camera: whatever shift is left is dropped at once, not eased away (it showed as a slow slide) */
    if (in_cutscene()) ease = 1.0f;
    DIAG(g_dbg.st[32] = dtf; ((volatile float*)&g_dbg.st[0])[33] = ease;)
    int moved = 0;
    for (int i = 0; i < 3; ++i) {
        s_camShift[i] += (t[i] - s_camShift[i]) * ease;
        if (xabs(s_camShift[i]) > 0.5f) moved = 1;
    }
#if COOP_DIAG
    { volatile float* F = (volatile float*)&g_dbg.st[0];
      for (int i = 0; i < 3; ++i) { F[57 + i] = m[12 + i]; F[60 + i] = s_camShift[i]; } }
#endif
    s_camAdjusted = moved;
    if (!moved) { s_camShift[0] = s_camShift[1] = s_camShift[2] = 0.0f; return (u32)m; }
    for (int i = 0; i < 3; ++i) s_shown[12 + i] += s_camShift[i];
    DIAG(g_dbg.st[1]++;)
    return (u32)s_shown;
}

__asm__(
    ".text\n"
    ".globl coop_hook_cam\n"
    "coop_hook_cam:\n"               /* 0x129990: arg 1 (the matrix) is replaced by our copy when the camera is shared */
    "    pushfl\n"
    "    pushal\n"
    "    subl $108, %esp\n"
    "    fnsave (%esp)\n"
    "    pushl 148(%esp)\n"
    "    call coop_cam\n"
    "    addl $4, %esp\n"
    "    movl %eax, 148(%esp)\n"
    "    frstor (%esp)\n"
    "    addl $108, %esp\n"
    "    popal\n"
    "    popfl\n"
    "    .byte 0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf0, 0x83, 0xec, 0x4c\n"
    "    pushl $0x129999\n"
    "    ret\n"
    ".globl coop_hook_fov\n"
    "coop_hook_fov:\n"               /* 0x211C30: the first call of a frame is the game's own field of view */
    "    incl coop_cam_fovcalls\n"
    "    cmpl $0, coop_cam_fovlock\n"
    "    jne 1f\n"
    "    pushl %eax\n"
    "    movl 8(%esp), %eax\n"
    "    movl %eax, coop_cam_fov\n"
    "    movl $1, coop_cam_fovlock\n"
    "    popl %eax\n"
    "1:\n"
    "    .byte 0xa1, 0xf4, 0x93, 0x61, 0x00, 0x85, 0xc0\n"
    "    pushl $0x211C37\n"
    "    ret\n"
);

/* ---- P1 per-frame entry
 * Where:  D3DDevice_Swap 0x504810 (7 bytes) -> coop_detour
 * Was:    Swap ran its own code only.
 * Now:    x87 and registers are saved, coop_tick runs, the displaced bytes run, execution continues at 0x504817.
 * Needed: A once-per-frame place to run the mod without OS threads.
 * Source: this project (the PC port updates once per frame too)
 */
/* The patched Swap enters here. AT&T syntax; `movl 0x50fe68, %esi` is the displaced `mov esi, ds:[0x50FE68]`.
 * The x87 state is saved around coop_tick (it uses floats; the game may have values live in the FPU). */
__asm__(
    ".section .text.detour,\"ax\"\n"
    ".globl coop_detour\n"
    "coop_detour:\n"
    "    pushfl\n"
    "    pushal\n"
    "    subl $108, %esp\n"
    "    fnsave (%esp)\n"
    "    call coop_tick\n"
    "    frstor (%esp)\n"
    "    addl $108, %esp\n"
    "    popal\n"
    "    popfl\n"
    "    pushl %esi\n"
    "    movl 0x50fe68, %esi\n"
    "    pushl $0x504817\n"
    "    ret\n"
    ".text\n");


static int cb_ptr(u32 p) { return p >= 0x10000u && page_ok(p) && page_ok(p + 3); }   /* stacks live outside vptr()'s ranges */


/* PLAYER TWO'S SKIN. The game's texture-name callback (0x14F7E0) answers "yes" to <name><suffix> only when the level declares that texture (kSceneIsDeclared 0x4BDA0).
 * Texture set 1 of Obi-Wan's and Anakin's costume lists has the suffix "_var01" (data words patched by patch_xbe.py); no level declares such a file, so at 0x14F8A2
 * (just after that check) we declare "<folder>\\<name>_var01.stx" ourselves, the way the unofficial PC port does (resources.cpp Declare): a node named after the path in
 * the scene's texture table. The scene then asks the packer to read it (kSceneResRead 0x527F0), which no PAK has: our hook there hands the engine's own texture decoder
 * a recoloured copy from memory (the engine's memory file). The result is an ordinary, refcounted texture. The mod contains no game pixels (see below). */
typedef void* (*AllocFn)(u32);
typedef void  (FASTCALL *NodeSetPathFn)(u8* node, void* edx, const void* path, u8* scene);
typedef i32   (FASTCALL *MapFindFn)(u8* map, void* edx, const char* key);
typedef void  (FASTCALL *MapInsertFn)(u8* map, void* edx, u32 key, u8* group);
typedef const char* (FASTCALL *PathKeyFn)(const void* path, void* edx, i32 woext);
typedef void* (FASTCALL *PathFromTextFn)(void* path, void* edx, const char* text, i32 kind);
typedef u16   (FASTCALL *AddStringFn)(u8* scene, void* edx, const char* text);
typedef void  (FASTCALL *BuildPathFn)(const void* path, void* edx, char* out, i32 dirOnly);
typedef void* (FASTCALL *MemCtorFn)(void* mem, void* edx);
typedef void* (FASTCALL *PackReaderCtorFn)(void* mem, void* edx, void* file, i32 arg);
typedef i32   (*DecodeFn)(i32 scene, void* source, i32 typeId, i32 extra);
typedef void  (FASTCALL *VCall0)(void* o, void* edx);
typedef void  (FASTCALL *VCall1)(void* o, void* edx, i32 a);
typedef i32   (FASTCALL *VOpen)(void* o, void* edx, const void* data, u32 size);
struct ReadSource { void* reader; void* path; u8 fromPak; u8 pad[3]; void* record; };

static u8* eng_scene(void)
{
    u32 g = 0x645F7C;
    if (!page_ok(g)) return 0;
    u32 a = *(const u32*)g;                        if (a < 0x10000u || !page_ok(a + 0x3B)) return 0;
    u32 b = *(const u32*)(a + 0x38);               if (b < 0x10000u || !page_ok(b + 0x13)) return 0;
    u32 s = *(const u32*)(b + 0x10);               if (s < 0x10000u || !page_ok(s) || !page_ok(s + 0x15C8)) return 0;
    return (u8*)s;
}

static u8* g_dfolders; static u32 g_dcap;

static int decl_texture(const char* text, u32 stxsize)
{
    u8* scene = eng_scene(); if (!scene) return 0;
    AllocFn alloc = *(AllocFn*)(*(u32*)0x645F7C + 0x50);
    u8* table = *(u8**)(scene + 0x14F8 + 0 * 4);   /* type 0: textures */
    if (!table) return 0;
    u8* map = table + 0x10;
    i32 count = *(i32*)(scene + 0x15C4);
    if (*(u8**)(scene + 0x15C0) != g_dfolders || (u32)(count + 16) > g_dcap) {   /* room for the folders NodeSetPath may add (the engine appends without checking) */
        u32 grown = (u32)count + 16u + 256u;
        u8* f = (u8*)alloc(grown * 0x26u);
        if (!f) return 0;
        u8* old = *(u8**)(scene + 0x15C0);
        for (u32 i = 0; i < (u32)count * 0x26u; ++i) f[i] = old[i];
        for (u32 i = (u32)count; i < grown; ++i) {
            u8* x = f + i * 0x26u;
            for (u32 k = 0; k < 0x26u; ++k) x[k] = 0;
            *(u16*)(x + 0x22) = 0xFFFF; *(u16*)(x + 0x24) = 0xFFFF;
        }
        *(u8**)(scene + 0x15C0) = f; g_dfolders = f; g_dcap = grown;
    }
    u32 path[0x40]; for (u32 i = 0; i < 0x40; ++i) path[i] = 0;
    ((PathFromTextFn)0x00233B70)(path, 0, text, 1);
    u8* node = (u8*)alloc(20); if (!node) return 0;
    for (u32 i = 0; i < 20; ++i) node[i] = 0;
    ((NodeSetPathFn)0x00055250)(node, 0, path, scene);
    *(u32*)(node + 0x10) = stxsize;
    char key[256]; const char* k = ((PathKeyFn)0x00233200)(path, 0, 1);
    u32 n = 0; for (; k && n < 255 && k[n]; ++n) key[n] = k[n]; key[n] = 0;
    i32 idx = ((MapFindFn)0x00055CC0)(map, 0, key);
    if (idx >= 0) {
        u8* group = *(u8**)(*(u8**)(map + 8) + idx * 8 + 4);
        *(u8**)node = group; *(u8**)(node + 4) = *(u8**)(group + 4); *(u8**)(group + 4) = node;
        return 1;
    }
    u8* group = (u8*)alloc(8); if (!group) return 0;
    u8* items = *(u8**)(map + 8);
    *(u16*)group = *(i32*)(map + 4) > 0 ? *(u16*)(*(u8**)(items + 4)) : 0;
    *(u16*)(group + 2) = ((AddStringFn)0x0004F160)(scene, 0, key);
    *(u8**)(group + 4) = node; *(u8**)node = group;
    ((MapInsertFn)0x00056610)(map, 0, *(u16*)(group + 2), group);
    return 1;
}


/* ---- Recolouring: the textures are NOT part of this mod. At the first request the original is read from the player's own level PAK on the disc, the colours of its
 * DXT blocks are shifted (hue / saturation / value, optionally only inside a rectangle) and the result is what the engine decodes. The numbers are in g_skincfg (0x977800),
 * a plain table of 32-bit words (they can be changed with a debugger while the game runs; they are read when a level asks for the texture). */
struct SkinTex { u32 enable; i32 hue; u32 sat, val, x0, y0, x1, y1, prepared, served, lock; i32 hfix; };   /* hue: degrees (-180..180); sat, val: percent (100 = unchanged); the rectangle in pixels; lock: percent (0..100) of the way every colour's hue is pulled to hfix (degrees), which removes the colour noise a large saturation boost brings out */
struct SkinCfg { u32 magic, version, flags, ntstatus; struct SkinTex t[5]; };   /* flags: bits 0-4 a part could not be read from the disc, bits 8-12 it was not the expected texture; t[0] = Obi-Wan tunic (obi_02), t[1] = Obi-Wan cloak (obiCloak), t[2] = Anakin tunic (anakin_01), t[3] = Anakin cloak (anakinCloak), t[4] = Anakin trousers (anakin_cloth) */
__attribute__((section(".coopcfg"))) struct SkinCfg g_skincfg = { 0x46434B53u, 1, 0, 0, { { 1, 0, 280, 58, 0, 0, 256, 256, 0, 0, 100, 36 }, { 1, 15, 28, 200, 0, 0, 256, 256, 0, 0, 0, 0 }, { 1, 0, 25, 100, 0, 0, 256, 256, 0, 0, 0, 0 }, { 1, 0, 15, 120, 0, 0, 256, 256, 0, 0, 0, 0 }, { 1, 0, 12, 90, 0, 0, 256, 256, 0, 0, 0, 0 } } };

/* Kernel calls, through the game's own import table (the loader fills it with the kernel's addresses). */
#define K_NtClose      0x0055F6C0u
#define K_NtReadFile   0x0055F6E4u
#define K_NtCreateFile 0x0055F700u
typedef i32 (__attribute__((stdcall)) *fnCreate)(void**, u32, void*, void*, void*, u32, u32, u32, u32);
typedef i32 (__attribute__((stdcall)) *fnClose)(void*);
typedef i32 (__attribute__((stdcall)) *fnReadFile)(void*, void*, void*, void*, void*, void*, u32, void*);
static u8* g_skinbuf[5];
struct SkinPart { const char* base; const char* var; u32 size; u8 fmt; u32 w, h; };   /* base: lowercase texture name; var: the declared file name; size: bytes of the .stx; fmt: 0x40 DXT1, 0x52 DXT5 */
static const struct SkinPart k_part[5] = {
    { "obi_02",      "\\obi_02_var01.stx",      33156u, 0x40, 256, 256 },
    { "obicloak",    "\\obicloak_var01.stx",    33156u, 0x40, 256, 256 },
    { "anakin_01",   "\\anakin_01_var01.stx",   65924u, 0x52, 256, 256 },
    { "anakincloak", "\\anakincloak_var01.stx", 33156u, 0x40, 256, 256 },
    { "anakin_cloth", "\\anakin_cloth_var01.stx", 16772u, 0x40, 256, 128 },
};

static int disc_read(const char* path, u32 off, u8* dst, u32 len, u8* scratch)
{
    u16 n = 0; while (path[n]) ++n;
    struct { u16 len, max; const char* buf; } name = { n, (u16)(n + 1), path };
    struct { void* root; void* name; u32 attr; } oa = { 0, &name, 0x40 };
    u32 iosb[2] = { 0, 0 }; void* h = 0;
    i32 st = ((fnCreate)*(u32*)K_NtCreateFile)(&h, 0x80100000u /* GENERIC_READ | SYNCHRONIZE */, &oa, iosb, 0, 0, 1 /* share read */, 1 /* OPEN */, 0x60);
    g_skincfg.ntstatus = (u32)st;
    if (st < 0 || !h) return 0;
    u32 a0 = off & ~2047u, lead = off - a0, total = (lead + len + 2047u) & ~2047u;
    u32 bo[2] = { a0, 0 }, io2[2] = { 0, 0 };
    st = ((fnReadFile)*(u32*)K_NtReadFile)(h, 0, 0, 0, io2, scratch, total, bo);
    ((fnClose)*(u32*)K_NtClose)(h);
    g_skincfg.ntstatus = (u32)st;
    if (st < 0) return 0;
    for (u32 i = 0; i < len; ++i) dst[i] = scratch[lead + i];
    return 1;
}

static u16 recol(u16 c, const struct SkinTex* t)
{
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    int d = mx - mn, v = mx, sat = mx ? d * 255 / mx : 0, h = 0;
    if (d) {
        if (mx == r) h = (g - b) * 256 / d; else if (mx == g) h = 512 + (b - r) * 256 / d; else h = 1024 + (r - g) * 256 / d;
        if (h < 0) h += 1536;
    }
    h += t->hue * 1536 / 360; h %= 1536; if (h < 0) h += 1536;
    if (t->lock) { int hf = t->hfix * 1536 / 360; hf %= 1536; if (hf < 0) hf += 1536; int dd = hf - h; if (dd > 768) dd -= 1536; if (dd < -768) dd += 1536; h += dd * (int)t->lock / 100; h %= 1536; if (h < 0) h += 1536; }
    sat = sat * (int)t->sat / 100; if (sat > 255) sat = 255;
    v = v * (int)t->val / 100; if (v > 255) v = 255;
    int sec = h / 256, f = h % 256;
    int p = v * (255 - sat) / 255, q = v * (255 - sat * f / 255) / 255, tt = v * (255 - sat * (255 - f) / 255) / 255;
    switch (sec) { case 0: r = v; g = tt; b = p; break; case 1: r = q; g = v; b = p; break; case 2: r = p; g = v; b = tt; break;
                   case 3: r = p; g = q; b = v; break; case 4: r = tt; g = p; b = v; break; default: r = v; g = p; b = q; break; }
    return (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void skin_transform(u8* stx, const struct SkinTex* t)
{
    u8 fmt = stx[0x3C]; u32 w = *(u32*)(stx + 0x0C), hh = *(u32*)(stx + 0x10);
    u32 stride = fmt == 0x52 ? 16 : 8, coff = fmt == 0x52 ? 8 : 0, bw = w / 4, bh = hh / 4;
    u8* px = stx + 388;
    for (u32 by = 0; by < bh; ++by) for (u32 bx = 0; bx < bw; ++bx) {
        u32 x = bx * 4, y = by * 4;
        if (x < t->x0 || x >= t->x1 || y < t->y0 || y >= t->y1) continue;
        u8* b = px + (by * bw + bx) * stride + coff;
        u16 c0 = (u16)(b[0] | (b[1] << 8)), c1 = (u16)(b[2] | (b[3] << 8));
        u32 idx = b[4] | (b[5] << 8) | (b[6] << 16) | ((u32)b[7] << 24);
        u16 n0 = recol(c0, t), n1 = recol(c1, t);
        if (fmt != 0x52) {                                  /* DXT1: c0 > c1 means four colours, otherwise three and a transparent index */
            if (c0 > c1) {
                if (n0 == n1) { if (n0 < 0xFFFF) ++n0; else --n1; }
                if (n0 < n1) { u16 z = n0; n0 = n1; n1 = z; idx ^= 0x55555555u; }
            } else if (n0 > n1) { u16 z = n0; n0 = n1; n1 = z; idx ^= (~(idx >> 1)) & 0x55555555u; }
        }
        b[0] = (u8)n0; b[1] = (u8)(n0 >> 8); b[2] = (u8)n1; b[3] = (u8)(n1 >> 8);
        b[4] = (u8)idx; b[5] = (u8)(idx >> 8); b[6] = (u8)(idx >> 16); b[7] = (u8)(idx >> 24);
    }
}

struct SkinSrc { u8 key; const char* pak; u32 off[5]; };   /* where the original textures lie in the level's PAK (offsets of their .stx data) */
static const struct SkinSrc k_skinsrc[] = {
    { 7,  "pak\\res_m06a_uta_separatisthq.pak", { 8811990u, 8845228u, 0u, 0u, 0u } },
    { 8,  "pak\\res_m06b_uta_controlroom.pak", { 8407534u, 0u, 0u, 0u, 0u } },          /* plain Obi-Wan */
    { 13, "pak\\res_m11a_uta_order66.pak",     { 8561555u, 0u, 0u, 0u, 0u } },          /* plain Obi-Wan: a tunic only, no cloak */
    { 11, "pak\\res_m09a_cor_temple.pak",      { 0u, 0u, 9025048u, 9093577u, 9008190u } },   /* Anakin, cloak costume */
    { 14, "pak\\res_m12b_mus_assassin.pak",    { 0u, 0u, 8032660u, 8101189u, 8015802u } },
};

static int skin_prepare(int which)
{
    struct SkinTex* c = &g_skincfg.t[which];
    if (!c->enable) return 0;
    const struct SkinSrc* src = 0;
    for (u32 i = 0; i < sizeof k_skinsrc / sizeof k_skinsrc[0]; ++i) if (k_skinsrc[i].key == s_key) src = &k_skinsrc[i];
    if (!src || !src->off[which]) return 0;
    u32 sv = *(u32*)0x645F7C; if (!sv) return 0;
    AllocFn alloc = *(AllocFn*)(sv + 0x50); void (*freef)(void*) = *(void (**)(void*))(sv + 0x54);
    if (!g_skinbuf[which]) g_skinbuf[which] = (u8*)alloc(k_part[which].size);
    u8* scratch = (u8*)alloc(k_part[which].size + 8192);
    if (!g_skinbuf[which] || !scratch) { if (scratch) freef(scratch); return 0; }
    char path[96]; static const char* const roots[2] = { "\\??\\D:\\", "\\Device\\CdRom0\\" };
    int ok = 0;
    for (int r = 0; r < 2 && !ok; ++r) {
        u32 n = 0; for (const char* q = roots[r]; *q; ++q) path[n++] = *q; for (const char* q = src->pak; *q; ++q) path[n++] = *q; path[n] = 0;
        ok = disc_read(path, src->off[which], g_skinbuf[which], k_part[which].size, scratch);
    }
    freef(scratch);
    if (!ok) { g_skincfg.flags |= 1u << which; return 0; }               /* bit 0/1: tunic/cloak could not be read */
    u8* b = g_skinbuf[which];
    const char* base = k_part[which].base;
    u32 k = 0; while (base[k] && (b[0x40 + k] >= 'A' && b[0x40 + k] <= 'Z' ? b[0x40 + k] + 32 : b[0x40 + k]) == (u8)base[k]) ++k;       /* name check ignores case (the file says obiCloak, anakinCloak) */
    if (base[k] || b[0x40 + k] || b[0x3C] != k_part[which].fmt || *(u32*)(b + 0x0C) != k_part[which].w || *(u32*)(b + 0x10) != k_part[which].h) { g_skincfg.flags |= 0x100u << which; return 0; }   /* bit 2/3: not the expected texture */
    skin_transform(b, c);
    { static const char suf[] = "_var01"; u32 e = k; for (u32 i = 0; i < 7; ++i) b[0x40 + e + i] = (u8)suf[i]; for (u32 i = e + 7; i < 32; ++i) b[0x40 + i] = 0; }   /* name + "_var01" */
    c->prepared++;
    return 1;
}

static int nm_eq(const char* t, const char* a) { u32 j = 0; while (a[j] && t[j] == a[j]) ++j; return !a[j] && !t[j]; }

u32 cb_force_c(const char* n, const char* folder)
{
    if (!cb_ptr((u32)n)) return 0;
    char t[16]; u32 i = 0;
    for (; i < 15; ++i) { if (!page_ok((u32)n + i)) return 0; char c = n[i]; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); t[i] = c; if (!c) break; }
    t[15] = 0;
    int which = -1;
    for (int p = 0; p < 5; ++p) if (nm_eq(t, k_part[p].base)) which = p;
    if (which < 0 || !skin_prepare(which)) return 0;
    /* declare "<folder>\\<name>_var01.stx" in the same folder the game's callback is asking about (robed Obi-Wan: obicloak, plain Obi-Wan: obi) */
    char full[160]; u32 m = 0;
    if (!cb_ptr((u32)folder)) return 0;
    for (u32 i = 0; i < 100 && page_ok((u32)folder + i) && folder[i]; ++i) full[m++] = folder[i];
    while (m && full[m - 1] == '\\') --m;
    full[m++] = '\\';
    for (const char* q = k_part[which].var + 1; *q; ++q) full[m++] = *q;   /* the tail without its leading backslash (added above) */
    full[m] = 0;
    int ok = decl_texture(full, k_part[which].size);
    return (u32)ok;
}

/* The packer's read (0x527F0): serves our two textures from memory, anything else runs the game's code. Called with the 7 stack arguments + ecx (packer). */
u32 coop_read_c(u32 packer, u32 decode, u32 path, u32 typeId, u32 record, u32 extra, u32 context, u32* res)
{
    if (typeId != 0 || !path || !page_ok(path)) return 0;
    char full[512]; for (u32 i = 0; i < 8; ++i) full[i] = 0;
    ((BuildPathFn)0x00233420)((const void*)path, 0, full, 0);
    u32 L = 0; while (L < 510 && full[L]) { if (full[L] >= 'A' && full[L] <= 'Z') full[L] = (char)(full[L] + 32); ++L; }
    const u8* data = 0;
    int w = -1;
    for (int p = 0; p < 5; ++p) { u32 lp = 0; while (k_part[p].var[lp]) ++lp; if (L >= lp && nm_eq(full + L - lp, k_part[p].var)) w = p; }
    if (w >= 0) { data = g_skinbuf[w]; if (data) g_skincfg.t[w].served++; }
    if (!data) return 0;
    AllocFn alloc = *(AllocFn*)(*(u32*)0x645F7C + 0x50);
    void** file = (void**)((MemCtorFn)0x002326D0)(alloc(0x6C), 0);
    ((VOpen)(*(void***)file)[0x40 / 4])(file, 0, data, k_part[w].size);
    void** reader = (void**)((PackReaderCtorFn)0x00232D20)(alloc(0x24), 0, file, 0);
    if (record) *(u32*)(record + 0x10) = k_part[w].size;
    struct ReadSource src = { reader, (void*)path, 0, {0, 0, 0}, (void*)record };
    i32 r = ((DecodeFn)decode)((i32)context, &src, (i32)typeId, (i32)extra);
    ((VCall0)(*(void***)file)[0x10 / 4])(file, 0);
    ((VCall1)(*(void***)file)[0])(file, 0, 1);
    ((VCall1)(*(void***)reader)[0])(reader, 0, 1);
    *(u8*)(packer + 0x89) = 0;
    *res = (u32)r;
    return 1;
}

__asm__(
    ".text\n"
    ".globl coop_read_hook\n"
    "coop_read_hook:\n"
    "    pushl %ebp\n"
    "    movl %esp, %ebp\n"
    "    pushl %ecx\n"
    "    pushl %edx\n"
    "    subl $4, %esp\n"
    "    leal -12(%ebp), %eax\n"
    "    pushl %eax\n"
    "    pushl 32(%ebp)\n" "    pushl 28(%ebp)\n" "    pushl 20(%ebp)\n" "    pushl 16(%ebp)\n" "    pushl 12(%ebp)\n" "    pushl 8(%ebp)\n"
    "    pushl -4(%ebp)\n"
    "    call coop_read_c\n"
    "    addl $32, %esp\n"
    "    testl %eax, %eax\n"
    "    jz 1f\n"
    "    movl -12(%ebp), %eax\n"
    "    movl %ebp, %esp\n"
    "    popl %ebp\n"
    "    ret $0x1C\n"
    "1:\n"
    "    movl -4(%ebp), %ecx\n"
    "    movl -8(%ebp), %edx\n"
    "    movl %ebp, %esp\n"
    "    popl %ebp\n"
    "    subl $0x90, %esp\n"        /* the displaced instruction */
    "    pushl $0x527F6\n"
    "    ret\n"
    ".text\n");

__asm__(
    ".text\n"
    ".globl coop_cb_force\n"
    "coop_cb_force:\n"
    "    testb %bl, %bl\n"
    "    jne 1f\n"
    "    movl $0x14F8B2, %eax\n"   /* already declared: carry on as the game does */
    "    pushl %eax\n"
    "    ret\n"
    "1:\n"
    "    pushl %eax\n" "    pushl %ecx\n" "    pushl %edx\n"
    "    movl (%esi), %eax\n"       /* esi = pointer to the name pointer */
    "    pushl %edi\n"                /* edi = the folder the game's callback is working in */
    "    pushl %eax\n"
    "    call cb_force_c\n"
    "    addl $8, %esp\n"
    "    testl %eax, %eax\n"
    "    popl %edx\n" "    popl %ecx\n" "    popl %eax\n"
    "    jne 2f\n"
    "    popl %edi\n"                /* the displaced pop, then the game's 'not declared' exit */
    "    pushl $0x14F8A7\n"
    "    ret\n"
    "2:\n"
    "    pushl $0x14F8B2\n"          /* forced: treat as declared */
    "    ret\n"
    ".text\n");

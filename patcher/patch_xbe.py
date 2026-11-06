#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Patch Star Wars: Episode III - Revenge of the Sith (Xbox, NTSC-U) default.xbe with the M1 co-op code.

  python3 patch_xbe.py default_original.xbe default.xbe [--blob coop.bin] [--sym coop.sym]

What it does (nothing else is touched):
  1. Checks the input is the supported XBE (MD5 6460ef37862de3af364a0563a0a18c4e).
  2. Moves the section header table into free header space and appends a 'COOP' section (the code
     blob from build.sh, linked at 0x96E000) at the end of the file.
  3. Replaces the first 7 bytes of D3DDevice_Swap (0x504810) with `jmp coop_detour` + 2 nops, and applies
     the other hooks (`hooks`: jumps into the mod), call replacements (`calls`: calls to GetPlayer 0xA30F0;
     `calls_other`: other calls). Every site's original bytes are checked first and the patch is refused on a
     mismatch, and so is a build whose blob lacks any of the symbols (nothing is skipped silently any more: a stale
     blob once hid missing hooks).
     The list, with the original bytes and the reason for each, is in CHANGES.md.
  4. Updates the XBE header (section count / table address / image and header sizes) and the SHA-1
     digests of the two touched sections. The RSA signature is NOT recomputed (a softmodded console
     and xemu do not check it).
"""
import argparse, hashlib, struct, sys

EXPECTED_MD5 = "6460ef37862de3af364a0563a0a18c4e"
HOOK_VA = 0x504810
HOOK_ORIG = bytes.fromhex("56 8b 35 68 fe 50 00".replace(" ", ""))  # push esi; mov esi,[0x50FE68]
NEW_VA = 0x96E000
NEW_FLAGS = 0x7  # writable | preload | executable (as the D3D/XPP sections)
NEW_TABLE_OFF = 0xB20  # in the header page, after the original header data (0xB16) and before 0x1000


def u32(d, o): return struct.unpack_from("<I", d, o)[0]
def put32(d, o, v): struct.pack_into("<I", d, o, v)


def section_digest(raw):
    return hashlib.sha1(struct.pack("<I", len(raw)) + raw).digest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src"); ap.add_argument("dst")
    ap.add_argument("--blob", default="coop.bin"); ap.add_argument("--sym", default="coop.sym")
    a = ap.parse_args()

    xbe = bytearray(open(a.src, "rb").read())
    if hashlib.md5(xbe).hexdigest() != EXPECTED_MD5:
        sys.exit("refusing: not the supported default.xbe (MD5 %s)" % hashlib.md5(xbe).hexdigest())
    blob = open(a.blob, "rb").read()
    syms = {l.split()[2]: int(l.split()[0], 16) for l in open(a.sym) if len(l.split()) == 3}
    detour = syms["coop_detour"]
    assert detour == NEW_VA, "the detour must be at the start of the section"

    base = u32(xbe, 0x104)
    nsec, sechdr_va = u32(xbe, 0x11C), u32(xbe, 0x120)
    assert (base, nsec, sechdr_va) == (0x10000, 17, 0x10370), "unexpected header layout"
    old_table = sechdr_va - base
    sections = [bytearray(xbe[old_table + i * 56: old_table + (i + 1) * 56]) for i in range(nsec)]

    # --- the hook ---------------------------------------------------------------------------
    def va2off(va):
        for s in sections:
            fl, v, vs, ro, rs = struct.unpack_from("<IIIII", s, 0)
            if v <= va < v + rs:
                return ro + va - v, s
        raise SystemExit("VA %#x not in the file" % va)
    hooks = [  # (VA, expected original bytes, symbol to jump to)
        (HOOK_VA, HOOK_ORIG, "coop_detour"),
        (0x151500, bytes.fromhex("568bf18a86d6050000"), "coop_hook_health"),  # ICharacter health change
        (0x152D30, bytes.fromhex("558bec83e4f083ec58"), "coop_hook_kill"),    # ICharacter instant kill
        (0x178070, bytes.fromhex("a1cc4369005668" + "8b7068")[:6] + bytes.fromhex("8b7068"), "coop_hook_pressed"),  # P5 / P15 Force-script fix
        (0x129990, bytes.fromhex("558bec83e4f083ec4c"), "coop_hook_cam"),  # P13 shared camera: master camera SetTransform
        (0x211C30, bytes.fromhex("a1f493610085c0"), "coop_hook_fov"),  # P13 renderer field of view (read only)
        (0x14F8A2, bytes.fromhex("84db740c5f"), "coop_cb_force"),  # player two's skin: declare <name>_var01.stx when the level does not ship it
        (0x2809E9, bytes.fromhex("f3 0f 59 44 24 0c".replace(" ", "")), "coop_yoda_regen"),  # P43: Yoda refills his Force twice as fast
        (0x527F0, bytes.fromhex("81ec90000000"), "coop_read_hook"),  # player two's skin: the packer's read serves the recoloured texture from memory
    ]
    calls_other = [  # (VA of an E8 call, the target it must have now, symbol to call instead)
        (0x269325, 0x269230, "coop_xp_stub"),  # a kill's experience: player two's goes to player one
    ]
    calls = [  # (VA of an E8 call to 0xA30F0 "the player", symbol to call instead)
        (0x1A20C8, "coop_gp_trigger"),  # "Player Only" trigger check
        (0x1A2258, "coop_gp_trigger"),  # "Player Only" trigger check
        (0xBDA1B, "coop_dmg_brk"),  # IBreakable: Damaged Only By Player
        (0x29481A, "coop_dmg_cnd"),  # IPowerConduit: Damaged Only by Player
        (0x11881B, "coop_pk_a"),  # IPickup touch: is the toucher the player
        (0x11DE82, "coop_pk_b"),  # IPickup
        (0x2B8B11, "coop_pk_c"),  # powerup: does the character need it
        (0x2B8B9E, "coop_pk_d"),  # powerup: can the character pick it up
        (0x2B8C6D, "coop_pk_e"),  # powerup: taken
        (0x2B9524, "coop_pk_f1"), # bacta tank: something entered
        (0x2B9532, "coop_pk_f2"), # bacta tank: its user
        (0x2B937F, "coop_pk_g"),  # bacta tank: user dead?
        (0xFF0AA, "coop_pain"),  # IPain touch: is the target the player ("affects player" off)
        (0xFF0C1, "coop_pain"),  # IPain touch: ("affects others" off)
        (0xFF106, "coop_pain"),  # IPain touch
        (0xFF2AC, "coop_pain"),  # IPain / IElectricalDischargeZone: can the object be hurt
    ]
    touched = {}
    datas = [(0x648F0C, bytes(4), struct.pack("<I", 0x57B6F0)), (0x648B1C, bytes(4), struct.pack("<I", 0x57B6F0))]  # texture-set suffix "_var01" (the game's own string) in the costume lists of Obi-Wan (0x648F0C) and Anakin (0x648B1C)
    for va, old, new in datas:
        off, sec = va2off(va)
        if bytes(xbe[off:off + 4]) != old:
            sys.exit("refusing: data at %#x is not the expected one" % va)
        xbe[off:off + 4] = new
        touched[id(sec)] = sec
    for va, orig, sym in hooks:
        if sym not in syms:
            sys.exit("refusing: the blob has no %s (a stale or incomplete build: check the linker output)" % sym)
        off, sec = va2off(va)
        if bytes(xbe[off:off + len(orig)]) != orig:
            sys.exit("refusing: bytes at %#x are not the expected ones: %s" % (va, xbe[off:off + len(orig)].hex()))
        rel = (syms[sym] - (va + 5)) & 0xFFFFFFFF
        xbe[off:off + len(orig)] = b"\xE9" + struct.pack("<I", rel) + b"\x90" * (len(orig) - 5)
        touched[id(sec)] = sec
    for va, sym in calls:
        if sym not in syms:
            sys.exit("refusing: the blob has no %s (a stale or incomplete build: check the linker output)" % sym)
        off, sec = va2off(va)
        rel_old = (0xA30F0 - (va + 5)) & 0xFFFFFFFF
        if bytes(xbe[off:off + 5]) != b"\xE8" + struct.pack("<I", rel_old):
            sys.exit("refusing: no call to 0xA30F0 at %#x: %s" % (va, xbe[off:off + 5].hex()))
        xbe[off:off + 5] = b"\xE8" + struct.pack("<I", (syms[sym] - (va + 5)) & 0xFFFFFFFF)
        touched[id(sec)] = sec
    for va, old, sym in calls_other:
        if sym not in syms:
            sys.exit("refusing: the blob has no %s (a stale or incomplete build: check the linker output)" % sym)
        off, sec = va2off(va)
        rel_old = (old - (va + 5)) & 0xFFFFFFFF
        if bytes(xbe[off:off + 5]) != b"\xE8" + struct.pack("<I", rel_old):
            sys.exit("refusing: no call to %#x at %#x: %s" % (old, va, xbe[off:off + 5].hex()))
        xbe[off:off + 5] = b"\xE8" + struct.pack("<I", (syms[sym] - (va + 5)) & 0xFFFFFFFF)
        touched[id(sec)] = sec
    # refresh each touched section's digest (SHA-1 over its raw size + raw data)
    for sec in touched.values():
        ro, rs = struct.unpack_from("<II", sec, 12)
        sec[0x24:0x38] = section_digest(bytes(xbe[ro:ro + rs]))

    # --- the new section --------------------------------------------------------------------
    pad = (-len(xbe)) % 0x1000
    xbe += b"\0" * pad
    raw_off = len(xbe)
    xbe += blob
    xbe += b"\0" * ((-len(xbe)) % 0x1000)
    vsize = (len(blob) + 3) & ~3

    new_table_end = NEW_TABLE_OFF + (nsec + 1) * 56
    name_off = new_table_end            # "COOP\0"
    ref_off = (name_off + 5 + 1) & ~1   # two u16 shared-page reference counts
    hdr_end = (ref_off + 4 + 0x1F) & ~0x1F
    assert hdr_end <= 0x1000, "no room in the header page"
    assert not any(xbe[0xB16:0x1000]), "header free space is not free"
    xbe[name_off:name_off + 5] = b"COOP\0"
    ns = bytearray(56)
    struct.pack_into("<IIIIIIIII", ns, 0, NEW_FLAGS, NEW_VA, vsize, raw_off, len(blob),
                     base + name_off, 0, base + ref_off, base + ref_off + 2)
    ns[0x24:0x38] = section_digest(blob)
    sections.append(ns)
    for i, s in enumerate(sections):
        xbe[NEW_TABLE_OFF + i * 56: NEW_TABLE_OFF + (i + 1) * 56] = s

    # --- the header -------------------------------------------------------------------------
    put32(xbe, 0x11C, nsec + 1)
    put32(xbe, 0x120, base + NEW_TABLE_OFF)
    put32(xbe, 0x108, hdr_end)  # sizeof_headers
    end = (NEW_VA + vsize + 0x1F) & ~0x1F
    put32(xbe, 0x10C, end - base)  # sizeof_image (offset from the base, as in the original)

    open(a.dst, "wb").write(xbe)
    print("wrote %s (%d bytes); COOP section %d bytes at VA %#x, detour %#x, g_dbg at %#x"
          % (a.dst, len(xbe), len(blob), NEW_VA, detour, syms["g_dbg"]))


if __name__ == "__main__":
    main()

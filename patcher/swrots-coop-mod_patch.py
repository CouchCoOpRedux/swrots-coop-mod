#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
SWROTS (UNOFFICIAL) Co-Op Mod patcher: adds two-player couch co-op to Star Wars: Episode III - Revenge of the Sith
(original Xbox, NTSC-U). It changes two files of your own copy of the game:

  default.xbe                              the game executable (the co-op code is appended to it)
  movies/Splash_Movies/Legal_XBOX.sfd      the first screen, replaced by the co-op notice

Nothing is downloaded and no game files are shipped: the patcher needs YOUR game.

  python swrots-coop-mod_patch.py game.iso                  patch an ISO     -> game_swrots-coop-mod.iso
  python swrots-coop-mod_patch.py game.iso -o out.iso       ... with a name of your choice
  python swrots-coop-mod_patch.py --folder GAMEDIR          patch an extracted game folder in place
                                                      (the two original files are first saved in a
                                                      folder named <GAMEDIR>_original_files)
  python swrots-coop-mod_patch.py --xbe default.xbe -o new.xbe    only the executable (no movie, no ISO tools)

ISO mode needs extract-xiso (https://github.com/XboxDev/extract-xiso): put it on your PATH, next to this
script, or give its location with --extract-xiso. The ISO is unpacked into a temporary folder next to the
output file (about as much free space as the game takes), patched, and packed again.
The original files are checked first; anything that is not the exact NTSC-U game is refused.
"""
import argparse, hashlib, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sfdmux

VERSION = "v0.38"
XBE_ORIG_MD5 = "6460ef37862de3af364a0563a0a18c4e"
XBE_PATCHED_MD5 = "2c405a77c5d52acaf18abb821ccf8b23"
SFD_ORIG_MD5 = "acfd4ff8cc82ac695961d611b598bac6"
SFD_PATCHED_MD5 = "08fe86ada567dbcc7983635abb4d52ec"
SFD_ORIG_SIZE = 1644544
SFD_FRAMES = 300                      # 10 s at 29.97 fps
SFD_REL = ("movies", "Splash_Movies", "Legal_XBOX.sfd")


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def find_ci(root, parts):
    """find root/parts[0]/parts[1]/... ignoring the case of each name; None if missing"""
    cur = root
    for p in parts:
        try:
            names = os.listdir(cur)
        except OSError:
            return None
        hit = next((n for n in names if n.lower() == p.lower()), None)
        if hit is None:
            return None
        cur = os.path.join(cur, hit)
    return cur


def die(msg):
    sys.exit("\nERROR: " + msg)


def need(path):
    p = os.path.join(HERE, path)
    if not os.path.isfile(p):
        die("missing patcher file %s (keep all files of the patcher folder together)" % path)
    return p


def patch_xbe(src, dst):
    got = md5(src)
    if got == XBE_PATCHED_MD5:
        print("  default.xbe is already patched (%s)" % VERSION)
        if os.path.abspath(src) != os.path.abspath(dst):
            shutil.copyfile(src, dst)
        return
    if got != XBE_ORIG_MD5:
        die("default.xbe is not the supported original (MD5 %s, expected %s).\n"
            "Only the NTSC-U release of Star Wars: Episode III - Revenge of the Sith is supported." % (got, XBE_ORIG_MD5))
    tmp = dst + ".tmp"
    r = subprocess.run([sys.executable, need("patch_xbe.py"), src, tmp,
                        "--blob", need("coop.bin"), "--sym", need("coop.sym")],
                       capture_output=True, text=True)
    if r.returncode != 0:
        die("patching default.xbe failed:\n" + r.stdout + r.stderr)
    if md5(tmp) != XBE_PATCHED_MD5:
        os.remove(tmp)
        die("the patched default.xbe does not match the expected result; nothing was written")
    os.replace(tmp, dst)
    print("  default.xbe patched")


def make_movie(src_sfd, dst):
    """build the notice movie; the three Sofdec header sectors come from the user's own file"""
    got = md5(src_sfd)
    if got == SFD_PATCHED_MD5:
        print("  start-up screen is already replaced")
        if os.path.abspath(src_sfd) != os.path.abspath(dst):
            shutil.copyfile(src_sfd, dst)
        return True
    if got != SFD_ORIG_MD5:
        print("  Legal_XBOX.sfd is not the retail file (MD5 %s): leaving the start-up screen alone" % got)
        return False
    with open(need("legal_screen.m1v"), "rb") as f:
        es = f.read()
    data, _ = sfdmux.mux(src_sfd, es, SFD_FRAMES)
    if hashlib.md5(data).hexdigest() != SFD_PATCHED_MD5:
        die("the new start-up movie does not match the expected result; nothing was written")
    tmp = dst + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, dst)
    print("  start-up screen replaced (%d bytes)" % len(data))
    return True


def patch_tree(root, backup):
    xbe = find_ci(root, ("default.xbe",))
    if not xbe:
        die("no default.xbe in %s" % root)
    sfd = find_ci(root, SFD_REL)
    if backup:
        # copies go beside the folder, not into it, so they never end up in an ISO built from it
        bdir = os.path.abspath(root).rstrip("\\/") + "_original_files"
        for p in [xbe] + ([sfd] if sfd else []):
            dst = os.path.join(bdir, os.path.relpath(p, root))
            if os.path.exists(dst):
                continue
            if p == xbe and md5(p) != XBE_ORIG_MD5:
                continue
            if p == sfd and md5(p) != SFD_ORIG_MD5:
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(p, dst)
            print("  saved the original %s in %s" % (os.path.basename(p), bdir))
    patch_xbe(xbe, xbe)
    if sfd:
        make_movie(sfd, sfd)
    else:
        print("  %s not found: the start-up screen is left alone" % "/".join(SFD_REL))


def find_xiso(arg):
    cands = [arg] if arg else []
    cands += [os.path.join(HERE, n) for n in ("extract-xiso", "extract-xiso.exe")]
    cands += [shutil.which("extract-xiso"), shutil.which("extract-xiso.exe")]
    for c in cands:
        if c and os.path.isfile(c):
            return c
    die("extract-xiso was not found. Get it from https://github.com/XboxDev/extract-xiso and put it on your PATH\n"
        "or next to this script, or pass its location with --extract-xiso.")


def run(cmd):
    print("  $", " ".join('"%s"' % c if " " in c else c for c in cmd))
    r = subprocess.run(cmd)
    if r.returncode != 0:
        die("command failed (exit code %d)" % r.returncode)


def main():
    ap = argparse.ArgumentParser(description="SWROTS (UNOFFICIAL) Co-Op Mod patcher (%s)" % VERSION)
    ap.add_argument("iso", nargs="?", help="the game's ISO (NTSC-U)")
    ap.add_argument("-o", "--output", help="output ISO (ISO mode) or patched default.xbe (--xbe mode)")
    ap.add_argument("--folder", help="patch an extracted game folder in place")
    ap.add_argument("--xbe", help="patch only this default.xbe (write the result to -o)")
    ap.add_argument("--no-backup", action="store_true", help="folder mode: do not keep *.orig copies")
    ap.add_argument("--extract-xiso", help="path of the extract-xiso program")
    ap.add_argument("--keep-temp", action="store_true", help="ISO mode: keep the unpacked folder")
    a = ap.parse_args()
    modes = [bool(a.iso), bool(a.folder), bool(a.xbe)]
    if sum(modes) != 1:
        ap.error("give exactly one of: an ISO file, --folder, --xbe")
    print("SWROTS (UNOFFICIAL) Co-Op Mod patcher, %s" % VERSION)

    if a.xbe:
        if not a.output:
            ap.error("--xbe needs -o for the patched file")
        if os.path.abspath(a.xbe) == os.path.abspath(a.output):
            die("the output must be a different file")
        patch_xbe(a.xbe, a.output)
        print("done: %s" % a.output)
        return

    if a.folder:
        if not os.path.isdir(a.folder):
            die("%s is not a folder" % a.folder)
        patch_tree(a.folder, not a.no_backup)
        print("done. Build the ISO from the folder as usual (extract-xiso -c).")
        return

    iso = a.iso
    if not os.path.isfile(iso):
        die("%s not found" % iso)
    out = a.output or (os.path.splitext(iso)[0] + "_swrots-coop-mod.iso")
    if os.path.abspath(out) == os.path.abspath(iso):
        die("the output must be a different file than the input ISO")
    if os.path.exists(out):
        die("%s already exists; remove it or choose another name with -o" % out)
    xiso = find_xiso(a.extract_xiso)
    work = tempfile.mkdtemp(prefix="swrots-coop-mod_", dir=os.path.dirname(os.path.abspath(out)))
    try:
        game = os.path.join(work, "game")
        print("[1/3] unpacking the ISO (this takes a while)")
        # options first: the Windows build of extract-xiso stops reading options at the first file name
        run([xiso, "-d", game, "-x", iso])
        # extract-xiso -d puts the files directly in `game`; some versions add a folder named after the ISO
        if not find_ci(game, ("default.xbe",)):
            sub = [n for n in os.listdir(game) if os.path.isdir(os.path.join(game, n))]
            if len(sub) == 1 and find_ci(os.path.join(game, sub[0]), ("default.xbe",)):
                game = os.path.join(game, sub[0])
        print("[2/3] patching")
        patch_tree(game, False)
        print("[3/3] packing the new ISO")
        built = os.path.join(work, "packed.iso")
        run([xiso, "-c", game, built])
        shutil.move(built, out)
        print("done: %s" % out)
    finally:
        if a.keep_temp:
            print("kept: %s" % work)
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()

# SPDX-License-Identifier: GPL-3.0-or-later
"""Disassemble a stretch of the ORIGINAL default.xbe by virtual address (needs objdump).
usage: python3 dis.py default_original.xbe <VA in hex> <length in hex>      e.g.  dis.py default.xbe 178070 20"""
import struct, sys, subprocess, tempfile, os
if len(sys.argv) != 4:
    sys.exit(__doc__)
d = open(sys.argv[1], 'rb').read()
n = struct.unpack_from('<I', d, 0x11C)[0]; sh = struct.unpack_from('<I', d, 0x120)[0] - 0x10000
secs = [struct.unpack_from('<IIIII', d, sh + i * 56) for i in range(n)]
def off(v):
    for f, va, vs, ro, rs in secs:
        if va <= v < va + rs: return ro + v - va
va = int(sys.argv[2], 16); ln = int(sys.argv[3], 16)
o = off(va)
if o is None: sys.exit("address not in any section")
with tempfile.TemporaryDirectory() as tmp:
    p = os.path.join(tmp, 't.bin'); open(p, 'wb').write(d[o:o + ln])
    print(subprocess.run(['objdump', '-D', '-b', 'binary', '-mi386', '--adjust-vma=0x%x' % va, p], capture_output=True, text=True).stdout.split('\n', 7)[-1])

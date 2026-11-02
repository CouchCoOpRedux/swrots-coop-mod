# Headroom diagnostic (not part of the release)

`headroom.patch` is a diff against `source/coop_m1.c` that adds a free-memory recorder: it calls the kernel's
`MmQueryStatistics` about three times a second and keeps, per level, the free memory after loading, the lowest and highest
values, and what a second player costs. It writes `E:\swrots_headroom.txt` (a small text table) and fills the block `g_hr`
(address in `coop.sym`; `python xbwatch.py headroom` prints it in xemu).

Build: `patch -p0 coop_m1.c < headroom.patch && sh build.sh && python3 patch_xbe.py default_original.xbe default.xbe`.
`harness.c` is a freestanding 32-bit test that feeds the recorder fake kernel calls (`gcc -m32 -DHR_TEST ... -nostdlib`).

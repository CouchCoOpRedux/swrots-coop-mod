#!/usr/bin/env python3
"""xbwatch - a flight recorder for the memory of a running Xbox (xemu) game.

It attaches to xemu's GDB server (start xemu with the -s option; it listens on localhost:1234), reads the regions of
memory a PROFILE (a small JSON file) names, and logs every change to a JSON-lines file, with the time, the game's frame
counter and the field offset. Play, press a mark key when something interesting happens, and the log shows what the
game changed around that moment.

  python xbwatch.py run profiles/swrots_coop.json      record (Ctrl+C stops it; the game is always left running)
  python xbwatch.py report logs/trace_....jsonl         print the marks and the rarely-changing words around them
  python xbwatch.py peek 0x976000 0x40                  print some memory once
  python xbwatch.py resume                              let the game run again if it was ever left stopped

Needs only Python 3.8+ (standard library). No game files are touched; nothing is written into the game.

The profile is re-read whenever its file changes (about every half second): regions, scans, marks and the rate can be
edited while the game runs. See README.md for the profile format.

How it reads: the GDB server of QEMU/xemu only answers while the guest is stopped, so every sample briefly stops the
guest (Ctrl+C packet), reads, and continues. A sample takes a few milliseconds; the guest's own clock is stopped
meanwhile, so the game does not lose time, it only runs a little slower in real time. The log's "stat" events show the
cost; lower rate_hz or the region sizes if the game stutters.
"""
import argparse
import collections
import json
import os
import re
import select
import socket
import struct
import sys
import threading
import time

# ------------------------------------------------------------------------------------------------------------------
# helpers

def num(x, default=None):
    """int from a JSON value: 123, "123", "0x7B"."""
    if x is None:
        return default
    if isinstance(x, bool):
        return int(x)
    if isinstance(x, (int, float)):
        return int(x)
    return int(str(x).strip(), 0)


def hx(v, w=8):
    return "0x%0*X" % (w, v)


def as_float(v):
    f = struct.unpack("<f", struct.pack("<I", v))[0]
    if f == f and 1e-4 < abs(f) < 1e7:
        return round(f, 4)
    return None


def say(msg):
    sys.stdout.write(msg + "\n")
    sys.stdout.flush()


# ------------------------------------------------------------------------------------------------------------------
# GDB remote serial protocol client (just what is needed)

class GdbError(Exception):
    pass


class Rsp:
    """GDB remote protocol client for a QEMU-style stub.

    QEMU's stub pauses the guest when a debugger connects and, while the guest runs, answers nothing but Ctrl+C (0x03),
    so: halt() = send 0x03 and wait for the stop reply; resume() = 'c'."""

    CHUNK = 0x800  # QEMU answers at most 2048 bytes per 'm' packet

    def __init__(self, host, port, timeout=5.0):
        try:
            self.s = socket.create_connection((host, port), timeout=timeout)
        except OSError as e:
            raise GdbError("cannot connect to %s:%d (%s). Is xemu running with the -s option?" % (host, port, e))
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b""
        self.running = False        # True after 'c' until a stop reply has been seen
        self.n_reads = 0

    # -- packets
    def _fill(self, timeout):
        r, _, _ = select.select([self.s], [], [], max(0.0, timeout))
        if not r:
            return False
        d = self.s.recv(65536)
        if not d:
            raise GdbError("xemu closed the connection")
        self.buf += d
        return True

    def send(self, payload):
        data = payload.encode()
        self.s.sendall(b"$" + data + b"#" + b"%02x" % (sum(data) & 0xFF))

    def packet(self, timeout=5.0):
        """The next packet's payload (and it is acknowledged)."""
        end = time.time() + timeout
        while True:
            i = self.buf.find(b"$")
            if i >= 0:
                j = self.buf.find(b"#", i)
                if j >= 0 and len(self.buf) >= j + 3:
                    payload = self.buf[i + 1:j]
                    self.buf = self.buf[j + 3:]
                    self.s.sendall(b"+")
                    return payload.decode("latin-1")
            left = end - time.time()
            if left <= 0:
                raise GdbError("timeout waiting for xemu")
            self._fill(left)

    def poll_async(self):
        """While the guest runs: notice a stop reply that arrived by itself (a breakpoint or a crash in the guest)."""
        while self._fill(0.0):
            pass
        i = self.buf.find(b"$")
        if i >= 0:
            j = self.buf.find(b"#", i)
            if j >= 0 and len(self.buf) >= j + 3:
                p = self.packet(1.0)
                if p[:1] in ("S", "T", "W", "X"):
                    self.running = False
                    return p
        return None

    # -- control
    def connect_handshake(self):
        """After the connection xemu normally has the guest paused. Find out and make it so."""
        self.send("?")
        try:
            self.packet(1.0)
            self.running = False
        except GdbError:
            self.running = True
            self.halt()

    def halt(self):
        if not self.running:
            return
        self.s.sendall(b"\x03")
        end = time.time() + 5.0
        while True:
            p = self.packet(max(0.1, end - time.time()))
            if p[:1] in ("S", "T", "W", "X"):
                self.running = False
                return
            if time.time() > end:
                raise GdbError("the guest did not stop")

    def resume(self):
        if self.running:
            return
        self.send("c")
        self.running = True

    def close(self):
        try:
            if not self.running:
                self.resume()
            time.sleep(0.05)
        except Exception:
            pass
        try:
            self.s.close()
        except Exception:
            pass

    # -- memory (guest must be stopped)
    def _m(self, addr, n):
        self.send("m%x,%x" % (addr, n))
        self.n_reads += 1
        while True:
            p = self.packet()
            if p and p[0] == "E":
                return None
            if p[:1] in ("S", "T", "W", "X"):
                continue
            try:
                return bytes.fromhex(p)
            except ValueError:
                return None

    def read(self, addr, n):
        """bytes, or None if any part is unmapped. Chunks never cross a 4 KB page, so one bad page costs one miss."""
        out = bytearray()
        a, end = addr, addr + n
        while a < end:
            step = min(end - a, self.CHUNK, 0x1000 - (a & 0xFFF))
            d = self._m(a, step)
            if d is None or len(d) != step:
                return None
            out += d
            a += step
        return bytes(out)

    def u32(self, addr):
        d = self.read(addr, 4)
        return None if d is None else struct.unpack("<I", d)[0]


# ------------------------------------------------------------------------------------------------------------------
# Windows global hotkeys (marks while the game has the focus)

VK = {"BACKSPACE": 8, "TAB": 9, "PAUSE": 0x13, "SCROLL": 0x91, "INSERT": 0x2D, "DELETE": 0x2E, "HOME": 0x24, "END": 0x23,
      "PGUP": 0x21, "PGDN": 0x22}
for _i in range(10):
    VK["NUMPAD%d" % _i] = 0x60 + _i
    VK[str(_i)] = 0x30 + _i
for _i in range(1, 13):
    VK["F%d" % _i] = 0x6F + _i
for _c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
    VK[_c] = ord(_c)


class Hotkeys:
    def __init__(self):
        self.user32 = None
        self.down = {}
        if os.name == "nt":
            try:
                import ctypes
                self.user32 = ctypes.windll.user32
            except Exception:
                self.user32 = None

    def pressed(self, names):
        """The names (of VK) that went down since the last call."""
        out = []
        if not self.user32:
            return out
        for n in names:
            vk = VK.get(str(n).upper())
            if not vk:
                continue
            st = bool(self.user32.GetAsyncKeyState(vk) & 0x8000)
            if st and not self.down.get(n):
                out.append(n)
            self.down[n] = st
        return out


# ------------------------------------------------------------------------------------------------------------------
# the recorder

class Region:
    def __init__(self, name, spec):
        self.name = name
        self.spec = spec
        self.base = None            # resolved address of the object (before "off")
        self.prev = None            # last bytes read
        self.rate = None            # per word: decayed count of recent changes
        self.muted = set()
        self.state = "new"          # new / ok / bad
        self.mask = set()         # object offsets (as reported in the log) whose changes are never logged
        self.hot = False
        self.hit = None           # address found by a scan (regions with "scan" and a numeric "index")
        L = num(spec.get("len"), 0x100) & ~3
        self.len = max(4, min(L, 0x2000))
        self.off = num(spec.get("off"), 0)
        for m in spec.get("mask", []):
            if isinstance(m, str) and "-" in m:
                a, b = (num(x) for x in m.split("-", 1))
            else:
                a = b = num(m)
            for o in range(a & ~3, b + 1, 4):
                self.mask.add(o)


class Recorder:
    def __init__(self, profile_path, host, port, log_path, rate=None):
        self.profile_path = profile_path
        self.host, self.port = host, port
        self.rate_override = rate
        self.cfg = {}
        self.cfg_mtime = 0
        self.cfg_check = 0
        self.rsp = None
        self.regions = {}           # name -> Region (state kept across profile reloads when the spec is unchanged)
        self.scan_hits = {}         # scan name -> [addresses]
        self.scan_jobs = collections.OrderedDict()   # key -> job dict (queued / running)
        self.scan_done_keys = set()
        self.scan_last = {}
        self.hot = Hotkeys()
        self.key_down = {}
        self.t0 = time.time()
        self.log = open(log_path, "a", encoding="utf-8")
        self.log_path = log_path
        self.frame_addr = None
        self.last_frame = None
        self.stat = dict(samples=0, pause=0.0, changes=0, t=time.time(), reads=0)
        self.marks_in_pos = 0
        self.pending_marks = collections.deque()
        self.mark_no = 0
        self.stop = False
        self.outbox = []            # messages printed after the guest has been resumed

    # -- log
    def emit(self, ev, **kw):
        d = {"ev": ev, "t": round(time.time() - self.t0, 3)}
        if self.last_frame is not None:
            d["fr"] = self.last_frame
        d.update(kw)
        self.log.write(json.dumps(d, separators=(",", ":")) + "\n")
        self.log.flush()

    # -- profile
    def load_profile(self, first=False):
        try:
            st = os.stat(self.profile_path)
        except OSError as e:
            if first:
                raise SystemExit("cannot read the profile: %s" % e)
            return
        if st.st_mtime == self.cfg_mtime and st.st_size == getattr(self, "cfg_size", -1):
            return
        try:
            with open(self.profile_path, "r", encoding="utf-8") as f:
                cfg = json.load(f)
        except Exception as e:
            if first:
                raise SystemExit("the profile is not valid JSON: %s" % e)
            say("profile NOT reloaded (%s); the old one stays in use" % e)
            self.cfg_mtime, self.cfg_size = st.st_mtime, st.st_size
            return
        self.cfg_mtime, self.cfg_size = st.st_mtime, st.st_size
        self.cfg = cfg
        self.frame_addr = num(cfg.get("frame_addr"))
        # regions: keep the state of those whose spec is unchanged
        new = {}
        for spec in cfg.get("regions", []):
            if spec.get("off") is None and False:
                pass
            name = spec.get("name") or ("r%d" % len(new))
            if spec.get("disabled"):
                continue
            old = self.regions.get(name)
            if old is not None and json.dumps(old.spec, sort_keys=True) == json.dumps(spec, sort_keys=True):
                new[name] = old
            else:
                new[name] = Region(name, spec)
        # sub-regions of scan templates are created while sampling; keep them while their template is unchanged
        for name, r in self.regions.items():
            base = name.split("@")[0]
            if "@" in name and base in new and base in self.regions and new[base] is self.regions[base]:
                new[name] = r
        self.regions = new
        # scans
        for sc in cfg.get("scans", []):
            key = json.dumps(sc, sort_keys=True)
            if key not in self.scan_done_keys and key not in self.scan_jobs and sc.get("auto", False):
                self.queue_scan(sc)
        self.emit("profile", regions=sorted(self.regions), scans=[s.get("name") for s in cfg.get("scans", [])],
                  rate_hz=self.rate())
        say("profile %s: %d regions, %d scans" % ("loaded" if first else "reloaded", len(self.regions), len(cfg.get("scans", []))))

    def rate(self):
        return float(self.rate_override or self.cfg.get("rate_hz", 15))

    # -- scans (incremental: a slice every sample). One pass over memory looks for all the dwords of a scan at once.
    @staticmethod
    def scan_finds(sc):
        """{name: dword} of a scan spec: "find": {...}, or the old "name" + "dword" form."""
        if "find" in sc:
            return {k: num(v) for k, v in sc["find"].items()}
        return {sc.get("name", "scan"): num(sc["dword"])}

    def queue_scan(self, sc):
        key = json.dumps(sc, sort_keys=True)
        if key in self.scan_jobs:
            return
        lo = num(sc.get("lo"), 0x00100000) & ~0xFFF
        hi = num(sc.get("hi"), num(self.cfg.get("ram_top"), 0x04000000))
        pats = {k: struct.pack("<I", v) for k, v in self.scan_finds(sc).items()}
        self.scan_jobs[key] = dict(sc=sc, cur=lo, hi=hi, pats=pats, hits={k: [] for k in pats}, t0=time.time(), unmapped=0)
        say("scan '%s' queued (%s .. %s): %s" % (sc.get("name"), hx(lo), hx(hi), ", ".join(pats)))

    def scan_slice(self, budget_s):
        if not self.scan_jobs:
            return
        key, job = next(iter(self.scan_jobs.items()))
        end_t = time.time() + budget_s
        align = max(1, num(job["sc"].get("align"), 4))
        while job["cur"] < job["hi"] and time.time() < end_t:
            a = job["cur"]
            n = min(0x800, 0x1000 - (a & 0xFFF), job["hi"] - a)
            d = self.rsp._m(a, n)
            if d is None:
                job["unmapped"] += 1
                job["cur"] = (a | 0xFFF) + 1          # the rest of this page
                continue
            for fname, pat in job["pats"].items():
                i = d.find(pat)
                while i >= 0:
                    if (a + i) % align == 0:
                        job["hits"][fname].append(a + i)
                    i = d.find(pat, i + 1)
            job["cur"] = a + n
        if job["cur"] >= job["hi"]:
            name = job["sc"].get("name", "scan")
            mx = num(job["sc"].get("max_hits"), 64)
            found = {}
            for fname, hits in job["hits"].items():
                self.scan_hits[fname] = hits[:mx]
                found[fname] = [hx(h) for h in hits[:mx]]
            secs = round(time.time() - job["t0"], 1)
            self.emit("scan", name=name, found=found, secs=secs, unmapped_pages=job["unmapped"])
            self.outbox.append("scan '%s' done in %.1f s: %s" % (name, secs, ", ".join("%s x%d" % (k, len(v)) for k, v in found.items())))
            del self.scan_jobs[key]
            self.scan_done_keys.add(key)
            self.scan_last[key] = time.time()

    def run_scans_now(self):
        for sc in self.cfg.get("scans", []):
            self.scan_done_keys.discard(json.dumps(sc, sort_keys=True))
            self.queue_scan(sc)

    def scans_due(self):
        """Periodic rescans ("every_s" in a scan spec)."""
        for sc in self.cfg.get("scans", []):
            ev = sc.get("every_s")
            if not ev:
                continue
            key = json.dumps(sc, sort_keys=True)
            if key not in self.scan_jobs and time.time() - self.scan_last.get(key, 0) >= float(ev):
                self.queue_scan(sc)

    # -- resolving where a region is
    def resolve(self, r, cache):
        sp = r.spec
        if "addr" in sp:
            return num(sp["addr"])
        top = num(self.cfg.get("ram_top"), 0x04000000)
        if "ptr" in sp:
            chain = sp["ptr"] if isinstance(sp["ptr"], list) else [sp["ptr"]]
            a = num(chain[0])
            v = cache.get(a)
            if v is None and a not in cache:
                v = cache[a] = self.rsp.u32(a)
            for o in chain[1:]:
                if v is None or not (0x10000 <= v < top):
                    return None
                a = v + num(o)
                if a not in cache:
                    cache[a] = self.rsp.u32(a)
                v = cache[a]
            return v if v is not None and 0x10000 <= v < top else None
        return None

    def expanded(self):
        """The regions to read now. A region with "scan" is a template: it gets one sub-region per object the scan found
        (index "all", at most "max"; or a numeric index), named name@ADDRESS. "chain" (offsets) follows pointers from the
        object: [0xA00] reads the pointer at object+0xA00 and watches what it points to."""
        out = []
        for name, r in list(self.regions.items()):
            if "@" in name:
                continue
            sp = r.spec
            if "scan" not in sp:
                out.append(r)
                continue
            hits = self.scan_hits.get(sp["scan"], [])
            idx = sp.get("index", 0)
            sel = hits[: num(sp.get("max"), 8)] if idx == "all" else ([hits[num(idx, 0)]] if num(idx, 0) < len(hits) else [])
            for h in sel:
                sub_name = "%s@%X" % (name, h)
                sub = self.regions.get(sub_name)
                if sub is None:
                    s2 = {k: v for k, v in sp.items() if k not in ("scan", "index", "max", "chain")}
                    ch = sp.get("chain")
                    if ch:
                        s2["ptr"] = [hx(h + num(ch[0]))] + list(ch[1:])
                    else:
                        s2["addr"] = hx(h)
                    s2["name"] = sub_name
                    sub = self.regions[sub_name] = Region(sub_name, s2)
                out.append(sub)
        return out

    # -- one sample (guest is stopped)
    def sample_regions(self, why=None, keep=False):
        cache = {}
        snap = {} if (why or keep) else None
        am = self.cfg.get("auto_mute", {})
        use_mute = am.get("on", True)
        hi, lo, decay = float(am.get("hi", 8)), float(am.get("lo", 2)), float(am.get("decay", 0.97))
        if self.frame_addr is not None:
            f = self.rsp.u32(self.frame_addr)
            if f is not None:
                self.last_frame = f
        for r in self.expanded():
            sp = r.spec
            base = self.resolve(r, cache)
            data = None
            if base is not None:
                data = self.rsp.read(base + r.off, r.len)
            if data is None:
                if r.state != "bad":
                    self.emit("unreadable", r=r.name, base=None if base is None else hx(base))
                    r.state = "bad"
                    r.prev = None
                continue
            if r.state == "new" or r.state == "bad" or base != r.base:
                if r.state != "new":
                    self.emit("rebase" if r.state == "ok" else "readable", r=r.name, base=hx(base),
                              old=None if r.base is None else hx(r.base))
                else:
                    self.emit("region", r=r.name, base=hx(base), off=hx(r.off), len=hx(r.len))
                r.base, r.state, r.prev = base, "ok", data
                n = r.len // 4
                r.rate = [0.0] * n
                r.muted = set()
                if snap is not None:
                    snap[r.name] = {"base": hx(base), "off": hx(r.off), "hex": data.hex()}
                continue
            if snap is not None:
                snap[r.name] = {"base": hx(base), "off": hx(r.off), "hex": data.hex()}
            if data == r.prev:
                if use_mute and r.hot:
                    r.rate = [x * decay for x in r.rate]
                    r.hot = max(r.rate) > 0.01
                    if r.muted:
                        self._unmute_quiet(r, lo)
                continue
            n = r.len // 4
            old = struct.unpack("<%dI" % n, r.prev)
            new = struct.unpack("<%dI" % n, data)
            ch = []
            for i in range(n):
                if use_mute:
                    r.rate[i] *= decay
                if old[i] == new[i] or (r.off + i * 4) in r.mask:
                    continue
                if use_mute:
                    r.hot = True
                    r.rate[i] += 1.0
                    if i not in r.muted and r.rate[i] > hi:
                        r.muted.add(i)
                        self.emit("mute", r=r.name, o=hx(r.off + i * 4, 3))
                if (r.off + i * 4) in r.mask or i in r.muted:
                    continue
                e = [hx(r.off + i * 4, 3), hx(old[i]), hx(new[i])]
                fv = as_float(new[i])
                if fv is not None:
                    e.append(fv)
                ch.append(e)
            if use_mute and r.muted:
                self._unmute_quiet(r, lo)
            r.prev = data
            if ch:
                self.stat["changes"] += len(ch)
                self.emit("chg", r=r.name, base=hx(base), c=ch)
        if snap is not None:
            if why:
                self.emit("snap", why=why, regions=snap)
            else:   # kept in memory only; written as "last_state" if the run ends abnormally (the game crashed)
                self.last_snap = (time.time() - self.t0, self.last_frame, snap)

    def _unmute_quiet(self, r, lo):
        """A muted word that has been quiet long enough (its decayed change count fell below lo) is logged again."""
        for i in list(r.muted):
            if r.rate[i] < lo:
                r.muted.discard(i)
                self.emit("unmute", r=r.name, o=hx(r.off + i * 4, 3))

    # -- marks
    def add_mark(self, label):
        self.pending_marks.append(label)

    def poll_marks(self):
        keys = self.cfg.get("marks", {})
        for k in self.hot.pressed(list(keys.keys()) + [self.cfg.get("scan_key", "NUMPAD0")]):
            if k == self.cfg.get("scan_key", "NUMPAD0"):
                self.run_scans_now()
            else:
                self.add_mark(keys[k])
        # lines appended to marks_in.txt next to the profile, or typed in the console
        p = os.path.join(os.path.dirname(os.path.abspath(self.profile_path)), "marks_in.txt")
        try:
            if os.path.exists(p):
                with open(p, "r", encoding="utf-8") as f:
                    f.seek(self.marks_in_pos)
                    chunk = f.read()
                    self.marks_in_pos = f.tell()
                for line in chunk.splitlines():
                    line = line.strip()
                    if line == "!scan":
                        self.run_scans_now()
                    elif line:
                        self.add_mark(line)
        except OSError:
            pass

    # -- main loop
    def run(self):
        self.rsp = Rsp(self.host, self.port)
        self.load_profile(first=True)
        mp = os.path.join(os.path.dirname(os.path.abspath(self.profile_path)), "marks_in.txt")
        self.marks_in_pos = os.path.getsize(mp) if os.path.exists(mp) else 0   # lines from earlier runs are not replayed
        self.emit("start", host=self.host, port=self.port, profile=os.path.abspath(self.profile_path))
        # a mark on stdin: type a line and press Enter
        if sys.stdin and sys.stdin.isatty():
            threading.Thread(target=self._stdin_marks, daemon=True).start()
        say("connected to xemu. Marks: %s ; scan key: %s ; or type a line + Enter. Ctrl+C stops." % (
            ", ".join("%s=%s" % (k, v) for k, v in self.cfg.get("marks", {}).items()) or "(none)", self.cfg.get("scan_key", "NUMPAD0")))
        say("log: " + os.path.abspath(self.log_path))
        self.end_reason = "stopped"
        self.last_keep = 0.0
        try:
            self.rsp.connect_handshake()
            first = True
            next_t = time.time()
            while not self.stop:
                now = time.time()
                if now - self.cfg_check > 0.5:
                    self.cfg_check = now
                    self.load_profile()
                self.poll_marks()
                self.scans_due()
                if now >= next_t or self.pending_marks:
                    t1 = time.time()
                    self.rsp.halt()
                    mark = self.pending_marks.popleft() if self.pending_marks else None
                    if mark is not None:
                        self.mark_no += 1
                    why = "start" if first else None
                    if mark is not None and self.cfg.get("snap_on_mark", True):
                        why = "mark %d: %s" % (self.mark_no, mark)
                    keep = False
                    if now - self.last_keep >= float(self.cfg.get("crash_snap_s", 2.0)):
                        self.last_keep = now; keep = True
                    try:
                        self.sample_regions(why if (first or mark is not None) else None, keep)
                    except GdbError:
                        raise
                    if mark is not None:
                        self.emit("mark", n=self.mark_no, label=mark)
                        self.outbox.append("MARK %d: %s   (frame %s)" % (self.mark_no, mark, self.last_frame))
                    first = False
                    self.scan_slice(float(self.cfg.get("scan_budget_ms", 80)) / 1000.0)
                    self.rsp.resume()
                    for m in self.outbox:
                        say(m)
                    self.outbox = []
                    dt = time.time() - t1
                    self.stat["samples"] += 1
                    self.stat["pause"] += dt
                    ratio = float(self.cfg.get("scan_run_ratio", 3.0)) if self.scan_jobs else float(self.cfg.get("min_run_ratio", 1.0))
                    next_t = max(next_t + 1.0 / self.rate(), time.time() + dt * ratio)
                    if time.time() - self.stat["t"] >= 5.0:
                        self.report_stat()
                else:
                    # sleep a little, noticing a stop that the guest caused by itself
                    ev = self.rsp.poll_async()
                    if ev:
                        say("the guest stopped by itself (%s); resuming it" % ev)
                        self.emit("guest_stop", reply=ev)
                        self.rsp.resume()
                    time.sleep(0.01)
        except KeyboardInterrupt:
            self.end_reason = "stopped by the user (Ctrl+C)"
            raise
        except BaseException as e:
            self.end_reason = "%s: %s" % (type(e).__name__, e)
            say("ERROR - the recorder lost the game: " + self.end_reason)
            raise
        finally:
            if self.end_reason not in ("stopped", "stopped by the user (Ctrl+C)") and getattr(self, "last_snap", None):
                t, fr, sn = self.last_snap
                self.emit("last_state", at=round(t, 3), at_fr=fr, regions=sn)   # the last full snapshot before the end
            self.emit("end", reason=self.end_reason)
            self.rsp.close()
            self.log.close()
            say("stopped; the game was left running. log: " + os.path.abspath(self.log_path))

    def report_stat(self):
        s = self.stat
        el = time.time() - s["t"]
        n = max(1, s["samples"])
        say("%.1f samples/s, %.1f ms stopped per sample (%.0f%% of the time), %d change(s) logged, frame %s" % (
            s["samples"] / el, 1000.0 * s["pause"] / n, 100.0 * s["pause"] / el, s["changes"], self.last_frame))
        self.emit("stat", sps=round(s["samples"] / el, 1), pause_ms=round(1000.0 * s["pause"] / n, 1),
                  stopped_pct=round(100.0 * s["pause"] / el), changes=s["changes"])
        self.stat = dict(samples=0, pause=0.0, changes=0, t=time.time(), reads=0)

    def _stdin_marks(self):
        try:
            for line in sys.stdin:
                line = line.strip()
                if line == "!scan":
                    self.run_scans_now()
                elif line:
                    self.add_mark(line)
        except Exception:
            pass


# ------------------------------------------------------------------------------------------------------------------
# report

def cmd_report(a):
    ev = []
    with open(a.file, "r", encoding="utf-8") as f:
        for line in f:
            try:
                ev.append(json.loads(line))
            except ValueError:
                pass
    marks = [e for e in ev if e["ev"] == "mark"]
    say("%d events, %d marks, last t=%.1f s" % (len(ev), len(marks), ev[-1]["t"] if ev else 0))
    for e in ev:
        if e["ev"] in ("scan", "unreadable", "rebase", "guest_stop"):
            say("  t=%7.2f fr=%s %s %s" % (e["t"], e.get("fr"), e["ev"], {k: v for k, v in e.items() if k not in ("ev", "t", "fr")}))
    cnt = collections.defaultdict(list)
    for e in ev:
        if e["ev"] == "chg":
            for c in e["c"]:
                cnt[(e["r"], c[0])].append((e["t"], c))
    def nearest(t):
        best = None
        for m in marks:
            if m["t"] <= t and (best is None or m["t"] > best["t"]):
                best = m
        return best
    say("")
    say("Words that changed %d time(s) or fewer (r, offset, changes: time after the mark before it / old -> new)" % a.max_changes)
    rows = sorted(((len(v), k) for k, v in cnt.items() if len(v) <= a.max_changes), key=lambda x: (x[1][0], x[0], x[1][1]))
    for n, k in rows[: a.limit]:
        parts = []
        for t, c in cnt[k]:
            m = nearest(t)
            where = ("+%.1fs after #%d '%s'" % (t - m["t"], m["n"], m["label"])) if m else "t=%.1f" % t
            fl = " (%s)" % c[3] if len(c) > 3 else ""
            parts.append("%s: %s->%s%s" % (where, c[1], c[2], fl))
        say("  %-12s %-7s x%d  %s" % (k[0], k[1], n, " | ".join(parts)))
    if len(rows) > a.limit:
        say("  ... %d more (use --limit)" % (len(rows) - a.limit))
    say("")
    say("Busiest words (the noise the auto-mute caught):")
    for k, v in sorted(cnt.items(), key=lambda kv: -len(kv[1]))[:10]:
        say("  %-12s %-7s x%d" % (k[0], k[1], len(v)))


def cmd_peek(a):
    r = Rsp(a.host, a.port)
    try:
        r.connect_handshake()
        addr, n = num(a.addr), num(a.length, 0x40)
        d = r.read(addr, n)
        if d is None:
            say("unreadable")
            return
        for i in range(0, len(d), 16):
            row = d[i:i + 16]
            words = " ".join("%08X" % struct.unpack("<I", row[j:j + 4].ljust(4, b"\0"))[0] for j in range(0, len(row), 4))
            say("%08X: %s" % (addr + i, words))
    finally:
        r.close()


def cmd_graph(a):
    """Find objects by class (vtable pointer) and id, and dump them with everything their pointers lead to (breadth first), as text.
    Needs no addresses: works after any level load. The guest is stopped for the few seconds it takes (a scan of all RAM)."""
    classes = [int(x, 16) for x in a.classes.split(",") if x.strip()]
    ids = set(int(x, 16) for x in a.ids.split(",") if x.strip()) if a.ids else None
    lo, hi, ram_top = num(a.lo), num(a.hi), num(a.ram_top)
    r = Rsp(a.host, a.port)
    try:
        r.connect_handshake()
        say("scanning %#x..%#x for %s ..." % (lo, hi, ", ".join("%#x" % c for c in classes)))
        pats = {c: struct.pack("<I", c) for c in classes}
        roots, cur, t0 = [], lo, time.time()
        while cur < hi:
            n = min(0x800, 0x1000 - (cur & 0xFFF), hi - cur)
            d = r._m(cur, n)
            if d is None:
                cur = (cur | 0xFFF) + 1
                continue
            for c, pat in pats.items():
                i = d.find(pat)
                while i >= 0:
                    if (cur + i) % 4 == 0:
                        roots.append((cur + i, c))
                    i = d.find(pat, i + 1)
            cur += n
        say("%d object(s) of those classes (%.1f s)" % (len(roots), time.time() - t0))
        keep = []
        for addr, c in roots:
            oid = r.u32(addr + 4)
            if ids is None or oid in ids:
                keep.append((addr, c, oid))
        say("kept %d: %s" % (len(keep), ", ".join("%X(id %X)" % (k[0], k[2] or 0) for k in keep[:20])))
        nodes, order, queue = {}, [], []
        for addr, c, oid in keep:
            queue.append((addr, 0, None, None))
        while queue and len(order) < a.max_nodes:
            addr, depth, parent, via = queue.pop(0)
            if addr in nodes:
                continue
            data = r.read(addr, a.len)
            if data is None:
                continue
            words = struct.unpack("<%dI" % (len(data) // 4), data)
            if depth > 0 and words[0] in classes:      # a sibling of the same class (the level's linked list): not followed
                continue
            nodes[addr] = (depth, parent, via, words)
            order.append(addr)
            if depth < a.depth:
                for i, w in enumerate(words):
                    if lo <= w < ram_top and w % 4 == 0 and w not in nodes:
                        queue.append((w, depth + 1, addr, i * 4))
        out = []
        for addr in order:
            depth, parent, via, words = nodes[addr]
            out.append("NODE %08X  depth %d%s" % (addr, depth, "" if parent is None else "  from %08X+%03X" % (parent, via)))
            for i in range(0, len(words), 8):
                cells = []
                for j, w in enumerate(words[i:i + 8]):
                    tag = "->" if (w in nodes and w != addr) else " "
                    cells.append("%s%08X" % (tag, w))
                fl = [as_float(w) for w in words[i:i + 8]]
                out.append("  %03X " % (i * 4) + " ".join(cells) + ("   " + " ".join("%g" % f for f in fl if f is not None and abs(f) > 1e-3 and abs(f) < 1e6) if any(f is not None for f in fl) else ""))
            out.append("")
        here = os.path.dirname(os.path.abspath(__file__))
        os.makedirs(os.path.join(here, "logs"), exist_ok=True)
        path = a.out or os.path.join(here, "logs", "graph_%s.txt" % time.strftime("%Y%m%d_%H%M%S"))
        with open(path, "w") as f:
            f.write("\n".join(out))
        say("%d node(s) written to %s" % (len(order), path))
    finally:
        r.close()


def probe_lines(p, ver):
    """The costume probe (mod version 25+): which character bodies each resource scene declares, by class and costume (meshes.json is made from the XBE's own lists).
    A scene that also says yes to a path nobody has is marked: it says nothing about the level."""
    if ver < 25 or ver > 28:
        return []                      # the probe only existed in versions 25 to 28
    here = os.path.dirname(os.path.abspath(__file__))
    try:
        with open(os.path.join(here, "meshes.json")) as f:
            tab = json.load(f)
    except OSError:
        return ["costume probe: meshes.json is missing next to xbwatch.py"]
    meshes = tab["meshes"]
    asked = (p[16] >> 8) & 0xFF
    out = ["costume probe: %d scene(s) found, %d meshes asked   player 1: costume %d texture set %d" % (p[17], asked, p[18], p[19])]
    shown = {}
    for i in range(4):
        addr = p[12 + i]
        if not addr:
            continue
        mask = p[3 * i] | (p[3 * i + 1] << 32) | (p[3 * i + 2] << 64)
        have = frozenset(m.lower() for j, m in enumerate(meshes) if j < asked and (mask >> j) & 1)
        bogus = (p[16] >> i) & 1
        out.append("  scene %d at %#x: %d of %d meshes declared%s" % (i, addr, len(have), asked,
                   "   <- says yes to a path nobody has: ignore" if bogus else ""))
        if not bogus:
            shown.setdefault(have, []).append(i)
    if not shown:
        out.append("  no usable scene")
        return out
    for have, scenes in shown.items():
        out.append("  --- bodies declared by scene(s) %s ---" % ",".join(str(i) for i in scenes))
        for c in tab["classes"]:
            vs = c["variants"]
            if not any(v["mesh"].lower() in have for v in vs):
                continue
            parts = ["%d %s%s" % (v["i"], v["name"], "" if v["mesh"].lower() in have else " (NOT)") for v in vs]
            out.append("  %s: %s" % (c["header"] or vs[0]["name"], "; ".join(parts)))
        gone = [(c["header"] or c["variants"][0]["name"]) for c in tab["classes"] if not any(v["mesh"].lower() in have for v in c["variants"])]
        out.append("  classes with no body here: " + (", ".join(gone) or "none"))
    return out


def cmd_levelid(a):
    """Reads what the co-op mod found when the level started (cutscene ids, level key) and appends it to logs\\levelids.txt."""
    base = num(a.base)
    r = Rsp(a.host, a.port)
    try:
        r.connect_handshake()
        head = r.read(base, 0x40)
        blk = r.read(base + 0x440, 0xC0)
        lv = r.read(base + 0x2A0, 0x50)
        lvx = r.read(base + 0x2F0, 0x10)
        sub9 = r.u32(base + 0x324)
        if head is None or blk is None or struct.unpack_from("<I", head, 0)[0] != 0x504F4F43:
            raise SystemExit("the co-op debug block is not at %#x (is the game running with the mod?)" % base)
        w = struct.unpack("<48I", blk)
        ver, frames, state = struct.unpack_from("<III", head, 4)
        ids = [x for x in w[4:28] if x]
        counts = {k: w[28 + k] for k in range(20) if w[28 + k]}
        probe = struct.unpack("<20I", lv)
        lines = [
            "time %s  mod version %d  frame %d  state %d" % (time.strftime("%Y-%m-%d %H:%M:%S"), ver, frames, state),
            "level key from the cutscene ids: %s   (0xFF = not recognised)   key the sweep decided: %s" % (hex(w[0]), hex(sub9)),
            "cutscenes found: %d   multi triggers found: %d   scans done: %d" % (w[1], w[2], w[3]),
            "cutscene ids: " + " ".join("%X" % x for x in ids),
            "signature ids matched per level key: %s" % (counts or "none"),
        ]
        lines += probe_lines(probe, ver)
        here = os.path.dirname(os.path.abspath(__file__))
        os.makedirs(os.path.join(here, "logs"), exist_ok=True)
        path = os.path.join(here, "logs", "levelids.txt")
        if a.note:
            lines.insert(0, "LEVEL: " + a.note)
        with open(path, "a") as f:
            f.write("\n".join(lines) + "\n\n")
        say("\n".join(lines))
        say("appended to " + path)
    finally:
        r.close()


def cmd_headroom(a):
    """Reads the headroom table of the headroom diagnostic build (free memory per level) and prints it; appends it to logs\\headroom.txt."""
    r = Rsp(a.host, a.port)
    try:
        r.connect_handshake()
        blk = r.read(num(a.base), 19 * 4 + 22 * 36)
        if blk is None or struct.unpack_from("<I", blk, 0)[0] != 0x44414548:
            raise SystemExit("the headroom block is not at %s (is this the headroom build, and is the game running?)" % a.base)
        h = struct.unpack_from("<19I", blk, 0)
        kb = lambda p: ("%d KB" % (p * 4)) if p else "-"
        lines = ["time %s   console total %s   free right now %s   lowest ever %s (slot %d)   most free in menus %s" % (
                 time.strftime("%Y-%m-%d %H:%M:%S"), kb(h[6]), kb(h[9]), kb(h[15]), h[16], kb(h[17])),
                 "level   first(after load)   lowest   highest   P2 before -> after (P2 lowest)"]
        for i in range(22):
            first, mn, mx, last, p2b, p2a, p2m = struct.unpack_from("<7I", blk, 76 + i * 36 + 4)
            n = struct.unpack_from("<I", blk, 76 + i * 36)[0]
            if not n:
                continue
            name = "L%d" % i if i < 20 else ("L?" if i == 20 else "menu")
            row = "%-5s  %-16s  %-8s %-8s" % (name, kb(first), kb(mn), kb(mx))
            if p2a:
                row += "  %s -> %s (%s)" % (kb(p2b), kb(p2a), kb(p2m))
            lines.append(row)
        here = os.path.dirname(os.path.abspath(__file__))
        os.makedirs(os.path.join(here, "logs"), exist_ok=True)
        with open(os.path.join(here, "logs", "headroom.txt"), "a") as f:
            f.write("\n".join(lines) + "\n\n")
        say("\n".join(lines))
    finally:
        r.close()


def cmd_ramdump(a):
    """Saves all of the guest's RAM (64 MB) to logs\\ram_<note>.bin.gz so it can be studied offline. Read only; the game is stopped for the
    few seconds it takes and runs on afterwards. Unreadable pages are stored as zeros."""
    import gzip
    top = num(a.top)
    here = os.path.dirname(os.path.abspath(__file__))
    os.makedirs(os.path.join(here, "logs"), exist_ok=True)
    name = "ram_%s_%s.bin.gz" % (re.sub(r"[^A-Za-z0-9]+", "_", a.note or "dump").strip("_"), time.strftime("%Y%m%d_%H%M%S"))
    path = os.path.join(here, "logs", name)
    r = Rsp(a.host, a.port)
    try:
        r.connect_handshake()
        say("saving %d MB of RAM to %s ..." % (top >> 20, path))
        t0, bad, cur = time.time(), 0, 0
        with gzip.open(path, "wb", compresslevel=3) as f:
            while cur < top:
                n = min(0x800, top - cur)
                d = r._m(cur, n)
                if d is None or len(d) != n:
                    d = b"\0" * n
                    bad += 1
                f.write(d)
                cur += n
        say("done in %.1f s (%d unreadable chunks): %s  (%.1f MB)" % (time.time() - t0, bad, path, os.path.getsize(path) / 1048576.0))
    finally:
        r.close()


def cmd_scriptid(a):
    """Reads the script recorder of mod version 34+ (what asked for player two's buttons, and the script instance bytes) and appends it to logs\\scripts.txt."""
    base = num(a.base)
    r = Rsp(a.host, a.port)
    try:
        r.connect_handshake()
        head = r.read(base, 0x40)
        if head is None or struct.unpack_from("<I", head, 0)[0] != 0x504F4F43:
            raise SystemExit("the co-op debug block is not at %#x (is the game running with the mod?)" % base)
        ver, frames, state = struct.unpack_from("<III", head, 4)
        if ver < 34:
            raise SystemExit("this game has mod version %d; the recorder in this form is in version 34 and later" % ver)
        chars = r.read(base + 0x500, 3 * 592)
        regs = r.read(base + 0xBF0, 16 * 24)
        if chars is None or regs is None:
            raise SystemExit("cannot read the recorder at %#x" % (base + 0x500))
        lines = ["time %s  mod version %d  frame %d  state %d" % (time.strftime("%Y-%m-%d %H:%M:%S"), ver, frames, state)]
        if a.note:
            lines.insert(0, "NOTE: " + a.note)
        found = 0
        for i in range(3):
            e = chars[i * 592:(i + 1) * 592]
            inst, hits = struct.unpack_from("<II", e, 8)
            if not hits:
                continue
            found += 1
            name = e[0:8].split(b"\0")[0].decode("ascii", "replace")
            first, last = e[16:16 + 0x120], e[16 + 0x120:16 + 0x240]
            lines.append("")
            lines.append("character %s  queries %d  script instance %#x" % (name, hits, inst))
            for k in range(16):
                lo, hi, h, i0, i1, chr_ = struct.unpack_from("<6I", regs, k * 24)
                if chr_ != i + 1:
                    continue
                ids = [n for n in range(64) if ((i0, i1)[n >> 5] >> (n & 31)) & 1]
                lines.append("  code %#x..%#x  queries %d  button ids: %s" % (lo, hi, h, " ".join("%X" % n for n in ids)))
            lines.append("instance bytes at the first query (offset: 16 bytes):")
            for o in range(0, 0x120, 16):
                lines.append("  %03X: %s" % (0x1D0 + o, " ".join("%02X" % b for b in first[o:o + 16])))
            ch = ["%03X %02X->%02X" % (0x1D0 + k, first[k], last[k]) for k in range(0x120) if first[k] != last[k]]
            lines.append("bytes that changed by the later copy: " + (" ".join(ch) if ch else "none"))
            lines.append("flag bytes 2D8..2E7 now: " + " ".join("%02X" % b for b in last[0x108:0x118]) + "   marker 1D6: %02X" % last[0x06])
        if not found:
            lines.append("nothing recorded yet: player two has not played a character since the game started")
        here = os.path.dirname(os.path.abspath(__file__))
        os.makedirs(os.path.join(here, "logs"), exist_ok=True)
        path = os.path.join(here, "logs", "scripts.txt")
        with open(path, "a") as f:
            f.write("\n".join(lines) + "\n\n")
        say("\n".join(lines))
        say("appended to " + path)
    finally:
        r.close()


def cmd_resume(a):
    r = Rsp(a.host, a.port)
    r.connect_handshake()
    r.close()
    say("sent 'continue': the game should be running")


def main():
    ap = argparse.ArgumentParser(description="xbwatch: memory flight recorder for xemu")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("run")
    p.add_argument("profile")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=1234)
    p.add_argument("--log", default=None)
    p.add_argument("--rate", type=float, default=None, help="samples per second (overrides the profile)")
    p = sub.add_parser("report")
    p.add_argument("file")
    p.add_argument("--max-changes", type=int, default=6)
    p.add_argument("--limit", type=int, default=80)
    p = sub.add_parser("peek")
    p.add_argument("addr")
    p.add_argument("length", nargs="?", default="0x40")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=1234)
    p = sub.add_parser("graph", help="dump objects found by class and id, with the objects their pointers lead to")
    p.add_argument("--classes", required=True, help="vtable addresses, comma separated (e.g. 0x58B3F0 for ITriggerMulti)")
    p.add_argument("--ids", default="", help="only objects whose id (word at +4) is in this comma separated list; empty: all")
    p.add_argument("--depth", type=int, default=2)
    p.add_argument("--len", type=int, default=0x100, help="bytes read per object")
    p.add_argument("--max-nodes", type=int, default=400)
    p.add_argument("--lo", default="0x977000")
    p.add_argument("--hi", default="0x04000000")
    p.add_argument("--ram-top", default="0x04000000")
    p.add_argument("--out", default=None)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=1234)
    p = sub.add_parser("levelid", help="print what the co-op mod found when the level started (cutscene ids, level key) and add it to logs/levelids.txt")
    p.add_argument("note", nargs="?", default="", help="which level this is, e.g. 'level 2'")
    p.add_argument("--base", default="0x976000")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=1234)
    p = sub.add_parser("scriptid", help="print what the mod recorded about the Force script of the character player two plays (mod version 32+) and add it to logs/scripts.txt")
    p.add_argument("note", nargs="?", default="", help="who and where, e.g. 'Serra, level 12, after trying saber throw'")
    p.add_argument("--base", default="0x976000")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=1234)
    p = sub.add_parser("headroom", help="print the free-memory table of the headroom diagnostic build")
    p.add_argument("--base", default="0x976D80")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=1234)
    p = sub.add_parser("ramdump", help="save all of RAM (read only) to logs/ram_<note>_<time>.bin.gz")
    p.add_argument("note", nargs="?", default="", help="what is going on, e.g. 'level7_p2_in'")
    p.add_argument("--top", default="0x4000000")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=1234)
    p = sub.add_parser("resume", help="let a game that was left stopped run again")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=1234)
    a = ap.parse_args()
    if a.cmd == "resume":
        try:
            return cmd_resume(a)
        except GdbError as e:
            raise SystemExit(str(e))
    if a.cmd == "report":
        return cmd_report(a)
    if a.cmd == "levelid":
        try:
            return cmd_levelid(a)
        except GdbError as e:
            raise SystemExit(str(e))
    if a.cmd == "ramdump":
        try:
            return cmd_ramdump(a)
        except GdbError as e:
            raise SystemExit(str(e))
    if a.cmd == "headroom":
        try:
            return cmd_headroom(a)
        except GdbError as e:
            raise SystemExit(str(e))
    if a.cmd == "scriptid":
        try:
            return cmd_scriptid(a)
        except GdbError as e:
            raise SystemExit(str(e))
    if a.cmd == "graph":
        try:
            return cmd_graph(a)
        except GdbError as e:
            raise SystemExit(str(e))
    if a.cmd == "peek":
        try:
            return cmd_peek(a)
        except GdbError as e:
            raise SystemExit(str(e))
    here = os.path.dirname(os.path.abspath(__file__))
    log = a.log
    if not log:
        os.makedirs(os.path.join(here, "logs"), exist_ok=True)
        log = os.path.join(here, "logs", "trace_%s.jsonl" % time.strftime("%Y%m%d_%H%M%S"))
    rec = Recorder(a.profile, a.host, a.port, log, a.rate)
    import signal
    def _stop(*_):
        rec.stop = True
    for sg in ("SIGTERM", "SIGBREAK"):
        if hasattr(signal, sg):
            signal.signal(getattr(signal, sg), _stop)
    try:
        rec.run()
    except GdbError as e:
        raise SystemExit("error: %s" % e)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

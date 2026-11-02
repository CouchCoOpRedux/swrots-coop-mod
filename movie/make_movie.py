#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""legal_screen.png -> 300 frames (1 s fade in, hold, 1 s fade out) -> MPEG-1 video stream
../patcher/legal_screen.m1v (the patcher turns that into the game's .sfd).
needs: python3, numpy, opencv-python, ffmpeg on the PATH.   python3 make_movie.py"""
import os, subprocess, tempfile, cv2, numpy as np
here = os.path.dirname(os.path.abspath(__file__))
img = cv2.imread(os.path.join(here, "legal_screen.png"))
N, FI, FO = 300, 30, 30
def smooth(t): t = min(1, max(0, t)); return t * t * (3 - 2 * t)
with tempfile.TemporaryDirectory() as tmp:
    for n in range(N):
        k = smooth((n + 1) / FI) if n < FI else (smooth((N - 1 - n) / FO) if n >= N - FO else 1.0)
        cv2.imwrite(os.path.join(tmp, "f_%03d.png" % n), (img.astype(np.float32) * k).round().astype(np.uint8))
    out = os.path.join(here, "..", "patcher", "legal_screen.m1v")
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-framerate", "30000/1001", "-i", os.path.join(tmp, "f_%03d.png"),
        "-vf", "scale=in_range=full:out_range=tv:out_color_matrix=bt601:flags=accurate_rnd+full_chroma_int,setsar=200/219,format=yuv420p",
        "-c:v", "mpeg1video", "-g", "15", "-bf", "4", "-b_strategy", "0", "-sc_threshold", "1000000000", "-q:v", "3",
        "-maxrate", "104857200", "-bufsize", "917504", "-f", "mpeg1video", out], check=True)
print("wrote", os.path.normpath(out))

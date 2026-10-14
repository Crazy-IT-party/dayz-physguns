#!/usr/bin/env python3
"""PhysGuns sounds, synthesised here (no samples from anywhere).

    python3 -I tools/build_sounds.py        -> mod/PhysGuns/data/sound/*.ogg

The gravity gun:
  hum    - a low electric drone (loops): mains-like buzz on 55 Hz with crackle, while it holds or pulls
  howl   - a wavering whine (loops): the script raises its volume and pitch as the beam swings
  grab   - a rising "vwoom" with a crackle, when it catches something
  drop   - the same, falling, when it lets go
  punt   - a thump under an electric snap
  launch - a bigger punt, for throwing what it holds
  dry    - a short snap: fired at nothing
The portal gun:
  portal_fire - its shot: a rising resonant "pew" over an airy whoosh
  portal_open - a portal opening where the shot lands: a soft low "whomp"
  portal_hum  - a portal's own sound (loops): low static, rumble and a slow throb
Loops are made of whole cycles and circular noise, so they repeat without a click. Needs ffmpeg with libvorbis.
"""
from __future__ import annotations

import shutil
import subprocess
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "mod/PhysGuns/data/sound"
BUILD = ROOT / "build/sound"
SR = 44100
RNG = np.random.default_rng(42)
FFMPEG = shutil.which("ffmpeg", path="/usr/bin") or shutil.which("ffmpeg")


def t_axis(sec):
    return np.arange(int(SR * sec)) / SR


def band_noise(n, lo, hi):
    """circular (loops seamlessly) noise between lo and hi Hz"""
    spec = np.fft.rfft(RNG.standard_normal(n))
    f = np.fft.rfftfreq(n, 1 / SR)
    spec[(f < lo) | (f > hi)] = 0
    x = np.fft.irfft(spec, n)
    return x / (np.abs(x).max() + 1e-9)


def sweep(f0, f1, sec, shape=1.0):
    t = t_axis(sec)
    k = (t / sec) ** shape
    f = f0 + (f1 - f0) * k
    return np.cumsum(f) / SR * 2 * np.pi


def env(n, attack, release, sec):
    t = np.arange(n) / SR
    a = np.clip(t / max(attack, 1e-4), 0, 1)
    r = np.clip((sec - t) / max(release, 1e-4), 0, 1)
    return a * r


def crackle(n, rate, width=0.002):
    """sparse electric clicks"""
    x = np.zeros(n)
    k = RNG.random(n) < rate / SR
    idx = np.nonzero(k)[0]
    w = int(SR * width)
    burst = RNG.standard_normal(w) * np.exp(-np.arange(w) / (w / 4))
    for i in idx:
        j = min(n, i + w)
        x[i:j] += burst[: j - i] * RNG.uniform(0.3, 1.0)
    return x


def hum():
    sec = 2.0
    t = t_axis(sec)
    n = len(t)
    f0 = 55.0                                                   # 110 whole cycles in 2 s
    x = (np.sin(2 * np.pi * f0 * t) * 0.55 + np.sin(2 * np.pi * 2 * f0 * t) * 0.35
         + np.sin(2 * np.pi * 3 * f0 * t) * 0.18 + np.sign(np.sin(2 * np.pi * 2 * f0 * t)) * 0.06)
    x *= 1 + 0.25 * np.sin(2 * np.pi * 3.0 * t)                 # a slow throb, 6 cycles
    x += band_noise(n, 80, 900) * 0.18 * (1 + 0.5 * np.sin(2 * np.pi * 1.5 * t))
    x += band_noise(n, 2000, 7000) * 0.05
    c = crackle(n, 14)
    x += c * 0.25
    return x, True


def howl():
    sec = 2.0
    t = t_axis(sec)
    n = len(t)
    f0 = 180.0                                                  # 360 cycles; the vibrato is whole cycles too
    vib = 0.06 * np.sin(2 * np.pi * 5.0 * t) + 0.03 * np.sin(2 * np.pi * 1.5 * t)
    ph = 2 * np.pi * f0 * t + 2 * np.pi * f0 * np.cumsum(vib) / SR
    ph -= (ph[-1] + 2 * np.pi * f0 / SR - 2 * np.pi * f0 * sec) * np.arange(n) / n     # end on whole cycles
    x = np.sin(ph) * 0.6 + np.sin(2 * ph) * 0.3 + np.sin(3 * ph + 0.5) * 0.18 + np.sin(5 * ph) * 0.06
    # a vocal "oo-wah" formant wobble
    x *= 0.75 + 0.25 * np.sin(2 * np.pi * 2.5 * t)
    x += band_noise(n, 300, 2500) * 0.12
    return x, True


def vwoom(rise: bool):
    sec = 0.55 if rise else 0.4
    n = int(SR * sec)
    ph = sweep(70, 340, sec, 0.6) if rise else sweep(320, 55, sec, 0.5)
    x = np.sin(ph) * 0.7 + np.sin(2 * ph) * 0.25 + np.sign(np.sin(ph)) * 0.08
    x += band_noise(n, 1500, 8000) * 0.12 + crackle(n, 60) * 0.35
    return x * env(n, 0.02 if rise else 0.005, 0.25, sec), False


def punt(big: bool):
    sec = 0.9 if big else 0.7
    n = int(SR * sec)
    t = t_axis(sec)
    thump = np.sin(sweep(150 if big else 120, 38, sec, 0.35)) * np.exp(-t / (0.22 if big else 0.16))
    snap = band_noise(n, 1200, 9000) * np.exp(-t / 0.035)
    zap = np.sign(np.sin(sweep(2600, 260, 0.18, 0.5)))
    zap = np.pad(zap, (0, n - len(zap))) * np.exp(-t / 0.06) * 0.35
    ring = np.sin(2 * np.pi * 95 * t) * np.exp(-t / 0.3) * 0.2
    x = thump * 1.0 + snap * 0.55 + zap + ring + crackle(n, 80) * 0.2 * np.exp(-t / 0.2)
    return x * env(n, 0.001, 0.08, sec), False


def dry():
    sec = 0.3
    n = int(SR * sec)
    t = t_axis(sec)
    x = band_noise(n, 1500, 9000) * np.exp(-t / 0.02) * 0.6
    x += np.sign(np.sin(sweep(1800, 400, sec, 0.5))) * np.exp(-t / 0.04) * 0.25
    x += np.sin(sweep(160, 70, sec)) * np.exp(-t / 0.05) * 0.5
    return x * env(n, 0.001, 0.05, sec), False


def portal_fire():
    sec = 0.6
    n = int(SR * sec)
    t = t_axis(sec)
    ph = sweep(260, 1250, sec, 0.45)
    tone = (np.sin(ph) * 0.6 + np.sin(2 * ph) * 0.25 + np.sin(3 * ph + 0.3) * 0.1) * np.exp(-t / 0.2)
    wob = 1 + 0.35 * np.sin(2 * np.pi * 28 * t)                 # a fast warble
    whoosh = band_noise(n, 600, 5000) * np.exp(-((t - 0.08) / 0.12) ** 2) * 0.45
    thud = np.sin(sweep(140, 60, sec, 0.4)) * np.exp(-t / 0.07) * 0.5
    x = tone * wob + whoosh + thud + crackle(n, 50) * 0.15 * np.exp(-t / 0.15)
    return x * env(n, 0.004, 0.15, sec), False


def portal_open():
    sec = 0.8
    n = int(SR * sec)
    t = t_axis(sec)
    whomp = np.sin(sweep(210, 45, sec, 0.35)) * np.exp(-t / 0.25)
    splat = band_noise(n, 150, 2500) * np.exp(-t / 0.08) * 0.5
    shimmer = np.sin(sweep(900, 500, sec, 0.6)) * np.exp(-t / 0.3) * 0.12
    return (whomp + splat + shimmer) * env(n, 0.003, 0.2, sec), False


def portal_hum():
    sec = 3.0
    t = t_axis(sec)
    n = len(t)
    x = band_noise(n, 20, 160) * 0.8                             # low static rumble
    x += band_noise(n, 160, 700) * 0.18 * (1 + 0.6 * np.sin(2 * np.pi * 2 / 3 * t))   # 2 throbs in 3 s
    x += (np.sin(2 * np.pi * 40 * t) * 0.35 + np.sin(2 * np.pi * 80 * t) * 0.12) * (1 + 0.3 * np.sin(2 * np.pi * 1 / 3 * t))
    x += crackle(n, 9, 0.004) * 0.18
    return x, True


def write(name, x, loop):
    x = x / (np.abs(x).max() + 1e-9) * 0.89
    if not loop:
        x[-64:] *= np.linspace(1, 0, 64)
    BUILD.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    wav = BUILD / f"{name}.wav"
    with wave.open(str(wav), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((x * 32767).astype("<i2").tobytes())
    subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-i", str(wav), "-c:a", "libvorbis", "-q:a", "5",
                    "-fflags", "+bitexact", "-flags:a", "+bitexact", "-serial_offset", "1",
                    str(OUT / f"{name}.ogg")], check=True)
    print(f"{name}.ogg {len(x) / SR:.2f}s")


def main():
    for name, (x, loop) in {"hum": hum(), "howl": howl(), "grab": vwoom(True), "drop": vwoom(False),
                            "punt": punt(False), "launch": punt(True), "dry": dry(),
                            "portal_fire": portal_fire(), "portal_open": portal_open(), "portal_hum": portal_hum()}.items():
        write(name, x, loop)


if __name__ == "__main__":
    main()

# CHOMPI — ORGAN (experimental)

A tonewheel organ firmware for **CHOMPI**, built on the WAVE firmware's hardware, MIDI,
sequencer and preset plumbing.

> Not yet run on hardware. It builds with the recommended toolchain, fits in memory with room
> to spare, and the DSP has been rendered and measured on a desktop, but expect a first
> flash to need some tuning.

---

## What it models

```
91 tonewheels ─> 9 drawbar busbars ─> + percussion ─> vibrato/chorus scanner
   ─> cabinet preamp (drive) ─> 800 Hz crossover ─> rotating horn + rotating drum
   ─> two virtual mics (L/R) ─> reverb ─> limiter ─> outs
```

- **Generator**: 91 wheels at the console gear ratios (20 rev/s motor, so the tuning is
  the slightly-off-equal-temperament of the real thing). Wheels are near-sine, taper off
  toward the top, and each has a fixed ±0.5 dB level variation.
- **Busbars**: one 61-key manual (C–C). 16' folds back on the lowest octave, everything
  past wheel 91 folds down. Wheel gains are only recomputed when a key or drawbar changes,
  so the audio loop costs the same for one key or sixty.
- **Key click**: each key's nine contacts close at slightly different times (scattered over
  up to ~1.5 ms), and the level steps instead of ramping, plus a short filtered noise burst.
  One knob sets how much.
- **Leakage**: each playing wheel bleeds faintly into the wheel four octaves away.
- **Percussion**: single-trigger (only re-fires once every key is up), 2nd or 3rd harmonic,
  normal/soft, fast (~1 s) / slow (~4 s) decay. Turning it on takes over the 1' drawbar and
  drops the rest of the manual ~3 dB at normal volume, like the console.
- **Scanner**: V1–V3 and C1–C3 at 6.86 Hz.
- **Rotary cabinet**: separate horn and drum rotors with their own spin-up/spin-down
  (horn ~1 s, drum a few seconds), Doppler, amplitude and horn brightness that follow the
  rotor angle, plus cabinet reflections. Slow / fast / brake.

## Controls

Knob names below are WAVE's: **pitch, attack, release, magic, tempo, volume**.

### Play layer

| Control | Function |
|---|---|
| Pitch, attack, release | Drawbars, three at a time (pitch = first drawbar of the bank, attack = second, release = third). **Click any of them** to step the bank:<br>A = 16' · 5⅓' · 8'  →  B = 4' · 2⅔' · 2'  →  C = 1⅗' · 1⅓' · 1'<br>One detent = one drawbar step (0–8). LED colour = drawbar colour (brown / white / black shown as blue), brightness = level. LEDs flash white on a bank change. |
| Magic | Page 1 **drive**, page 2 **reverb**. Click flips page. |
| Tempo | Sequencer tempo, click = tap tempo |
| Volume | Page 1 **volume** (VU meter), page 2 **rotary mix** (0 = direct, no cabinet). Click flips page, hold shows battery. |
| Toggle switch | Rotary speed: **up = slow, down = fast**. Acts when flipped, so a MIDI mod wheel can override it until the next flip. |
| CHOMPI key | Hold for the shift layer. While recording a sequence it inserts a rest instead. The LED pulses with the horn. |
| Play / Loop | Sequencer, same as WAVE (gate is fixed at full length so lines stay legato) |
| Keys | Notes. Default range is C3–C5 at 8' pitch (same MIDI notes as WAVE: 48–72). |

### Shift layer (hold CHOMPI)

| Control | Function |
|---|---|
| Pitch | Vibrato/chorus: off, V1, V2, V3, C1, C2, C3 (LED blue for V, pink for C, brighter = deeper) |
| Attack | Key click amount |
| Release | Tonewheel leakage |
| Magic | Rotary balance, drum (orange) ↔ horn (blue) |
| Tempo | Clock division (click resets tempo) |
| Volume | Transpose ±12 semitones |
| Knob clicks | Reset that knob's shift function |
| Black keys, left → right | Octave down · Octave up · **Perc on** · **Perc soft** · **Perc fast** · **Perc third** · **Rotor brake** · Erase · Copy · Save |
| White keys | Preset slots 1–14; the last white key is the defaults slot |

Percussion keys light yellow when the tab is "down". Octave keys light dim for ±1, bright for ±2.

### Presets

Presets live in `organ_presets.json` on the card. If the file isn't there, the firmware
writes a factory set on first boot:

| Slot | Drawbars | Notes |
|---|---|---|
| 1 | 888000000 | 3rd percussion, fast, C3 |
| 2 | 888800000 | C3 |
| 3 | 838000000 | V2, light drive |
| 4 | 808808008 | C3 |
| 5 | 688600000 | C3, more reverb |
| 6 | 006876400 | V1 |
| 7 | 008080800 | no scanner |
| 8 | 800000008 | C3 |
| 9 | 888000000 | 2nd percussion, soft, slow, V3 |
| 10 | 886000000 | 3rd percussion, no scanner, more drive |
| 11 | 888888888 | full |
| 12 | 888888888 | full, heavy drive |
| 13 | 008000000 | pure 8', direct out (no cabinet) |
| 14 | 886400000 | 2nd percussion, C1 |

A preset stores drawbars, vibrato, percussion tabs, drive, reverb, rotary mix, click,
leakage and rotary balance. Volume, tempo, octave, transpose and rotor speed are left alone.
Delete the file to get the factory set back.

## MIDI

Notes 21–108 are accepted (velocity ignored); anything past the 61-key manual folds back
by octaves. Same in/out channel settings as WAVE (`options.json`).

| CC | Function |
|---|---|
| 1 | Rotary: ≥ 64 fast, < 64 slow |
| 11 | Expression (swell) |
| 12 | Vibrato/chorus, swept across off · V1 · V2 · V3 · C1 · C2 · C3 |
| 13 | Rotor brake (≥ 64 on) |
| 20–22 | The three drawbar knobs (current bank) |
| 23 | Magic knob, whichever page is showing (sent as 23 for drive, 29 for reverb) |
| 25 | Volume knob, whichever page is showing (sent as 25 for volume, 30 for rotary mix) |
| 70–78 | Drawbars 16' … 1' (also sent when you turn a drawbar knob) |
| 80–83 | Percussion on / soft / fast / third (≥ 64 = tab down) |
| 123 | All notes off |

## Building

Same toolchain and steps as the other firmwares (see [`../README.md`](../README.md)), with
**GNU Arm Embedded 10.3-2021.10**:

```bash
cd code/src
make
```

This folder doesn't carry its own copy of the libraries: the Makefile points at
`../chompi-wave/code/libs` (and WAVE's bootloader in `../chompi-wave/bin`), so keep the
WAVE folder alongside it. Put `build/CHOMPI.bin` on the card as the only `.bin` file.

Memory with GCC 10.3.1: 178 KB of 232 KB code space (WAVE uses 204 KB), 149 KB of 280 KB SRAM.

## Where things are

```
ToneWheelOrgan.h   the DSP: generator, busbars, percussion, click, scanner, preamp, rotary
organEngine.h      CHOMPI wrapper (KeyRequest queue, reverb, limiter, parameter API)
NormalPage.h       play layer
MenuPage.h         shift layer and preset load/save
PresetManager.h    organ_presets.json, factory set
MidiManager.h      MIDI in/out, organ CC map
```

Everything else is WAVE's, lightly adjusted (the sequencer gate defaults to full length,
the CHOMPI key opens the shift layer whatever the toggle says).

## CPU

The organ core was profiled by counting instructions for a Cortex-A7 Thumb-2/VFP build under
QEMU (close to the M7's instruction set; the M7 is dual-issue, so this should be pessimistic):

| Case | Instructions per 24-sample block | Share of a 480 MHz M7's block budget (240k cycles) |
|---|---|---|
| 4-note chord, 888000000, percussion, C3, drive, fast rotor | ~30k | ~13% at 1 cycle/instr |
| All 61 keys, all drawbars out | ~61k | ~26% at 1 cycle/instr |

Reverb and the output limiter come on top of that (they ran fine in WAVE).
The fixed cost is mostly the rotary cabinet, about 400 instructions per sample.

## Desktop test harness

`code/host_test/organ_render.cpp` builds the DSP core on a desktop and renders a few
WAVs (slow/fast rotor, percussion, full drawbars with drive, vibrato, click, brake):

```bash
cd code/host_test
g++ -O2 -std=c++14 -I../src organ_render.cpp -o organ_render
./organ_render .
```

## License

MIT, like the rest of this repo. The DSP is original to this firmware; no code from other
organ emulators is included.

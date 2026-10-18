# WAVE → SUB: aggressive analog-style bass firmware for CHOMPI

A fork of the open-source WAVE 1.0 firmware. The wavetable voice is replaced by an
analog-modelled sub/bass voice. The UI, sequencer, MIDI, presets, delay/reverb and
output chain are WAVE's, unchanged.

## Voice

Everything below runs at 2x the audio rate (96 kHz). The summed voices go through one
47-tap halfband decimator back to 48 kHz.

- Oscillators: PolyBLEP saw, a second detunable saw, PolyBLEP pulse, a square sub
  (-1 oct) and a sine sub (-2 oct). Analog drift is about ±4 cents of slow random
  pitch wander per oscillator.
- Wavefolder: sine fold ahead of the filter, used by the FOLD character.
- Filter: a 4-pole transistor-ladder model in zero-delay (TPT) form, with tanh in the
  feedback loop. It stays in tune up to Nyquist and self-oscillates near full
  resonance. Partial bass compensation keeps the low end from thinning as resonance
  goes up.
- Filter envelope: fast attack, exponential decay to sustain. Depth scales with
  velocity. Key tracking is set per character.
- After the filter, a clean sine an octave down is added back in so the fundamental
  stays solid when the filter is closed or screaming. Then comes asymmetric tanh
  saturation, which adds even harmonics.
- Mono characters use last-note priority. Overlapping notes slide (glide) without
  retriggering. Releasing a note slides back to the one still held.

## Controls (same knobs as WAVE, new jobs)

| WAVE control | Now |
|---|---|
| Frame scan (pitch knob, 2nd page) | **GRIT** macro: more drive into the ladder, more detune, more fold, more post saturation |
| SHIFT + wavetable select | **Character** (7 slots, same LED colours as the 7 tables) |
| Filter cutoff | Ladder cutoff, ~25 Hz to 18 kHz |
| SHIFT + resonance | Ladder resonance (self-oscillates near the top on ACID) |
| Attack / release | Amp envelope (release also sets the filter env release) |
| Pitch LFO / filter LFO | Same as WAVE (filter LFO depth = ±4 octaves of cutoff) |
| Pitch, octave, delay/reverb, comp/saturation, gain, pan, sequencer | Unchanged |

Characters, in table-slot order:

1. **SUB** (mono): square plus -2 oct sine, mostly closed. Round and heavy.
2. **GROWL** (mono, default): saw plus square sub slammed into the ladder.
3. **REESE** (mono): two detuned saws over a sine sub.
4. **FOLD** (mono): wavefolded sub, metallic and harsh.
5. **PULSE** (poly): narrow pulse plus square sub, for stabs.
6. **FAT POLY** (poly): detuned saw pair plus sub.
7. **ACID** (mono): single saw, high resonance, big env sweep, slide.

Presets save GRIT and Character in the slots WAVE used for frame and table. Old WAVE
presets will load, but they'll land on whatever character matches their table number.

## Install

Start from the WAVE 1.0 card profile and replace `CHOMPI.bin` with the one built here.
Leave the wavetables on the card, because WAVE's boot sequence still loads them. Going
back to stock works the same way.

## Build

GNU Arm Embedded 10.3-2021.10, as for stock WAVE:

    cd code/libs/libDaisy && make
    cd ../DaisySP && make
    cd ../../src && make        # -> build/CHOMPI.bin

Desktop audition (no hardware needed): `desktop/build_demo.sh && desktop/render_demo out.wav`

## Not yet verified on hardware

- **CPU:** a rough count puts 8 voices sounding at once at roughly 40% of the M7,
  before WAVE's reverb and delay. Mono characters use one voice.
  - If poly chords crackle, lower `NUM_VOICES` in `subtractiveEngine.h`.
  - The commented-out `CpuLoadMeter` in `chompi_main.cpp` will show the real number.
- **Level:** one mono voice peaks around 5-6x louder than a single WAVE note. The
  gain knob and output limiter handle it, but presets made on stock WAVE will sound
  hotter.

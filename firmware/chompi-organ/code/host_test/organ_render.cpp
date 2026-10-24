/** Desktop render + timing harness for ToneWheelOrgan.h
 *
 *  Build:  g++ -O2 -std=c++14 -I../src organ_render.cpp -o organ_render
 *  Run:    ./organ_render out_dir
 *
 *  Writes a few 48 kHz stereo WAVs exercising the main features and prints
 *  the time per 24-sample block.
 */
#include "ToneWheelOrgan.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#include <xmmintrin.h>

static constexpr float kFs    = 48000.f;
static constexpr int   kBlock = 24;

struct Wav
{
    std::vector<float> l, r;
    void Save(const std::string &path)
    {
        FILE *f = fopen(path.c_str(), "wb");
        if(!f)
            return;
        const uint32_t n = (uint32_t)l.size(), bytes = n * 4, sr = (uint32_t)kFs;
        const uint32_t riff = 36 + bytes, fmt_len = 16, byte_rate = sr * 4;
        const uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
        fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
        fwrite(&fmt_len, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
        fwrite(&sr, 4, 1, f); fwrite(&byte_rate, 4, 1, f); fwrite(&align, 2, 1, f);
        fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
        float peak = 0.f;
        for(uint32_t i = 0; i < n; i++)
        {
            peak = std::max(peak, std::max(fabsf(l[i]), fabsf(r[i])));
            int16_t s[2] = {(int16_t)(std::max(-1.f, std::min(1.f, l[i])) * 32767.f),
                            (int16_t)(std::max(-1.f, std::min(1.f, r[i])) * 32767.f)};
            fwrite(s, 2, 2, f);
        }
        fclose(f);
        double rms = 0;
        for(uint32_t i = 0; i < n; i++)
            rms += l[i] * l[i] + r[i] * r[i];
        rms = sqrt(rms / (2.0 * n));
        printf("  %-28s peak %.3f  rms %.3f\n", path.substr(path.rfind('/') + 1).c_str(), peak, rms);
    }
};

static void Run(organ::ToneWheelOrgan &o, Wav &w, float seconds)
{
    const int blocks = (int)(seconds * kFs / kBlock);
    float     l[kBlock], r[kBlock];
    for(int b = 0; b < blocks; b++)
    {
        o.UpdateControl();
        o.Process(l, r, kBlock);
        w.l.insert(w.l.end(), l, l + kBlock);
        w.r.insert(w.r.end(), r, r + kBlock);
    }
}

static void Registration(organ::ToneWheelOrgan &o, const char *bars)
{
    for(int i = 0; i < 9; i++)
        o.SetDrawbar(i, bars[i] - '0');
}

// manual key index: 0 = C2 (65 Hz at 8'); C4 = 24
static const int kChord[]  = {24, 28, 31, 36}; // C4 E4 G4 C5
static const int kLick[]   = {24, 27, 29, 30, 31, 34, 36, 34, 31, 29, 27, 24};

int main(int argc, char **argv)
{
    _mm_setcsr(_mm_getcsr() | 0x8040); // FTZ/DAZ like the M7 default-NaN/FZ config
    std::string dir = argc > 1 ? argv[1] : ".";
    static organ::ToneWheelOrgan o;

    // 1. Classic 888000000 chord: slow, then switch to fast, then back
    {
        o.Init(kFs);
        Registration(o, "888000000");
        o.SetVibrato(organ::VIB_C3);
        o.SetDrive(.25f);
        Wav w;
        for(int k : kChord) o.NoteOn(k);
        Run(o, w, 3.f);
        o.SetRotorSpeed(organ::RotorSpeed::FAST);
        Run(o, w, 3.f);
        o.SetRotorSpeed(organ::RotorSpeed::SLOW);
        Run(o, w, 3.f);
        for(int k : kChord) o.NoteOff(k);
        Run(o, w, .5f);
        w.Save(dir + "/01_888_slow_fast_slow.wav");
    }

    // 2. Jazz: 888000000 + 3rd harmonic fast percussion, single-trigger lick
    {
        o.Init(kFs);
        Registration(o, "888000000");
        o.SetPercussion(true);
        o.SetPercThird(true);
        o.SetPercFast(true);
        o.SetVibrato(organ::VIB_C3);
        o.SetDrive(.3f);
        Wav w;
        for(int k : kLick)
        {
            o.NoteOn(k);
            Run(o, w, .16f);
            o.NoteOff(k);
            Run(o, w, .04f);
        }
        // legato: percussion should NOT retrigger on the overlapped notes
        o.NoteOn(24);
        Run(o, w, .2f);
        o.NoteOn(28);
        o.NoteOff(24);
        Run(o, w, .2f);
        o.NoteOn(31);
        o.NoteOff(28);
        Run(o, w, .4f);
        o.NoteOff(31);
        Run(o, w, .5f);
        w.Save(dir + "/02_jazz_percussion.wav");
    }

    // 3. Full organ, hard drive, fast rotor
    {
        o.Init(kFs);
        Registration(o, "888888888");
        o.SetDrive(.85f);
        o.SetRotorSpeed(organ::RotorSpeed::FAST);
        Wav w;
        Run(o, w, .5f);
        for(int k : kChord) o.NoteOn(k);
        o.NoteOn(12);
        Run(o, w, 3.f);
        o.AllNotesOff();
        Run(o, w, .5f);
        w.Save(dir + "/03_full_driven_fast.wav");
    }

    // 4. Ballad: 808000000 vibrato V3, clean, rotor off (direct)
    {
        o.Init(kFs);
        Registration(o, "808000000");
        o.SetVibrato(organ::VIB_V3);
        o.SetRotorMix(0.f);
        o.SetClick(0.f);
        Wav w;
        for(int k : kChord) o.NoteOn(k);
        Run(o, w, 3.f);
        o.AllNotesOff();
        Run(o, w, .4f);
        w.Save(dir + "/04_808_v3_direct.wav");
    }

    // 5. Key click: single drawbar, max click, staccato
    {
        o.Init(kFs);
        Registration(o, "008000000");
        o.SetClick(1.f);
        o.SetRotorMix(0.f);
        Wav w;
        for(int i = 0; i < 6; i++)
        {
            o.NoteOn(24 + i * 2);
            Run(o, w, .12f);
            o.NoteOff(24 + i * 2);
            Run(o, w, .18f);
        }
        w.Save(dir + "/05_click.wav");
    }

    // 6. Brake: fast, then stop
    {
        o.Init(kFs);
        Registration(o, "888800000");
        o.SetRotorSpeed(organ::RotorSpeed::FAST);
        Wav w;
        for(int k : kChord) o.NoteOn(k);
        Run(o, w, 2.5f);
        o.SetRotorSpeed(organ::RotorSpeed::STOP);
        Run(o, w, 3.f);
        o.AllNotesOff();
        Run(o, w, .3f);
        w.Save(dir + "/06_brake.wav");
    }

    // ---- timing: worst case, every key down with all drawbars, perc, drive, fast ----
    {
        o.Init(kFs);
        Registration(o, "888888888");
        o.SetPercussion(true);
        o.SetVibrato(organ::VIB_C3);
        o.SetDrive(.6f);
        o.SetRotorSpeed(organ::RotorSpeed::FAST);
        for(int k = 0; k < organ::kNumKeys; k++)
            o.NoteOn(k);
        float      l[kBlock], r[kBlock];
        const int  blocks = 20000;
        for(int b = 0; b < 200; b++)
        {
            o.UpdateControl();
            o.Process(l, r, kBlock);
        }
        auto t0 = std::chrono::high_resolution_clock::now();
        for(int b = 0; b < blocks; b++)
        {
            o.UpdateControl();
            o.Process(l, r, kBlock);
        }
        auto   t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / blocks;
        printf("worst case: %.2f us per %d-sample block (budget %.0f us)\n", us, kBlock,
               1e6 * kBlock / kFs);

        // typical: 4 keys
        o.AllNotesOff();
        for(int b = 0; b < 200; b++) { o.UpdateControl(); o.Process(l, r, kBlock); }
        for(int k : kChord) o.NoteOn(k);
        for(int b = 0; b < 200; b++) { o.UpdateControl(); o.Process(l, r, kBlock); }
        t0 = std::chrono::high_resolution_clock::now();
        for(int b = 0; b < blocks; b++)
        {
            o.UpdateControl();
            o.Process(l, r, kBlock);
        }
        t1 = std::chrono::high_resolution_clock::now();
        us = std::chrono::duration<double, std::micro>(t1 - t0).count() / blocks;
        printf("4-note chord: %.2f us per block\n", us);
        printf("sizeof(ToneWheelOrgan) = %zu bytes\n", sizeof(organ::ToneWheelOrgan));
    }
    return 0;
}

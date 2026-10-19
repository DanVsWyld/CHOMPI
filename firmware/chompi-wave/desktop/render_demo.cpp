// Desktop audition harness for SubSynth.h (not part of the firmware build).
// Renders a bassline through each of the seven characters, sweeping GRIT and the
// cutoff, using the same voice/decimator code the firmware runs.
//
// Build: see build_demo.sh. Output: 48 kHz 16-bit stereo WAV.
#include "SubSynth.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace subsynth;

static const float kSR = 48000.f;

struct MiniEngine
{
    // Same mono/legato behaviour as myEngine::StartMono/StopMono, cut down.
    SubVoice v[8];
    HalfbandDecimator dec;
    int ch = 0;
    float grit = 0.f;
    int held[16], nheld = 0;
    float last_hz = 0.f;
    void Assign(SubVoice& x, int midi, float vel)
    {
        const float hz = Hz(midi);
        if(last_hz > 0.f && kCharacters[ch].glide_s > 0.f)
        {
            x.SetFrequency(last_hz);
            x.GlideTo(hz);
        }
        else
            x.SetFrequency(hz);
        last_hz    = hz;
        x.velocity = vel;
        x.key      = midi;
        x.gate     = true;
    }

    void Init()
    {
        for(int i = 0; i < 8; ++i)
        {
            v[i].Init(kSR, i + 1);
            v[i].amp_env.SetAttackTime(0.003f, 1.f);
            v[i].SetRelease(0.12f);
        }
        SetChar(0);
    }
    void SetChar(int c)
    {
        ch = c;
        for(auto& x : v)
            x.SetCharacter(kCharacters[c], grit);
    }
    void SetGrit(float g)
    {
        grit = g;
        for(auto& x : v)
            x.SetGrit(g);
    }
    void SetCut(float c, float r)
    {
        for(auto& x : v)
        {
            x.SetCutoffKnob(c);
            x.SetResKnob(r);
        }
    }
    static float Hz(int midi) { return 440.f * powf(2.f, (midi - 69) / 12.f); }

    void NoteOn(int midi, float vel)
    {
        if(kCharacters[ch].mono)
        {
            held[nheld++] = midi;
            if(v[0].gate)
                v[0].GlideTo(Hz(midi));
            else
            {
                v[0].SetFrequency(Hz(midi));
                v[0].velocity = vel;
                v[0].gate     = true;
            }
            v[0].key = midi;
        }
        else
        {
            for(auto& x : v)
                if(!x.gate && !x.amp_env.IsRunning())
                {
                    Assign(x, midi, vel);
                    return;
                }
            for(auto& x : v)
                if(!x.gate)
                {
                    Assign(x, midi, vel);
                    return;
                }
        }
    }
    void NoteOff(int midi)
    {
        if(kCharacters[ch].mono)
        {
            for(int i = 0; i < nheld; ++i)
                if(held[i] == midi)
                {
                    bool top = i == nheld - 1;
                    for(int j = i; j < nheld - 1; ++j)
                        held[j] = held[j + 1];
                    --nheld;
                    if(nheld == 0)
                        v[0].gate = false;
                    else if(top)
                        v[0].GlideTo(Hz(held[nheld - 1]));
                    break;
                }
        }
        else
            for(auto& x : v)
                if(x.key == midi && x.gate)
                {
                    x.gate = false;
                    break;
                }
    }
    float Tick(float lfo)
    {
        float os[2] = {0.f, 0.f};
        for(auto& x : v)
            x.Render(os, lfo, 1.f);
        return dec.Process(os[0], os[1]) * 0.35f;
    }
};

struct Ev
{
    float beat;
    int   on; // 1 on, 0 off
    int   note;
    float vel;
};

static void WriteWav(const char* path, const std::vector<float>& mono)
{
    FILE* f = fopen(path, "wb");
    uint32_t n = mono.size(), bytes = n * 4, sr = 48000, br = sr * 4;
    uint16_t ch = 2, bits = 16, ba = 4, fmt = 1;
    uint32_t riff = 36 + bytes, sixteen = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&sixteen, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for(float s : mono)
    {
        int v = (int)lrintf(fmaxf(-1.f, fminf(1.f, s)) * 32767.f);
        int16_t x = (int16_t)v;
        fwrite(&x, 2, 1, f);
        fwrite(&x, 2, 1, f);
    }
    fclose(f);
}

int main(int argc, char** argv)
{
    const char* out = argc > 1 ? argv[1] : "subsynth_demo.wav";
    const float bpm = 140.f, spb = 60.f / bpm;

    // 2-bar riff in E (MIDI 28 = E1), 16ths, with overlapping notes for slides
    std::vector<Ev> riff;
    auto add = [&](float b, float len, int n, float vel) {
        riff.push_back({b, 1, n, vel});
        riff.push_back({b + len, 0, n, 0});
    };
    add(0.00f, 0.20f, 28, 1.0f);  add(0.50f, 0.20f, 28, 0.7f);  add(0.75f, 0.45f, 40, 0.9f);
    add(1.00f, 0.40f, 38, 0.8f);  // overlaps -> slide 40 -> 38
    add(1.50f, 0.20f, 28, 1.0f);  add(2.00f, 0.20f, 31, 0.8f);  add(2.50f, 0.20f, 28, 0.6f);
    add(3.00f, 0.55f, 33, 1.0f);  add(3.50f, 0.45f, 35, 0.9f);  // slide 33 -> 35
    add(4.00f, 0.20f, 28, 1.0f);  add(4.25f, 0.20f, 28, 0.6f);  add(4.75f, 0.45f, 43, 1.0f);
    add(5.00f, 0.40f, 40, 0.8f);  add(5.50f, 0.20f, 28, 1.0f);  add(6.00f, 0.90f, 26, 1.0f);
    add(7.00f, 0.20f, 31, 0.9f);  add(7.50f, 0.20f, 33, 0.9f);

    // polyphonic stab for the poly characters
    std::vector<Ev> stab;
    int chord[3] = {40, 47, 52};
    for(int bar = 0; bar < 2; ++bar)
        for(float b : {0.f, 1.5f, 3.0f})
            for(int k = 0; k < 3; ++k)
            {
                stab.push_back({bar * 4.f + b, 1, chord[k], 1.f});
                stab.push_back({bar * 4.f + b + 0.6f, 0, chord[k], 0});
            }

    MiniEngine e;
    e.Init();
    std::vector<float> audio;
    float peak = 0.f, sum = 0.f, sumsq = 0.f;
    long  nan = 0;

    const char* names[kNumCharacters] = {"SUB", "GROWL", "REESE", "FOLD", "PULSE", "FAT POLY", "ACID"};
    for(int c = 0; c < kNumCharacters; ++c)
    {
        e.SetChar(c);
        // two passes: bass riff at low GRIT, then chord stabs at high GRIT,
        // cutoff swept across each 8-beat pass
        for(int pass = 0; pass < 2; ++pass)
        {
            const auto& evs = pass == 0 ? riff : stab;
            e.SetGrit(pass == 0 ? 0.2f : 0.8f);
            const int total = (int)(8.f * spb * kSR);
            size_t ei = 0;
            std::vector<Ev> sorted = evs;
            std::sort(sorted.begin(), sorted.end(), [](const Ev& a, const Ev& b) {
                return a.beat < b.beat || (a.beat == b.beat && a.on < b.on);
            });
            float cpeak = 0.f;
            for(int i = 0; i < total; ++i)
            {
                const float beat = i / (spb * kSR);
                while(ei < sorted.size() && sorted[ei].beat <= beat)
                {
                    if(sorted[ei].on)
                        e.NoteOn(sorted[ei].note, sorted[ei].vel);
                    else
                        e.NoteOff(sorted[ei].note);
                    ++ei;
                }
                const float sweep = 0.25f + 0.35f * (0.5f - 0.5f * cosf(2.f * kPi * beat / 8.f));
                e.SetCut(sweep, c == 6 ? 0.85f : 0.45f);
                float s = e.Tick(0.f);
                if(!std::isfinite(s))
                {
                    ++nan;
                    s = 0.f;
                }
                audio.push_back(s);
                peak = fmaxf(peak, fabsf(s));
                cpeak = fmaxf(cpeak, fabsf(s));
                sum += s;
                sumsq += s * s;
            }
            // release tail
            for(int i = 0; i < (int)(0.25f * kSR); ++i)
                audio.push_back(e.Tick(0.f));
            printf("%-9s %-6s peak %.3f\n", names[c], pass == 0 ? "riff" : "chords", cpeak);
        }
    }
    printf("total %.1f s  peak %.3f  rms %.3f  dc %.5f  nan %ld\n",
           audio.size() / kSR, peak, sqrtf(sumsq / audio.size()), sum / audio.size(), nan);

    // normalise to -1 dBFS for listening (the firmware's gain/limiter stage does this on hardware)
    const float norm = 0.89f / fmaxf(peak, 1e-6f);
    for(float& s : audio)
        s *= norm;
    WriteWav(out, audio);
    return nan ? 1 : 0;
}

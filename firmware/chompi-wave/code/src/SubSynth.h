/** @file SubSynth.h
 *  @brief Aggressive analog-style sub/bass voice for the CHOMPI WAVE fork.
 *
 *  Signal path per voice (all of it runs at 2x the audio rate):
 *
 *    saw / saw2 (detuned) / pulse   --+
 *    square sub (-1 oct)              +--> mix --> pre-drive --> 4-pole ladder (TPT, tanh in the loop)
 *    sine sub (-2 oct)                |            (wavefolder        |   cutoff <- knob, key track,
 *                                     |             on some voices)   |             filter env, filter LFO
 *    analog drift + glide on pitch  --+                               v
 *                                     clean sine sub (-1 oct) --> + --> asymmetric post-saturation --> amp env
 *
 *  The voices are summed at 2x and decimated once on the bus (HalfbandDecimator),
 *  so the drive and filter stages alias far less than they would at 48 kHz.
 *
 *  Nothing here depends on libDaisy; only daisysp::Adsr is used, so the voice can
 *  be compiled and auditioned on a desktop.
 */
#pragma once
#include <cmath>
#include <cstdint>
#include "daisysp.h"

namespace subsynth
{

static constexpr int   kOversample = 2;
static constexpr int   kControlEvery = 8;  // oversampled samples per control-rate tick
static constexpr float kPi = 3.14159265359f;

// ---------------------------------------------------------------- helpers

/** Rational tanh approximation, exact at 0 and continuous at the +/-3 clamp. */
inline float FastTanh(float x)
{
    if(x > 3.f)
        return 1.f;
    if(x < -3.f)
        return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

/** sin(2*pi*ph) for ph in [0, 1). Parabola + one refinement step, ~0.1% error. */
inline float FastSin2Pi(float ph)
{
    float t = ph - 0.5f;                       // [-0.5, 0.5)
    float y = 8.f * t - 16.f * t * fabsf(t);   // ~ sin(2*pi*t)
    y       = 0.225f * (y * fabsf(y) - y) + y;
    return -y;                                 // sin(2*pi*ph) = -sin(2*pi*t)
}

/** Two-sample polynomial band-limited step correction. */
inline float PolyBlep(float t, float dt)
{
    if(t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.f;
    }
    if(t > 1.f - dt)
    {
        t = (t - 1.f) / dt;
        return t * t + t + t + 1.f;
    }
    return 0.f;
}

inline float Wrap01(float p)
{
    return p - floorf(p);
}

/** Small LCG; plenty for drift and start-phase scatter. */
struct Rand
{
    uint32_t s = 22222u;
    float    Next() // [-1, 1)
    {
        s = s * 1664525u + 1013904223u;
        return (float)(int32_t)s * 4.656612873e-10f;
    }
};

// ---------------------------------------------------------------- oscillators

struct Phasor
{
    float ph = 0.f;
    float dt = 0.f;
    inline void Advance()
    {
        ph += dt;
        if(ph >= 1.f)
            ph -= 1.f;
    }
};

inline float SawBlep(const Phasor& p)
{
    return (2.f * p.ph - 1.f) - PolyBlep(p.ph, p.dt);
}

inline float PulseBlep(const Phasor& p, float pw)
{
    float y = p.ph < pw ? 1.f : -1.f;
    y += PolyBlep(p.ph, p.dt);
    y -= PolyBlep(Wrap01(p.ph + 1.f - pw), p.dt);
    return y;
}

// ---------------------------------------------------------------- filter

/** 4-pole transistor-ladder model in topology-preserving-transform form.
 *  The feedback loop is solved without a unit delay, so it stays in tune and
 *  stable right up to Nyquist; a tanh on the loop input gives the ladder its
 *  growl and bounds self-oscillation. */
class Ladder
{
  public:
    void Init(float sample_rate)
    {
        sr_ = sample_rate;
        for(float& s : s_)
            s = 0.f;
        SetCutoff(1000.f);
        SetRes(0.f);
    }

    void SetCutoff(float hz)
    {
        const float max_hz = 0.42f * sr_;
        if(hz < 15.f)
            hz = 15.f;
        if(hz > max_hz)
            hz = max_hz;
        g_   = tanf(kPi * hz / sr_);
        G_   = g_ / (1.f + g_);
        inv_ = 1.f / (1.f + g_);
    }

    /** 0..1, ~1 is the edge of self-oscillation. */
    void SetRes(float r) { k_ = 4.05f * r; }

    float Process(float x)
    {
        const float G2 = G_ * G_, G3 = G2 * G_, G4 = G3 * G_;
        const float S  = (G3 * s_[0] + G2 * s_[1] + G_ * s_[2] + s_[3]) * inv_;

        // Partial passband-gain compensation so resonance doesn't hollow out the low end.
        const float comp = 1.f + 0.55f * k_;
        float       u    = (x * comp - k_ * S) / (1.f + k_ * G4);
        u                = FastTanh(u);

        float y = u;
        for(int i = 0; i < 4; ++i)
        {
            const float v = (y - s_[i]) * G_;
            y             = v + s_[i];
            s_[i]         = y + v;
        }
        return y;
    }

    void Reset()
    {
        for(float& s : s_)
            s = 0.f;
    }

  private:
    float sr_ = 96000.f;
    float g_ = 0.f, G_ = 0.f, inv_ = 1.f, k_ = 0.f;
    float s_[4];
};

// ---------------------------------------------------------------- filter envelope

/** Instant-ish attack, exponential decay to sustain, exponential release. */
class FilterEnv
{
  public:
    void Init(float sample_rate) { sr_ = sample_rate; }

    void Set(float decay_s, float sustain)
    {
        dec_coef_ = expf(-1.f / (decay_s * sr_));
        sus_      = sustain;
    }

    void SetRelease(float release_s) { rel_coef_ = expf(-1.f / (release_s * sr_)); }

    void Trigger() { attacking_ = true; }

    float Process(bool gate)
    {
        if(attacking_)
        {
            val_ += atk_step_;
            if(val_ >= 1.f)
            {
                val_       = 1.f;
                attacking_ = false;
            }
        }
        else if(gate)
            val_ = sus_ + (val_ - sus_) * dec_coef_;
        else
            val_ *= rel_coef_;
        return val_;
    }

  private:
    float sr_ = 96000.f;
    float val_ = 0.f, sus_ = 0.f;
    float dec_coef_ = 0.999f, rel_coef_ = 0.999f;
    float atk_step_ = 1.f / 96.f; // ~1 ms at 96 kHz
    bool  attacking_ = false;
};

// ---------------------------------------------------------------- characters

/** One "patch" of the voice. Picked with SHIFT + knob 1 (page 2), where WAVE
 *  picked its wavetable; the GRIT macro (knob 1, page 2, where WAVE scanned
 *  frames) pushes every one of them harder. */
struct Character
{
    float saw, saw2, pulse, pw;    // main oscillator levels, pulse width
    float detune_cents;            // saw2 detune (Reese)
    float sub_sq, sub_sine;        // -1 oct square, -2 oct sine
    float fold;                    // wavefolder amount before the filter
    float drive;                   // pre-filter gain into the ladder
    float env_oct, env_decay, env_sus; // filter env: depth in octaves, decay s, sustain 0..1
    float key_track;               // 0..1
    float res_scale;               // multiplies the resonance knob
    float post_sat;                // asymmetric saturation after the filter
    float sub_inject;              // clean -1 oct sine added after the filter
    float glide_s;                 // portamento (mono characters only)
    bool  mono;                    // mono, last-note priority, legato
    bool  phase_reset;             // reset oscillators on a fresh note for a consistent attack
};

static constexpr int kNumCharacters = 7;

static const Character kCharacters[kNumCharacters] = {
    //  saw   saw2  pulse pw    det   subSq subSin fold  drive envO  envD  envS  kTrk  resS  post  inj   glide mono   reset
    // 1 SUB: square into a mostly closed filter plus a -2 oct sine. Heavy, round, still bites when driven.
    {0.00f, 0.00f, 0.70f, 0.50f, 0.f, 0.80f, 0.70f, 0.00f, 2.0f, 2.0f, 0.18f, 0.25f, 0.30f, 0.60f, 0.25f, 0.60f, 0.045f, true, true},
    // 2 GROWL: saw + square sub slammed into the ladder, snappy env.
    {0.90f, 0.00f, 0.00f, 0.50f, 0.f, 0.70f, 0.30f, 0.00f, 4.0f, 3.5f, 0.22f, 0.20f, 0.50f, 0.85f, 0.55f, 0.40f, 0.040f, true, true},
    // 3 REESE: two detuned saws beating against a solid sine sub.
    {0.80f, 0.80f, 0.00f, 0.50f, 14.f, 0.00f, 0.60f, 0.00f, 2.6f, 1.5f, 0.60f, 0.50f, 0.40f, 0.50f, 0.45f, 0.55f, 0.060f, true, false},
    // 4 FOLD: sine/triangle-ish source through a wavefolder - harsh, metallic overtones.
    {0.00f, 0.00f, 0.30f, 0.50f, 0.f, 0.60f, 0.80f, 0.55f, 1.6f, 2.5f, 0.30f, 0.40f, 0.60f, 0.40f, 0.50f, 0.45f, 0.035f, true, true},
    // 5 PULSE: narrow pulse + square sub, polyphonic for stabs.
    {0.00f, 0.00f, 0.90f, 0.22f, 0.f, 0.60f, 0.00f, 0.00f, 2.8f, 3.0f, 0.25f, 0.15f, 0.60f, 0.70f, 0.40f, 0.30f, 0.000f, false, false},
    // 6 FAT POLY: detuned saw pair + sub, polyphonic.
    {0.80f, 0.80f, 0.00f, 0.50f, 9.f, 0.50f, 0.00f, 0.00f, 2.2f, 2.5f, 0.45f, 0.35f, 0.50f, 0.70f, 0.30f, 0.25f, 0.000f, false, false},
    // 7 ACID: single saw, screaming resonance, big env sweep, slide.
    {1.00f, 0.00f, 0.00f, 0.50f, 0.f, 0.25f, 0.00f, 0.00f, 3.0f, 5.0f, 0.16f, 0.05f, 0.40f, 1.05f, 0.60f, 0.15f, 0.070f, true, true},
};

// ---------------------------------------------------------------- voice

class SubVoice
{
  public:
    void Init(float audio_rate, uint32_t seed)
    {
        sr_ = audio_rate * kOversample;
        rnd_.s = seed * 2654435761u + 1u;
        amp_env.Init(sr_);
        amp_env.SetSustainLevel(1.f);
        amp_env.SetAttackTime(0.002f);
        amp_env.SetReleaseTime(0.15f);
        fenv_.Init(sr_);
        fenv_.SetRelease(0.15f);
        ladder_.Init(sr_);
        dc_x_ = dc_y_ = 0.f;
        SetCharacter(kCharacters[1], 0.f);
        cur_l2_ = tgt_l2_ = log2f(55.f);
        ctl_count_ = 0;
    }

    // -- settings ----------------------------------------------------------

    void SetCharacter(const Character& c, float grit)
    {
        ch_   = &c;
        grit_ = grit;
        fenv_.Set(c.env_decay, c.env_sus);
        glide_coef_ = c.glide_s > 0.f
                          ? 1.f - expf(-1.f / (c.glide_s * (sr_ / kControlEvery)))
                          : 1.f;
    }

    void SetGrit(float grit) { grit_ = grit; }
    void SetCutoffKnob(float v) { cutoff_knob_ = v; }
    void SetResKnob(float v) { res_knob_ = v; }
    void SetRelease(float s)
    {
        amp_env.SetReleaseTime(s);
        fenv_.SetRelease(s);
    }

    /** Jump straight to a pitch (fresh note). */
    void SetFrequency(float hz)
    {
        frequency = hz;
        tgt_l2_ = cur_l2_ = log2f(hz);
    }

    /** Slide to a pitch at the character's glide rate (legato). */
    void GlideTo(float hz)
    {
        frequency = hz;
        tgt_l2_   = log2f(hz);
    }

    /** Ask for the envelopes to restart on the next sample even if the gate is already high. */
    void Retrigger() { retrig_ = true; }

    // -- audio ---------------------------------------------------------------

    /** Renders kOversample samples and ADDS them into os[]. */
    void Render(float* os, float filter_lfo, float pitch_lfo_mult)
    {
        if(!gate && !amp_env.IsRunning() && !retrig_)
        {
            prev_gate_    = false;
            was_sounding_ = false;
            return;
        }

        const Character& c = *ch_;

        if((gate && !prev_gate_) || retrig_)
        {
            fenv_.Trigger();
            if(retrig_)
                amp_env.Retrigger(false);
            if(c.phase_reset && !was_sounding_)
            {
                saw_.ph = saw2_.ph = pulse_.ph = sub1_.ph = sub2_.ph = 0.f;
            }
            else if(!was_sounding_)
            {
                // free-running analog oscillators: scatter the start phases
                saw_.ph   = 0.5f + 0.5f * rnd_.Next();
                saw2_.ph  = 0.5f + 0.5f * rnd_.Next();
                pulse_.ph = 0.5f + 0.5f * rnd_.Next();
                sub1_.ph  = sub2_.ph = 0.f; // keep the subs phase-aligned for punch
            }
            retrig_ = false;
        }
        prev_gate_ = gate;

        for(int n = 0; n < kOversample; ++n)
        {
            if(ctl_count_ == 0)
                ControlTick(c, filter_lfo, pitch_lfo_mult);
            if(++ctl_count_ >= kControlEvery)
                ctl_count_ = 0;

            // ---- oscillators
            float mix = 0.f;
            if(c.saw > 0.f)
                mix += c.saw * SawBlep(saw_);
            if(saw2_lvl_ > 0.f)
                mix += saw2_lvl_ * SawBlep(saw2_);
            if(c.pulse > 0.f)
                mix += c.pulse * PulseBlep(pulse_, pw_);
            if(c.sub_sq > 0.f)
                mix += c.sub_sq * PulseBlep(sub1_, 0.5f);
            if(c.sub_sine > 0.f)
                mix += c.sub_sine * FastSin2Pi(sub2_.ph);
            const float clean_sub = FastSin2Pi(sub1_.ph);

            saw_.Advance();
            saw2_.Advance();
            pulse_.Advance();
            sub1_.Advance();
            sub2_.Advance();

            mix *= mix_norm_;

            // ---- wavefolder (before the filter, so the ladder can tame it)
            if(fold_ > 0.f)
            {
                const float fg = 1.f + 5.f * fold_;
                // sine fold: maps the signal onto sin(pi/2 * x * gain)
                const float folded = FastSin2Pi(Wrap01(0.25f * mix * fg + 1.f));
                mix = mix + fold_ * (folded - mix);
            }

            // ---- drive into the ladder
            float y = ladder_.Process(mix * drive_);

            // ---- clean sub reinforcement after the filter keeps the fundamental solid
            y += c.sub_inject * 0.8f * clean_sub;

            // ---- asymmetric post saturation (even harmonics), DC removed per voice
            const float a    = 1.f + 5.f * post_;
            const float bias = 0.2f * post_;
            y = (FastTanh(a * y + bias) - FastTanh(bias)) * post_makeup_;
            const float dc = y - dc_x_ + 0.9995f * dc_y_;
            dc_x_          = y;
            dc_y_          = dc;
            y              = dc;

            // ---- amp
            fe_ = fenv_.Process(gate);
            const float amp = amp_env.Process(gate);
            os[n] += y * amp * (0.45f + 0.55f * velocity) * 0.25f;
        }
        was_sounding_ = amp_env.IsRunning();
    }

    // state the engine reads/writes directly (same names as WAVE's voice)
    float       frequency = 110.f;
    int         key       = -1;
    float       nn        = 0.f;
    bool        gate      = false;
    float       velocity  = 1.f;
    int         prioroty  = 0;
    bool        activeFromUser = false;
    bool        activeFromSequencer = false;
    daisysp::Adsr amp_env;

  private:
    void ControlTick(const Character& c, float filter_lfo, float pitch_lfo_mult)
    {
        // analog drift: slow filtered noise, about +/- 4 cents, slightly different per oscillator
        drift_ += 0.0025f * (rnd_.Next() - drift_);
        const float drift_l2 = drift_ * (4.f / 1200.f);

        // glide in the log domain
        cur_l2_ += (tgt_l2_ - cur_l2_) * glide_coef_;
        const float f = exp2f(cur_l2_ + drift_l2) * pitch_lfo_mult;

        const float g      = grit_;
        const float det    = c.detune_cents * (1.f + 1.5f * g);
        const float inv_sr = 1.f / sr_;
        saw_.dt   = f * inv_sr;
        saw2_.dt  = f * exp2f(det / 1200.f + drift_l2 * 0.7f) * inv_sr;
        pulse_.dt = f * exp2f(-drift_l2 * 0.5f) * inv_sr;
        sub1_.dt  = 0.5f * f * inv_sr;
        sub2_.dt  = 0.25f * f * inv_sr;

        // saw2 only exists on detuned characters; GRIT can bring in a little on others
        saw2_lvl_ = c.saw2;
        pw_       = c.pw - 0.12f * g * (c.pw > 0.3f ? 1.f : 0.f);

        // levels: normalise the oscillator sum so characters sit at similar loudness
        const float sum = c.saw + saw2_lvl_ + c.pulse + c.sub_sq + c.sub_sine;
        mix_norm_ = sum > 1.f ? 1.f / sum : 1.f;

        // GRIT macro: more drive, more fold, more post saturation
        drive_ = c.drive * (1.f + 4.f * g * g + 1.5f * g);
        fold_  = c.fold > 0.f ? fminf(1.f, c.fold + 0.45f * g) : 0.f;
        post_  = fminf(1.f, c.post_sat + 0.6f * g);
        post_makeup_ = 1.f / (0.9f + 0.1f * post_);

        // cutoff: knob sweeps ~25 Hz .. 18 kHz, plus key tracking, envelope and LFO, in octaves
        float l2fc = 4.64f + cutoff_knob_ * 9.5f;                    // log2(25) = 4.64
        l2fc += c.key_track * (log2f(f) - 6.78f);                   // relative to 110 Hz
        l2fc += c.env_oct * fe_ * (0.55f + 0.45f * velocity);
        l2fc += 4.f * filter_lfo;
        ladder_.SetCutoff(exp2f(l2fc));

        float r = res_knob_ * c.res_scale;
        ladder_.SetRes(r > 1.05f ? 1.05f : r);
    }

    float sr_ = 96000.f;
    const Character* ch_ = nullptr;
    Rand  rnd_;
    Phasor saw_, saw2_, pulse_, sub1_, sub2_;
    Ladder ladder_;
    FilterEnv fenv_;
    float fe_ = 0.f;
    float grit_ = 0.f, cutoff_knob_ = 0.5f, res_knob_ = 0.3f;
    float saw2_lvl_ = 0.f, pw_ = 0.5f, mix_norm_ = 1.f;
    float drive_ = 1.f, fold_ = 0.f, post_ = 0.f, post_makeup_ = 1.f;
    float cur_l2_ = 0.f, tgt_l2_ = 0.f, glide_coef_ = 1.f;
    float drift_ = 0.f;
    float dc_x_ = 0.f, dc_y_ = 0.f;
    int   ctl_count_ = 0;
    bool  prev_gate_ = false, retrig_ = false, was_sounding_ = false;
};

// ---------------------------------------------------------------- bus decimator

/** 47-tap halfband FIR (passband to 20 kHz, -69 dB from 28 kHz at 96 kHz),
 *  run once on the summed voice bus: two 96 kHz samples in, one 48 kHz out. */
class HalfbandDecimator
{
  public:
    float Process(float a, float b)
    {
        Push(a);
        Push(b);
        float y = 0.5f * At(23);
        for(int k = 0; k < 12; ++k)
            y += kH[k] * (At(2 * k) + At(46 - 2 * k));
        return y;
    }

  private:
    inline void  Push(float x) { buf_[pos_++ & 63u] = x; }
    inline float At(int delay) const { return buf_[(pos_ - 1u - (unsigned)delay) & 63u]; }

    const float kH[12] = {
        -5.073979975e-04f, 1.093013113e-03f, -2.191657580e-03f, 3.933365063e-03f,
        -6.552245359e-03f, 1.036334390e-02f, -1.583487815e-02f, 2.377710873e-02f,
        -3.586938644e-02f, 5.646606473e-02f, -1.016447744e-01f, 3.167995415e-01f};
    float    buf_[64] = {};
    unsigned pos_     = 0;
};

} // namespace subsynth

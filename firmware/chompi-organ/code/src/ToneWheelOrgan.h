/** @file ToneWheelOrgan.h
 *  @brief Tonewheel organ DSP core: generator, busbars, percussion, key click,
 *  vibrato/chorus scanner, preamp drive and a two-rotor rotary speaker.
 *
 *  No libDaisy / DaisySP dependencies so it can be rendered and profiled on a
 *  desktop machine (see code/host_test). organEngine.h wraps it for CHOMPI.
 *
 *  Signal chain, mirroring a console organ into a 122-style cabinet:
 *
 *    91 tonewheels -> 9 drawbar busbars -> (percussion added)
 *      -> vibrato/chorus scanner -> cabinet preamp (drive, 2x oversampled)
 *      -> 800 Hz crossover -> rotating horn + rotating drum -> two mics (L/R)
 *
 *  Control-rate work happens once per audio block in UpdateControl(): key
 *  contacts open/close, and every wheel's gain is recomputed only when
 *  something changed. The audio loop then costs the same whether one key or
 *  sixty are down, because the generator only ever sums 91 wheels.
 */
#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>

namespace organ
{

static constexpr int   kNumWheels   = 92; // 1..91 used, 0 unused
static constexpr int   kFirstWheel  = 1;
static constexpr int   kLastWheel   = 91;
static constexpr int   kNumKeys     = 61; // one manual, C..C. key 0 = C at 65.4 Hz (8')
static constexpr int   kNumDrawbars = 9;
static constexpr int   kMaxBlock    = 64;
static constexpr int   kSineBits    = 10;
static constexpr int   kSineSize    = 1 << kSineBits;
static constexpr float kTwoPi       = 6.28318530718f;

/** Drawbar footages, left to right: 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1' */
static constexpr int kDrawbarOffset[kNumDrawbars] = {-12, 7, 0, 12, 19, 24, 28, 31, 36};

/** Vibrato/chorus scanner settings, in the order the shift knob steps through */
enum VibratoMode : uint8_t
{
    VIB_OFF = 0,
    VIB_V1,
    VIB_V2,
    VIB_V3,
    VIB_C1,
    VIB_C2,
    VIB_C3,
    VIB_LAST,
};

enum class RotorSpeed : uint8_t
{
    SLOW, // chorale
    FAST, // tremolo
    STOP, // brake
};

/** ---------------------------------------------------------------------
 *  Small building blocks
 *  --------------------------------------------------------------------- */

/** Interpolated lookup table, phase is a full 32 bit accumulator */
struct WaveTable
{
    float t[kSineSize + 1];

    /** h2/h3 are 2nd/3rd harmonic amplitudes relative to the fundamental */
    void Init(float h2, float h3)
    {
        for(int i = 0; i <= kSineSize; i++)
        {
            const float x = kTwoPi * (float)i / (float)kSineSize;
            t[i] = (sinf(x) + h2 * sinf(2.f * x) + h3 * sinf(3.f * x)) / (1.f + h2 + h3);
        }
    }

    inline float Read(uint32_t phase) const
    {
        const uint32_t idx  = phase >> (32 - kSineBits);
        const float    frac = (float)(phase & ((1u << (32 - kSineBits)) - 1))
                           * (1.f / (float)(1u << (32 - kSineBits)));
        const float a = t[idx];
        return a + (t[idx + 1] - a) * frac;
    }

    /** phase in cycles, [0, 1) */
    inline float ReadF(float phase) const
    {
        const float fi   = phase * (float)kSineSize;
        const int   idx  = (int)fi;
        const float frac = fi - (float)idx;
        const float a    = t[idx & (kSineSize - 1)];
        return a + (t[(idx & (kSineSize - 1)) + 1] - a) * frac;
    }
};

struct Biquad
{
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
    float z1 = 0.f, z2 = 0.f;

    enum Type { LP, HP, PEAK };

    void Set(Type type, float fc, float q, float fs, float gain_db = 0.f)
    {
        const float w0 = kTwoPi * fc / fs;
        const float cw = cosf(w0);
        const float sw = sinf(w0);
        const float al = sw / (2.f * q);
        float nb0, nb1, nb2, na0, na1, na2;
        if(type == LP)
        {
            nb0 = (1.f - cw) * .5f; nb1 = 1.f - cw; nb2 = nb0;
            na0 = 1.f + al; na1 = -2.f * cw; na2 = 1.f - al;
        }
        else if(type == HP)
        {
            nb0 = (1.f + cw) * .5f; nb1 = -(1.f + cw); nb2 = nb0;
            na0 = 1.f + al; na1 = -2.f * cw; na2 = 1.f - al;
        }
        else
        {
            const float A = powf(10.f, gain_db / 40.f);
            nb0 = 1.f + al * A; nb1 = -2.f * cw; nb2 = 1.f - al * A;
            na0 = 1.f + al / A; na1 = -2.f * cw; na2 = 1.f - al / A;
        }
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0;
        a1 = na1 / na0; a2 = na2 / na0;
    }

    inline float Process(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void Reset() { z1 = z2 = 0.f; }
};

/** cheap tanh, exact at 0 and continuous at the +/-3 clamp */
inline float FastTanh(float x)
{
    if(x > 3.f) return 1.f;
    if(x < -3.f) return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

inline float Slew(float cur, float tgt, float coef) { return cur + (tgt - cur) * coef; }

/** One-pole coefficient for a time constant in seconds */
inline float TimeCoef(float seconds, float fs)
{
    return 1.f - expf(-1.f / (seconds * fs));
}

/** Fractional delay line, power of two length, linear interpolation */
template <int N>
struct DelayLine
{
    float buf[N];
    int   w = 0;

    void Reset()
    {
        memset(buf, 0, sizeof(buf));
        w = 0;
    }
    inline void Write(float x)
    {
        w       = (w + 1) & (N - 1);
        buf[w]  = x;
    }
    /** d in samples, >= 1 */
    inline float Read(float d) const
    {
        const int   di   = (int)d;
        const float frac = d - (float)di;
        const float a    = buf[(w - di) & (N - 1)];
        const float b    = buf[(w - di - 1) & (N - 1)];
        return a + (b - a) * frac;
    }
};

/** ---------------------------------------------------------------------
 *  Vibrato / chorus scanner
 *  A swept tap on a short delay line, like the scanner reading taps off the
 *  organ's LC delay line. Chorus = vibrato mixed with the dry signal.
 *  --------------------------------------------------------------------- */
class Scanner
{
  public:
    void Init(float fs, const WaveTable *sine)
    {
        fs_    = fs;
        sine_  = sine;
        inc_   = 6.86f / fs; // scanner rotation in a 60 Hz console
        smooth_ = TimeCoef(.015f, fs);
        lp_coef_ = 1.f - expf(-kTwoPi * 7000.f / fs);
        line_.Reset();
        SetMode(VIB_OFF);
        depth_ = depth_tgt_;
        dry_   = dry_tgt_;
        wet_   = wet_tgt_;
    }

    void SetMode(uint8_t mode)
    {
        if(mode >= VIB_LAST)
            mode = VIB_OFF;
        mode_ = mode;
        // peak-to-peak sweep in ms for depth 1/2/3
        static constexpr float kDepthMs[4] = {0.f, .30f, .55f, .85f};
        const int   d     = mode == VIB_OFF ? 0 : (mode <= VIB_V3 ? mode : mode - 3);
        depth_tgt_        = kDepthMs[d] * .001f * fs_;
        const bool chorus = mode >= VIB_C1;
        if(mode == VIB_OFF)
        {
            dry_tgt_ = 1.f;
            wet_tgt_ = 0.f;
        }
        else if(chorus)
        {
            dry_tgt_ = .6f;
            wet_tgt_ = .6f;
        }
        else
        {
            dry_tgt_ = 0.f;
            wet_tgt_ = 1.f;
        }
    }

    uint8_t GetMode() const { return mode_; }

    void Process(float *buf, int n)
    {
        const float base = .00015f * fs_ + 1.f;
        for(int i = 0; i < n; i++)
        {
            depth_ = Slew(depth_, depth_tgt_, smooth_);
            dry_   = Slew(dry_, dry_tgt_, smooth_);
            wet_   = Slew(wet_, wet_tgt_, smooth_);

            phase_ += inc_;
            if(phase_ >= 1.f)
                phase_ -= 1.f;
            // the scanner sweep is closer to a triangle than a sine
            const float tri = 4.f * fabsf(phase_ - .5f) - 1.f;
            const float sn  = sine_->ReadF(phase_);
            const float lfo = .7f * tri + .3f * sn; // -1..1

            const float x = buf[i];
            line_.Write(x);
            float wet = line_.Read(base + depth_ * .5f * (1.f + lfo));
            lp_ += (wet - lp_) * lp_coef_;
            buf[i] = dry_ * x + wet_ * lp_;
        }
    }

  private:
    DelayLine<128>   line_;
    const WaveTable *sine_;
    float            fs_, inc_, phase_ = 0.f, smooth_, lp_coef_, lp_ = 0.f;
    float            depth_, depth_tgt_, dry_, dry_tgt_, wet_, wet_tgt_;
    uint8_t          mode_ = VIB_OFF;
};

/** ---------------------------------------------------------------------
 *  Cabinet preamp: asymmetric tube-style saturation, 2x oversampled
 *  --------------------------------------------------------------------- */
class Preamp
{
  public:
    void Init(float fs)
    {
        const float fs2 = fs * 2.f;
        // 4th order Butterworth at 19 kHz for both interpolation and decimation
        up_[0].Set(Biquad::LP, 19000.f, .5412f, fs2);
        up_[1].Set(Biquad::LP, 19000.f, 1.3066f, fs2);
        dn_[0].Set(Biquad::LP, 19000.f, .5412f, fs2);
        dn_[1].Set(Biquad::LP, 19000.f, 1.3066f, fs2);
        dc_coef_ = 1.f - expf(-kTwoPi * 12.f / fs);
        smooth_  = TimeCoef(.02f, fs);
        SetDrive(0.f);
        pre_    = pre_tgt_;
        makeup_ = makeup_tgt_;
    }

    /** 0 = clean, 1 = cooking */
    void SetDrive(float d)
    {
        if(d < 0.f) d = 0.f;
        if(d > 1.f) d = 1.f;
        pre_tgt_ = .8f * powf(20.f, d);
        // keeps perceived level roughly constant as the drive comes up
        makeup_tgt_ = 1.f / (.45f + .55f * powf(pre_tgt_, .55f));
    }

    void Process(float *buf, int n)
    {
        static constexpr float kBias = .18f;
        const float            bias_out = FastTanh(kBias);
        for(int i = 0; i < n; i++)
        {
            pre_    = Slew(pre_, pre_tgt_, smooth_);
            makeup_ = Slew(makeup_, makeup_tgt_, smooth_);

            const float x  = buf[i] * 2.f; // zero-stuffing gain
            float       u0 = up_[1].Process(up_[0].Process(x));
            float       u1 = up_[1].Process(up_[0].Process(0.f));
            u0             = FastTanh(pre_ * u0 + kBias) - bias_out;
            u1             = FastTanh(pre_ * u1 + kBias) - bias_out;
            dn_[1].Process(dn_[0].Process(u0));
            float y = dn_[1].Process(dn_[0].Process(u1));

            // the bias leaves a little DC behind
            dc_ += (y - dc_) * dc_coef_;
            buf[i] = (y - dc_) * makeup_;
        }
    }

  private:
    Biquad up_[2], dn_[2];
    float  pre_, pre_tgt_, makeup_, makeup_tgt_, smooth_, dc_coef_, dc_ = 0.f;
};

/** ---------------------------------------------------------------------
 *  Rotary speaker: treble horn and bass drum on separate rotors with their
 *  own inertia, picked up by two virtual mics. Each rotor is a modulated
 *  delay line (Doppler) with amplitude and, for the horn, brightness that
 *  follow where the rotor points, plus a couple of cabinet reflections.
 *  --------------------------------------------------------------------- */
class Rotary
{
  public:
    void Init(float fs, const WaveTable *sine)
    {
        fs_   = fs;
        sine_ = sine;
        horn_line_.Reset();
        drum_line_.Reset();

        // Linkwitz-Riley 4th order crossover at 800 Hz
        for(int i = 0; i < 2; i++)
        {
            xlp_[i].Set(Biquad::LP, 800.f, .7071f, fs);
            xhp_[i].Set(Biquad::HP, 800.f, .7071f, fs);
        }
        // horn driver/throat colour
        horn_peak_.Set(Biquad::PEAK, 2800.f, 1.2f, fs, 3.f);

        horn_up_   = TimeCoef(.16f, fs);
        horn_down_ = TimeCoef(.45f, fs);
        drum_up_   = TimeCoef(1.25f, fs);
        drum_down_ = TimeCoef(1.6f, fs);

        // horn brightness: pointing at the mic vs pointing away
        lp_face_ = 1.f - expf(-kTwoPi * 9000.f / fs);
        lp_away_ = 1.f - expf(-kTwoPi * 2600.f / fs);

        smooth_ = TimeCoef(.03f, fs);
        InitAngles();
        horn_hz_ = kHornSlow;
        drum_hz_ = kDrumSlow;
        // start the rotors at unrelated angles
        horn_ph_ = .13f;
        drum_ph_ = .61f;
        SetSpeed(RotorSpeed::SLOW);
        SetBalance(.5f);
        SetMix(1.f);
        horn_lvl_ = horn_lvl_tgt_;
        drum_lvl_ = drum_lvl_tgt_;
        mix_      = mix_tgt_;
    }

    void SetSpeed(RotorSpeed s)
    {
        speed_ = s;
        switch(s)
        {
            case RotorSpeed::SLOW:
                horn_tgt_ = kHornSlow;
                drum_tgt_ = kDrumSlow;
                break;
            case RotorSpeed::FAST:
                horn_tgt_ = kHornFast;
                drum_tgt_ = kDrumFast;
                break;
            case RotorSpeed::STOP:
                horn_tgt_ = 0.f;
                drum_tgt_ = 0.f;
                break;
        }
    }
    RotorSpeed GetSpeed() const { return speed_; }

    /** 0 = all drum, .5 = stock, 1 = all horn */
    void SetBalance(float b)
    {
        if(b < 0.f) b = 0.f;
        if(b > 1.f) b = 1.f;
        horn_lvl_tgt_ = 1.2f * sqrtf(b);
        drum_lvl_tgt_ = 1.2f * sqrtf(1.f - b);
    }

    /** 0 = direct (mono, no rotor), 1 = full cabinet */
    void SetMix(float m)
    {
        if(m < 0.f) m = 0.f;
        if(m > 1.f) m = 1.f;
        mix_tgt_ = m;
    }

    /** current horn speed as 0..1 of fast, for LEDs */
    float HornSpeedNorm() const { return horn_hz_ / kHornFast; }
    float HornPhase() const { return horn_ph_; }

    void Process(const float *in, float *outl, float *outr, int n)
    {
        const float horn_dop = kHornDopplerSec * fs_;
        const float drum_dop = kDrumDopplerSec * fs_;
        const float inv_fs   = 1.f / fs_;

        for(int i = 0; i < n; i++)
        {
            horn_hz_ = Slew(horn_hz_, horn_tgt_, horn_tgt_ > horn_hz_ ? horn_up_ : horn_down_);
            drum_hz_ = Slew(drum_hz_, drum_tgt_, drum_tgt_ > drum_hz_ ? drum_up_ : drum_down_);
            horn_ph_ += horn_hz_ * inv_fs;
            drum_ph_ += drum_hz_ * inv_fs;
            if(horn_ph_ >= 1.f) horn_ph_ -= 1.f;
            if(drum_ph_ >= 1.f) drum_ph_ -= 1.f;

            horn_lvl_ = Slew(horn_lvl_, horn_lvl_tgt_, smooth_);
            drum_lvl_ = Slew(drum_lvl_, drum_lvl_tgt_, smooth_);
            mix_      = Slew(mix_, mix_tgt_, smooth_);

            const float x  = in[i];
            const float lo = xlp_[1].Process(xlp_[0].Process(x));
            const float hi = horn_peak_.Process(xhp_[1].Process(xhp_[0].Process(x)));
            horn_line_.Write(hi);
            drum_line_.Write(lo);

            // rotor direction once per sample; each mic/tap is a fixed angle
            // offset, so cos(theta - a) = cos(theta)cos(a) + sin(theta)sin(a)
            const float hc = Cos(horn_ph_), hs = Sin(horn_ph_);
            const float dc = Cos(drum_ph_), ds = Sin(drum_ph_);

            float out[2];
            for(int m = 0; m < 2; m++)
            {
                float h = 0.f;
                for(int t = 0; t < kHornTaps; t++)
                {
                    // c = 1 when the horn mouth points at this mic
                    const float c  = hc * horn_cos_[m][t] + hs * horn_sin_[m][t];
                    const float pt = .5f + .5f * c;
                    const float d  = kHornTap[t].base * fs_ + horn_dop * (1.f - pt);
                    float       s  = horn_line_.Read(d);
                    const float a  = lp_away_ + (lp_face_ - lp_away_) * pt;
                    float &     z  = horn_lp_[m][t];
                    z += (s - z) * a;
                    h += z * kHornTap[t].gain * (1.f - kHornAM + kHornAM * pt);
                }

                float dr = 0.f;
                for(int t = 0; t < kDrumTaps; t++)
                {
                    const float c  = dc * drum_cos_[m][t] + ds * drum_sin_[m][t];
                    const float pt = .5f + .5f * c;
                    const float d  = kDrumTap[t].base * fs_ + drum_dop * (1.f - pt);
                    dr += drum_line_.Read(d) * kDrumTap[t].gain * (1.f - kDrumAM + kDrumAM * pt);
                }
                out[m] = h * horn_lvl_ + dr * drum_lvl_;
            }

            const float dry = x * (1.f - mix_);
            outl[i]         = dry + out[0] * mix_;
            outr[i]         = dry + out[1] * mix_;
        }
    }

  private:
    struct Tap
    {
        float angle; // cycles
        float base;  // seconds
        float gain;
    };

    static constexpr float kHornSlow = .80f, kHornFast = 6.75f;
    static constexpr float kDrumSlow = .67f, kDrumFast = 5.9f;
    static constexpr float kHornDopplerSec = .00087f; // ~15 cm horn radius
    static constexpr float kDrumDopplerSec = .00035f;
    static constexpr float kHornAM = .62f, kDrumAM = .32f;
    static constexpr int   kHornTaps = 3, kDrumTaps = 2;
    // Arrays live in plain members (C++14: no inline static arrays)
    const float kMicAngle[2] = {0.f, .42f}; // ~150 degrees apart
    const Tap   kHornTap[kHornTaps] = {{0.f, .0006f, 1.f}, {.25f, .0012f, .34f}, {.62f, .0024f, .2f}};
    const Tap   kDrumTap[kDrumTaps] = {{0.f, .0008f, 1.f}, {.5f, .0017f, .28f}};

    /** ph in [0, 1) */
    inline float Sin(float ph) const { return sine_->ReadF(ph); }
    inline float Cos(float ph) const
    {
        ph += .25f;
        if(ph >= 1.f)
            ph -= 1.f;
        return sine_->ReadF(ph);
    }

    void InitAngles()
    {
        for(int m = 0; m < 2; m++)
        {
            for(int t = 0; t < kHornTaps; t++)
            {
                const float a   = kTwoPi * (kMicAngle[m] + kHornTap[t].angle);
                horn_cos_[m][t] = cosf(a);
                horn_sin_[m][t] = sinf(a);
            }
            for(int t = 0; t < kDrumTaps; t++)
            {
                const float a   = kTwoPi * (kMicAngle[m] + kDrumTap[t].angle);
                drum_cos_[m][t] = cosf(a);
                drum_sin_[m][t] = sinf(a);
            }
        }
    }

    float horn_cos_[2][kHornTaps], horn_sin_[2][kHornTaps];
    float drum_cos_[2][kDrumTaps], drum_sin_[2][kDrumTaps];

    DelayLine<1024>  horn_line_, drum_line_;
    const WaveTable *sine_;
    Biquad           xlp_[2], xhp_[2], horn_peak_;
    float            fs_;
    float            horn_hz_, horn_tgt_, drum_hz_, drum_tgt_;
    float            horn_up_, horn_down_, drum_up_, drum_down_;
    float            horn_ph_, drum_ph_;
    float            lp_face_, lp_away_;
    float            horn_lp_[2][kHornTaps] = {};
    float            horn_lvl_, horn_lvl_tgt_, drum_lvl_, drum_lvl_tgt_;
    float            mix_, mix_tgt_, smooth_;
    RotorSpeed       speed_ = RotorSpeed::SLOW;
};

/** ---------------------------------------------------------------------
 *  The organ: generator, busbars, percussion, click, and the chain above
 *  --------------------------------------------------------------------- */
class ToneWheelOrgan
{
  public:
    void Init(float fs)
    {
        fs_ = fs;
        // tonewheels are close to, but not quite, sine waves
        wheel_tab_.Init(.012f, .008f);
        sine_tab_.Init(0.f, 0.f);

        InitWheels();
        InitKeyMap();
        // ~3 dB per drawbar step, 8 = full, 0 = off
        level_gain_[0] = 0.f;
        for(int l = 1; l <= 8; l++)
            level_gain_[l] = powf(10.f, -3.f * (float)(8 - l) / 20.f);

        memset(cur_gain_, 0, sizeof(cur_gain_));
        memset(tgt_gain_, 0, sizeof(tgt_gain_));
        memset(perc_gain_, 0, sizeof(perc_gain_));
        memset(key_refs_, 0, sizeof(key_refs_));
        memset(contact_, 0, sizeof(contact_));
        memset(contact_want_, 0, sizeof(contact_want_));
        memset(contact_timer_, 0, sizeof(contact_timer_));
        keys_down_ = 0;
        settling_  = false;
        perc_trigger_ = perc_attack_ = false;

        for(int d = 0; d < kNumDrawbars; d++)
            drawbar_[d] = 0;
        drawbar_[0] = drawbar_[1] = drawbar_[2] = 8;

        perc_on_ = false;
        perc_third_ = true;
        perc_soft_  = false;
        perc_fast_  = true;
        perc_env_   = 0.f;

        click_amt_ = .5f;
        leak_amt_  = .3f;
        click_env_ = 0.f;
        expr_ = expr_tgt_ = 1.f;
        expr_coef_ = TimeCoef(.01f, fs);
        click_decay_ = expf(-1.f / (.0018f * fs));
        click_hp_coef_ = expf(-kTwoPi * 600.f / fs);
        click_lp_coef_ = 1.f - expf(-kTwoPi * 4500.f / fs);
        manual_gain_ = manual_gain_tgt_ = 1.f;

        scanner_.Init(fs, &sine_tab_);
        preamp_.Init(fs);
        rotary_.Init(fs, &sine_tab_);

        UpdatePercDecay();
        dirty_ = true;
    }

    /** ---- key interface (manual key 0..60) ---- */
    void NoteOn(int key)
    {
        key = FoldKey(key);
        if(key_refs_[key]++ > 0)
            return;

        // single-trigger percussion: only fires if every key was up
        if(perc_on_ && keys_down_ == 0)
            perc_trigger_ = true;
        keys_down_++;

        const int spread = ContactSpread();
        for(int d = 0; d < kNumDrawbars; d++)
        {
            contact_want_[key][d] = 1;
            contact_timer_[key][d] = spread ? (int8_t)(Rand() % (spread + 1)) : 0;
        }
        TriggerClick(1.f);
        settling_ = true;
        dirty_ = true;
    }

    void NoteOff(int key)
    {
        key = FoldKey(key);
        if(key_refs_[key] == 0)
            return;
        if(--key_refs_[key] > 0)
            return;

        keys_down_--;
        const int spread = ContactSpread();
        for(int d = 0; d < kNumDrawbars; d++)
        {
            contact_want_[key][d] = 0;
            contact_timer_[key][d] = spread ? (int8_t)(Rand() % (spread + 1)) : 0;
        }
        TriggerClick(.45f);
        settling_ = true;
        dirty_ = true;
    }

    void AllNotesOff()
    {
        for(int k = 0; k < kNumKeys; k++)
        {
            if(key_refs_[k])
            {
                key_refs_[k] = 1;
                NoteOff(k);
            }
        }
        keys_down_ = 0;
    }

    bool KeyIsDown(int key) const { return key_refs_[FoldKey(key)] > 0; }

    /** ---- parameters ---- */
    void SetDrawbar(int idx, int level)
    {
        if(idx < 0 || idx >= kNumDrawbars)
            return;
        if(level < 0) level = 0;
        if(level > 8) level = 8;
        if(drawbar_[idx] != level)
        {
            drawbar_[idx] = (uint8_t)level;
            dirty_ = true;
        }
    }
    int GetDrawbar(int idx) const { return (idx >= 0 && idx < kNumDrawbars) ? drawbar_[idx] : 0; }

    void SetVibrato(uint8_t mode) { scanner_.SetMode(mode); }
    uint8_t GetVibrato() const { return scanner_.GetMode(); }

    void SetPercussion(bool on)
    {
        if(perc_on_ != on)
        {
            perc_on_ = on;
            dirty_   = true; // percussion borrows the 1' busbar
            UpdateManualGain();
        }
    }
    void SetPercThird(bool third)
    {
        perc_third_ = third;
        dirty_      = true;
    }
    void SetPercSoft(bool soft)
    {
        perc_soft_ = soft;
        UpdateManualGain();
    }
    void SetPercFast(bool fast)
    {
        perc_fast_ = fast;
        UpdatePercDecay();
    }
    bool GetPercussion() const { return perc_on_; }
    bool GetPercThird() const { return perc_third_; }
    bool GetPercSoft() const { return perc_soft_; }
    bool GetPercFast() const { return perc_fast_; }

    /** 0..1 */
    void  SetClick(float c) { click_amt_ = Clamp01(c); }
    float GetClick() const { return click_amt_; }
    void  SetLeakage(float l)
    {
        leak_amt_ = Clamp01(l);
        dirty_    = true;
    }
    float GetLeakage() const { return leak_amt_; }
    void  SetDrive(float d) { preamp_.SetDrive(d); }
    /** swell pedal, 0..1 */
    void SetExpression(float e)
    {
        e         = Clamp01(e);
        expr_tgt_ = .05f + .95f * e * e;
    }

    void       SetRotorSpeed(RotorSpeed s) { rotary_.SetSpeed(s); }
    RotorSpeed GetRotorSpeed() const { return rotary_.GetSpeed(); }
    void       SetRotorBalance(float b) { rotary_.SetBalance(b); }
    void       SetRotorMix(float m) { rotary_.SetMix(m); }
    float      HornSpeedNorm() const { return rotary_.HornSpeedNorm(); }
    float      HornPhase() const { return rotary_.HornPhase(); }

    /** Call once per block before Process(). Cheap unless something changed. */
    void UpdateControl()
    {
        // contacts close/open on their own schedule (busbar contact bounce).
        // Only scanned while some key is still settling.
        if(settling_)
        {
            bool still = false;
            for(int k = 0; k < kNumKeys; k++)
            {
                for(int d = 0; d < kNumDrawbars; d++)
                {
                    if(contact_[k][d] != contact_want_[k][d])
                    {
                        if(contact_timer_[k][d] > 0)
                        {
                            contact_timer_[k][d]--;
                            still = true;
                        }
                        else
                        {
                            contact_[k][d] = contact_want_[k][d];
                            dirty_         = true;
                        }
                    }
                }
            }
            settling_ = still;
        }

        if(dirty_)
        {
            RecomputeGains();
            dirty_ = false;
        }
    }

    /** n <= kMaxBlock. Writes stereo. */
    void Process(float *outl, float *outr, int n)
    {
        if(n > kMaxBlock)
            n = kMaxBlock;

        float mono[kMaxBlock];
        float pacc[kMaxBlock];
        float penv[kMaxBlock];
        memset(mono, 0, sizeof(float) * n);
        memset(pacc, 0, sizeof(float) * n);

        // percussion envelope, sample accurate
        if(perc_trigger_)
        {
            perc_trigger_ = false;
            perc_attack_  = true;
        }
        for(int i = 0; i < n; i++)
        {
            if(perc_attack_)
            {
                perc_env_ += .25f; // ~4 sample rise, avoids a hard step
                if(perc_env_ >= 1.f)
                {
                    perc_env_    = 1.f;
                    perc_attack_ = false;
                }
            }
            else
            {
                perc_env_ *= perc_decay_;
                if(perc_env_ < 1e-5f)
                    perc_env_ = 0.f;
            }
            penv[i] = perc_env_;
        }

        // ramp length controls how hard a contact closing steps the level
        int ramp = 1 + (int)((1.f - click_amt_) * (float)(n - 1));
        if(ramp > n) ramp = n;
        const float inv_ramp = 1.f / (float)ramp;

        // ---- the generator ----
        for(int w = kFirstWheel; w <= kLastWheel; w++)
        {
            uint32_t    ph  = phase_[w];
            const uint32_t inc = inc_[w];
            const float g0  = cur_gain_[w];
            const float g1  = tgt_gain_[w];
            const float pg  = perc_gain_[w];

            if(g0 == 0.f && g1 == 0.f && pg == 0.f)
            {
                phase_[w] = ph + inc * (uint32_t)n; // wheels never stop spinning
                continue;
            }

            if(g0 == g1)
            {
                if(pg == 0.f)
                {
                    for(int i = 0; i < n; i++)
                    {
                        mono[i] += g0 * wheel_tab_.Read(ph);
                        ph += inc;
                    }
                }
                else
                {
                    for(int i = 0; i < n; i++)
                    {
                        const float s = wheel_tab_.Read(ph);
                        mono[i] += g0 * s;
                        pacc[i] += pg * s;
                        ph += inc;
                    }
                }
            }
            else
            {
                const float dg = (g1 - g0) * inv_ramp;
                float       g  = g0;
                for(int i = 0; i < n; i++)
                {
                    if(i < ramp)
                        g += dg;
                    else
                        g = g1;
                    const float s = wheel_tab_.Read(ph);
                    mono[i] += g * s;
                    pacc[i] += pg * s;
                    ph += inc;
                }
                cur_gain_[w] = g1;
            }
            phase_[w] = ph;
        }

        // ---- mix busbars, percussion and key click ----
        const float perc_lvl = perc_soft_ ? kPercSoft : kPercNormal;
        for(int i = 0; i < n; i++)
        {
            manual_gain_ = Slew(manual_gain_, manual_gain_tgt_, expr_coef_);
            expr_        = Slew(expr_, expr_tgt_, expr_coef_);

            float click = 0.f;
            if(click_env_ > 0.f)
            {
                const float nz = (float)(int32_t)Rand() * (1.f / 2147483648.f);
                // band-limit the noise: one-pole HP into one-pole LP
                const float hp = nz - click_hp_x_ + click_hp_coef_ * click_hp_y_;
                click_hp_x_ = nz;
                click_hp_y_ = hp;
                click_lp_ += (hp - click_lp_) * click_lp_coef_;
                click = click_lp_ * click_env_;
                click_env_ *= click_decay_;
                if(click_env_ < 1e-5f)
                    click_env_ = 0.f;
            }

            mono[i] = (mono[i] * manual_gain_ + pacc[i] * penv[i] * perc_lvl + click)
                      * kGenScale * expr_;
        }

        scanner_.Process(mono, n);
        preamp_.Process(mono, n);
        rotary_.Process(mono, outl, outr, n);
    }

  private:
    static constexpr float kGenScale   = .085f;
    static constexpr float kPercNormal = 1.3f;
    static constexpr float kPercSoft   = .6f;

    static inline float Clamp01(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

    /** keep any incoming key on the manual by folding whole octaves */
    static inline int FoldKey(int key)
    {
        while(key < 0)
            key += 12;
        while(key >= kNumKeys)
            key -= 12;
        return key;
    }

    inline uint32_t Rand()
    {
        rng_ = rng_ * 1664525u + 1013904223u;
        return rng_;
    }

    /** more click = contacts scatter over more blocks (up to ~1.5 ms) */
    int ContactSpread() const { return (int)(click_amt_ * 3.f + .5f); }

    void TriggerClick(float scale)
    {
        const float r   = .7f + .3f * (float)(Rand() >> 8) * (1.f / 16777216.f);
        const float amt = click_amt_ * click_amt_ * 4.f * scale * r;
        if(amt > click_env_)
            click_env_ = amt;
    }

    void UpdatePercDecay()
    {
        // time to -60 dB
        const float t60 = perc_fast_ ? 1.f : 4.f;
        perc_decay_     = expf(-6.9078f / (t60 * fs_));
    }

    void UpdateManualGain()
    {
        // with percussion at normal volume the rest of the manual drops ~3 dB
        manual_gain_tgt_ = (perc_on_ && !perc_soft_) ? .7f : 1.f;
    }

    void InitWheels()
    {
        // B-3 tone generator gear ratios, C..B, driven at 20 rev/s (1200 rpm)
        static constexpr float kGear[12][2] = {{85, 104}, {71, 82}, {67, 73}, {105, 108},
                                               {103, 100}, {84, 77}, {74, 64}, {98, 80},
                                               {96, 74}, {88, 64}, {67, 46}, {108, 70}};
        uint32_t seed = 0x5eed1234u;
        for(int w = kFirstWheel; w <= kLastWheel; w++)
        {
            float hz;
            if(w <= 84)
            {
                const int note  = (w - 1) % 12;
                const int teeth = 2 << ((w - 1) / 12);
                hz = 20.f * kGear[note][0] / kGear[note][1] * (float)teeth;
            }
            else
            {
                // top seven wheels: 192 teeth on the F..B gears
                const int note = (w - 85) + 5;
                hz = 20.f * kGear[note][0] / kGear[note][1] * 192.f;
            }
            inc_[w] = (uint32_t)((double)hz / (double)fs_ * 4294967296.0);

            seed       = seed * 1664525u + 1013904223u;
            phase_[w]  = seed;

            // output tapers off toward the top of the generator, and no two
            // wheels are quite the same level (+/- ~0.5 dB)
            float taper = 1.f;
            if(w > 48)
                taper = 1.f - .42f * (float)(w - 48) / 43.f;
            seed = seed * 1664525u + 1013904223u;
            const float var = ((float)(seed >> 8) * (1.f / 16777216.f) - .5f) * .12f;
            wheel_level_[w] = taper * (1.f + var);
        }
        wheel_level_[0] = 0.f;
        inc_[0]         = 0;
        phase_[0]       = 0;
    }

    void InitKeyMap()
    {
        for(int k = 0; k < kNumKeys; k++)
        {
            for(int d = 0; d < kNumDrawbars; d++)
                key_wheel_[k][d] = (uint8_t)FoldWheel(k + 13 + kDrawbarOffset[d]);
            perc_wheel_[k][0] = (uint8_t)FoldWheel(k + 13 + 12); // 2nd harmonic (4')
            perc_wheel_[k][1] = (uint8_t)FoldWheel(k + 13 + 19); // 3rd harmonic (2 2/3')
        }
    }

    /** manuals never reach wheels 1-12 (16' folds back) and fold down at the top */
    static int FoldWheel(int w)
    {
        while(w < 13)
            w += 12;
        while(w > kLastWheel)
            w -= 12;
        return w;
    }

    void RecomputeGains()
    {
        float base[kNumWheels];
        memset(base, 0, sizeof(base));
        memset(perc_gain_, 0, sizeof(perc_gain_));

        float bar[kNumDrawbars];
        for(int d = 0; d < kNumDrawbars; d++)
            bar[d] = level_gain_[drawbar_[d]];
        if(perc_on_)
            bar[8] = 0.f; // 1' busbar is taken over by the percussion circuit

        for(int k = 0; k < kNumKeys; k++)
        {
            for(int d = 0; d < kNumDrawbars; d++)
            {
                if(contact_[k][d] && bar[d] > 0.f)
                    base[key_wheel_[k][d]] += bar[d];
            }
            if(perc_on_ && key_refs_[k])
                perc_gain_[perc_wheel_[k][perc_third_ ? 1 : 0]] += 1.f;
        }

        // generator leakage: each wheel shares a magnet compartment with the
        // wheel four octaves away, which bleeds faintly into anything playing
        const float leak = leak_amt_ * .035f;
        for(int w = kFirstWheel; w <= kLastWheel; w++)
        {
            float g = base[w];
            if(leak > 0.f)
            {
                if(w + 48 <= kLastWheel)
                    g += base[w + 48] * leak;
                if(w - 48 >= kFirstWheel)
                    g += base[w - 48] * leak;
            }
            tgt_gain_[w]  = g * wheel_level_[w];
            perc_gain_[w] *= wheel_level_[w];
        }
    }

    float fs_;
    WaveTable wheel_tab_, sine_tab_;

    uint32_t phase_[kNumWheels];
    uint32_t inc_[kNumWheels];
    float    wheel_level_[kNumWheels];
    float    cur_gain_[kNumWheels];
    float    tgt_gain_[kNumWheels];
    float    perc_gain_[kNumWheels];

    uint8_t key_wheel_[kNumKeys][kNumDrawbars];
    uint8_t perc_wheel_[kNumKeys][2];
    uint8_t key_refs_[kNumKeys];
    uint8_t contact_[kNumKeys][kNumDrawbars];
    uint8_t contact_want_[kNumKeys][kNumDrawbars];
    int8_t  contact_timer_[kNumKeys][kNumDrawbars];
    int     keys_down_ = 0;
    uint8_t drawbar_[kNumDrawbars];
    float   level_gain_[9];
    bool    dirty_;
    bool    settling_ = false;

    bool  perc_on_, perc_third_, perc_soft_, perc_fast_;
    bool  perc_trigger_ = false, perc_attack_ = false;
    float perc_env_, perc_decay_;

    float click_amt_, click_env_, click_decay_;
    float click_hp_coef_, click_hp_x_ = 0.f, click_hp_y_ = 0.f;
    float click_lp_coef_, click_lp_ = 0.f;
    float leak_amt_;
    float expr_, expr_tgt_, expr_coef_;
    float manual_gain_, manual_gain_tgt_;
    uint32_t rng_ = 22222u;

    Scanner scanner_;
    Preamp  preamp_;
    Rotary  rotary_;
};

} // namespace organ

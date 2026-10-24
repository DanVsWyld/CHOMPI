/** @file organEngine.h
 *  @brief CHOMPI wrapper around the tonewheel organ (ToneWheelOrgan.h).
 *
 *  Keeps the same outside shape as WAVE's subtractiveEngine (class name
 *  myEngine, KeyRequest FIFO, Prepare()/Process()) so the sequencer, MIDI and
 *  UI plumbing carry over. Note calls come in as KeyRequests from MIDI-in, the
 *  keyboard keys, or the Sequencer and are turned into manual key presses.
 *
 *  Thread notes: Prepare()/Process() and MIDI-in run in the audio ISR. The UI
 *  pages run from the main loop and only call the simple setters here, which
 *  write single values the ISR picks up on its next block. stopAllVoices() is
 *  deferred to the ISR for the same reason.
 */
#pragma once
#include "daisy.h"
#include "daisysp.h"
#include "EnvFollower.h"
#include "limiter.h"
#include "reverb.h"
#include "ToneWheelOrgan.h"
#include <cmath>

using namespace daisy;
using namespace daisysp;
// Sequencer/MidiManager name chompi types unqualified (in WAVE this came in
// through DJFilter.h)
using namespace chompi;

// This shouldn't live here, but it's visible where it's needed, so...
static const uint8_t kSlotNone = 100;

struct KeyRequest
{
    enum class Type
    {
        START,
        STOP,
        DUMMY,
        ALL_OFF, // queued by stopAllVoices() so it lands in order with key events
    };

    enum class Source
    {
        USER,
        SEQUENCER,
    };

    Type  type_;
    float transpose_nn_; // MIDI note - 60
    int   key_;          // UI key id (Hardware::SwId for front-panel keys)
    float vel_;
    Source src_;

    KeyRequest(Type type, float transpose_nn, int key, float vel, Source src = Source::USER)
    : type_(type), transpose_nn_(transpose_nn), key_(key), vel_(vel), src_(src)
    {
    }

    KeyRequest()
    : type_(Type::DUMMY), transpose_nn_(0.f), key_(0), vel_(127.f), src_(Source::USER)
    {
    }
};

class myEngine
{
  public:
    static constexpr int kMaxKeyIds = 128;
    static constexpr int kMinOctave = -2;
    static constexpr int kMaxOctave = 2;

    myEngine() {}
    ~myEngine() {}

    FIFO<KeyRequest, 64> request_fifo;

    void Init(float sample_rate, daisysp::Reverb *reverb)
    {
        organ_.Init(sample_rate);

        reverb_ = reverb;
        reverb_->Init(sample_rate);
        reverb_->SetAmount(0.f);
        reverb_->SetInputGain(.25f);
        reverb_->SetTime(.72f);
        reverb_->SetLowpass(.62f);
        reverb_->SetDiffusion(.6f);

        lim_l_.Init();
        lim_r_.Init();
        output_env_follower.Init();

        for(int i = 0; i < kMaxKeyIds; i++)
        {
            manual_key_[i] = 0;
            sounding_[i]   = false;
            from_user_[i]  = false;
            from_seq_[i]   = false;
        }

        octave_     = 0;
        transpose_  = 0;
        gain_ = gain_target_ = 1.f;
        reverb_amt_ = reverb_amt_target_ = 0.f;
        voice_slot_ = 15;
        rotor_fast_ = false;
        brake_      = false;
        all_off_pending_ = false;
        ApplyRotor();
    }

    /** Once per audio block, before Process() */
    void Prepare()
    {
        // drain every queued key event so chords land in the same block
        while(!request_fifo.IsEmpty())
        {
            KeyRequest req = request_fifo.PopFront();
            if(req.type_ == KeyRequest::Type::START)
                StartPlayback(req.transpose_nn_, req.key_, req.src_);
            else if(req.type_ == KeyRequest::Type::STOP)
                StopPlayback(req.key_, req.src_);
            else if(req.type_ == KeyRequest::Type::ALL_OFF)
                AllOff();
        }

        // fallback for when the queue was full at stopAllVoices()
        if(all_off_pending_)
        {
            all_off_pending_ = false;
            AllOff();
        }

        organ_.UpdateControl();
    }

    void Process(const float *const *in, float **out, size_t size)
    {
        (void)in;
        float *l = out[0];
        float *r = out[1];

        // the organ core works in blocks of up to kMaxBlock
        size_t done = 0;
        while(done < size)
        {
            size_t n = size - done;
            if(n > (size_t)organ::kMaxBlock)
                n = organ::kMaxBlock;
            organ_.Process(l + done, r + done, (int)n);
            done += n;
        }

        for(size_t i = 0; i < size; ++i)
        {
            fonepole(reverb_amt_, reverb_amt_target_, .001f);
            fonepole(gain_, gain_target_, .001f);

            reverb_->SetAmount(reverb_amt_);
            reverb_->Process(&l[i], &r[i]);

            const float ol = lim_l_.Process(l[i] * gain_);
            const float or_ = lim_r_.Process(r[i] * gain_);

            out[0][i] = ol;  // headphones
            out[1][i] = or_;
            out[2][i] = ol;  // line out
            out[3][i] = or_;

            output_env_follower.Process(ol + or_);
        }
    }

    /** Called from the UI (main loop) and MIDI. Goes through the key queue
     *  so it is applied in order with key events already waiting. */
    void stopAllVoices()
    {
        if(!request_fifo.PushBack(KeyRequest(KeyRequest::Type::ALL_OFF, 0.f, 0, 0.f)))
            all_off_pending_ = true;
    }

    /** ------------- organ parameters ------------- */
    void setDrawbar(int idx, int level) { organ_.SetDrawbar(idx, level); }
    int  getDrawbar(int idx) const { return organ_.GetDrawbar(idx); }

    void    setVibrato(uint8_t mode) { organ_.SetVibrato(mode); }
    uint8_t getVibrato() const { return organ_.GetVibrato(); }

    void setPercussion(bool on) { organ_.SetPercussion(on); }
    void setPercThird(bool third) { organ_.SetPercThird(third); }
    void setPercSoft(bool soft) { organ_.SetPercSoft(soft); }
    void setPercFast(bool fast) { organ_.SetPercFast(fast); }
    bool getPercussion() const { return organ_.GetPercussion(); }
    bool getPercThird() const { return organ_.GetPercThird(); }
    bool getPercSoft() const { return organ_.GetPercSoft(); }
    bool getPercFast() const { return organ_.GetPercFast(); }

    /** percussion flags packed for presets: on=1, third=2, soft=4, fast=8 */
    uint8_t getPercBits() const
    {
        return (getPercussion() ? 1 : 0) | (getPercThird() ? 2 : 0)
               | (getPercSoft() ? 4 : 0) | (getPercFast() ? 8 : 0);
    }
    void setPercBits(uint8_t b)
    {
        setPercussion(b & 1);
        setPercThird(b & 2);
        setPercSoft(b & 4);
        setPercFast(b & 8);
    }

    void  setClick(float c) { organ_.SetClick(c); }
    float getClick() const { return organ_.GetClick(); }
    void  setLeakage(float l) { organ_.SetLeakage(l); }
    float getLeakage() const { return organ_.GetLeakage(); }

    void  setDrive(float d) { drive_ = d; organ_.SetDrive(d); }
    float getDrive() const { return drive_; }

    void setReverb(float r)
    {
        reverb_param_      = r;
        reverb_amt_target_ = r * r * .55f;
    }
    float getReverb() const { return reverb_param_; }

    void  setRotorMix(float m) { rotor_mix_ = m; organ_.SetRotorMix(m); }
    float getRotorMix() const { return rotor_mix_; }
    void  setRotorBalance(float b) { rotor_balance_ = b; organ_.SetRotorBalance(b); }
    float getRotorBalance() const { return rotor_balance_; }

    void setRotorFast(bool fast)
    {
        rotor_fast_ = fast;
        ApplyRotor();
    }
    bool getRotorFast() const { return rotor_fast_; }
    void setBrake(bool on)
    {
        brake_ = on;
        ApplyRotor();
    }
    bool  getBrake() const { return brake_; }
    float getHornSpeed() const { return organ_.HornSpeedNorm(); }
    float getHornPhase() const { return organ_.HornPhase(); }

    /** swell pedal, 0..1 */
    void setExpression(float e) { organ_.SetExpression(e); }

    /** semitones, applied to new notes */
    void setTranspose(int semis)
    {
        if(semis < -12) semis = -12;
        if(semis > 12) semis = 12;
        transpose_ = semis;
    }
    int getTranspose() const { return transpose_; }

    void setOctave(int value)
    {
        octave_ += value;
        if(octave_ > kMaxOctave) octave_ = kMaxOctave;
        if(octave_ < kMinOctave) octave_ = kMinOctave;
    }
    int getOctave() { return octave_; }

    void setGain(float amount) { gain_target_ = 1.4f * amount * amount; }

    bool isKeyPlaying(int key)
    {
        return key >= 0 && key < kMaxKeyIds && sounding_[key];
    }

    float getVUSample() { return output_env_follower.GetLastSamp(); }

    /** The toggle switch is the half-moon: it only acts when it moves, so a
     *  MIDI rotor-speed CC can override it until the next flip. */
    void setSwitchState(bool state)
    {
        if(!switch_init_ || state != switch_state)
        {
            switch_init_ = true;
            switch_state = state;
            setRotorFast(state);
        }
    }
    bool getSwitchState() { return switch_state; }

    void setVoiceSlot(uint8_t slot) { voice_slot_ = slot; }
    int  getVoiceSlot() { return voice_slot_; }

    // nothing streams from the card in this firmware
    bool ProcessFileRequests() { return false; }
    bool checkLoaded() { return true; }

  private:
    void ApplyRotor()
    {
        organ_.SetRotorSpeed(brake_        ? organ::RotorSpeed::STOP
                             : rotor_fast_ ? organ::RotorSpeed::FAST
                                           : organ::RotorSpeed::SLOW);
    }

    /** MIDI note (transpose_nn + 60) at 8' -> manual key, C2 = key 0 */
    int ManualKey(float transpose_nn) const
    {
        return (int)lrintf(transpose_nn) + 60 + 12 * octave_ + transpose_ - 36;
    }

    /** A key id sounds once, however many sources hold it */
    void StartPlayback(float transpose_nn, int key, KeyRequest::Source src)
    {
        if(key < 0 || key >= kMaxKeyIds)
            return;
        if(src == KeyRequest::Source::USER)
            from_user_[key] = true;
        else
            from_seq_[key] = true;

        if(!sounding_[key])
        {
            // remember where it landed so octave/transpose changes while the
            // key is held still release the right note. Can be outside 0..60;
            // the organ folds it onto the manual.
            manual_key_[key] = (int16_t)ManualKey(transpose_nn);
            sounding_[key]   = true;
            organ_.NoteOn(manual_key_[key]);
        }
    }

    /** Only stops once both the user and the sequencer have let go */
    void StopPlayback(int key, KeyRequest::Source src)
    {
        if(key < 0 || key >= kMaxKeyIds)
            return;
        if(src == KeyRequest::Source::USER)
            from_user_[key] = false;
        else
            from_seq_[key] = false;

        if(!from_user_[key] && !from_seq_[key])
            ReleaseKey(key);
    }

    void ReleaseKey(int key)
    {
        if(sounding_[key])
        {
            organ_.NoteOff(manual_key_[key]);
            sounding_[key] = false;
        }
    }

    void AllOff()
    {
        for(int i = 0; i < kMaxKeyIds; i++)
        {
            from_user_[i] = from_seq_[i] = false;
            ReleaseKey(i);
        }
        organ_.AllNotesOff();
    }

    organ::ToneWheelOrgan organ_;
    daisysp::Reverb      *reverb_;
    chompi::Limiter       lim_l_, lim_r_;
    chompi::EnvFollower   output_env_follower;

    int16_t manual_key_[kMaxKeyIds];
    bool    sounding_[kMaxKeyIds];
    bool    from_user_[kMaxKeyIds];
    bool    from_seq_[kMaxKeyIds];

    int   octave_, transpose_;
    float gain_, gain_target_;
    float reverb_amt_, reverb_amt_target_, reverb_param_ = 0.f;
    float drive_ = 0.f, rotor_mix_ = 1.f, rotor_balance_ = .5f;
    bool  rotor_fast_, brake_;
    bool  switch_state = false, switch_init_ = false;
    volatile bool all_off_pending_;
    uint8_t voice_slot_;
};

/** @file NormalPage.h
 *  @brief The main play-mode UiPage (see ui.h), active until the CHOMPI key
 *  is held. Organ layout:
 *
 *   Knobs 0-2   three drawbars at a time. Click any of them to step the bank:
 *               A = 16'  5 1/3'  8'    B = 4'  2 2/3'  2'    C = 1 3/5'  1 1/3'  1'
 *               One detent = one drawbar step (0-8). The LED takes the
 *               drawbar's colour (brown / white / black shown as blue) and
 *               its brightness follows the level.
 *   Knob 3      page 0 drive (cabinet preamp), page 1 reverb. Click flips page.
 *   Knob 4      tempo, click = tap tempo (sequencer)
 *   Knob 5      page 0 volume (with VU), page 1 rotary mix (0 = direct out).
 *               Hold to show battery.
 *   Toggle      rotary speed: up = slow (chorale), down = fast (tremolo)
 *   CHOMPI key  hold for the shift layer (MenuPage); while recording a
 *               sequence it inserts a rest instead
 *   Play/Loop   sequencer, as in WAVE
 */
#pragma once

#include "hardware.h"
#include "organEngine.h"
#include "Sequencer.h"
#include "temp_led_stuff.h"
#include "clockManager.h"
#include "PresetManager.h"

namespace chompi
{
        static const uint8_t cc_map[3][6] = {
            {20, 21, 22, 23, 24, 25},
            {26, 27, 28, 29, 0, 30},
            {0, 0, 0, 0, 0, 0}};

        /** first MIDI CC of the nine drawbar CCs (16' .. 1') */
        static const uint8_t kDrawbarCCBase = 70;

        static const uint8_t key_map[40] = {
            0x01, /**< ENC_1_SW  page */
            0x02, /**< ENC_2_SW page */
            0x03, /**< ENC_3_SW page */
            0x00, /**< ENC_4_SW page */
            0x04, /**< ENC_5_SW page */
            0x15, /**< KEY_26 chompi cc */
            0x00, /**< SW_TOG skip */
            0x31, /**< KEY_16 */
            0x32, /**< KEY_2 */
            0x34, /**< KEY_3 */
            0x35, /**< KEY_4 */
            0x37, /**< KEY_5 */
            0x33, /**< KEY_17 */
            0x36, /**< KEY_18 */
            0x38, /**< KEY_19 */
            0x30, /**< KEY_1 */
            0x39, /**< KEY_6 */
            0x3b, /**< KEY_7 */
            0x3c, /**< KEY_8 */
            0x3e, /**< KEY_9 */
            0x40, /**< KEY_10 */
            0x3a, /**< KEY_20 */
            0x3d, /**< KEY_21 */
            0x3f, /**< KEY_22 */
            0x41, /**< KEY_11 */
            0x43, /**< KEY_12 */
            0x45, /**< KEY_13 */
            0x47, /**< KEY_14 */
            0x48, /**< KEY_15 */
            0x42, /**< KEY_23 */
            0x44, /**< KEY_24 */
            0x46, /**< KEY_25 */
            0x05, /**< ENC_6_SW page */
            0x17, /**< KEY_27 play cc */
            0x18, /**< KEY_28 loop cc */
            0x00, /**< NC_1 skip */
            0x00, /**< NC_2 skip */
            0x00, /**< NC_3 skip */
            0x00, /**< NC_4 skip */
            0x00, /**< NC_5 skip */
        };

        static const uint8_t led_map[40]{
            2,    /**< ENC_1_SW  page */
            3,    /**< ENC_2_SW page */
            4,    /**< ENC_3_SW page */
            1,    /**< ENC_4_SW cc */
            0,    /**< ENC_5_SW page */
            0,    /**< KEY_26 chompi cc */
            0x00, /**< SW_TOG skip */
            0,    /**< KEY_16 */
            23,   /**< KEY_2 */
            22,   /**< KEY_3 */
            21,   /**< KEY_4 */
            20,   /**< KEY_5 */
            1,    /**< KEY_17 */
            2,    /**< KEY_18 */
            3,    /**< KEY_19 */
            24,   /**< KEY_1 */
            19,   /**< KEY_6 */
            18,   /**< KEY_7 */
            17,   /**< KEY_8 */
            16,   /**< KEY_9 */
            15,   /**< KEY_10 */
            4,    /**< KEY_20 */
            5,    /**< KEY_21 */
            6,    /**< KEY_22 */
            14,   /**< KEY_11 */
            13,   /**< KEY_12 */
            12,   /**< KEY_13 */
            11,   /**< KEY_14 */
            10,   /**< KEY_15 */
            7,    /**< KEY_23 */
            8,    /**< KEY_24 */
            9,    /**< KEY_25 */
            9,    /**< ENC_6_SW page */
            7,    /**< KEY_27 play cc */
            8,    /**< KEY_28 loop cc */
            0x00, /**< NC_1 skip */
            0x00, /**< NC_2 skip */
            0x00, /**< NC_3 skip */
            0x00, /**< NC_4 skip */
            0x00, /**< NC_5 skip */
        };

    static const float kEncoderTempoStep = .003125f;
    static const float kEncoderCoarseStep = .01f;

    uint8_t led_pth_cache[kNumPthLeds][3]; /**< RGB data */
    uint8_t led_smt_cache[kNumSmtLeds][3]; /**< RGB data */

    static const float white[3] = {1.f, 1.f, 1.f};
    static const float red[3] = {1.f, 0.f, 0.f};
    static const float orange[3] = {1.f, .6f, .24f};
    static const float yellow[3] = {1.f, .95f, 0.05f};
    static const float green[3] = {0.f, 1.f, 0.f};
    static const float teal[3] = {.14f, 1.f, .92f};
    static const float med_blue[3] = {0.f, .84f, 1.f};
    static const float blue[3] = {0.f, 0.f, 1.f};
    static const float purple[3] = {.58f, .05f, 1.f};
    static const float pink[3] = {1.f, .36f, .62f};
    static const float brown[3] = {1.f, .3f, .03f};

    /** drawbar colours, 16' .. 1'. Black drawbars show as blue. */
    static const float* const kDrawbarColor[organ::kNumDrawbars] = {
        brown, brown, white, white, med_blue, white, med_blue, med_blue, white};

    static const uint8_t kNumDrawbarBanks = 3;

    // Number of pages per knob (knobs 0-2 use the drawbar bank instead)
    static const uint8_t knob_num_pages[6] = {1, 1, 1, 2, 1, 2};

    class NormalPage : public daisy::UiPage
    {
    public:
        uint32_t init_time;
        bool init_ignore = true;

        void Init(clockManager *clock_manager, myEngine *engine, Sequencer *seq, Hardware *hw, float** enc_arr, const float** def_arr,
                    uint8_t* page, PresetManager* pre, uint8_t midi_out_channel)
        {
            hw_ = hw;
            engine_ = engine;
            seq_ = seq;
            clock_manager_ = clock_manager;
            presets_ = pre;
            enc_values = enc_arr;
            enc_defaults = def_arr;
            knob_page = page;

            midi_channel = midi_out_channel;

            for (int knob = 0; knob < 6; knob++)
            {
                for (int page = 0; page < 3; page++)
                {
                    enc_values[page][knob] = enc_defaults[page][knob];
                }
            }

            for (int i = 0; i < kNumSmtLeds; i++)
                SetSmtLed(i, 0, 0, 0);
            for (int i = 0; i < kNumPthLeds; i++)
                SetPthLed(i, 0, 0, 0);

            // 5mm leds
            SetPthLedFloat(5, 1.f, 1.f, 1.f);
            SetPthLedFloat(6, 1.f, 1.f, 1.f);
            SetPthLedFloat(7, 1.f, 1.f, 1.f);
            SetPthLedFloat(8, 1.f, 1.f, 1.f);

            init_time = System::GetNow();
        }

        void ResetSmtLeds()
        {
            for(size_t i = 0; i < 25; i++)
            {
                SetSmtLed(i, 0.f, 0.f, 0.f);
            }
        }

        void CacheLeds()
        {
            std::copy(&led_pth_data[0][0], &led_pth_data[0][0] + kNumPthLeds * 3, &led_pth_cache[0][0]);
            std::copy(&led_smt_data[0][0], &led_smt_data[0][0] + kNumSmtLeds * 3, &led_smt_cache[0][0]);
        }

        void RefreshLeds()
        {
            std::copy(&led_pth_cache[0][0], &led_pth_cache[0][0] + kNumPthLeds * 3, &led_pth_data[0][0]);
            std::copy(&led_smt_cache[0][0], &led_smt_cache[0][0] + kNumSmtLeds * 3, &led_smt_data[0][0]);
        }

        /** Push the knob-held values (drive, reverb, volume, rotary mix) to the
         *  engine. Cheap, and covers every path that edits enc_values
         *  (knobs, CC, presets, shift-menu resets). */
        void ApplyKnobValues()
        {
            engine_->setDrive(enc_values[0][3]);
            engine_->setReverb(enc_values[1][3]);
            engine_->setGain(enc_values[0][5]);
            engine_->setRotorMix(enc_values[1][5]);
        }

        static void DrawbarLed(uint8_t pth, int drawbar, int level)
        {
            const float* c = kDrawbarColor[drawbar];
            // a faint glow at 0 so the bank is still readable
            const float b = .05f + .95f * (float)level / 8.f;
            SetPthLedFloat(pth, c[0] * b, c[1] * b, c[2] * b);
        }

        // Things that need to run repeatedly like checking sequencer stuff or button holds run here
        void Draw(const daisy::UiCanvasDescriptor &canvasDescriptor) override
        {
            uint32_t now = System::GetNow();

            if (seq_->checkReset()) {
                last_clear_blink = now;
            }
            if (seq_->showClearedAll()) {
                erase_rainbow_startt = now;
            }
            if (seq_->showSequenceFull()) {
                full_blink_startt = now;
            }

            // ignore the first 1500 ms of inputs. Hack to stop random button presses on boot for now.
            if (init_ignore)
            {
                if (now - init_time > 1500)
                {
                    init_ignore = false;
                }
            }

            ApplyKnobValues();

            for(size_t i = 7; i < (25 + 7); i++)
            {
                if (engine_->isKeyPlaying(i)) {
                    SetSmtLedFloat(led_map[i], 1.f, 1.f, 1.f);
                }
                else {
                    SetSmtLed(led_map[i], 0, 0, 0);
                }
            }

            // =========   drawbar knobs   =========
            for (int k = 0; k < 3; k++)
            {
                const int db = bank_ * 3 + k;
                // a quick white flash confirms a bank change
                if (now - bank_flash_t_ < 120)
                    SetPthLedFloat(1 + k, 1.f, 1.f, 1.f);
                else
                    DrawbarLed(1 + k, db, engine_->getDrawbar(db));
            }

            float r, g, b;

            // =========   drive / reverb   =========
            {
                const float value = enc_values[knob_page[3]][3];
                if (knob_page[3] == 0) // drive
                {
                    r = color_triple_xfade(green[0], yellow[0], red[0], value);
                    g = color_triple_xfade(green[1], yellow[1], red[1], value);
                    b = color_triple_xfade(green[2], yellow[2], red[2], value);
                }
                else // reverb
                {
                    r = color_xfade(blue[0] * .1f, med_blue[0], value);
                    g = color_xfade(blue[1] * .1f, med_blue[1], value);
                    b = color_xfade(blue[2] * .1f, med_blue[2], value);
                }
                SetPthLedFloat(4, r, g, b);
            }

            // =========   tempo   =========
            {
                const float value = enc_values[0][4];
                r = color_quad_xfade(med_blue[0], green[0], yellow[0], red[0], value);
                g = color_quad_xfade(med_blue[1], green[1], yellow[1], red[1], value);
                b = color_quad_xfade(med_blue[2], green[2], yellow[2], red[2], value);

                SetPthLedFloat(5, r, g, b);
                SetPthLedFloat(6, r, g, b);

                if (seq_->getPlaying()) {
                    if (seq_->getLeftLights()) {
                        SetPthLedFloat(5, 0.f, 0.f, 0.f);
                    }
                    else {
                        SetPthLedFloat(6, 0.f, 0.f, 0.f);
                    }
                }
            }

            // =========   volume / rotary mix   =========
            {
                const uint8_t page = knob_page[5];
                const float value = enc_values[page][5];
                if(batt_display && now - batt_hold > 1250)
                {
                    const float* color = &green[0];

                    switch(hw_->GetBatteryLevel())
                    {
                        case Hardware::BatteryLevel::FULL:   color = &white[0];  break;
                        case Hardware::BatteryLevel::HIGH:   color = &green[0];  break;
                        case Hardware::BatteryLevel::MEDIUM: color = &yellow[0]; break;
                        case Hardware::BatteryLevel::LOW:    color = &red[0];    break;
                        default: break;
                    }

                    r = color[0];
                    g = color[1];
                    b = color[2];
                }
                else if (page == 0)
                {
                    float vu_sample = engine_->getVUSample();

                    r = value * color_quad_xfade(.1f, green[0], yellow[0], pink[0], vu_sample);
                    g = value * color_quad_xfade(.1f, green[1], yellow[1], pink[1], vu_sample);
                    b = value * color_quad_xfade(.1f, green[2], yellow[2], pink[2], vu_sample);
                }
                else
                {
                    r = color_xfade(white[0] * .1f, purple[0], value);
                    g = color_xfade(white[1] * .1f, purple[1], value);
                    b = color_xfade(white[2] * .1f, purple[2], value);
                }
                SetPthLedFloat(9, r, g, b);
            }

            /** PTH leds */
            // play key
            r = g = b = 0.f;
            if (seq_->getPlaying()) {
                r = teal[0];
                g = teal[1];
                b = teal[2];
            }
            else if (seq_->getSequence()) {
                r = teal[0] * .1f;
                g = teal[1] * .1f;
                b = teal[2] * .1f;
            }

            SetPthLedFloat(led_map[33], r, g, b);

            // loop key
            if (now - full_blink_startt < 1000) {
                if (((now - full_blink_startt) / 125) % 2 == 0) {
                    r = g = b = 0.f;
                }
                else {
                    r = 1.f;
                    g = b = 0.f;
                }
            }
            else if (now - last_clear_blink < 200) {
                if (seq_->getRecording()) {
                    r = g = b = 0.f;
                }
                else {
                    r = 1.f;
                    g = b = 0.f;
                }
            }
            else if (seq_->getRecording()) {
                r = 1.f;
                g = b = 0.f;
            }
            else {
                r = g = b = 0.f;
            }
            SetPthLedFloat(led_map[34], r, g, b);

            if (now - erase_rainbow_startt < 1000) {
                static const float rainbow_r[8] = {1.f, 1.f, 1.f, 0.f, 0.f, .29f, .93f, 1.f};
                static const float rainbow_g[8] = {0.f, .57f, 1.f, 1.f, 0.f, 0.f, .51f, 0.f};
                static const float rainbow_b[8] = {0.f, 0.f, 0.f, 0.f, 1.f, .51f, .93f, 0.f};
                float idx = (now - erase_rainbow_startt) * .007f; // 0..7 over the second
                int i0 = static_cast<int>(idx);
                float frac = idx - i0;
                if (i0 > 6) { i0 = 6; frac = 1.f; }
                float rr = rainbow_r[i0] + (rainbow_r[i0 + 1] - rainbow_r[i0]) * frac;
                float rg = rainbow_g[i0] + (rainbow_g[i0 + 1] - rainbow_g[i0]) * frac;
                float rb = rainbow_b[i0] + (rainbow_b[i0 + 1] - rainbow_b[i0]) * frac;
                SetPthLedFloat(led_map[33], rr, rg, rb);
                SetPthLedFloat(led_map[34], rr, rg, rb);
            }

            // chompi key: the horn going round. Pulses at the rotor rate,
            // red when braked, dark when the cabinet is bypassed.
            {
                const float mix = enc_values[1][5];
                if (engine_->getBrake() && engine_->getHornSpeed() < .02f)
                {
                    r = .25f * mix;
                    g = b = 0.f;
                }
                else
                {
                    const float ph = engine_->getHornPhase();
                    const float pulse = .5f + .5f * cosf(6.2831853f * ph);
                    const float lvl = mix * (.08f + .5f * pulse * pulse);
                    r = purple[0] * lvl;
                    g = purple[1] * lvl;
                    b = purple[2] * lvl;
                }
                SetPthLedFloat(led_map[5], r, g, b);
            }

            // ========   send the data   =========
            fill_led_data();
        }

        bool OnButton(uint16_t buttonID,
                      uint8_t numberOfPresses,
                      bool isRetriggering) override
        {
            if (init_ignore)
                return false;

            bool rising = numberOfPresses == 1;
            switch (buttonID)
            {
            // NO CONNECT, SKIP THESE
            case static_cast<uint16_t>(Hardware::SwId::NC_1): // fall through
            case static_cast<uint16_t>(Hardware::SwId::NC_2): // fall through
            case static_cast<uint16_t>(Hardware::SwId::NC_3): // fall through
            case static_cast<uint16_t>(Hardware::SwId::NC_4): // fall through
            case static_cast<uint16_t>(Hardware::SwId::NC_5): // fall through
                break;

            // drawbar knobs: any click steps the bank
            case static_cast<uint16_t>(Hardware::SwId::ENC_1_SW): // fall through
            case static_cast<uint16_t>(Hardware::SwId::ENC_2_SW): // fall through
            case static_cast<uint16_t>(Hardware::SwId::ENC_4_SW):
            {
                if(!rising)
                {
                    bank_ = (bank_ + 1) % kNumDrawbarBanks;
                    bank_flash_t_ = System::GetNow();
                    for (int k = 0; k < 3; k++)
                        detent_acc_[k] = 0;
                }
                break;
            }

            // drive / reverb page
            case static_cast<uint16_t>(Hardware::SwId::ENC_3_SW):
            {
                if(!rising)
                {
                    knob_page[3]++;
                    knob_page[3] %= knob_num_pages[3];
                }
                break;
            }

            // Hold to check battery level. Click flips volume / rotary mix.
            case static_cast<uint16_t>(Hardware::SwId::ENC_6_SW):
            {
                if(!rising && System::GetNow() - batt_hold < 1250)
                {
                    knob_page[5]++;
                    knob_page[5] %= knob_num_pages[5];
                }

                batt_hold = System::GetNow();
                batt_display = rising;

                break;
            }

            // transport encoder press
            case ENC_5_SW:
            {
                if (!rising)
                {
                    hw_->SendCC(midi_channel, cc_map[0][4], enc_values[0][4] * 127.f);
                }
                else {
                    enc_values[0][4] = clock_manager_->processTapClock(enc_values[0][4]);
                }

                break;
            }

            // the toggle is read continuously in ui.h (rotary speed)
            case static_cast<uint16_t>(Hardware::SwId::SW_TOG):
                break;

            case static_cast<uint16_t>(Hardware::SwId::KEY_27): // play
            {
                seq_->playButton(rising);
                break;
            }
            case static_cast<uint16_t>(Hardware::SwId::KEY_28): // loop
            {
                seq_->loopButton(rising);
                hw_->SendCC(midi_channel, 15, rising ? 127 : 0);
                break;
            }

            // CHOMPI key: shift is opened from ui.h. Here it only matters
            // while recording, where it adds a rest.
            case static_cast<uint16_t>(Hardware::SwId::KEY_26):
            {
                if (rising && seq_->getRecording())
                    seq_->insertRest();
                break;
            }

            // keys
            default:
                if (rising)
                {
                    // real keypress
                    if(!isRetriggering)
                    {
                        key_note_on_[buttonID] = true;
                        engine_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::START,
                            key_map[buttonID] - 60, buttonID, 127.f));

                        int n = key_map[buttonID] + 12 * engine_->getOctave() + engine_->getTranspose();
                        if(n < 0) n = 0; else if(n > 127) n = 127;
                        midi_note_sent_[buttonID] = static_cast<uint8_t>(n);
                        hw_->queueMidiNote(midi_channel, midi_note_sent_[buttonID], 127, NoteOn);
                    }
                }
                else
                {
                    if(!isRetriggering && key_note_on_[buttonID])
                    {
                        // Gate on key_note_on_: releases of keys whose PRESS was
                        // consumed by the shift menu must not stop voices, record sequencer
                        // steps, or emit MIDI note-offs (otherwise menu keys record)
                        key_note_on_[buttonID] = false;
                        engine_->request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP,
                            0, buttonID, 127.f));
                        if (seq_->getRecording()) {
                            seq_->insertNextKey(KeyRequest(KeyRequest::Type::STOP,
                            key_map[buttonID] - 60, buttonID, 127.f));
                        }
                        hw_->queueMidiNote(midi_channel, midi_note_sent_[buttonID], 127, NoteOff);
                    }
                }
                break;
            }

            return true;
        }

        /** stepsPerRevolution > 0 marks a MIDI CC (turns = absolute 0-127),
         *  0 marks a physical knob (turns = relative steps) */
        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            if (init_ignore)
                return false;

            const bool from_midi = stepsPerRevolution > 0;

            // ---- drawbars ----
            if (encoderID < 3)
            {
                const int db = bank_ * 3 + encoderID;
                int level = engine_->getDrawbar(db);
                if (from_midi)
                {
                    level = (turns * 8 + 63) / 127;
                }
                else
                {
                    // knob 0 reports 1 per detent, knobs 1-2 report 3
                    detent_acc_[encoderID] += turns;
                    const int per = encoderID == 0 ? 1 : 3;
                    while (detent_acc_[encoderID] >= per) { level++; detent_acc_[encoderID] -= per; }
                    while (detent_acc_[encoderID] <= -per) { level--; detent_acc_[encoderID] += per; }
                }
                level = level < 0 ? 0 : (level > 8 ? 8 : level);
                engine_->setDrawbar(db, level);

                if (!from_midi)
                    hw_->SendCC(midi_channel, kDrawbarCCBase + db, (level * 127) / 8);
                return true;
            }

            uint8_t page = knob_page[encoderID];

            if(from_midi)
            {
                enc_values[page][encoderID] = turns / 127.f;
            }
            else{
                float inc = turns * kEncoderCoarseStep;
                if (encoderID == 4 && page == 0) {
                    inc = turns * kEncoderTempoStep;
                }
                enc_values[page][encoderID] += inc;
            }

            // clip
            enc_values[page][encoderID] = fclamp(enc_values[page][encoderID], 0.f, 1.f);

            if (encoderID == 4)
            {
                uint16_t t = clock_manager_->getTempo();
                t += turns;
                if (t > 480) {
                    t = 480;
                }
                else if (t < 160) {
                    t = 160;
                }
                clock_manager_->changeTempo(t);
            }

            ApplyKnobValues();

            if (!from_midi) {
                // only PHYSICAL turns re-emit CC - MIDI-sourced turns must not bounce
                // back out or a mirroring DAW creates a feedback loop
                hw_->SendCC(midi_channel, cc_map[page][encoderID], enc_values[page][encoderID] * 127);
            }

            return true;
        }

        inline bool getSwitchState() { return switch_state; }

        void SetSwitchState(bool state) { switch_state = state; }

        inline void SetInitIgnore(bool ignore) { init_ignore = ignore; }

        inline uint8_t GetDrawbarBank() { return bank_; }

    private:
        Hardware *hw_;
        myEngine *engine_;
        Sequencer *seq_;
        clockManager *clock_manager_;
        PresetManager* presets_;

        float** enc_values;
        const float** enc_defaults;

        uint8_t midi_channel = 0;
        bool switch_state = false;
        bool key_note_on_[40] = {};
        uint8_t midi_note_sent_[40] = {};
        uint32_t last_clear_blink = 0;
        uint32_t full_blink_startt = 0;
        uint32_t erase_rainbow_startt = 0;
        uint8_t* knob_page;

        uint8_t bank_ = 0;
        uint32_t bank_flash_t_ = 0;
        int16_t detent_acc_[3] = {};

        bool batt_display = false;
        uint32_t batt_hold = 0;
    };

} // namespace chompi

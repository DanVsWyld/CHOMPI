/** @file MenuPage.h
 *  @brief The shift layer (see ui.h), open while the CHOMPI key is held.
 *  Closes a moment after release (IsClosable()) unless a preset operation is
 *  still in progress.
 *
 *  Knobs (click resets that knob to its default, LED flashes white):
 *   0  vibrato / chorus: off, V1, V2, V3, C1, C2, C3 (blue = V, pink = C)
 *   1  key click amount
 *   2  tonewheel leakage
 *   3  rotary balance, drum <-> horn
 *   4  sequencer clock division (click resets tempo)
 *   5  transpose, +/- 12 semitones
 *
 *  Black keys, left to right:
 *   octave down, octave up,
 *   PERC on, PERC soft, PERC fast, PERC third   (lit = tab down)
 *   rotor brake,
 *   erase, copy, save
 *
 *  White keys pick preset slots 1-14, the last white key is the defaults slot.
 */
#include "hardware.h"
#include "temp_led_stuff.h"
#include "Sequencer.h"

namespace chompi
{
    /** Default patch, also used for empty slots */
    struct OrganDefaults
    {
        static constexpr uint8_t kVibrato = organ::VIB_C3;
        static constexpr uint8_t kPercBits = 2 | 8; // off, third, normal, fast
        static constexpr float kClick = .5f;
        static constexpr float kLeak = .3f;
        static constexpr float kBalance = .5f;
    };
    static const uint8_t kDefaultDrawbars[organ::kNumDrawbars] = {8, 8, 8, 0, 0, 0, 0, 0, 0};

    /** Preset control indices (PresetManager stores kMaxControls per slot) */
    enum PresetCtrl : uint8_t
    {
        PRE_DRAWBAR_0 = 0, // .. 8
        PRE_VIBRATO = 9,
        PRE_PERC = 10,
        PRE_DRIVE = 11,
        PRE_REVERB = 12,
        PRE_ROTOR_MIX = 13,
        PRE_CLICK = 14,
        PRE_LEAK = 15,
        PRE_BALANCE = 16,
    };

    class MenuPage : public daisy::UiPage
    {
    public:

        enum class PresetMode
        {
            NONE = 0,
            ERASE_SEL,
            ERASING,
            COPY_SRC,
            COPY_DEST,
            COPYING,
            SAVE_SEL,
            SAVING,
            LAST,
        };


        void Init(clockManager *clock_manager, myEngine *engine, Sequencer *seq, Hardware *hw, float** enc_arr, const float** def_arr,
            uint8_t* page, PresetManager* pre)
        {
            hw_ = hw;
            engine_ = engine;
            seq_ = seq;
            clock_manager_ = clock_manager;
            presets_ = pre;
            enc_values = enc_arr;
            enc_defaults = def_arr;
            knob_page = page;

            last_blink = System::GetNow();
            chompi_key_pressed = false;
            preset_mode = PresetMode::NONE;
        }

        void Draw(const daisy::UiCanvasDescriptor &canvasDescriptor) override
        {
            /** PTH leds */
            float r, g, b;
            uint32_t now = System::GetNow();

            if(now - last_blink > 250)
            {
                blink_state = !blink_state;
                last_blink = now;
            }

            // chompi key
            if (chompi_key_pressed && preset_mode == PresetMode::NONE)
            {
                r = .67f;
                g = 0.f;
                b = 1.f;
            }
            else if (
                    (preset_mode == PresetMode::SAVE_SEL
                    || preset_mode == PresetMode::ERASE_SEL
                    || preset_mode == PresetMode::COPY_DEST)
                    && selected_slot != kSlotNone)
            {
                r = blink_state;
                g = b = 0.f;
            }
            else if (preset_mode == PresetMode::SAVING
                        || preset_mode == PresetMode::COPYING
                        || preset_mode == PresetMode::ERASING)
            {
                r = g = b = blink_state;
            }
            else
            {
                r = g = b = 0.f;
            }
            SetPthLedFloat(0, r, g, b);

            // play / overdub keys
            if(preset_mode == PresetMode::NONE)
            {
                if(tempo_reset)
                {
                    SetPthLedFloat(5, 1.f, 1.f, 1.f);
                    SetPthLedFloat(6, 1.f, 1.f, 1.f);
                }
                else
                {
                    float dled[6];
                    clock_manager_->fillLEDdata(dled);
                    SetPthLedFloat(5, dled[0], dled[1], dled[2]);
                    SetPthLedFloat(6, dled[3], dled[4], dled[5]);
                }
            }
            else if (preset_mode == PresetMode::COPY_SRC
                || preset_mode == PresetMode::COPY_DEST)
            {
                if(selected_slot == 16)
                {
                    SetPthLedFloat(7, blue[0], blue[1], blue[2]);
                    SetPthLedFloat(8, blue[0], blue[1], blue[2]);
                }
                else if(copy_src == 16)
                {
                    SetPthLedFloat(7, green[0], green[1], green[2]);
                    SetPthLedFloat(8, green[0], green[1], green[2]);
                }
                else if(!blink_state)
                {
                    SetPthLedFloat(7, 0.f, 0.f, 0.f);
                    SetPthLedFloat(8, 0.f, 0.f, 0.f);
                }
            }
            else
            {
                SetPthLedFloat(7, 0.f, 0.f, 0.f);
                SetPthLedFloat(8, 0.f, 0.f, 0.f);
            }

            // shift knob display
            if(chompi_key_pressed)
            {
                // vibrato / chorus
                if(reset_flash[0])
                    SetPthLedFloat(1, 1.f, 1.f, 1.f);
                else
                {
                    const uint8_t v = engine_->getVibrato();
                    if(v == organ::VIB_OFF)
                        SetPthLedFloat(1, .04f, .04f, .04f);
                    else
                    {
                        const bool chorus = v >= organ::VIB_C1;
                        const float depth = (float)(chorus ? v - 3 : v) / 3.f;
                        const float* c = chorus ? pink : med_blue;
                        SetPthLedFloat(1, c[0] * depth, c[1] * depth, c[2] * depth);
                    }
                }

                // click
                if(reset_flash[1])
                    SetPthLedFloat(2, 1.f, 1.f, 1.f);
                else
                {
                    const float v = .03f + .97f * engine_->getClick();
                    SetPthLedFloat(2, orange[0] * v, orange[1] * v, orange[2] * v);
                }

                // leakage
                if(reset_flash[2])
                    SetPthLedFloat(3, 1.f, 1.f, 1.f);
                else
                {
                    const float v = .03f + .97f * engine_->getLeakage();
                    SetPthLedFloat(3, green[0] * v, green[1] * v, green[2] * v);
                }

                // rotary balance: orange = drum, blue = horn
                if(reset_flash[3])
                    SetPthLedFloat(4, 1.f, 1.f, 1.f);
                else
                {
                    const float v = engine_->getRotorBalance();
                    SetPthLedFloat(4, color_xfade(orange[0], med_blue[0], v),
                                      color_xfade(orange[1], med_blue[1], v),
                                      color_xfade(orange[2], med_blue[2], v));
                }

                // transpose: white at 0, green up, blue down
                if(reset_flash[5])
                    r = g = b = 1.f;
                else
                {
                    const int t = engine_->getTranspose();
                    if(t == 0)
                        r = g = b = .15f;
                    else
                    {
                        const float amt = .15f + .85f * (float)abs(t) / 12.f;
                        const float* c = t > 0 ? green : blue;
                        r = c[0] * amt;
                        g = c[1] * amt;
                        b = c[2] * amt;
                    }
                }
                SetPthLedFloat(9, r, g, b);
            }

            // save key
            if((preset_mode == PresetMode::SAVE_SEL
                || preset_mode == PresetMode::SAVING
                || preset_mode == PresetMode::NONE)
                && !no_sd_card_)
            {
                SetSmtLedFloat(9, blue[0], blue[1], blue[2]);
            }
            else
            {
                SetSmtLedFloat(9, 0.f, 0.f, 0.f);
            }

            // copy key
            if((preset_mode == PresetMode::COPY_SRC
                || preset_mode == PresetMode::COPY_DEST
                || preset_mode == PresetMode::COPYING
                || preset_mode == PresetMode::NONE)
                && !no_sd_card_)
            {
                SetSmtLedFloat(8, green[0], green[1], green[2]);
            }
            else
            {
                SetSmtLedFloat(8, 0.f, 0.f, 0.f);
            }

            // erase key
            if((preset_mode == PresetMode::ERASE_SEL
                || preset_mode == PresetMode::ERASING
                || preset_mode == PresetMode::NONE)
                && !no_sd_card_)
            {
                SetSmtLedFloat(7, red[0], red[1], red[2]);
            }
            else
            {
                SetSmtLedFloat(7, 0.f, 0.f, 0.f);
            }

            // percussion tabs and brake
            const float tab_off = .03f;
            const bool tabs[4] = {engine_->getPercussion(), engine_->getPercSoft(),
                                  engine_->getPercFast(), engine_->getPercThird()};
            for(int i = 0; i < 4; i++)
            {
                const float v = tabs[i] ? 1.f : tab_off;
                SetSmtLedFloat(2 + i, yellow[0] * v, yellow[1] * v, yellow[2] * v);
            }
            if(engine_->getBrake())
                SetSmtLedFloat(6, red[0], red[1], red[2]);
            else
                SetSmtLedFloat(6, tab_off, 0.f, 0.f);

            // white keys
            for (uint8_t i = 1; i < 16; i++)
            {
                if(no_sd_card_ && i != 15)
                    SetSmtLedFloat(25 - i, red[0], red[1], red[2]);
                else if(preset_mode == PresetMode::SAVE_SEL && i == 15)
                    SetSmtLedFloat(25 - i, pink[0], pink[1], pink[2]);
                else if(selected_slot == i && preset_mode == PresetMode::SAVE_SEL)
                    SetSmtLedFloat(25 - i, blue[0], blue[1], blue[2]);
                else if(selected_slot == i && preset_mode == PresetMode::ERASE_SEL)
                    SetSmtLedFloat(25 - i, red[0], red[1], red[2]);
                else if(selected_slot == i && preset_mode == PresetMode::COPY_DEST)
                    SetSmtLedFloat(25 - i, blue[0], blue[1], blue[2]);
                else if((selected_slot == i && preset_mode == PresetMode::COPY_SRC) || (copy_src == i && preset_mode == PresetMode::COPY_DEST))
                    SetSmtLedFloat(25 - i, green[0], green[1], green[2]);
                else if(engine_->getVoiceSlot() == i && preset_mode == PresetMode::NONE)
                    SetSmtLedFloat(25 - i, 1.f, 1.f, 1.f);
                else if (
                    preset_mode == PresetMode::SAVE_SEL
                    || preset_mode == PresetMode::ERASE_SEL
                    || preset_mode == PresetMode::COPY_SRC
                    || preset_mode == PresetMode::COPY_DEST
                )
                {
                    if (!blink_state)
                        SetSmtLedFloat(25 - i, 0.f, 0.f, 0.f);
                    else if(i == 15)
                        SetSmtLedFloat(25 - i, 0.f, 0.f, 0.f);
                    else if(presets_->IsValid(i))
                        SetSmtLedFloat(25 - i, .4f * purple[0], .4f * purple[1], .4f * purple[2]);
                    else if (
                        preset_mode == PresetMode::SAVE_SEL
                        || preset_mode == PresetMode::COPY_DEST
                    )
                    {
                        SetSmtLedFloat(25 - i, .4f, .4f, .4f);
                    }
                    else
                        SetSmtLedFloat(25 - i, 0.f, 0.f, 0.f);
                }
                else if(i == 15) // defaults slot
                    SetSmtLedFloat(25 - i, pink[0], pink[1], pink[2]);
                else if(presets_->slotValid[i-1])
                        SetSmtLedFloat(25 - i, purple[0], purple[1], purple[2]);
                else
                    SetSmtLedFloat(25 - i, 0.f, 0.f, 0.f);
            }

            // octave keys: dim for one octave, bright for two
            SetSmtLedFloat(0, 0.f, 0.f, 0.f);
            SetSmtLedFloat(1, 0.f, 0.f, 0.f);
            const int oct = engine_->getOctave();
            if(oct != 0)
            {
                const float v = abs(oct) == 1 ? .35f : 1.f;
                SetSmtLedFloat(oct > 0 ? 1 : 0, orange[0] * v, orange[1] * v, orange[2] * v);
            }

            // ========   send the data   =========
            fill_led_data();
        }


        bool OnEncoderTurned(uint16_t encoderID,
                             int16_t turns,
                             uint16_t stepsPerRevolution) override
        {
            // we're receiving a knob position via CC
            if(stepsPerRevolution > 0)
                return false; // fall through to normalpage

            if(preset_mode != PresetMode::NONE)
                return true;

            const float inc = turns * kEncoderCoarseStep;

            switch(encoderID)
            {
                case 0: // vibrato / chorus, one setting per detent
                {
                    int v = (int)engine_->getVibrato() + (turns > 0 ? 1 : -1);
                    if(v < 0) v = 0;
                    if(v >= organ::VIB_LAST) v = organ::VIB_LAST - 1;
                    engine_->setVibrato((uint8_t)v);
                    break;
                }
                case 1:
                    engine_->setClick(fclamp(engine_->getClick() + inc, 0.f, 1.f));
                    break;
                case 2:
                    engine_->setLeakage(fclamp(engine_->getLeakage() + inc, 0.f, 1.f));
                    break;
                case 3:
                    engine_->setRotorBalance(fclamp(engine_->getRotorBalance() + inc, 0.f, 1.f));
                    break;
                case 4:
                    clock_manager_->changeDiv(turns);
                    break;
                case 5: // transpose, one semitone per detent (3 steps per detent)
                {
                    transpose_acc_ += turns;
                    int t = engine_->getTranspose();
                    while(transpose_acc_ >= 3) { t++; transpose_acc_ -= 3; }
                    while(transpose_acc_ <= -3) { t--; transpose_acc_ += 3; }
                    engine_->setTranspose(t);
                    break;
                }
                default:
                    break;
            }

            return true;
        }

        void DumpValuePresets(uint8_t slot)
        {
            for(int d = 0; d < organ::kNumDrawbars; d++)
                presets_->SetValue((float)engine_->getDrawbar(d), slot, PRE_DRAWBAR_0 + d);
            presets_->SetValue((float)engine_->getVibrato(), slot, PRE_VIBRATO);
            presets_->SetValue((float)engine_->getPercBits(), slot, PRE_PERC);
            presets_->SetValue(enc_values[0][3], slot, PRE_DRIVE);
            presets_->SetValue(enc_values[1][3], slot, PRE_REVERB);
            presets_->SetValue(enc_values[1][5], slot, PRE_ROTOR_MIX);
            presets_->SetValue(engine_->getClick(), slot, PRE_CLICK);
            presets_->SetValue(engine_->getLeakage(), slot, PRE_LEAK);
            presets_->SetValue(engine_->getRotorBalance(), slot, PRE_BALANCE);
        }

        /** Loads a slot into the engine. Slot 15, and any empty slot, is the
         *  default patch. Volume, tempo, octave and transpose are left alone. */
        void SetVoiceSlot(size_t slot)
        {
            engine_->setVoiceSlot(slot);

            const size_t mode = 0;
            const size_t bank = 0;

            if(slot == 15 || !presets_->IsValid(slot))
            {
                for(int d = 0; d < organ::kNumDrawbars; d++)
                    engine_->setDrawbar(d, kDefaultDrawbars[d]);
                engine_->setVibrato(OrganDefaults::kVibrato);
                engine_->setPercBits(OrganDefaults::kPercBits);
                enc_values[0][3] = enc_defaults[0][3];
                enc_values[1][3] = enc_defaults[1][3];
                enc_values[1][5] = enc_defaults[1][5];
                engine_->setClick(OrganDefaults::kClick);
                engine_->setLeakage(OrganDefaults::kLeak);
                engine_->setRotorBalance(OrganDefaults::kBalance);
            }
            else
            {
                auto get = [&](uint8_t ctrl) { return presets_->GetValue(mode, bank, slot, ctrl); };
                for(int d = 0; d < organ::kNumDrawbars; d++)
                    engine_->setDrawbar(d, (int)lrintf(get(PRE_DRAWBAR_0 + d)));
                engine_->setVibrato((uint8_t)lrintf(get(PRE_VIBRATO)));
                engine_->setPercBits((uint8_t)lrintf(get(PRE_PERC)));
                enc_values[0][3] = fclamp(get(PRE_DRIVE), 0.f, 1.f);
                enc_values[1][3] = fclamp(get(PRE_REVERB), 0.f, 1.f);
                enc_values[1][5] = fclamp(get(PRE_ROTOR_MIX), 0.f, 1.f);
                engine_->setClick(fclamp(get(PRE_CLICK), 0.f, 1.f));
                engine_->setLeakage(fclamp(get(PRE_LEAK), 0.f, 1.f));
                engine_->setRotorBalance(fclamp(get(PRE_BALANCE), 0.f, 1.f));
            }

            engine_->setDrive(enc_values[0][3]);
            engine_->setReverb(enc_values[1][3]);
            engine_->setRotorMix(enc_values[1][5]);
        }

        bool OnButton(uint16_t buttonID,
                uint8_t numberOfPresses,
                bool isRetriggering) override
        {
            if(isRetriggering)
                return true;

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

            // Each encoder's switch resets that knob's shift function
            case static_cast<uint16_t>(Hardware::SwId::ENC_4_SW): // knob 0
                reset_flash[0] = rising;
                if(rising)
                    engine_->setVibrato(OrganDefaults::kVibrato);
                break;

            case static_cast<uint16_t>(Hardware::SwId::ENC_1_SW): // knob 1
                reset_flash[1] = rising;
                if(rising)
                    engine_->setClick(OrganDefaults::kClick);
                break;

            case static_cast<uint16_t>(Hardware::SwId::ENC_2_SW): // knob 2
                reset_flash[2] = rising;
                if(rising)
                    engine_->setLeakage(OrganDefaults::kLeak);
                break;

            case static_cast<uint16_t>(Hardware::SwId::ENC_3_SW): // knob 3
                reset_flash[3] = rising;
                if(rising)
                    engine_->setRotorBalance(OrganDefaults::kBalance);
                break;

            case ENC_5_SW:
            {
                tempo_reset = rising;
                if (rising) {
                    clock_manager_->resetDiv();
                    clock_manager_->changeTempo(320);
                    enc_values[0][4] = .5f; // (320 - 160) / 320
                }
                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::ENC_6_SW): // knob 5
                reset_flash[5] = rising;
                if(rising)
                {
                    engine_->setTranspose(0);
                    transpose_acc_ = 0;
                }
                break;

            case static_cast<uint16_t>(Hardware::SwId::SW_TOG):
                break;

            case static_cast<uint16_t>(Hardware::SwId::KEY_26): // chompi
            {
                chompi_key_pressed = rising;

                if(rising && selected_slot != kSlotNone)
                {
                    if(preset_mode == PresetMode::SAVE_SEL)
                    {
                        engine_->stopAllVoices();

                        blink_startt = System::GetNow();

                        preset_mode = PresetMode::SAVING;
                        DumpValuePresets(selected_slot);
                        presets_->Save(selected_slot);
                        SetVoiceSlot(selected_slot);
                    }
                    else if(preset_mode == PresetMode::COPY_DEST)
                    {
                        engine_->stopAllVoices();

                        blink_startt = System::GetNow();

                        preset_mode = PresetMode::COPYING;
                        presets_->Copy(0, cs_bank, copy_src, 0, 0, selected_slot);
                        SetVoiceSlot(selected_slot);
                    }
                    else if(preset_mode == PresetMode::ERASE_SEL)
                    {
                        engine_->stopAllVoices();

                        preset_mode = PresetMode::ERASING;

                        blink_startt = System::GetNow();

                        presets_->Invalidate(selected_slot);
                        SetVoiceSlot(15);
                    }
                }
                return false;
            }

            case static_cast<uint16_t>(Hardware::SwId::KEY_16): // octave down
            case static_cast<uint16_t>(Hardware::SwId::KEY_17): // octave up
            {
                if(!rising)
                    return false; // note off falls through
                engine_->setOctave(buttonID == static_cast<uint16_t>(Hardware::SwId::KEY_16) ? -1 : 1);
                break;
            }

            // percussion tabs, in the order they sit on a console
            case static_cast<uint16_t>(Hardware::SwId::KEY_18):
                if(!rising) return false;
                engine_->setPercussion(!engine_->getPercussion());
                break;
            case static_cast<uint16_t>(Hardware::SwId::KEY_19):
                if(!rising) return false;
                engine_->setPercSoft(!engine_->getPercSoft());
                break;
            case static_cast<uint16_t>(Hardware::SwId::KEY_20):
                if(!rising) return false;
                engine_->setPercFast(!engine_->getPercFast());
                break;
            case static_cast<uint16_t>(Hardware::SwId::KEY_21):
                if(!rising) return false;
                engine_->setPercThird(!engine_->getPercThird());
                break;

            case static_cast<uint16_t>(Hardware::SwId::KEY_22): // rotor brake
                if(!rising) return false;
                engine_->setBrake(!engine_->getBrake());
                break;

            case static_cast<uint16_t>(Hardware::SwId::KEY_23): // erase
            {
                if(rising && !no_sd_card_)
                {
                    if(preset_mode == PresetMode::NONE)
                        preset_mode = PresetMode::ERASE_SEL;
                    else if(preset_mode == PresetMode::ERASE_SEL)
                        preset_mode = PresetMode::NONE;
                    else
                        break;

                    selected_slot = kSlotNone;
                }
                else if(!rising)
                    return false; // note off falls through

                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::KEY_24): // copy
            {
                if(rising && !no_sd_card_)
                {
                    if(preset_mode == PresetMode::NONE)
                        preset_mode = PresetMode::COPY_SRC;
                    else if(preset_mode == PresetMode::COPY_SRC
                            || preset_mode == PresetMode::COPY_DEST)
                        preset_mode = PresetMode::NONE;
                    else
                        break;

                    copy_src = kSlotNone;
                    selected_slot = kSlotNone;
                    cs_bank = kSlotNone;
                }
                else if(!rising)
                    return false; // note off falls through

                break;
            }

            case static_cast<uint16_t>(Hardware::SwId::KEY_25): // save
            {
                if(rising && !no_sd_card_)
                {
                    if(preset_mode == PresetMode::NONE)
                        preset_mode = PresetMode::SAVE_SEL;
                    else if(preset_mode == PresetMode::SAVE_SEL)
                        preset_mode = PresetMode::NONE;
                    else
                        break;

                    selected_slot = kSlotNone;
                }
                else if(!rising)
                    return false; // note off falls through

                break;
            }

            // white keys and play/pause
            default:
                if (rising)
                {
                    size_t slot_req = KeyToSlot(buttonID);
                    if(buttonID == 33 || buttonID == 34)
                        slot_req = 16;

                    if(slot_req == kSlotNone)
                    {
                        // do nothing
                    }
                    else if(slot_req == 16)
                    {
                        if (preset_mode == PresetMode::COPY_DEST && copy_src != 16)
                        {
                            selected_slot = slot_req;
                        }
                    }
                    else if(
                        preset_mode == PresetMode::COPY_SRC
                        && presets_->IsValid(slot_req)
                    )
                    {
                        copy_src = slot_req;
                        preset_mode = PresetMode::COPY_DEST;
                    }
                    else if(preset_mode == PresetMode::COPY_DEST && copy_src != slot_req
                            && slot_req != 15) // slot 15 is defaults, never a copy target
                    {
                        selected_slot = slot_req;
                    }
                    else if(preset_mode == PresetMode::NONE
                            && (slot_req == 15 || presets_->slotValid[slot_req - 1])
                            && !no_sd_card_)
                    {
                        selected_slot = slot_req;
                        SetVoiceSlot(selected_slot);
                    }
                    else if(
                        preset_mode == PresetMode::ERASE_SEL
                        && slot_req != 15
                        && presets_->slotValid[slot_req - 1]
                    )
                    {
                        selected_slot = slot_req;
                    }
                    else if(preset_mode == PresetMode::SAVE_SEL && slot_req != 15)
                    {
                        selected_slot = slot_req;
                    }
                }
                else if(buttonID < 29) // white keys, no play / pause
                {
                    return false; // allow releasing notes in shift menu
                }
                break;
            }

            return true;
        }

        void OnFocusGained() override
        {
            for(int i = 0; i < 6; i++)
                reset_flash[i] = false;
            tempo_reset = false;
            transpose_acc_ = 0;
            chompi_key_pressed = true;

            copy_src = kSlotNone;
            preset_mode = PresetMode::NONE;
        }

        inline void SetSwitchState(bool state) { switch_state = state; }

        bool IsClosable()
        {
            if(
                System::GetNow() - blink_startt > 1250
                && ((preset_mode == PresetMode::SAVING)    // (finished saving OR
                || (preset_mode == PresetMode::COPYING)    // finished copying OR
                || (preset_mode == PresetMode::ERASING)        // finished erasing OR
                || (preset_mode == PresetMode::NONE && !chompi_key_pressed))        // did nothing
            )
            {
                return true;
            }

            return false;
        }

        /** Maps a physical key's Hardware::SwId-numbered buttonID to its preset slot
         *  number (1-14, 15 = defaults), or kSlotNone if that key isn't a slot key */
        size_t KeyToSlot(size_t buttonID) const
        {
            if (buttonID == 7)
                return kSlotNone;
            else if (buttonID < 12)
                return buttonID - 6;
            else if (buttonID < 15)
                return kSlotNone;
            else if (buttonID == 15)
                return 1;
            else if (buttonID < 21)
                return buttonID - 10;
            else if (buttonID < 24)
                return kSlotNone;
            else if (buttonID < 29)
                return buttonID - 13;

            return kSlotNone;
        }

        bool no_sd_card_ = false;
        inline void NoSDCard() { no_sd_card_ = true; }

    private:
        myEngine *engine_;
        Sequencer *seq_;
        clockManager *clock_manager_;
        Hardware *hw_;
        PresetManager* presets_;
        float** enc_values;
        const float** enc_defaults;
        uint8_t* knob_page;

        bool chompi_key_pressed = false;
        bool blink_state = true;
        uint32_t blink_startt = 0;
        uint32_t last_blink;

        bool reset_flash[6] = {};
        bool tempo_reset = false;
        int16_t transpose_acc_ = 0;

        uint8_t selected_slot = kSlotNone;
        uint8_t copy_src = kSlotNone;
        uint8_t cs_bank = kSlotNone;
        bool switch_state;

        PresetMode preset_mode = PresetMode::NONE;
    };
} // namespace chompi

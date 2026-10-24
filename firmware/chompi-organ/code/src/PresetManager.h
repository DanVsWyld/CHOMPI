/** @file PresetManager.h
 *  @brief Presets stored on SD card (organ_presets.json), read/parsed once at boot.
 *
 *  Each slot is a JSON array of kMaxControls numbers followed by a valid flag.
 *  Integer controls (drawbars, vibrato mode, percussion bits) are stored as-is,
 *  continuous controls (0..1) are stored * 1000. See PresetCtrl in MenuPage.h
 *  for the control order.
 */
#pragma once
#include "daisy.h"
#include "fatfs.h"
#include "core_json.h"

#define PRE_VERSION "1"

namespace chompi {
class PresetManager
{
    public:
    PresetManager() {}
    ~PresetManager() {}

    void Init(float* defaults)
    {
        updated = true;

        for(size_t slot = 0; slot < kMaxSlots; slot++)
        {
            for(int control = 0; control < kMaxControls; control++)
            {
                slotValues[slot][control] = defaults[control];
            }
            slotValid[slot] = false;
        }
    }

    static constexpr int kMaxModes = 2;
    static constexpr int kMaxBanks = 3;
    static constexpr int kMaxSlots = 14;
    static constexpr int kMaxControls = 17;
    // 0-8 drawbars 16'..1' (0-8), 9 vibrato mode (0-6), 10 percussion bits
    // (on=1 third=2 soft=4 fast=8), 11 drive, 12 reverb, 13 rotary mix,
    // 14 click, 15 leakage, 16 rotary balance

    static constexpr bool IsIntControl(int ctrl) { return ctrl <= 10; }

    enum class Result
    {
        OK,
        ERR_BUFF_OVERFLOW,
        ERR_INVALID_JSON,
        ERR_GENERIC,
    };

    void StrAppend(char* buffer, const char* append)
    {
        size_t len = strlen(buffer);
        size_t app_len = strlen(append);

        for(size_t i = 0; i < app_len; i++)
        {
            buffer[len + i] = append[i];
        }
        buffer[len + app_len] = '\0';
    }

    Result WriteWholeFile(char* buffer, size_t size)
    {
        if(!updated)
            return Result::ERR_GENERIC;

        updated = false;

        std::fill_n(buffer, size, '\0');
        strcpy(buffer, "[");
        char append[16];
        for(int slot = 0; slot < kMaxSlots; slot++)
        {
            StrAppend(buffer, "[");

            for(int ctrl = 0; ctrl < kMaxControls; ctrl++)
            {
                if (IsIntControl(ctrl))
                    sprintf(append, "%d,", int(lrintf(slotValues[slot][ctrl])));
                else
                    sprintf(append, "%d,", int(lrintf(slotValues[slot][ctrl] * 1000.f)));

                StrAppend(buffer, append);
            }

            StrAppend(buffer, slotValid[slot] ? "true]," : "false],");
        }

        StrAppend(buffer, PRE_VERSION);
        StrAppend(buffer, "]");

        return Result::OK;
    }

    /** loads the JSON file. Returns OK only if it parsed. */
    Result Parse(char* buffer, size_t size)
    {
        updated = true;
        JSONStatus_t json_res;

        Result ret = Result::ERR_INVALID_JSON;

        size_t len = strlen(buffer);
        json_res = JSON_Validate(buffer, len);
        if(json_res == JSONSuccess)
        {
            char   query[24];
            char*  value;
            size_t value_len;

            for(int slot = 0; slot < kMaxSlots; slot++)
            {
                sprintf(query, "[%d][%d]", slot, kMaxControls);
                json_res = JSON_Search(
                    buffer, len, query, strlen(query), &value, &value_len);

                bool valid = json_res == JSONSuccess && value_len == 4
                             && strncmp(value, "true", 4) == 0;

                if(valid)
                {
                    for(int ctrl = 0; ctrl < kMaxControls; ctrl++)
                    {
                        sprintf(query, "[%d][%d]", slot, ctrl);

                        json_res = JSON_Search(
                            buffer, len, query, strlen(query), &value, &value_len);

                        if(json_res == JSONSuccess)
                        {
                            char tmp[16];
                            size_t n = value_len < sizeof(tmp)-1 ? value_len : sizeof(tmp)-1;
                            memcpy(tmp, value, n);
                            tmp[n] = '\0';

                            const float v = (float)atoi(tmp);
                            slotValues[slot][ctrl] = IsIntControl(ctrl) ? v : v * .001f;
                        }
                    }
                }
                slotValid[slot] = valid;
            }

            ret = Result::OK;
        }

        WriteWholeFile(buffer, size);

        return ret;
    }

    /** Factory registrations, used when the card has no organ_presets.json */
    void LoadFactory()
    {
        struct Factory
        {
            const char* bars; // 16' .. 1'
            uint8_t vib, perc;
            float drive, reverb, mix;
        };
        // perc bits: on=1 third=2 soft=4 fast=8. "10" = off, third, fast.
        static const Factory kFactory[kMaxSlots] = {
            {"888000000", 6, 11, .30f, .15f, 1.f},  //  1 three-bar + 3rd perc
            {"888800000", 6, 10, .35f, .15f, 1.f},  //  2 four-bar
            {"838000000", 2, 10, .15f, .25f, 1.f},  //  3 ballad
            {"808808008", 6, 10, .30f, .15f, 1.f},  //  4 blues
            {"688600000", 6, 10, .25f, .30f, 1.f},  //  5 lead
            {"006876400", 1, 10, .20f, .20f, 1.f},  //  6 reedy
            {"008080800", 0, 10, .10f, .25f, 1.f},  //  7 flute
            {"800000008", 6, 10, .25f, .20f, 1.f},  //  8 hollow + top
            {"888000000", 3,  5, .10f, .30f, 1.f},  //  9 2nd soft slow perc, V3
            {"886000000", 0, 11, .40f, .10f, 1.f},  // 10 dry perc, no scanner
            {"888888888", 6, 10, .45f, .20f, 1.f},  // 11 full
            {"888888888", 6, 10, .85f, .10f, 1.f},  // 12 full, cooking
            {"008000000", 1, 10, 0.f,  .35f, 0.f},  // 13 pure 8', direct out
            {"886400000", 4,  9, .30f, .15f, 1.f},  // 14 2nd perc, C1
        };

        for(int s = 0; s < kMaxSlots; s++)
        {
            const Factory& f = kFactory[s];
            for(int d = 0; d < 9; d++)
                slotValues[s][d] = (float)(f.bars[d] - '0');
            slotValues[s][9]  = f.vib;
            slotValues[s][10] = f.perc;
            slotValues[s][11] = f.drive;
            slotValues[s][12] = f.reverb;
            slotValues[s][13] = f.mix;
            slotValues[s][14] = .5f; // click
            slotValues[s][15] = .3f; // leakage
            slotValues[s][16] = .5f; // balance
            slotValid[s] = true;
        }
        updated = true;
    }

    bool IsValid(size_t slot)
    {
        slot -= 1;
        if(slot >= kMaxSlots)
            return false;

        return slotValid[slot];
    }

    float GetValue(size_t mode, size_t bank, size_t slot, size_t control)
    {
        slot -= 1;
        if(slot >= kMaxSlots || control >= kMaxControls)
            return 0.f;

        if(!slotValid[slot])
            return 0.f;

        return slotValues[slot][control];
    }

    void SetValue(float value, size_t slot, size_t control)
    {
        slot -= 1;
        if(slot >= kMaxSlots || control >= kMaxControls)
            return;

        slotValues[slot][control] = value;
    }

    void Invalidate(uint8_t slot)
    {
        slot -= 1;
        if(slot >= kMaxSlots)
            return;

        slotValid[slot] = false;

        updated = true;
    }

    void Save(size_t slot)
    {
        if(slot < 1 || slot > kMaxSlots)
            return;
        updated = true;
        slotValid[slot - 1] = true;
    }

    void Copy(uint8_t mode_src, uint8_t bank_src, uint8_t slot_src,
              uint8_t mode_trg, uint8_t bank_trg, uint8_t slot_trg)
    {
        slot_trg -= 1;
        slot_src -= 1;
        if(slot_src >= kMaxSlots || slot_trg >= kMaxSlots)
            return;

        float* src = slotValues[slot_src];
        float* dest = slotValues[slot_trg];
        for(size_t i = 0; i < kMaxControls; i++)
        {
            dest[i] = src[i];
        }

        slotValid[slot_trg] = slotValid[slot_src];

        updated = true;
    }

    float slotValues[kMaxSlots][kMaxControls];
    bool slotValid[kMaxSlots];

    private:
        bool updated;
};
} // namespace chompi

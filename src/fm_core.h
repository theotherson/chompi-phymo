/** @file fm_core.h
 *  @brief The FM model: a sine carrier phase-modulated by a pair of sine
 *  modulators, with its own envelope.
 *
 *  The pair crossfades along a list of modulator ratios; each modulator
 *  changes ratio only at zero weight, so scanning never clicks. Each has
 *  self-feedback. The index is depth x brightness x strength, falling with
 *  the level (`tilt`), and limited by Carson's rule to keep the top sideband
 *  under 0.45 of the sample rate.
 *
 *  Envelope: attack, then a 60 dB fall over the decay time while held or the
 *  damper time once released. Retriggers rise from the current level.
 */
#pragma once
#include "dsp_util.h"
#include <cmath>
#include <cstdint>

namespace phymo
{

static constexpr int kFmLists   = 4;
static constexpr int kFmEntries = 8;
static const float   kFmRatio[kFmLists][kFmEntries] = {
    {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f},                              // harmonic
    {1.f, 3.f, 5.f, 7.f, 9.f, 11.f, 13.f, 15.f},                           // odd
    {0.5f, 1.f, 1.5f, 2.f, 2.5f, 3.f, 3.5f, 4.f},                          // halves
    {1.4142f, 2.7565f, 3.7321f, 5.4039f, 6.2832f, 8.9330f, 11.f, 13.3443f}, // metal
};

/** Settings, each update. */
struct FmSettings
{
    float position = 0.f; // 0 to kFmEntries - 1
    int   list     = 0;
    float stretch  = 0.f; // -0.5 to 0.5: ratios to the power 1 + this
    float snap     = 0.f; // towards whole-number ratios
    float index    = 2.f; // radians, before tilt and Carson's limit
    float tilt     = 0.f; // index falls as the level to this power
    float feedback = 0.f; // self-feedback, radians
};

class FmCore
{
  public:
    static constexpr float kTop  = 0.45f; // top sideband under this share of the sample rate
    static constexpr float kPast = 0.05f; // attack aims this far past 1

    void Init(float sr)
    {
        sr_ = sr;
        Reset();
    }

    void Reset()
    {
        ph_c_ = ph_[0] = ph_[1] = 0.f;
        prev_[0] = prev_[1] = 0.f;
        inc_c_ = inc_[0] = inc_[1] = 0.f;
        w_[0] = w_[1] = dw_[0] = dw_[1] = 0.f;
        index_ = dindex_ = 0.f;
        fb_    = 0.f;
        level_ = 0.f;
        attack_ = false;
        ka_ = kd_ = 0.f;
        gain_ = 0.f;
    }

    /** Note on: attack from the current level. */
    void Trigger(float gain)
    {
        attack_ = true;
        gain_   = gain;
    }

    /** Note (Hz), attack (s), fall (T60, s), settings, and samples until the
     *  next Set (weights and index glide across them). */
    void Set(float freq, float attack, float t60, const FmSettings& s, int steps)
    {
        const float n = static_cast<float>(steps < 1 ? 1 : steps);
        attack        = attack < 0.001f ? 0.001f : attack;
        ka_           = 1.f - Exp2Fast(-Log2Fast((1.f + kPast) / kPast) / (attack * sr_));
        kd_           = Exp2Fast(-9.9657843f / ((t60 < 0.01f ? 0.01f : t60) * sr_)); // 60 dB: log2 1000

        float p = s.position < 0.f ? 0.f : s.position > kFmEntries - 1 ? kFmEntries - 1 : s.position;
        int   i = static_cast<int>(p);
        i       = i > kFmEntries - 2 ? kFmEntries - 2 : i;
        const float f = p - static_cast<float>(i);
        // even entries on modulator 0, odd on 1
        const int   e[2] = {i % 2 == 0 ? i : i + 1, i % 2 == 0 ? i + 1 : i};
        const float wt[2] = {i % 2 == 0 ? 1.f - f : f, i % 2 == 0 ? f : 1.f - f};
        const int   list  = s.list < 0 ? 0 : s.list >= kFmLists ? kFmLists - 1 : s.list;
        float       top_ratio = 0.f;
        for(int k = 0; k < 2; k++)
        {
            float ratio = Exp2Fast((1.f + s.stretch) * Log2Fast(kFmRatio[list][e[k]]));
            if(s.snap > 0.f)
            {
                float whole = roundf(ratio);
                whole       = whole < 1.f ? 1.f : whole;
                ratio += (whole - ratio) * s.snap;
            }
            inc_[k] = freq * ratio / sr_;
            dw_[k]  = (wt[k] - w_[k]) / n;
            if(wt[k] > 0.f && ratio > top_ratio)
                top_ratio = ratio;
        }
        inc_c_ = freq / sr_;
        fb_    = s.feedback;

        // index: tilt, then Carson's limit
        float idx = s.index * (s.tilt > 0.f ? Exp2Fast(s.tilt * Log2Fast(level_ > 1e-6f ? level_ : 1e-6f)) : 1.f);
        const float fm   = freq * (top_ratio > 0.f ? top_ratio : 1.f);
        const float imax = (kTop * sr_ - freq) / fm - 1.f;
        idx              = imax <= 0.f ? 0.f : idx / (1.f + idx / imax);
        dindex_          = (idx - index_) / n;
    }

    float Process()
    {
        if(attack_)
        {
            level_ += (1.f + kPast - level_) * ka_;
            if(level_ >= 1.f)
                level_ = 1.f, attack_ = false;
        }
        else
            level_ *= kd_;
        // modulators with feedback, then the carrier
        w_[0] += dw_[0], w_[1] += dw_[1], index_ += dindex_;
        float m = 0.f;
        for(int k = 0; k < 2; k++)
        {
            const float o = SinCycles(ph_[k] + fb_ * prev_[k] * 0.15915494f);
            prev_[k]      = o;
            m += w_[k] * o;
            ph_[k] += inc_[k];
            ph_[k] -= floorf(ph_[k]);
        }
        const float y = SinCycles(ph_c_ + index_ * m * 0.15915494f); // radians to cycles
        ph_c_ += inc_c_;
        ph_c_ -= ph_c_ >= 1.f ? 1.f : 0.f;
        return gain_ * level_ * y;
    }

    /** The envelope's level, 0 to 1. */
    float Level() const { return level_; }
    bool  Attacking() const { return attack_; }

  private:
    float sr_;
    float ph_c_, inc_c_; // carrier phase and step, cycles
    float ph_[2], inc_[2]; // modulators' phases and steps
    float prev_[2]; // last outputs, for feedback
    float w_[2], dw_[2]; // weights and their glide per sample
    float index_, dindex_; // radians, and its glide
    float fb_;                    // the feedback, radians
    float level_, ka_, kd_; // level, attack step, fall per sample
    bool  attack_;
    float gain_; // note strength
};

} // namespace phymo

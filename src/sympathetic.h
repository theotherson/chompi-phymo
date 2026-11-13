/** @file sympathetic.h
 *  @brief Sympathetic strings (the Bowed model's Stretch): four unplayed
 *  Karplus-Strong strings on the mix, two each side, ringing along with
 *  notes on their harmonics. Tuned to G3, D4, A4, E5, or to the newest
 *  note's root, fifth, octave and twelfth, gliding (~20 ms) when retuned.
 */
#pragma once
#include "dsp_util.h"
#include <cmath>
#include <cstddef>

namespace phymo
{

class Sympathetic
{
  public:
    static constexpr int   kStrings = 4;
    static constexpr int   kLen     = 512; // down to ~94 Hz at 48 kHz
    static constexpr float kT60     = 3.f; // ring time, s
    static constexpr float kIn      = 0.06f; // input level
    static constexpr float kWet     = 0.2f; // output level at full amount
    static constexpr float kDamp    = 0.55f; // loop damping one-pole
    static constexpr float kGlide   = 0.05f; // length glide per block (~20 ms)

    void Init(float sr)
    {
        sr_ = sr;
        Reset();
    }

    void Reset()
    {
        for(auto& s : buf_)
            for(auto& v : s)
                v = 0.f;
        w_ = 0;
        for(int i = 0; i < kStrings; i++)
            d_[i] = d_to_[i] = 200.f, g_[i] = 0.f, lp_[i] = 0.f;
        amount_ = target_ = 0.f;
    }

    /** 0 none to 1. */
    void SetAmount(float a) { target_ = a < 0.f ? 0.f : a > 1.f ? 1.f : a; }

    /** Pitches, Hz: each string's target length and loss. */
    void Tune(const float* hz)
    {
        const float lag = (1.f - kDamp) / kDamp; // the one-pole's delay at low frequencies
        for(int i = 0; i < kStrings; i++)
        {
            float f = hz[i];
            while(f < sr_ / static_cast<float>(kLen - 4)) // too low for the line: up an octave
                f *= 2.f;
            float d = sr_ / f - lag;
            d       = d < 4.f ? 4.f : d > kLen - 4.f ? kLen - 4.f : d;
            d_to_[i] = d;
            g_[i]    = Exp2Fast(-9.9657843f / (f * kT60)); // 0.001 ^ (1 / (f t60))
        }
    }

    /** Fixed tuning: G3, D4, A4, E5. */
    void TuneOpen()
    {
        static const float kOpen[kStrings] = {196.f, 293.66f, 440.f, 659.26f}; // G3, D4, A4, E5
        Tune(kOpen);
    }

    /** To note `hz`: its root, fifth, octave and twelfth. */
    void TuneTo(float hz)
    {
        const float f      = hz < 100.f ? 2.f * hz : hz;
        const float t[kStrings] = {f, 1.5f * f, 2.f * f, 3.f * f};
        Tune(t);
    }

    PHYMO_HOT void Process(float* l, float* r, size_t n)
    {
        const float a0 = amount_;
        amount_ += (target_ - amount_) * 0.1f;
        amount_ = amount_ < 1e-4f && target_ <= 0.f ? 0.f : amount_;
        if(a0 <= 0.f && amount_ <= 0.f)
            return;
        for(int i = 0; i < kStrings; i++)
            d_[i] += (d_to_[i] - d_[i]) * kGlide;
        const float da = (amount_ - a0) / static_cast<float>(n);
        float       a  = a0;
        for(size_t j = 0; j < n; j++)
        {
            a += da;
            const float in  = kIn * 0.5f * (l[j] + r[j]);
            float       out[kStrings];
            for(int i = 0; i < kStrings; i++)
            {
                // read back the period, interpolated
                const float x = static_cast<float>(w_) - d_[i] + static_cast<float>(kLen);
                const int   k = static_cast<int>(x);
                const float f = x - static_cast<float>(k);
                const float p = buf_[i][k & (kLen - 1)], q = buf_[i][(k + 1) & (kLen - 1)];
                lp_[i] += (p + (q - p) * f - lp_[i]) * kDamp;
                buf_[i][w_] = in + g_[i] * lp_[i];
                out[i]      = lp_[i];
            }
            w_ = (w_ + 1) & (kLen - 1);
            l[j] += a * kWet * (out[0] + 0.5f * (out[1] + out[2]));
            r[j] += a * kWet * (out[3] + 0.5f * (out[1] + out[2]));
        }
    }

  private:
    float buf_[kStrings][kLen];
    int   w_;
    float d_[kStrings], d_to_[kStrings]; // lengths, samples, and their targets
    float g_[kStrings]; // loss per trip
    float lp_[kStrings]; // damping state
    float amount_, target_; // amount, gliding to the target
    float sr_;
};

} // namespace phymo

/** @file exciter.h
 *  @brief The strike that sets a resonator ringing.
 *
 *  A pulse plus a 15 ms noise burst, through a one-pole low-pass (hardness)
 *  and a DC blocker. The pulse morphs from a hammer (raised cosine) to a pick
 *  (fast ramp up, slower down); its length, a share of the period, is the
 *  strike position. Strength sets the level (~18 dB range) and, with
 *  hardness, the brightness. Transient() falls from 1 over the strike, for
 *  the resonator to open its damping.
 */
#pragma once
#include "dsp_util.h"
#include <cmath>
#include <cstdint>

namespace phymo
{

/** The strike's settings, each 0 to 1. */
struct StrikeParams
{
    float type     = 0.3f; // hammer (0) to pick (1)
    float position = 0.3f; // pulse length, 2% to 50% of the period
    float hardness = 0.5f; // soft felt (0) to metal (1)
    float noise    = 0.1f; // noise burst level
    float strength = 0.8f; // level and brightness
};

class Exciter
{
  public:
    static constexpr float kNoiseTime = 0.015f; // the burst's length, s
    static constexpr float kTransient = 0.015f; // Transient()'s time constant, s

    static constexpr float kQuiet = 1e-9f; // below this the tail is done

    void Init(float sr)
    {
        sr_    = sr;
        tr_k_  = expf(-1.f / (kTransient * sr));
        noise_ = static_cast<int>(kNoiseTime * sr);
        Reset();
    }

    void Reset()
    {
        pos_ = len_ = 0;
        noise_pos_  = noise_;
        type_ = gain_ = noise_lvl_ = 0.f;
        lp_a_ = lp_ = 0.f;
        dc_x_ = dc_y_ = 0.f;
        tr_   = 0.f;
        rng_  = 0x9e3779b9u;
    }

    /** Starts a strike for a note of `period` samples. */
    void Strike(float period, const StrikeParams& p)
    {
        const float share = 0.02f + 0.48f * p.position;
        len_       = static_cast<int>(share * period);
        len_       = len_ < 4 ? 4 : len_;
        pos_       = 0;
        noise_pos_ = 0;
        type_      = p.type;
        gain_      = 1.5f * (0.08f + 0.92f * p.strength * p.strength); // ~18 dB, gentle to hard
        noise_lvl_ = p.noise;
        // 200 Hz at the softest, up to ~8 octaves higher
        float fc   = 200.f * exp2f(6.5f * p.hardness + 1.5f * p.strength);
        fc         = fc > 0.45f * sr_ ? 0.45f * sr_ : fc;
        lp_a_      = expf(-6.2831853f * fc / sr_);
        tr_        = 1.f;
    }

    /** The next sample (0 once the strike's over, but for the DC blocker's tail). */
    float Process()
    {
        // finished: only the transient remains
        if(pos_ >= len_ && noise_pos_ >= noise_ && dc_y_ < kQuiet && dc_y_ > -kQuiet && lp_ < kQuiet && lp_ > -kQuiet)
        {
            lp_ = dc_x_ = dc_y_ = 0.f;
            tr_ *= tr_k_;
            return 0.f;
        }
        float x = 0.f;
        if(pos_ < len_)
        {
            const float t      = static_cast<float>(pos_) / static_cast<float>(len_);
            const float hammer = 0.5f - 0.5f * SinCycles(t + 0.25f); // cos 2πt
            const float pick   = t < 0.15f ? t / 0.15f : (1.f - t) / 0.85f;
            x                  = hammer + (pick - hammer) * type_;
            pos_++;
        }
        if(noise_pos_ < noise_)
        {
            const float u = 1.f - static_cast<float>(noise_pos_) / static_cast<float>(noise_);
            x += noise_lvl_ * u * u * u * Noise();
            noise_pos_++;
        }
        lp_ = x + (lp_ - x) * lp_a_;
        // DC blocker, ~40 Hz
        dc_y_ = lp_ - dc_x_ + 0.995f * dc_y_;
        dc_x_ = lp_;
        tr_ *= tr_k_;
        return gain_ * dc_y_;
    }

    /** Pulse length in samples. */
    int Length() const { return len_; }
    /** 1 at the strike, falling (kTransient). */
    float Transient() const { return tr_; }
    /** Still striking (the DC blocker's tail not counted). */
    bool Busy() const { return pos_ < len_ || noise_pos_ < noise_; }

  private:
    /** White noise, -1 to 1 (xorshift). */
    float Noise()
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<float>(static_cast<int32_t>(rng_)) * 4.6566129e-10f;
    }

    float    sr_;
    int      pos_, len_; // position in the pulse, and its length
    int      noise_pos_, noise_; // position in the burst, and its length
    float    type_, gain_, noise_lvl_;
    float    lp_a_, lp_; // hardness low-pass
    float    dc_x_, dc_y_;        // the DC blocker
    float    tr_, tr_k_;          // Transient()
    uint32_t rng_;
};

} // namespace phymo

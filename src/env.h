/** @file env.h
 *  @brief The ENV page's ADSR, one a voice. A modulation source only.
 *
 *  One-pole stages: the attack aims past 1 (kOver) and stops there; decay
 *  and release reach within 1% of their target in their times. Retriggering
 *  starts the attack from the current level. Runs in steps of `n` samples.
 */
#pragma once
#include "dsp_util.h"
#include <cmath>

namespace phymo
{

/** Stage coefficients for a step (shared by the voices). */
struct EnvTimes
{
    float ka = 1.f, kd = 1.f, kr = 1.f; // per-step coefficients
    float sustain = 0.f;                // 0 to 1
};

class Env
{
  public:
    static constexpr float kOver = 1.2f; // attack target (stops at 1)

    /** Stage times (s) to coefficients for an `n`-sample step. */
    static EnvTimes Times(float attack, float decay, float sustain, float release, float sr, int n)
    {
        auto k = [&](float tau) { return 1.f - expf(-static_cast<float>(n) / (tau * sr)); };
        EnvTimes t;
        t.ka      = k(attack / logf(kOver / (kOver - 1.f)) + 1e-6f); // reaches 1 in `attack`
        t.kd      = k(decay / 4.6f + 1e-6f); // 99% in `decay`
        t.kr      = k(release / 4.6f + 1e-6f);
        t.sustain = sustain < 0.f ? 0.f : sustain > 1.f ? 1.f : sustain;
        return t;
    }

    void Reset()
    {
        v_     = 0.f;
        stage_ = kIdle;
    }

    /** Starts the attack from the current level. */
    void Trigger() { stage_ = kAttack; }

    /** One step; `gate` while the key is held. Returns 0 to 1. */
    float Step(const EnvTimes& t, bool gate)
    {
        if(!gate && stage_ != kIdle)
            stage_ = kRelease;
        switch(stage_)
        {
            case kAttack:
                v_ += (kOver - v_) * t.ka;
                if(v_ >= 1.f)
                    v_ = 1.f, stage_ = kDecay;
                break;
            case kDecay: v_ += (t.sustain - v_) * t.kd; break;
            case kRelease:
                v_ += (0.f - v_) * t.kr;
                if(v_ < 1e-4f)
                    v_ = 0.f, stage_ = kIdle;
                break;
            default: break;
        }
        return v_;
    }

    float Value() const { return v_; }

    /** Attack and decay done (Cycle mode retriggers here). */
    bool Cycled(const EnvTimes& t) const
    {
        return stage_ == kDecay && fabsf(v_ - t.sustain) <= 0.01f * (1.f - t.sustain) + 1e-6f;
    }

  private:
    enum Stage
    {
        kIdle,
        kAttack,
        kDecay,
        kRelease
    };
    float v_;
    Stage stage_;
};

} // namespace phymo

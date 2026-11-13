/** @file lfo.h
 *  @brief An LFO with a delay and fade-in after triggering.
 */
#pragma once
#include "dsp_util.h"
#include <cmath>
#include <cstdint>

namespace phymo
{

class Lfo
{
  public:
    /** Shapes. Sample & hold takes a new random value each cycle. */
    enum class Shape { Triangle, Sine, Square, SawUp, SawDown, SampleHold, Count };

    void Init(float sr)
    {
        sr_ = sr;
        Reset();
    }

    void Reset()
    {
        phase_ = 0.f;
        timer_ = 0;
        SetRate(5.f);
        SetDelay(0.f);
        SetFade(0.f);
        shape_ = Shape::Triangle;
        held_  = 0.f;
        rng_   = 0x1234567u;
    }

    void SetShape(Shape s) { shape_ = s; }

    void SetRate(float hz) { inc_ = hz / sr_; }

    /** Silence after Trigger(), s. */
    void SetDelay(float seconds) { delay_ = static_cast<uint32_t>(seconds * sr_); }

    /** Fade-in time after the delay, s. */
    void SetFade(float seconds) { fade_ = static_cast<uint32_t>(seconds * sr_); }

    /** Restarts the delay, keeping the phase. */
    void Trigger() { timer_ = 0; }

    /** Restarts the delay and the cycle. */
    void Restart()
    {
        timer_ = 0;
        phase_ = 0.f;
        NewHeld();
    }

    /** One sample: -1 to 1, times the fade-in. */
    float Process() { return Advance(1); }

    /** `n` samples on. */
    float Advance(uint32_t n)
    {
        phase_ += inc_ * static_cast<float>(n);
        if(phase_ >= 1.f)
        {
            phase_ -= floorf(phase_);
            NewHeld();
        }
        float y;
        switch(shape_)
        {
            case Shape::Sine: y = SinCycles(phase_); break;
            case Shape::Square: y = phase_ < 0.5f ? 1.f : -1.f; break;
            case Shape::SawUp: y = 2.f * phase_ - 1.f; break;
            case Shape::SawDown: y = 1.f - 2.f * phase_; break;
            case Shape::SampleHold: y = held_; break;
            default: y = phase_ < 0.5f ? 4.f * phase_ - 1.f : 3.f - 4.f * phase_; break;
        }
        float gain = 1.f;
        if(timer_ < delay_)
            gain = 0.f;
        else if(timer_ < delay_ + fade_)
            gain = static_cast<float>(timer_ - delay_) / static_cast<float>(fade_);
        if(timer_ < delay_ + fade_)
            timer_ += n;
        return y * gain;
    }

  private:
    void NewHeld()
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        held_ = static_cast<float>(rng_) * (2.f / 4294967296.f) - 1.f;
    }

    float    sr_;
    float    phase_; // 0 to 1
    float    inc_; // phase increment per sample
    uint32_t delay_; // samples of silence after Trigger()
    uint32_t fade_; // fade-in samples
    uint32_t timer_; // samples since Trigger()
    Shape    shape_;
    float    held_;  // sample & hold's value
    uint32_t rng_;
};

} // namespace phymo

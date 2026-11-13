/** @file output.h
 *  @brief The output stage: a peak compressor, then a soft limit, each side
 *  on its own.
 *
 *  Peak follower (fast attack, slow release); above the threshold a gain of
 *  1 / (ratio x (1 + overshoot)), gliding. The input is driven by kDrive;
 *  kMatch keeps one held note at the same level with it on or off.
 */
#pragma once
#include "dsp_util.h" // PHYMO_HOT
#include <cmath>

namespace phymo
{

class OutputStage
{
  public:
    static constexpr float kThresh = 0.25f;
    static constexpr float kRatio  = 1.f;
    static constexpr float kMakeup = 0.9f;
    static constexpr float kDrive  = 4.f; // +12 dB in
    static constexpr float kMatch  = 0.303f; // one note as loud on as off (output_test)

    void Init()
    {
        l_.Init(), r_.Init();
        on_ = true;
    }

    void SetOn(bool on) { on_ = on; }
    bool On() const { return on_; }

    /** In place, stereo. */
    PHYMO_HOT void Process(float* l, float* r, size_t n)
    {
        if(!on_)
            return;
        for(size_t i = 0; i < n; i++)
        {
            l[i] = SoftClip(l_.Process(l[i] * kDrive)) * kMatch;
            r[i] = SoftClip(r_.Process(r[i] * kDrive)) * kMatch;
        }
    }

  private:
    /** One side's compressor. */
    struct Comp
    {
        float peak_, gain_;
        void  Init() { peak_ = 0.5f, gain_ = 1.f; }
        float Process(float in)
        {
            Slope(peak_, fabsf(in), 0.05f, 0.0002f);
            const float g = peak_ <= kThresh ? 1.f : 1.f / (kRatio * (1.f + (peak_ - kThresh)));
            Slope(gain_, g, 0.001f, 0.005f);
            return SoftLimit(in * gain_ * kMakeup);
        }
    };

    /** Slews towards `in`: `up` rising, `down` falling. */
    static void Slope(float& out, float in, float up, float down)
    {
        const float e = in - out;
        out += (e > 0.f ? up : down) * e;
    }

    static float SoftLimit(float x) { return x * (27.f + x * x) / (27.f + 9.f * x * x); }
    static float SoftClip(float x) { return x < -3.f ? -1.f : x > 3.f ? 1.f : SoftLimit(x); }

    Comp l_, r_;
    bool on_;
};

} // namespace phymo

/** @file effect.h
 *  @brief The effect model interface.
 *
 *    Amount      the slot's knob; 0 passes the dry signal
 *    Parameters  up to eight, 0 to 1, with defaults
 *    Variant     a stepped choice
 *
 *  Process() runs in place in the audio interrupt, at most kMaxBlock frames,
 *  with no allocation or blocking. Reset() sets all state, buffers included
 *  (memory isn't zeroed at power-on) and runs from the main loop. Amount and
 *  parameters arrive smoothed, once a block.
 */
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace fx
{

static constexpr size_t kMaxBlock = 64;
static constexpr float  kPi       = 3.14159265358979f;
static constexpr float  kHalfPi   = 1.57079632679490f;

class Effect
{
  public:
    /** The sample rate, once at boot, before the first Reset. */
    virtual void Init(float sample_rate)
    {
        sr_      = sample_rate;
        amount_  = 0.f;
        freeze_  = false;
        variant_ = DefaultVariant();
        for(int i = 0; i < kParams; i++)
            param_[i] = ParamDefault(i);
    }

    /** Resets all state (main loop only). */
    virtual void Reset() = 0;

    /** One block, in place. */
    virtual void Process(float* l, float* r, size_t n) = 0;

    /** How many variants (1 = none to pick). */
    virtual int Variants() const { return 1; }

    /** The variant a slot starts on. */
    virtual int DefaultVariant() const { return 0; }

    static constexpr int kParams = 8;
    /** CHOMPI + knob `k` (0-3): a knob's second parameter. */
    static constexpr int ShiftParam(int k) { return 4 + k; }
    /** The parameters a model has: bits 0-3 knobs 1-4, bits 4-7 CHOMPI + knobs 1-4. */
    virtual unsigned ParamMask() const { return 0; }
    bool             HasParam(int i) const { return (ParamMask() >> i) & 1; }
    /** Each parameter's default, 0 to 1. */
    virtual float ParamDefault(int i) const { return .5f; }

    /** LED blink rate for parameter `p` at value `v`, in Hz; 0 if it isn't a time. */
    virtual float BlinkHz(int p, float v, int variant) const { return 0.f; }

    /** LED colour for parameter `p` at value `v`; false to use the slot's colour. */
    virtual bool ParamLed(int p, float v, float* rgb) const { return false; }

    /** Freeze: hold the tail (models without one ignore it). */
    void SetFreeze(bool f) { freeze_ = f; }

    void SetAmount(float a) { amount_ = a; }
    void SetParam(int i, float v) { param_[i] = v; }
    void SetVariant(int v) { variant_ = v > 0 ? v : 0; }

    float Amount() const { return amount_; }
    float Param(int i) const { return param_[i]; }
    int   Variant() const { return variant_; }

  protected:
    float sr_      = 48000.f;
    float amount_  = 0.f;
    float param_[kParams] = {.5f, .5f, .5f, .5f, .5f, .5f, .5f, .5f};
    int   variant_ = 0;
    bool  freeze_  = false;
};

/** Freeze amount, 0 to 1, gliding ~10 ms. */
inline void FreezeGlide(float& frz, bool on) { frz += ((on ? 1.f : 0.f) - frz) * .002f; }
/** The dry level while frozen: full, so playing over a held tail is heard. */
inline float FrozenDry(float dry, float frz) { return dry + (1.f - dry) * frz; }

/** Clamp to [lo, hi]. */
inline float Clamp(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

/** Dry level for an additive effect: full up to 3/4 Amount, then easing to none. */
inline float TopDry(float amount)
{
    if(amount <= .75f)
        return 1.f;
    return cosf(kHalfPi * Clamp((amount - .75f) * 4.f, 0.f, 1.f));
}
/** Wet make-up as the dry fades, up to `top` times. */
inline float TopWet(float dry, float top) { return 1.f + (top - 1.f) * (1.f - dry); }

/** Soft clip (from DaisySP, Electrosmith, MIT): rational curve, flat past +-3. */
inline float SoftClip(float x)
{
    if(x < -3.f)
        return -1.f;
    if(x > 3.f)
        return 1.f;
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
}

/** sin(x) for |x| <= pi/2 (9th-order series). */
inline float SinHalf(float x)
{
    const float x2 = x * x;
    return x * (1.f + x2 * (-1.f / 6.f + x2 * (1.f / 120.f + x2 * (-1.f / 5040.f + x2 * (1.f / 362880.f)))));
}

/** xorshift32 random numbers. Never seeded with 0. */
struct Random
{
    uint32_t state = 0x9E3779B9u;
    void     Seed(uint32_t s) { state = s ? s : 0x9E3779B9u; }
    uint32_t Next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    /** 0 to 1. */
    float Uniform() { return static_cast<float>(Next()) * (1.f / 4294967296.f); }
};

} // namespace fx

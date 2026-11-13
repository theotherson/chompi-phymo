/** @file body.h
 *  @brief Colour after the resonators: a pickup per voice; a body, a vowel
 *  filter and a tremolo on the mix.
 *
 *  Pickup (per voice, as it's nonlinear):
 *   - Tine: an offset soft clip (even harmonics, more on hard notes), an
 *     800 Hz peak and a 4.5 kHz roll-off.
 *   - Reed: a near-symmetric clip (odd harmonics), a 160 Hz high-pass and a
 *     1.2 kHz nasal peak.
 *   Tone shifts the peak and roll-off ±1.5 octaves; Amount is the drive.
 *   A note at kRef keeps its level; a DC blocker follows.
 *
 *  Body (stereo, on the mix): six band-pass resonances added to a slightly
 *  dipped dry signal, the right side 4% higher. Tone scales the size
 *  ±1.5 octaves. Violin uses a violin's resonances, including the bridge
 *  hill around 3 kHz, and dips the dry further.
 *
 *  Vowel (stereo, on the mix): four formants sweeping a, e, i, o, u
 *  (Peterson & Barney 1952), level-matched to the dry, gliding ~20 ms.
 *
 *  Tremolo (on the mix): a sine, 1.5 to 12 Hz; Spread goes from the same on
 *  both sides to opposite (autopan).
 */
#pragma once
#include "dsp_util.h"
#include <cmath>
#include <cstddef>

namespace phymo
{

enum class PickupMode { Clean, Tine, Reed, Wood, Vowel, Violin, Count }; // stored in presets: add new ones at the end

class Pickup
{
  public:
    static constexpr float kTineBias = 0.4f; // the tine's offset from the pickup's centre
    static constexpr float kReedBias = 0.1f;
    static constexpr float kRef      = 0.08f; // reference level, kept as loud
    static constexpr float kDcR      = 0.9987f; // DC blocker, ~10 Hz at 48 kHz
    static constexpr float kTinePeak = 1.5f; // peak: +8 dB
    static constexpr float kReedPeak = 2.2f;  // +10 dB
    static constexpr float kTineTrim = 0.7f; // output trim (body_test)
    static constexpr float kReedTrim = 0.8f;

    void Init(float sr)
    {
        sr_ = sr;
        Reset();
    }

    void Reset()
    {
        mode_ = PickupMode::Clean;
        drive_ = 1.f, bias_ = 0.f, s0_ = 0.f, norm_ = 1.f;
        a_.Reset(), b_.Reset(), c_.Reset(), dc_.Reset();
        amount_ = -1.f; // forces the first Set
        tone_   = 0.5f;
    }

    /** Mode, amount and tone (0 to 1). Cheap when unchanged. */
    void Set(PickupMode mode, float amount, float tone = 0.5f)
    {
        if(mode == mode_ && amount == amount_ && tone == tone_)
            return;
        if(mode != mode_)
            a_.Reset(), b_.Reset(), c_.Reset(), dc_.Reset();
        mode_          = mode;
        amount_        = amount;
        tone_          = tone;
        const float sc = Exp2Fast(3.f * tone - 1.5f); // ±1.5 octaves
        if(mode == PickupMode::Tine)
        {
            drive_ = 1.f + 11.f * amount, bias_ = kTineBias;
            a_.Set(800.f * sc, 1.2f, sr_); // peak
            b_.Set(4500.f * sc, 0.8f, sr_); // roll-off
        }
        else if(mode == PickupMode::Reed)
        {
            drive_ = 1.f + 13.f * amount, bias_ = kReedBias;
            a_.Set(1200.f * sc, 2.5f, sr_);  // the peak
            b_.Set(6000.f * sc, 0.7f, sr_); // roll-off
            c_.Set(160.f, 0.7f, sr_); // thin low end (fixed)
        }
        s0_   = SoftClip(bias_);
        norm_ = kRef / (SoftClip(drive_ * kRef + bias_) - s0_); // a note at kRef as loud as clean
    }

    float Process(float x)
    {
        if(mode_ != PickupMode::Tine && mode_ != PickupMode::Reed)
            return x; // Clean, Wood and Violin: their bodies are on the mix
        float y = (SoftClip(drive_ * x + bias_) - s0_) * norm_;
        y       = dc_.Process(y, kDcR);
        if(mode_ == PickupMode::Reed)
        {
            c_.Process(y);
            y = c_.Hp();
        }
        a_.Process(y);
        y += (mode_ == PickupMode::Tine ? kTinePeak : kReedPeak) * a_.Bp();
        b_.Process(y);
        return (mode_ == PickupMode::Tine ? kTineTrim : kReedTrim) * b_.Lp();
    }

  private:
    float      sr_;
    PickupMode mode_;
    float      amount_, tone_;
    float      drive_, bias_, s0_, norm_; // shaper gain, offset, rest output, small-signal scale
    Svf        a_, b_, c_; // peak, roll-off, reed high-pass
    DcBlock    dc_;
};

/** A wooden body's or a violin's resonances, stereo. */
class Body
{
  public:
    static constexpr int   kModes = 6;
    static constexpr float kWet   = 3.f; // resonance level at full amount
    static constexpr float kDip   = 0.35f; // dry dip at full amount
    static constexpr float kDipViolin = 0.6f; // the violin's dip

    /** A guitar-like body, or a violin's. */
    void SetKind(bool violin)
    {
        if(violin == violin_)
            return;
        violin_ = violin;
        const float t = tone_;
        tone_         = -1.f, SetTone(t < 0.f ? 0.5f : t); // retune
    }

    void Init(float sr)
    {
        sr_ = sr;
        Reset();
    }

    void Reset()
    {
        for(int i = 0; i < kModes; i++)
            l_[i].Reset(), r_[i].Reset();
        amount_ = target_ = 0.f;
        violin_ = false;
        tone_   = -1.f;
        SetTone(0.5f);
    }

    void SetAmount(float a) { target_ = a; }

    /** 0 to 1: body size, ±1.5 octaves. */
    void SetTone(float t)
    {
        static const float kHz[2][kModes] = {{98.f, 204.f, 290.f, 480.f, 1150.f, 2900.f},
                                             {275.f, 405.f, 470.f, 545.f, 1100.f, 3100.000f}};
        static const float kQ[2][kModes]  = {{12.f, 9.f, 7.f, 5.f, 3.f, 2.f}, {14.f, 9.f, 11.f, 9.f, 4.f, 1.800f}};
        if(t == tone_)
            return;
        tone_          = t;
        const float sc = Exp2Fast(3.f * t - 1.5f);
        const int   v  = violin_ ? 1 : 0;
        for(int i = 0; i < kModes; i++)
        {
            l_[i].Set(kHz[v][i] * sc, kQ[v][i], sr_);
            r_[i].Set(1.04f * kHz[v][i] * sc, kQ[v][i], sr_);
        }
    }

    PHYMO_HOT void Process(float* l, float* r, size_t n)
    {
        static const float kGainOf[2][kModes] = {{1.f, 0.9f, 0.6f, 0.55f, 0.35f, 0.3f}, {0.810f, 0.360f, 0.900f, 0.855f, 0.45f, 4.000f}};
        const float* const kGain = kGainOf[violin_ ? 1 : 0];
        const float        dip   = violin_ ? kDipViolin : kDip;
        if(amount_ <= 0.f && target_ <= 0.f)
            return;
        const float k  = 1.f - Exp2Fast(-1.4426950f * static_cast<float>(n) / (0.01f * sr_)); // ~10 ms
        const float a0 = amount_;
        amount_ += (target_ - amount_) * k;
        amount_        = amount_ < 1e-4f && target_ <= 0.f ? 0.f : amount_;
        const float da = (amount_ - a0) / static_cast<float>(n);
        float       a  = a0;
        for(size_t i = 0; i < n; i++)
        {
            a += da;
            float wl = 0.f, wr = 0.f;
            for(int k = 0; k < kModes; k++)
            {
                l_[k].Process(l[i]);
                r_[k].Process(r[i]);
                wl += kGain[k] * l_[k].Bp();
                wr += kGain[k] * r_[k].Bp();
            }
            const float dry = 1.f - dip * a;
            l[i]            = dry * l[i] + kWet * a * wl;
            r[i]            = dry * r[i] + kWet * a * wr;
        }
    }

  private:
    Svf   l_[kModes], r_[kModes];
    float amount_, target_; // amount, gliding to the target
    float tone_;
    bool  violin_; // violin resonances
    float sr_;
};

/** Four formants, stereo. */
class Vowel
{
  public:
    static constexpr int   kVowels   = 5; // a, e, i, o, u
    static constexpr int   kFormants = 4;
    static constexpr float kFollow   = 0.1f; // level follower time, s

    void Init(float sr)
    {
        sr_ = sr;
        Reset();
    }

    void Reset()
    {
        for(int i = 0; i < kFormants; i++)
            l_[i].Reset(), r_[i].Reset();
        amount_ = target_ = 0.f;
        pos_ = to_ = 0.f;
        dry_ = wet_ = 0.f;
        gain_       = 2.f;
        Tune();
    }

    void SetAmount(float a) { target_ = a; }
    /** 0 to 1: a, e, i, o, u. */
    void SetVowel(float v) { to_ = v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

    PHYMO_HOT void Process(float* l, float* r, size_t n)
    {
        static const float kGain[kFormants] = {1.f, 0.65f, 0.4f, 0.25f};
        if(amount_ <= 0.f && target_ <= 0.f)
        {
            if(pos_ != to_) // silent: jump straight there
                pos_ = to_, Tune();
            return;
        }
        if(pos_ != to_) // gliding, ~20 ms
        {
            pos_ += (to_ - pos_) * (1.f - Exp2Fast(-1.4426950f * static_cast<float>(n) / (0.02f * sr_)));
            pos_ = fabsf(to_ - pos_) < 1e-4f ? to_ : pos_;
            Tune();
        }
        const float a0 = amount_;
        amount_ += (target_ - amount_) * (1.f - Exp2Fast(-1.4426950f * static_cast<float>(n) / (0.01f * sr_)));
        amount_        = amount_ < 1e-4f && target_ <= 0.f ? 0.f : amount_;
        const float da = (amount_ - a0) / static_cast<float>(n);
        float       a  = a0;
        // wet gain from the followers, slid across the block
        float       g = gain_, to = gain_;
        if(wet_ > 1e-12f && dry_ > 1e-12f)
        {
            to = sqrtf(dry_ / wet_);
            to = to < 0.25f ? 0.25f : to > 8.f ? 8.f : to;
        }
        const float dg = (to - g) / static_cast<float>(n);
        float       ed = 0.f, ew = 0.f;
        for(size_t i = 0; i < n; i++)
        {
            a += da, g += dg;
            float wl = 0.f, wr = 0.f;
            for(int f = 0; f < kFormants; f++)
            {
                l_[f].Process(l[i]);
                r_[f].Process(r[i]);
                wl += kGain[f] * l_[f].Bp();
                wr += kGain[f] * r_[f].Bp();
            }
            ed += l[i] * l[i] + r[i] * r[i], ew += wl * wl + wr * wr;
            l[i] += a * (g * wl - l[i]);
            r[i] += a * (g * wr - r[i]);
        }
        gain_          = to;
        const float kf = 1.f - Exp2Fast(-1.4426950f * static_cast<float>(n) / (kFollow * sr_));
        dry_ += (ed / static_cast<float>(n) - dry_) * kf;
        wet_ += (ew / static_cast<float>(n) - wet_) * kf;
    }

    /** Formant `f`'s frequency, Hz (left side). */
    float Formant(int f) const { return hz_[f]; }

  private:
    /** Formants at pos_, interpolated in octaves. */
    void Tune()
    {
        // Peterson & Barney, men's averages: a, e, i, o, u; F4 3.5 kHz
        static const float kHz[kVowels][kFormants] = {
            {730.f, 1090.f, 2440.f, 3500.f},
            {530.f, 1840.f, 2480.f, 3500.f},
            {270.f, 2290.f, 3010.f, 3500.f},
            {570.f, 840.f, 2410.f, 3500.f},
            {300.f, 870.f, 2240.f, 3500.f},
        };
        static const float kBw[kFormants] = {90.f, 110.f, 170.f, 250.f}; // bandwidths, Hz
        const float        x = pos_ * (kVowels - 1);
        int                v = static_cast<int>(x);
        v                    = v > kVowels - 2 ? kVowels - 2 : v;
        const float t        = x - static_cast<float>(v);
        for(int f = 0; f < kFormants; f++)
        {
            hz_[f] = kHz[v][f] * Exp2Fast(t * Log2Fast(kHz[v + 1][f] / kHz[v][f]));
            l_[f].Set(hz_[f], hz_[f] / kBw[f], sr_);
            r_[f].Set(1.03f * hz_[f], 1.03f * hz_[f] / kBw[f], sr_);
        }
    }

    Svf   l_[kFormants], r_[kFormants];
    float hz_[kFormants];
    float amount_, target_; // amount, gliding to the target
    float pos_, to_; // vowel position, gliding to the target
    float dry_, wet_; // dry and wet power followers
    float gain_; // wet gain to match the dry
    float sr_;
};

/** Tremolo, the same or opposite on each side. */
class Tremolo
{
  public:
    void Init(float sr)
    {
        sr_ = sr;
        Reset();
    }

    void Reset()
    {
        phase_ = 0.f;
        rate_ = 5.f, depth_ = 0.f, spread_ = 0.f;
        gl_ = gr_ = 1.f;
    }

    void SetRate(float hz) { rate_ = hz; }
    void SetDepth(float d) { depth_ = d; }
    /** 0: the same both sides; 1: opposite (autopan). */
    void SetSpread(float s) { spread_ = s; }

    PHYMO_HOT void Process(float* l, float* r, size_t n)
    {
        phase_ += rate_ * static_cast<float>(n) / sr_;
        phase_ -= phase_ >= 1.f ? 1.f : 0.f;
        const float m  = 0.5f + 0.5f * SinCycles(phase_); // 0 to 1
        const float mr = m + (1.f - 2.f * m) * spread_; // m, or 1 − m at full spread
        const float tl = 1.f - depth_ * m, tr = 1.f - depth_ * mr;
        if(depth_ <= 0.f && gl_ == 1.f && gr_ == 1.f)
            return;
        const float dl = (tl - gl_) / static_cast<float>(n), dr = (tr - gr_) / static_cast<float>(n);
        for(size_t i = 0; i < n; i++)
        {
            gl_ += dl, gr_ += dr;
            l[i] *= gl_, r[i] *= gr_;
        }
        gl_ = tl, gr_ = tr;
    }

  private:
    float sr_;
    float phase_;                // 0 to 1
    float rate_, depth_, spread_;
    float gl_, gr_; // gains at the end of the last block
};

} // namespace phymo

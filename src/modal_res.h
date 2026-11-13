/** @file modal_res.h
 *  @brief The Spectral model's resonator: up to 24 decaying partials (modal
 *  synthesis), shaped from tables.h.
 *
 *  Each partial is a decaying phasor, z ← r e^(jω) z + x, heard as Im(z), so
 *  frequencies can move while ringing without jumps. Gains apply at the
 *  output, sliding across each update.
 *
 *   - Table: interpolates ratios (in octaves) and gains between tables.
 *   - Stretch: ratios to the power 1 ± 0.5.
 *   - Snap: pulls ratios smoothly towards whole numbers.
 *   - Partials: 1 to 24, the last fading in.
 *   - Tilt: higher partials decay faster, ratio^(−tilt).
 *   - Tink: fast-decaying overtones for the tine and reed tables.
 *   - Tonebar: an extra phasor a few cents above the fundamental, driven by
 *     it, ringing longer: a bloom and beating that outlasts the tine.
 *  Partials fade out approaching 0.45 of the sample rate. Only sounding
 *  partials run, each across a chunk of samples at a time.
 */
#pragma once
#include "dsp_util.h"
#include "tables.h"
#include <cmath>

namespace phymo
{

/** The spectrum, from the panel and the voice's morph. */
struct Shape
{
    float table    = 0.f; // 0 to kTables − 1
    float stretch  = 0.f; // −0.5 to 0.5
    float snap     = 0.f; // 0 to 1
    float partials = 24.f; // 1 to kModes
    float tilt     = 1.f; // decay tilt with ratio
    float bar      = 0.f; // tonebar, 0 (off) to 1
};

class ModalRes
{
  public:
    static constexpr float kLevel = 0.25f; // output level (modal_test)
    static constexpr float kTop   = 0.45f; // partials stop short of this share of the sample rate
    // the tonebar along its knob: a slow bloom low, a fast shimmer high
    static constexpr float kBarCents   = 3.f; // cents above the fundamental, at the bottom
    static constexpr float kBarSpread  = 12.f; // extra cents at the top
    static constexpr float kBarSustain = 3.f; // ring time against the fundamental's, at the bottom
    static constexpr float kBarSustainTop = 2.5f; // and at the top
    static constexpr float kBarCouple  = 0.6f; // its level against the fundamental's
    static constexpr float kBarReach   = 0.4f; // reaching it by this far along the knob

    void Init(float sr)
    {
        sr_ = sr;
        auto& lr = Octaves();
        for(int t = 0; t < kTables; t++)
            for(int k = 0; k < kModes; k++)
                lr[t][k] = log2f(kRatio[t][k]);
        Reset();
    }

    void Reset()
    {
        for(int k = 0; k < kModes; k++)
            re_[k] = im_[k] = 0.f, cr_[k] = ci_[k] = 0.f, g_[k] = dg_[k] = gt_[k] = 0.f;
        bar_re_ = bar_im_ = bar_cr_ = bar_ci_ = bar_c_ = 0.f;
        run_   = 0;
        steps_ = 1;
    }

    /** Note (Hz), T60 (s), spectrum, and samples until the next Set (gains
     *  slide across them). */
    PHYMO_HOT void Set(float freq, float t60, const Shape& s, int steps)
    {
        t60 = t60 < 0.01f ? 0.01f : t60;
        float pos = s.table < 0.f ? 0.f : s.table > kTables - 1 ? kTables - 1 : s.table;
        int   a   = static_cast<int>(pos);
        a         = a > kTables - 2 ? kTables - 2 : a;
        const float f     = pos - static_cast<float>(a);
        const float pow1  = 1.f + s.stretch;
        const float top   = kTop * sr_;
        const float k60   = -9.9657843f / (t60 * sr_); // log2(0.001) per sample, at ratio 1
        const float cyc   = freq / sr_; // cycles per sample
        int         n     = static_cast<int>(ceilf(s.partials));
        n                 = n < 1 ? 1 : n > kModes ? kModes : n;
        steps_            = steps < 1 ? 1 : steps;
        const auto& lr    = Octaves();
        const float room  = Log2Fast(top / freq); // octaves to the top
        const float p_snap = 1.f / (1.f - 0.97f * (s.snap > 1.f ? 1.f : s.snap)); // Snap's exponent: 1 off, 33 full
        const float ta = kTink[a], tb = kTink[a + 1];
        const float tink = -9.9657843f / sr_ * ((ta > 0.f ? 1.f / ta : 0.f) * (1.f - f) + (tb > 0.f ? 1.f / tb : 0.f) * f); // tink decay
        float       sum   = 0.f, prev = 0.f;
        float       t0 = 0.f, k0 = 0.f; // the fundamental's, for the tonebar
        for(int k = 0; k < kModes; k++)
            g_[k] = gt_[k]; // finish the last slide exactly
        for(int k = 0; k < n; k++)
        {
            float oct  = pow1 * (lr[a][k] + (lr[a + 1][k] - lr[a][k]) * f); // ratio, in octaves
            float gain = kGain[a][k] + (kGain[a + 1][k] - kGain[a][k]) * f;
            float ratio = Exp2Fast(oct);
            if(s.snap > 0.f)
            {
                float       n = floorf(ratio + 0.5f);
                n             = n < 1.f ? 1.f : n;
                const float d = ratio - n, a = fabsf(2.f * d); // 0 on a whole number, 1 halfway
                ratio         = n + (d < 0.f ? -0.5f : 0.5f) * (a > 1e-6f ? Exp2Fast(p_snap * Log2Fast(a)) : 0.f);
                oct           = Log2Fast(ratio);
                // fade as it meets the partial below
                const float gap = (ratio - prev) * 2.f;
                gain *= 1.f - s.snap * (1.f - (gap > 1.f ? 1.f : gap < 0.f ? 0.f : gap));
            }
            prev             = ratio;
            const float fade = s.partials - static_cast<float>(k); // the last one fades in
            gain *= fade > 1.f ? 1.f : fade;
            const float left = room - oct; // octaves to the top
            if(left <= 0.f)
            {
                gt_[k] = 0.f; // past the top: silent
                continue;
            }
            gain *= left < 0.5f ? 2.f * left : 1.f; // fading over the last half octave
            const float t = cyc * ratio; // cycles per sample
            float       kk = k60 * Exp2Fast(s.tilt * oct); // decay: ratio^tilt faster
            if(k == 0)
                t0 = t, k0 = kk; // for the tonebar
            kk             = k > 0 && tink < kk ? tink : kk; // overtones: the tink's decay, if faster
            const float r  = Exp2Fast(kk);
            cr_[k]        = r * SinCycles(t + 0.25f);
            ci_[k]        = r * SinCycles(t);
            gt_[k]        = gain;
            sum += gain;
        }
        for(int k = n; k < kModes; k++)
            gt_[k] = 0.f;
        // tonebar: a few cents up, slower decay, driven by the fundamental
        if(s.bar > 0.f && t0 > 0.f)
        {
            const float tb = t0 * Exp2Fast((kBarCents + kBarSpread * s.bar) / 1200.f);
            const float rb = Exp2Fast(k0 / (kBarSustain + (kBarSustainTop - kBarSustain) * s.bar));
            bar_cr_        = rb * SinCycles(tb + 0.25f);
            bar_ci_        = rb * SinCycles(tb);
            const float d  = 6.2831853f * (tb - t0), l = 1.f - rb;
            const float lv = kBarCouple * (s.bar < kBarReach ? s.bar / kBarReach : 1.f);
            bar_c_         = lv * sqrtf(l * l + d * d);
        }
        else
            bar_c_ = 0.f, bar_re_ = bar_im_ = 0.f;
        const float norm = sum > 1e-6f ? kLevel / sqrtf(sum) : 0.f;
        run_             = 0;
        for(int k = 0; k < kModes; k++)
        {
            gt_[k] *= norm;
            dg_[k] = (gt_[k] - g_[k]) / static_cast<float>(steps_);
            if(gt_[k] != 0.f || g_[k] != 0.f)
                run_ = k + 1;
            else
                re_[k] = im_[k] = 0.f; // silent: clear it
        }
    }

    /** `n` samples: `x` in (the strike), the partials' sum added to `y`. */
    PHYMO_HOT void Process(const float* x, float* y, int n)
    {
        float f0[kChunk]; // the fundamental, for the tonebar
        const bool bar = bar_c_ > 0.f && n <= kChunk;
        if(bar)
            for(int i = 0; i < n; i++)
                f0[i] = 0.f;
        for(int k = 0; k < run_; k++)
        {
            float       re = re_[k], im = im_[k], g = g_[k];
            const float cr = cr_[k], ci = ci_[k], dg = dg_[k];
            if(g == 0.f && dg == 0.f)
                continue; // silent, kept for a later note
            for(int i = 0; i < n; i++)
            {
                const float nr = cr * re - ci * im + x[i];
                im             = ci * re + cr * im;
                re             = nr;
                g += dg;
                y[i] += g * im;
                if(bar && k == 0)
                    f0[i] = im;
            }
            re_[k] = re, im_[k] = im, g_[k] = g;
        }
        if(bar) // tonebar, driven by the fundamental
        {
            float       re = bar_re_, im = bar_im_;
            const float cr = bar_cr_, ci = bar_ci_, c = bar_c_, g = g_[0];
            for(int i = 0; i < n; i++)
            {
                const float nr = cr * re - ci * im + c * f0[i];
                im             = ci * re + cr * im;
                re             = nr;
                y[i] += g * im;
            }
            bar_re_ = re, bar_im_ = im;
        }
    }

    /** Partial `k`'s pitch (Hz, 0 if silent) and gain. */
    float Hz(int k) const { return gt_[k] != 0.f ? Atan2Fast(ci_[k], cr_[k]) * 0.15915494f * sr_ : 0.f; }
    float Gain(int k) const { return gt_[k]; }

  private:
    /** Table ratios in octaves, shared by all voices. */
    static float (&Octaves())[kTables][kModes]
    {
        static float lr[kTables][kModes];
        return lr;
    }

    static constexpr int kChunk = 32; // max samples per Process
    float re_[kModes], im_[kModes]; // phasors
    float bar_re_, bar_im_, bar_cr_, bar_ci_, bar_c_; // tonebar phasor, rotation and drive
    float cr_[kModes], ci_[kModes]; // rotation per sample: r cos w, r sin w
    float g_[kModes], dg_[kModes], gt_[kModes]; // output gain, slide per sample, target
    int   run_; // partials to run
    int   steps_; // samples between Sets
    float sr_;
};

} // namespace phymo

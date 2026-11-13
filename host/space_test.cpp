// Checks the send bus (space.h): nothing sent is untouched; the delay's
// echo lands at its time and repeats, straight or ping-pong; the space knob
// picks delay, both or reverb; tails lengthen the reverb; full tails stay
// bounded; times it; renders chords through it:
//   make -C host space_test && (cd host && ./space_test)
#include "../src/synth.h"
#include "wav.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr float kSr    = 48000.f;
static int             g_fails = 0;

static std::vector<uint16_t> g_tank(phymo::Space::kTankWords);
static std::vector<int16_t>  g_lines(phymo::Space::kDelayWords);

static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "pass" : "FAIL", what);
    g_fails += !ok;
}

/** A click through `space` (both sides), `secs` long. */
static void Click(phymo::Space& space, float secs, std::vector<float>& l, std::vector<float>& r)
{
    const size_t n = static_cast<size_t>(secs * kSr);
    l.assign(n, 0.f), r.assign(n, 0.f);
    l[0] = r[0] = 1.f;
    for(size_t pos = 0; pos < n; pos += 48)
        space.Process(&l[pos], &r[pos], n - pos < 48 ? n - pos : 48);
}

/** The biggest |x| between `a` and `b` seconds. */
static float Peak(const std::vector<float>& x, float a, float b)
{
    float p = 0.f;
    for(size_t i = static_cast<size_t>(a * kSr); i < static_cast<size_t>(b * kSr) && i < x.size(); i++)
        p = fmaxf(p, fabsf(x[i]));
    return p;
}

/** Where the biggest |x| is between `a` and `b` seconds, in seconds. */
static float Where(const std::vector<float>& x, float a, float b)
{
    size_t w = static_cast<size_t>(a * kSr);
    for(size_t i = w; i < static_cast<size_t>(b * kSr) && i < x.size(); i++)
        if(fabsf(x[i]) > fabsf(x[w]))
            w = i;
    return w / kSr;
}

/** How long until the level stays below -60 dB of its peak (s). */
static float Tail(const std::vector<float>& x)
{
    float peak = 0.f;
    for(float v : x)
        peak = fmaxf(peak, fabsf(v));
    size_t last = 0;
    for(size_t f = 0; f + 480 <= x.size(); f += 480)
    {
        float m = 0.f;
        for(size_t i = f; i < f + 480; i++)
            m = fmaxf(m, fabsf(x[i]));
        if(m > peak * 0.001f)
            last = f + 480;
    }
    return last / kSr;
}

static void Setup(phymo::Space& s, float send, float space, float time, float tails, float pp)
{
    s.Init(kSr, g_tank.data(), g_lines.data());
    s.SetSend(send), s.SetSpace(space), s.SetTime(time), s.SetTails(tails), s.SetPingPong(pp);
}

int main()
{
    phymo::Space        s;
    std::vector<float> l, r;
    const float        t = .99f * .125f * 2.f + .01f; // the time at 0.5: ~258 ms

    // Nothing sent: untouched.
    Setup(s, 0.f, 0.5f, 0.5f, 0.5f, 1.f);
    Click(s, 1.f, l, r);
    Check(l[0] == 1.f && Peak(l, 0.001f, 1.f) == 0.f && Peak(r, 0.001f, 1.f) == 0.f,
          "send 0: the signal passes untouched");

    // The delay alone, straight: an echo on both sides at the time, repeating.
    Setup(s, 0.5f, 0.f, 0.5f, 0.5f, 0.f);
    Click(s, 1.f, l, r);
    {
        const float e1 = Where(l, 0.01f, 1.5f * t), e2 = Where(l, e1 + 0.01f, e1 + 1.5f * t);
        char        what[96];
        std::snprintf(what, sizeof what, "delay: echoes at %.0f and %.0f ms (time %.0f ms), both sides", 1000.f * e1,
                      1000.f * e2, 1000.f * t);
        Check(fabsf(e1 - t) < 0.002f && fabsf(e2 - 2.f * t) < 0.004f && Peak(r, e1 - 0.002f, e1 + 0.002f) > 0.1f
                  && Peak(l, e2 - 0.002f, e2 + 0.002f) < Peak(l, e1 - 0.002f, e1 + 0.002f),
              what);
        Check(Peak(l, e1 + 0.01f, e2 - 0.01f) < 0.01f, "delay alone: no reverb between the echoes");
    }

    // Ping-pong: the first echo on the left only, the next on the right only.
    Setup(s, 0.5f, 0.f, 0.5f, 0.5f, 1.f);
    Click(s, 1.f, l, r);
    {
        const float l1 = Peak(l, t - 0.002f, t + 0.002f), r1 = Peak(r, t - 0.002f, t + 0.002f);
        const float l2 = Peak(l, 2 * t - 0.003f, 2 * t + 0.003f), r2 = Peak(r, 2 * t - 0.003f, 2 * t + 0.003f);
        char        what[110];
        std::snprintf(what, sizeof what, "ping-pong: echo 1 L %.2f R %.2f, echo 2 L %.2f R %.2f", l1, r1, l2, r2);
        Check(l1 > 0.1f && r1 < 0.001f && r2 > 0.05f && l2 < 0.001f, what);
    }

    // The reverb alone: no echo at the time, a tail; tails lengthen it.
    float tails[2];
    for(int k = 0; k < 2; k++)
    {
        Setup(s, 0.5f, 1.f, 0.5f, k ? 0.9f : 0.2f, 1.f);
        Click(s, 12.f, l, r);
        tails[k] = Tail(l);
    }
    std::printf("      reverb tail to -60 dB: %.1f s at tails 0.2, %.1f s at 0.9\n", tails[0], tails[1]);
    Check(tails[1] > 2.f * tails[0] && tails[0] > 0.2f, "reverb alone: the tails knob lengthens it");

    // Both: an echo at the time and the reverb.
    Setup(s, 0.5f, 0.5f, 0.5f, 0.5f, 0.f);
    Click(s, 1.f, l, r);
    Check(Peak(l, t - 0.002f, t + 0.002f) > 0.1f && Peak(l, 0.06f, t - 0.01f) > 0.005f,
          "space 0.5: the echo and the reverb both");

    // Changing the time: crossfading keeps the repeats' pitch; gliding bends it.
    // A steady 1 kHz into the delay alone (all wet, no feedback), the time
    // changed at 1 s; the repeats' pitch over the next 0.4 s, in 10 ms frames.
    auto bend = [&](bool glide) {
        Setup(s, 1.f, 0.f, 0.5f, 0.f, 0.f);
        s.SetGlide(glide);
        const int          n = 2 * 48000;
        std::vector<float> bl(n), br(n);
        for(int b = 0; b < n; b += 48)
        {
            if(b == 48000)
                s.SetTime(0.6f);
            for(int i = 0; i < 48; i++)
                bl[b + i] = br[b + i] = 0.5f * sinf(6.2831853f * 1000.f * (b + i) / kSr);
            s.Process(&bl[b], &br[b], 48);
        }
        float worst = 0.f;
        for(int f = 48000; f + 480 < 48000 + 19200; f += 480)
        {
            int ups = 0, first = -1, last = -1;
            for(int i = f + 1; i < f + 480; i++)
                if(bl[i - 1] < 0.f && bl[i] >= 0.f)
                    first = first < 0 ? i : first, last = i, ups++;
            if(ups > 2)
            {
                const float hz = kSr * (ups - 1) / float(last - first);
                worst          = fmaxf(worst, fabsf(hz - 1000.f));
            }
        }
        return worst;
    };
    const float fade_dev = bend(false), glide_dev = bend(true);
    std::printf("      a time change: the repeats' pitch off by up to %.0f Hz crossfading, %.0f Hz gliding (of 1 kHz)\n",
                fade_dev, glide_dev);
    Check(fade_dev < 10.f, "crossfade: the repeats don't bend in pitch");
    Check(glide_dev > 50.f, "glide: they bend, as tape");

    // Full tails, loud noise in, 20 s: bounded.
    Setup(s, 1.f, 0.5f, 0.3f, 1.f, 1.f);
    {
        uint32_t rng  = 1;
        float    peak = 0.f, bl[48], br[48];
        bool     ok   = true;
        for(int b = 0; b < 20 * 1000; b++)
        {
            for(int i = 0; i < 48; i++)
            {
                rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
                bl[i] = br[i] = ((rng >> 8) / 8388608.f - 1.f);
            }
            s.Process(bl, br, 48);
            for(int i = 0; i < 48; i++)
                ok &= std::isfinite(bl[i]) && std::isfinite(br[i]), peak = fmaxf(peak, fabsf(bl[i]));
        }
        char what[64];
        std::snprintf(what, sizeof what, "full tails stay bounded (peak %.2f)", peak);
        Check(ok && peak < 4.f, what);
    }

    // The cost, against the synth's six voices (desktop: compare only).
    auto time = [&](bool fx) {
        phymo::Synth syn;
        syn.Init(kSr);
        syn.InitSpace(g_tank.data(), g_lines.data());
        syn.SetChorus(phymo::Chorus::Mode::I);
        if(fx)
            syn.SetSend(0.4f), syn.SetSpace(0.5f);
        for(int n = 0; n < 6; n++)
            syn.NoteOn(48 + 4 * n);
        float      bl[48], br[48];
        const auto t0 = std::chrono::steady_clock::now();
        for(int b = 0; b < 20000; b++)
            syn.Process(bl, br, 48);
        return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count()
               / (20000.0 * 48);
    };
    const double dry = time(false), wet = time(true);
    std::printf("      six voices + chorus %.0f ns a sample; with delay and reverb %.0f (+%.0f%%)\n", dry, wet,
                100.0 * (wet - dry) / dry);

    // A render: chords, chorus I, ping-pong delay and reverb.
    phymo::Synth syn;
    syn.Init(kSr);
    syn.InitSpace(g_tank.data(), g_lines.data());
    syn.SetVolume(0.5f);
    syn.SetChorus(phymo::Chorus::Mode::I);
    syn.SetDamper(0.4f);
    syn.SetSend(0.4f), syn.SetSpace(0.4f), syn.SetTime(0.55f), syn.SetTails(0.5f), syn.SetPingPong(1.f);
    const int          chords[4][4] = {{48, 55, 60, 64}, {45, 52, 57, 60}, {41, 48, 53, 57}, {43, 50, 55, 59}};
    const int          n            = 11 * 48000;
    std::vector<float> ol(n), orr(n);
    for(int b = 0; b < n; b += 48)
    {
        const int sec = b / 48000;
        if(b % 96000 == 0 && sec / 2 < 4)
            for(int k : chords[sec / 2])
                syn.NoteOn(k);
        if(b % 96000 == 30000 && sec / 2 < 4)
            for(int k : chords[sec / 2])
                syn.NoteOff(k);
        syn.Process(&ol[b], &orr[b], 48);
    }
    WriteWav("out/space_chords.wav", ol, orr, 48000);
    std::printf("      wrote out/space_chords.wav (short chords: ping-pong delay and reverb)\n");
    return g_fails ? 1 : 0;
}

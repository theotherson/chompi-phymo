// Checks the chorus against Juno-60 measurements, then
// renders a chord through each mode to listen to:
//   make -C host chorus_test && (cd host && ./chorus_test)
//
// The delay is measured the direct way: a click every 10 ms, and where its
// echo comes out of each side. That gives each side's delay over time, so
// the sweep's range, rate and the right side's upside-down sweep can be
// checked against the table in chorus.h.
#include "../src/chorus.h"
#include "wav.h"
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr float kSr    = 48000.f;
static int             g_fails = 0;

static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "pass" : "FAIL", what);
    g_fails += !ok;
}

struct Sweep
{
    float min_ms, max_ms, hz;
    bool  opposite; // the two sides' delays move in opposite directions
    bool  same;     // the two sides are identical
};

static Sweep MeasureSweep(phymo::Chorus::Mode mode)
{
    phymo::Chorus ch;
    ch.Init(kSr);
    ch.SetMode(mode);
    const int          n = 8 * 48000, every = 480; // 8 s, a click each 10 ms
    std::vector<float> in(n, 0.f), l(n), r(n);
    for(int i = every; i < n; i += every)
        in[i] = 1.f;
    ch.Process(in.data(), l.data(), r.data(), n);

    // Each click's echo: the biggest wet sample in the 10 ms after it (the
    // dry click itself is at offset 0, so skip the first 1 ms).
    std::vector<float> dl, dr, tt;
    float              diff = 0.f;
    for(int c = 2 * 48000; c + every < n; c += every) // after the fade-in
    {
        int   bl = 0, br = 0;
        float vl = 0.f, vr = 0.f;
        for(int k = 48; k < every; k++)
        {
            if(fabsf(l[c + k]) > vl)
                vl = fabsf(l[c + k]), bl = k;
            if(fabsf(r[c + k]) > vr)
                vr = fabsf(r[c + k]), br = k;
        }
        dl.push_back(bl / 48.f);
        dr.push_back(br / 48.f);
        tt.push_back(c / kSr);
    }
    for(int i = 0; i < n; i++)
        diff = fmaxf(diff, fabsf(l[i] - r[i]));

    Sweep s{1e9f, 0.f, 0.f, false, diff < 1e-6f};
    for(float d : dl)
        s.min_ms = fminf(s.min_ms, d), s.max_ms = fmaxf(s.max_ms, d);
    // the rate: count the left delay's crossings of its middle, upwards
    const float mid = 0.5f * (s.min_ms + s.max_ms);
    int         ups = 0;
    float       first = -1.f, last = 0.f;
    for(size_t i = 1; i < dl.size(); i++)
        if(dl[i - 1] < mid && dl[i] >= mid)
        {
            if(first < 0.f)
                first = tt[i];
            last = tt[i];
            ups++;
        }
    s.hz = ups > 1 ? (ups - 1) / (last - first) : 0.f;
    // opposite: when the left is long the right is short
    double corr = 0;
    for(size_t i = 0; i < dl.size(); i++)
        corr += (dl[i] - mid) * (dr[i] - mid);
    s.opposite = corr < 0;
    return s;
}

static float Rms(const std::vector<float>& x, int from)
{
    double a = 0;
    for(size_t i = from; i < x.size(); i++)
        a += x[i] * x[i];
    return sqrtf(float(a / (x.size() - from)));
}

/** A plain saw (not band-limited: enough to hear a chorus by). */
struct Saw
{
    float ph = 0.f, inc = 0.f;
    void  Init(float) {}
    void  SetFreq(float hz) { inc = hz / kSr; }
    float Process()
    {
        ph += inc;
        ph -= ph >= 1.f ? 1.f : 0.f;
        return 2.f * ph - 1.f;
    }
};

/** A C minor chord of saws, 4 s, through `mode`. */
static void Chord(phymo::Chorus::Mode mode, std::vector<float>& l, std::vector<float>& r, std::vector<float>& dry)
{
    const float notes[3] = {261.63f, 311.13f, 392.f};
    Saw          dco[3];
    for(int v = 0; v < 3; v++)
    {
        dco[v].Init(kSr);
        dco[v].SetFreq(notes[v]);
    }
    const int n = 4 * 48000;
    dry.assign(n, 0.f), l.assign(n, 0.f), r.assign(n, 0.f);
    for(int i = 0; i < n; i++)
        for(auto& d : dco)
            dry[i] += 0.2f * d.Process();
    phymo::Chorus ch;
    ch.Init(kSr);
    ch.SetMode(mode);
    ch.Process(dry.data(), l.data(), r.data(), n);
}

int main()
{
    struct Want
    {
        phymo::Chorus::Mode mode;
        const char*        name;
        float              min_ms, max_ms, hz;
        bool               mono;
    };
    const Want want[3] = {{phymo::Chorus::Mode::I, "I", 1.66f, 5.35f, 0.513f, false},
                          {phymo::Chorus::Mode::II, "II", 1.66f, 5.35f, 0.863f, false},
                          {phymo::Chorus::Mode::Both, "I + II", 3.3f, 3.7f, 9.75f, true}};
    for(const Want& w : want)
    {
        const Sweep s = MeasureSweep(w.mode);
        std::printf("      %-6s delay %.2f - %.2f ms (want %.2f - %.2f), %.3f Hz (want %.3f)\n", w.name, s.min_ms,
                    s.max_ms, w.min_ms, w.max_ms, s.hz, w.hz);
        char what[96];
        // the echo's peak lands up to ~0.1 ms late (the filters), and the
        // fast mode is sampled only every 10 ms, so allow for both
        std::snprintf(what, sizeof what, "%s: the delay sweep's range", w.name);
        Check(fabsf(s.min_ms - w.min_ms) < 0.15f && fabsf(s.max_ms - w.max_ms) < 0.15f, what);
        if(!w.mono)
        {
            std::snprintf(what, sizeof what, "%s: the LFO's rate", w.name);
            Check(fabsf(s.hz - w.hz) < 0.02f, what);
            std::snprintf(what, sizeof what, "%s: stereo, the sides sweeping in opposite directions", w.name);
            Check(s.opposite && !s.same, what);
        }
        else
        {
            std::snprintf(what, sizeof what, "%s: mono", w.name);
            Check(s.same, what);
        }
    }

    // Off is the dry signal, exactly; on, the level stays about the same.
    std::vector<float> l, r, dry;
    Chord(phymo::Chorus::Mode::Off, l, r, dry);
    float worst = 0.f;
    for(size_t i = 0; i < dry.size(); i++)
        worst = fmaxf(worst, fmaxf(fabsf(l[i] - dry[i]), fabsf(r[i] - dry[i])));
    Check(worst == 0.f, "Off: exactly the dry signal");
    WriteWav("out/chorus_off.wav", l, r, 48000);

    const struct
    {
        phymo::Chorus::Mode mode;
        const char *       name, *file;
    } renders[4] = {{phymo::Chorus::Mode::I, "I", "out/chorus_I.wav"},
                    {phymo::Chorus::Mode::II, "II", "out/chorus_II.wav"},
                    {phymo::Chorus::Mode::Both, "I + II", "out/chorus_both.wav"},
                    {phymo::Chorus::Mode::Parallel, "I | II", "out/chorus_parallel.wav"}};
    for(const auto& m : renders)
    {
        Chord(m.mode, l, r, dry);
        const float db = 20.f * log10f(0.5f * (Rms(l, 48000) + Rms(r, 48000)) / Rms(dry, 48000));
        char        what[64];
        std::snprintf(what, sizeof what, "%s: level within 2 dB of dry (%+.1f dB)", m.name, db);
        Check(fabsf(db) < 2.f, what);
        WriteWav(m.file, l, r, 48000);
    }

    // I | II: both sweeps in each side, so stereo, and moving at both rates.
    {
        Chord(phymo::Chorus::Mode::Parallel, l, r, dry);
        double lr = 0, ll = 0, rr = 0;
        for(size_t i = 48000; i < l.size(); i++)
            lr += l[i] * r[i], ll += l[i] * l[i], rr += r[i] * r[i];
        const float corr = float(lr / sqrt(ll * rr));
        char        what[64];
        std::snprintf(what, sizeof what, "I | II: stereo (sides' correlation %.2f)", corr);
        Check(corr < 0.98f, what);
    }
    std::printf("      wrote out/chorus_off, I, II, both, parallel.wav (a C minor chord of saws)\n");
    return g_fails ? 1 : 0;
}

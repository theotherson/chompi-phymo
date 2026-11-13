// Checks the Spectral model (modal_res.h, tables.h, the voice's morph): the
// fundamental's tuning; each table's partials where the table puts them;
// the morph gliding a note from one spectrum to another; the decay and its
// tilt; the partials count, snap and stretch; snap continuous, the top
// faded, a stepped sweep as clean as a smooth one; its level against the
// string's; bounded. Then renders some morphs to listen to:
//   make -C host modal_test && (cd host && ./modal_test)
#include "../src/synth.h"
#include "wav.h"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr float kSr     = 48000.f;
static int             g_fails = 0;
using phymo::Model;
using phymo::kRatio;
using phymo::kTables;

static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "pass" : "FAIL", what);
    g_fails += !ok;
}

static void Play(phymo::Synth& s, int ms, std::vector<float>& l, std::vector<float>& r)
{
    float lb[48], rb[48];
    for(int b = 0; b < ms; b++)
    {
        s.Process(lb, rb, 48);
        l.insert(l.end(), lb, lb + 48), r.insert(r.end(), rb, rb + 48);
    }
}

/** A fresh synth on the Spectral model: mono, a long decay, bright (no tilt), no key tracking. */
static void Fresh(phymo::Synth& s, float table = 0.f)
{
    s.Init(kSr);
    s.SetKeyMode(phymo::KeyMode::Mono);
    s.SetModel(Model::Spectral);
    s.SetVolume(1.f), s.SetDecay(8.f), s.SetKeyTrack(0.f), s.SetBrightness(1.f);
    s.SetTable(table / (kTables - 1));
}

static std::vector<float> Note(phymo::Synth& s, int note, int ms)
{
    s.NoteOn(note);
    std::vector<float> l, r;
    Play(s, ms, l, r);
    return l;
}

/** |X(hz)|² of `x` over `n` samples from `from`, Hann-windowed. */
static double Mag(const std::vector<float>& x, size_t from, size_t n, double hz)
{
    double re = 0, im = 0;
    for(size_t i = 0; i < n; i++)
    {
        const double w = 0.5 - 0.5 * cos(6.283185307 * i / n), ph = 6.283185307 * hz * i / kSr;
        re += w * x[from + i] * cos(ph), im -= w * x[from + i] * sin(ph);
    }
    return re * re + im * im;
}

/** The strongest frequency within `cents` of `expect`: coarse, then fine. */
static double Peak(const std::vector<float>& x, size_t from, size_t n, double expect, double cents = 60)
{
    double best = expect, bm = -1;
    for(double c = -cents; c <= cents; c += 5)
    {
        const double hz = expect * pow(2.0, c / 1200.0), m = Mag(x, from, n, hz);
        if(m > bm)
            bm = m, best = hz;
    }
    const double centre = best;
    for(double c = -5; c <= 5; c += 0.1)
    {
        const double hz = centre * pow(2.0, c / 1200.0), m = Mag(x, from, n, hz);
        if(m > bm)
            bm = m, best = hz;
    }
    return best;
}

static double Cents(double hz, double want) { return 1200.0 * log2(hz / want); }

static double Rms(const std::vector<float>& x, size_t from, size_t n)
{
    double e = 0;
    for(size_t i = from; i < from + n; i++)
        e += static_cast<double>(x[i]) * x[i];
    return sqrt(e / n);
}

static double Hz(int note) { return 440.0 * pow(2.0, (note - 69) / 12.0); }

int main()
{
    phymo::Synth s;

    // Tuning: the harmonic table's fundamental, C1 to C7.
    {
        double worst = 0;
        int    wn    = 24;
        for(int note = 24; note <= 96; note += 8)
        {
            Fresh(s);
            const auto   x = Note(s, note, 1200);
            const double c = Cents(Peak(x, 9600, 38400, Hz(note)), Hz(note));
            if(fabs(c) > fabs(worst))
                worst = c, wn = note;
        }
        char what[80];
        std::snprintf(what, sizeof what, "tuning C1-C7: worst %+.2f cents (note %d)", worst, wn);
        Check(fabs(worst) < 1.0, what);
    }

    // Each table: its 2nd and 3rd partials where it puts them (C3; those under the top; the
    // tine's and reed's in their first 50 ms, their tink).
    {
        double worst = 0;
        int    wt = 0, wk = 2;
        for(int t = 0; t < kTables; t++)
        {
            Fresh(s, static_cast<float>(t));
            const auto x = Note(s, 48, 800);
            for(int k = 1; k <= 2; k++)
            {
                const double want = Hz(48) * kRatio[t][k];
                if(want > 0.4 * kSr)
                    continue;
                const bool   quick = phymo::kTink[t] > 0.f; // (its overtones a tink: heard from the strike, gone by 0.1 s)
                const double c     = Cents(Peak(x, quick ? 0 : 4800, quick ? 2400 : 28800, want, 30), want);
                if(fabs(c) > fabs(worst))
                    worst = c, wt = t, wk = k + 1;
            }
        }
        char what[96];
        std::snprintf(what, sizeof what, "every table's 2nd and 3rd partials: worst %+.2f cents (table %d, partial %d)",
                      worst, wt, wk);
        Check(fabs(worst) < 1.0, what);
    }

    // The tink: at full brightness (8 s, no tilt), the tine's and reed's
    // overtones gone within 0.2 s, their fundamental ringing on; the free
    // bar's (no tink) still there.
    {
        double drop[3];
        const int tab[3] = {3, 4, 5};
        for(int i = 0; i < 3; i++)
        {
            Fresh(s, static_cast<float>(tab[i]));
            const auto   x  = Note(s, 48, 400);
            const double f1 = Hz(48), f2 = Hz(48) * kRatio[tab[i]][1];
            // the 2nd partial against the 1st: at the strike, and at 0.2 s
            const double a = 10 * log10(Mag(x, 0, 960, f2) / Mag(x, 0, 960, f1));
            const double b = 10 * log10(Mag(x, 9600, 4800, f2) / Mag(x, 9600, 4800, f1));
            drop[i] = a - b;
        }
        char what[120];
        std::snprintf(what, sizeof what, "the tink: the 2nd partial down %.0f dB by 0.2 s (tine), %.0f (reed), %.0f (free bar, none)",
                      drop[0], drop[1], drop[2]);
        Check(drop[0] > 40 && drop[1] > 20 && drop[2] < 10, what);
    }

    // The tonebar: on the tine (C4, 4 s), at full, the note's level at
    // 4.5 s at least 8 dB over none's (its long ring); its swell and fall
    // early (0.3 to 1.5 s, 50 ms steps, the decay's line taken off) over
    // a dB; none, under half one.
    {
        double late[2], swing[2];
        for(int b = 0; b < 2; b++)
        {
            Fresh(s, 3.f);
            s.SetDecay(4.f), s.SetTonebar(b ? 1.f : 0.f);
            const auto x = Note(s, 60, 5000);
            late[b]      = 20 * log10(Rms(x, 216000, 2400) + 1e-12);
            std::vector<double> env;
            for(int w = 6; w < 30; w++)
                env.push_back(20 * log10(Rms(x, w * 2400, 2400) + 1e-12));
            double sx = 0, sy = 0, sxx = 0, sxy = 0;
            const int m = static_cast<int>(env.size());
            for(int i = 0; i < m; i++)
                sx += i, sy += env[i], sxx += i * i, sxy += i * env[i];
            const double k = (m * sxy - sx * sy) / (m * sxx - sx * sx), c = (sy - k * sx) / m;
            double lo = 1e9, hi = -1e9;
            for(int i = 0; i < m; i++)
                lo = fmin(lo, env[i] - (k * i + c)), hi = fmax(hi, env[i] - (k * i + c));
            swing[b] = hi - lo;
        }
        char what[160];
        std::snprintf(what, sizeof what, "tonebar: at 4.5 s %.1f dB against none's %.1f; its swell early %.1f dB (none %.1f)", late[1], late[0],
                      swing[1], swing[0]);
        Check(late[1] > late[0] + 8.0 && swing[1] > 1.0 && swing[0] < 0.5, what);
    }

    // The morph: harmonic at the Table knob, each note starting at the tine
    // (3 tables up) and gliding back over 0.4 s. The 2nd partial: near the
    // tine's 6.27 at first, near 2 once the morph's done.
    {
        Fresh(s, 0.f);
        s.SetMorph(0.5f + 0.5f * 3.f / (kTables - 1)), s.SetMorphTime(0.4f);
        const auto   x     = Note(s, 48, 1500);
        const double f0    = Hz(48);
        const double early = Mag(x, 0, 1920, 6.27 * f0) / Mag(x, 0, 1920, 2.0 * f0);    // the first 40 ms
        const double late  = Mag(x, 28800, 19200, 2.0 * f0) / Mag(x, 28800, 19200, 6.27 * f0); // 0.6 s on
        const double where = Peak(x, 28800, 19200, 2.0 * f0, 40);
        char         what[120];
        std::snprintf(what, sizeof what, "morph: the 2nd partial at the tine's ratio first (%.0fx), harmonic later (%.0fx, %+.1f cents)",
                      early, late, Cents(where, 2.0 * f0));
        Check(early > 3.0 && late > 30.0 && fabs(Cents(where, 2.0 * f0)) < 3.0, what);
    }

    // Decay: T60 2 s, 30 dB down a second later; brightness 0 tilts the
    // decay, so the 3rd partial falls much faster than the 1st.
    {
        Fresh(s);
        s.SetDecay(2.f);
        const auto   x    = Note(s, 48, 2000);
        const double drop = 20.0 * log10(Rms(x, 19200, 4800) / Rms(x, 67200, 4800));
        char         what[80];
        std::snprintf(what, sizeof what, "decay 2 s: %.1f dB down in a second (30)", drop);
        Check(fabs(drop - 30.0) < 3.0, what);

        Fresh(s);
        s.SetDecay(2.f), s.SetBrightness(0.f);
        const auto   y  = Note(s, 48, 1200);
        const double f0 = Hz(48);
        const double d1 = 10.0 * log10(Mag(y, 4800, 9600, f0) / Mag(y, 38400, 9600, f0));
        const double d3 = 10.0 * log10(Mag(y, 4800, 9600, 3 * f0) / Mag(y, 38400, 9600, 3 * f0));
        std::snprintf(what, sizeof what, "tilt: over 0.7 s, the 1st partial falls %.0f dB, the 3rd %.0f dB", d1, d3);
        Check(d3 > 2.0 * d1, what);
    }

    // Partials 1: the fundamental alone. Snap 1: the free bar's 2.757 on 3.
    // Stretch 1: the harmonic 2nd at 2^1.5.
    {
        const double f0 = Hz(48);
        Fresh(s);
        s.SetPartials(0.f);
        auto         x   = Note(s, 48, 600);
        const double alone = 10.0 * log10(Mag(x, 4800, 19200, 2 * f0) / Mag(x, 4800, 19200, f0));
        Fresh(s, 4.f);
        s.SetSnap(1.f);
        x                = Note(s, 48, 600);
        const double snap = Cents(Peak(x, 4800, 19200, 3 * f0, 30), 3 * f0);
        Fresh(s);
        s.SetStretch(1.f);
        x                   = Note(s, 48, 600);
        const double want   = f0 * pow(2.0, 1.5);
        const double stretch = Cents(Peak(x, 4800, 19200, want, 30), want);
        char what[120];
        std::snprintf(what, sizeof what, "partials 1: the 2nd %.0f dB under the 1st; snap: %+.1f cents off 3; stretch: %+.1f off 2^1.5",
                      alone, snap, stretch);
        Check(alone < -60.0 && fabs(snap) < 1.0 && fabs(stretch) < 1.0, what);
    }

    // Snap moves partials continuously: Stretch swept in fine steps at half
    // and full Snap, no partial's pitch jumps (rounding would jump one a
    // whole number as its ratio crossed a half); at full Snap they land on
    // whole numbers. And a partial nearing the top fades rather than cuts.
    {
        phymo::ModalRes m;
        m.Init(kSr);
        phymo::Shape sh;
        sh.table = 0.f, sh.partials = 24.f, sh.tilt = 0.f;
        double worst_jump[2] = {0, 0}, worst_whole = 0;
        for(int half = 0; half < 2; half++)
        {
            sh.snap = half ? 1.f : 0.5f;
            float last[phymo::kModes] = {};
            for(int i = 0; i <= 2000; i++)
            {
                sh.stretch = -0.2f + 0.4f * i / 2000.f; // the ratios as powers 0.8 to 1.2
                m.Set(55.f, 2.f, sh, 64);
                for(int k = 0; k < phymo::kModes; k++)
                {
                    const float hz = m.Hz(k);
                    if(i > 0 && hz > 0.f && last[k] > 0.f)
                        worst_jump[half] = fmax(worst_jump[half], fabs(Cents(hz, last[k])));
                    last[k] = hz;
                    if(half && i == 1000 && hz > 0.f) // stretch 0: harmonic already; at ±: checked below
                        worst_whole = fmax(worst_whole, fabs(Cents(hz, 55.0 * round(hz / 55.0))));
                }
            }
        }
        // full Snap on the free bar (no partial near a half): every one on a whole number
        sh.table = 4.f, sh.stretch = 0.f, sh.snap = 1.f;
        m.Set(55.f, 2.f, sh, 64);
        for(int k = 0; k < phymo::kModes; k++)
            if(m.Hz(k) > 0.f && m.Gain(k) > 0.f)
            {
                const double r = m.Hz(k) / 55.0, frac = fabs(r - round(r));
                if(frac < 0.45) // (one right at a half has nowhere to go)
                    worst_whole = fmax(worst_whole, fabs(Cents(r, round(r))));
            }
        char what[160];
        std::snprintf(what, sizeof what, "snap continuous: Stretch in 2000 steps, the biggest jump %.1f cents at half Snap, %.1f at full; full Snap within %.2f cents of whole",
                      worst_jump[0], worst_jump[1], worst_whole);
        // (full Snap is all but a step, so one crossing a half moves fast; but
        // rounding would jump it a whole number: 72 cents or more, up to 24)
        Check(worst_jump[0] < 5.0 && worst_jump[1] < 40.0 && worst_whole < 1.0, what);
        // the top: a partial a quarter octave under it at half gain, one octave under, whole
        sh.table = 0.f, sh.snap = 0.f, sh.stretch = 0.f;
        const float top = phymo::ModalRes::kTop * kSr;
        m.Set(top / 24.f * powf(2.f, -0.25f), 2.f, sh, 64); // the 24th a quarter octave under the top
        const float g_quarter = m.Gain(23) / m.Gain(11);
        m.Set(top / 48.f, 2.f, sh, 64); // the 24th an octave under
        const float g_octave = m.Gain(23) / m.Gain(11);
        std::snprintf(what, sizeof what, "near the top: the 24th partial a quarter octave under it at %.2f of its gain an octave under (%.2f)",
                      g_quarter / g_octave, g_octave);
        Check(fabsf(g_quarter / g_octave - 0.5f) < 0.02f, what);
    }

    // Sweeping a knob in its steps (a turn) is no clickier than the same
    // sweep made smooth: Table across all eight in 100 steps against 2000.
    {
        auto clicks = [](int steps) {
            phymo::Synth s;
            s.Init(kSr);
            s.SetKeyMode(phymo::KeyMode::Mono), s.SetVolume(1.f), s.SetModel(phymo::Model::Spectral);
            s.SetDecay(20.f), s.SetKeyTrack(0.f), s.SetBrightness(0.7f);
            s.NoteOn(48);
            std::vector<float> x, l, r;
            Play(s, 200, l, r);
            const int per = 3300 / steps; // ms a step
            for(int i = 0; i < steps; i++)
            {
                s.SetTable(i / (steps - 1.f));
                l.clear(), r.clear();
                Play(s, per, l, r);
                x.insert(x.end(), l.begin(), l.end());
            }
            // each ms's highs (the third difference's power) against the 21 ms around it: the worst
            std::vector<double> w;
            for(size_t i = 3; i + 48 < x.size(); i += 48)
            {
                double e = 0;
                for(size_t j = i; j < i + 48; j++)
                {
                    const double d = x[j] - 3 * x[j - 1] + 3 * x[j - 2] - x[j - 3];
                    e += d * d;
                }
                w.push_back(e);
            }
            double worst = 0;
            for(size_t i = 10; i + 10 < w.size(); i++)
            {
                std::vector<double> nb(w.begin() + i - 10, w.begin() + i + 11);
                std::sort(nb.begin(), nb.end());
                worst = fmax(worst, w[i] / (nb[10] + 1e-20));
            }
            return 10.0 * log10(worst);
        };
        const double stepped = clicks(100), smooth = clicks(2000);
        char         what[140];
        std::snprintf(what, sizeof what, "Table swept in 100 steps: its worst click %+.1f dB, against %+.1f swept smoothly", stepped,
                      smooth);
        Check(stepped < smooth + 3.0, what);
    }

    // Level: a default note as loud as the string's, within 6 dB.
    {
        phymo::Synth t;
        t.Init(kSr);
        t.SetKeyMode(phymo::KeyMode::Mono);
        t.SetVolume(1.f);
        const auto string = Note(t, 60, 500);
        Fresh(s);
        s.SetBrightness(0.5f), s.SetDecay(3.f);
        const auto   spec = Note(s, 60, 500);
        const double db   = 20.0 * log10(Rms(spec, 0, 24000) / Rms(string, 0, 24000));
        char         what[80];
        std::snprintf(what, sizeof what, "level: %+.1f dB against the string", db);
        Check(fabs(db) < 6.0, what);
    }

    // Switching the model while a note rings: no pop (the sharpest step
    // after the switch no worse than before it), and the note rings on as
    // what it was struck as.
    {
        phymo::Synth t;
        t.Init(kSr);
        t.SetKeyMode(phymo::KeyMode::Mono), t.SetVolume(1.f), t.SetDecay(6.f);
        t.NoteOn(48);
        std::vector<float> l, r;
        Play(t, 300, l, r);
        const size_t at = l.size();
        t.SetModel(Model::Fm), t.SetPickup(phymo::PickupMode::Wood), t.SetPickupAmount(1.f);
        Play(t, 300, l, r);
        auto rough = [&](size_t a, size_t b) {
            double d = 0;
            for(size_t i = a; i < b; i++)
                d = fmax(d, fabs(l[i] - 2 * l[i - 1] + l[i - 2]));
            return d;
        };
        const double before = rough(at - 4800, at), after = rough(at, at + 4800);
        const double ring   = Rms(l, at + 9600, 4800) / Rms(l, at - 4800, 4800);
        char         what[120];
        std::snprintf(what, sizeof what, "switching the model mid-note: sharpest step %.4f after, %.4f before; rings on (%.2f)",
                      after, before, ring);
        Check(after < 1.5 * before && ring > 0.3, what);
    }

    // Bounded: six hard notes, every table, the morph and the LFO moving it.
    {
        float peak   = 0.f;
        bool  finite = true;
        for(int t = 0; t < kTables; t++)
        {
            s.Init(kSr);
            s.SetModel(Model::Spectral), s.SetVolume(1.f), s.SetStrength(1.f), s.SetHardness(1.f);
            s.SetBrightness(1.f), s.SetDecay(20.f), s.SetTable(t / (kTables - 1.f)), s.SetMorph(t % 2 ? 1.f : 0.f);
            s.SetLfoToMorph(1.f), s.SetLfoRate(6.f);
            for(int n : {36, 43, 48, 55, 60, 64})
                s.NoteOn(n);
            std::vector<float> l, r;
            Play(s, 400, l, r);
            for(float v : l)
                peak = fmaxf(peak, fabsf(v)), finite = finite && std::isfinite(v);
        }
        char what[80];
        std::snprintf(what, sizeof what, "six hard notes on every table, morphing: peak %.2f, no NaN", peak);
        Check(finite && peak < 1.f, what);
    }

    // Cost here (the CHOMPI's meter is the one that counts).
    {
        Fresh(s);
        s.SetKeyMode(phymo::KeyMode::Poly1);
        for(int n : {36, 43, 48, 55, 60, 64})
            s.NoteOn(n);
        const auto         t0 = std::chrono::steady_clock::now();
        std::vector<float> l, r;
        Play(s, 5000, l, r);
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        std::printf("      six voices, 24 partials, on this computer: %.1f ns a sample a voice\n",
                    1000.0 * us / (240000.0 * 6));
    }

    // Renders.
    struct Demo
    {
        const char*       file;
        float             table, morph, morph_time, stretch, snap, partials, bright, decay, lfo_morph, lfo_rate;
        phymo::PickupMode pickup;
        float             drive, trem, spread, type, hardness;
        int               mode; // 0 chords, 1 an arpeggio
    };
    using PM          = phymo::PickupMode;
    const Demo demos[] = {
        // a bell striking, settling into a harmonic pad
        {"out/spectral_bell_to_pad.wav", 0.f, 5.f, 2.5f, 0.5f, 0.f, 1.f, 0.7f, 8.f, 0.f, 1.f, PM::Clean, 0.f, 0.f, 0.f, 0.3f, 0.6f, 0},
        // vibraphone: the tuned bar, a soft mallet, the motor
        {"out/spectral_vibes.wav", 2.f, 0.f, 0.5f, 0.5f, 0.f, 1.f, 0.4f, 4.f, 0.f, 1.f, PM::Clean, 0.f, 0.5f, 0.f, 0.f, 0.35f, 1},
        // electric piano: the tine through the tine pickup, autopanned
        {"out/spectral_tine_piano.wav", 3.f, 0.f, 0.5f, 0.5f, 0.f, 1.f, 0.35f, 3.f, 0.f, 1.f, PM::Tine, 0.6f, 0.4f, 1.f, 0.f, 0.55f, 0},
        // scanning: the LFO sweeping the table slowly under held chords
        {"out/spectral_scan.wav", 1.5f, 0.f, 0.5f, 0.55f, 0.3f, 1.f, 0.9f, 12.f, 1.f, 0.15f, PM::Clean, 0.f, 0.f, 0.f, 0.3f, 0.5f, 0},
        // a gong: the plate, stretched, the morph falling from the membrane
        {"out/spectral_gong.wav", 7.f, 1.f, 1.5f, 0.7f, 0.f, 1.f, 0.8f, 6.f, 0.f, 1.f, PM::Clean, 0.f, 0.f, 0.f, 0.6f, 0.4f, 1},
        // a mallet whose spectrum rises from harmonic to the free bar on each note
        {"out/spectral_rising.wav", 5.f, -5.f, 0.3f, 0.5f, 0.f, 1.f, 0.6f, 2.f, 0.f, 1.f, PM::Clean, 0.f, 0.f, 0.f, 0.5f, 0.7f, 1},
    };
    const int chords[4][4] = {{48, 55, 60, 64}, {45, 52, 57, 60}, {41, 48, 53, 57}, {43, 50, 55, 59}};
    for(const auto& d : demos)
    {
        phymo::Synth t;
        t.Init(kSr);
        t.SetModel(Model::Spectral), t.SetVolume(0.8f), t.SetDamper(0.4f), t.SetKeyTrack(0.4f);
        t.SetTable(d.table / (kTables - 1.f)), t.SetMorph(0.5f + 0.5f * d.morph / (kTables - 1.f)), t.SetMorphTime(d.morph_time);
        t.SetStretch(d.stretch), t.SetSnap(d.snap), t.SetPartials(d.partials), t.SetBrightness(d.bright);
        t.SetDecay(d.decay), t.SetLfoToMorph(d.lfo_morph), t.SetLfoRate(d.lfo_rate), t.SetLfoFree(true);
        t.SetLfoShape(phymo::Lfo::Shape::Sine);
        t.SetPickup(d.pickup), t.SetPickupAmount(d.drive), t.SetTremoloRate(5.f), t.SetTremoloDepth(d.trem);
        t.SetTremoloSpread(d.spread), t.SetType(d.type), t.SetHardness(d.hardness);
        if(d.pickup == PM::Tine)
            t.SetChorus(phymo::Chorus::Mode::I);
        std::vector<float> l, r;
        if(d.mode == 0)
            for(const auto& ch : chords)
            {
                for(int n : ch)
                    t.NoteOn(n);
                Play(t, 1800, l, r);
                for(int n : ch)
                    t.NoteOff(n);
                Play(t, 200, l, r);
            }
        else
            for(int rep = 0; rep < 2; rep++)
                for(int n : {48, 55, 60, 64, 67, 72, 67, 64})
                {
                    t.NoteOn(n);
                    Play(t, 250, l, r);
                    t.NoteOff(n);
                }
        Play(t, 1500, l, r);
        WriteWav(d.file, l, r, 48000);
        std::printf("      wrote %s\n", d.file);
    }

    std::printf(g_fails ? "\n%d FAILED\n" : "\nall passed\n", g_fails);
    return g_fails ? 1 : 0;
}

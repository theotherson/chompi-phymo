// Checks the body (body.h): the pickups add harmonics as a note's struck
// harder (even ones the tine, mostly odd ones the reed), keep a quiet note's
// level and leave no offset; the wooden body lifts its resonances and leaves
// the rest; the vowel's formants land on each vowel's and keep the level;
// the tremolo moves the level, opposite on each side at full
// spread; nothing runs away. Then renders each:
//   make -C host body_test && (cd host && ./body_test)
#include "../src/synth.h"
#include "wav.h"
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr float kSr     = 48000.f;
static int             g_fails = 0;
using phymo::PickupMode;

static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "pass" : "FAIL", what);
    g_fails += !ok;
}

/** `ms` milliseconds of both sides. */
static void Play(phymo::Synth& s, int ms, std::vector<float>& l, std::vector<float>& r)
{
    float lb[48], rb[48];
    for(int b = 0; b < ms; b++)
    {
        s.Process(lb, rb, 48);
        l.insert(l.end(), lb, lb + 48), r.insert(r.end(), rb, rb + 48);
    }
}

/** One note, mono (in the middle), the left side: `ms` of it. */
static std::vector<float> Note(PickupMode m, float amount, float strength, int note = 48, int ms = 400,
                               float bright = 0.6f)
{
    phymo::Synth s;
    s.Init(kSr);
    s.SetKeyMode(phymo::KeyMode::Mono);
    s.SetVolume(1.f), s.SetDecay(8.f), s.SetBrightness(bright);
    s.SetPickup(m), s.SetPickupAmount(amount), s.SetStrength(strength);
    s.NoteOn(note);
    std::vector<float> l, r;
    Play(s, ms, l, r);
    return l;
}

/** The size of `x`'s component at `hz`, from 100 ms on, over 200 ms (Hann window). */
static double Amp(const std::vector<float>& x, double hz)
{
    const size_t from = 4800, n = 9600;
    double       re = 0, im = 0;
    for(size_t i = 0; i < n; i++)
    {
        const double w = 0.5 - 0.5 * cos(6.283185307 * i / n), ph = 6.283185307 * hz * i / kSr;
        re += w * x[from + i] * cos(ph), im -= w * x[from + i] * sin(ph);
    }
    return sqrt(re * re + im * im);
}

static double Rms(const std::vector<float>& x, size_t from, size_t n)
{
    double e = 0;
    for(size_t i = from; i < from + n; i++)
        e += static_cast<double>(x[i]) * x[i];
    return sqrt(e / n);
}

int main()
{
    // A quiet note: about as loud through each pickup as clean (the drive
    // shapes the colour, not the level).
    {
        const double clean = Rms(Note(PickupMode::Clean, 0.f, 0.f), 4800, 9600);
        for(PickupMode m : {PickupMode::Tine, PickupMode::Reed})
        {
            const double db = 20.0 * log10(Rms(Note(m, 0.f, 0.f), 4800, 9600) / clean);
            char         what[80];
            std::snprintf(what, sizeof what, "%s, a quiet note: %+.1f dB against clean",
                          m == PickupMode::Tine ? "tine" : "reed", db);
            Check(fabs(db) < 4.0, what);
        }
    }

    // Each pickup's shaper, on a sine at the levels a gentle and a hard note
    // ring at (about 0.01 and 0.3): the tine's 2nd harmonic grows with the
    // level (the bark), the reed's 3rd more than its 2nd. (Measured on a sine:
    // a string's own harmonics would hide what's added.)
    {
        auto harm = [](PickupMode m, float level, int h) {
            phymo::Pickup p;
            p.Init(kSr);
            p.Set(m, 0.6f);
            std::vector<float> y(20000);
            for(int i = 0; i < 20000; i++)
                y[i] = p.Process(level * sinf(6.2831853f * 130.81f * i / kSr));
            return 20.0 * log10(Amp(y, h * 130.81) / Amp(y, 130.81));
        };
        const double t_soft = harm(PickupMode::Tine, 0.01f, 2), t_hard = harm(PickupMode::Tine, 0.3f, 2);
        const double r2 = harm(PickupMode::Reed, 0.3f, 2) - harm(PickupMode::Reed, 0.01f, 2);
        const double r3 = harm(PickupMode::Reed, 0.3f, 3) - harm(PickupMode::Reed, 0.01f, 3);
        char         what[120];
        std::snprintf(what, sizeof what, "tine: 2nd harmonic %.0f dB gentle, %.0f dB hard", t_soft, t_hard);
        Check(t_hard - t_soft > 15.0, what);
        std::snprintf(what, sizeof what, "reed: hard against gentle, the 3rd harmonic %+.0f dB, the 2nd %+.0f dB", r3,
                      r2);
        Check(r3 > r2 && r3 > 15.0, what);
    }

    // In the voice, on a dark string: a hard note through the tine, driven
    // halfway, differs from the same pickup undriven by more than a gentle
    // one does, in shape (each matched to the other's level first: the
    // squash changes a gentle one's; and undriven, so the voicing filters,
    // the same at any level, aren't counted).
    {
        auto diff = [](float strength) {
            const auto p = Note(PickupMode::Tine, 0.5f, strength, 48, 400, 0.f);
            const auto c = Note(PickupMode::Tine, 0.f, strength, 48, 400, 0.f);
            double     pc = 0, pp = 0;
            for(size_t i = 4800; i < p.size(); i++)
                pc += p[i] * c[i], pp += p[i] * p[i];
            const double k = pc / pp; // the gain that brings p nearest c
            double       d = 0, e = 0;
            for(size_t i = 4800; i < p.size(); i++)
                d += (k * p[i] - c[i]) * (k * p[i] - c[i]), e += c[i] * c[i];
            return 10.0 * log10(d / e);
        };
        const double soft = diff(0.15f), hard = diff(1.f);
        char         what[100];
        std::snprintf(what, sizeof what, "tine in the voice: driven, differs from undriven by %.1f dB gentle, %.1f dB hard", soft,
                      hard);
        Check(hard > soft + 3.0, what);
    }

    // No offset: a hard-driven tine's output averages to nothing.
    {
        const auto x    = Note(PickupMode::Tine, 1.f, 1.f, 48, 1000);
        double     mean = 0;
        for(size_t i = 4800; i < x.size(); i++)
            mean += x[i];
        mean /= static_cast<double>(x.size() - 4800);
        char what[80];
        std::snprintf(what, sizeof what, "tine driven hard: no offset (mean %.5f, rms %.3f)", mean,
                      Rms(x, 4800, x.size() - 4800));
        Check(fabs(mean) < 0.002, what);
    }

    // The wooden body: its lowest resonance lifted, between its resonances
    // (700 Hz) only the dry's dip.
    {
        auto gain = [](float hz, float amount) {
            phymo::Body b;
            b.Init(kSr);
            b.SetAmount(amount);
            std::vector<float> l(48000), r(48000), in(48000);
            for(int i = 0; i < 48000; i++)
                in[i] = l[i] = r[i] = 0.1f * sinf(6.2831853f * hz * i / kSr);
            for(int i = 0; i < 48000; i += 48)
                b.Process(&l[i], &r[i], 48);
            return 20.0 * log10(Rms(l, 24000, 24000) / Rms(in, 24000, 24000));
        };
        const double lo = gain(98.f, 1.f), mid = gain(700.f, 1.f), off = gain(98.f, 0.f);
        char         what[96];
        std::snprintf(what, sizeof what, "wood: %+.1f dB at 98 Hz, %+.1f at 700 Hz, %+.1f off", lo, mid, off);
        Check(lo > 10.0 && fabs(mid) < 4.0 && fabs(off) < 0.01, what);
    }

    // The violin's body: its two strong body modes (470, 545) and the
    // bridge hill (2.5 kHz) lifted; under its air mode (100 Hz) held back
    // (the dip, nothing to lift it); at Tone 0, a cello's, its air mode
    // down at ~100 Hz.
    {
        auto gain = [](float hz, float tone) {
            phymo::Body b;
            b.Init(kSr);
            b.SetKind(true), b.SetTone(tone), b.SetAmount(1.f);
            std::vector<float> l(48000), r(48000), in(48000);
            for(int i = 0; i < 48000; i++)
                in[i] = l[i] = r[i] = 0.1f * sinf(6.2831853f * hz * i / kSr);
            for(int i = 0; i < 48000; i += 48)
                b.Process(&l[i], &r[i], 48);
            return 20.0 * log10(Rms(l, 24000, 24000) / Rms(in, 24000, 24000));
        };
        const double b1 = gain(470.f, 0.5f), b2 = gain(545.f, 0.5f), hill = gain(2500.f, 0.5f), low = gain(100.f, 0.5f);
        const double cello = gain(275.f * exp2f(-1.5f), 0.f);
        char         what[160];
        std::snprintf(what, sizeof what,
                      "violin: %+.1f dB at 470 Hz, %+.1f at 545, %+.1f at 2.5 kHz, %+.1f at 100 Hz; a cello's air mode %+.1f",
                      b1, b2, hill, low, cello);
        Check(b1 > 8.0 && b2 > 8.0 && hill > 4.0 && low < -2.0 && cello > 8.0, what);
    }

    // The tremolo: 4 Hz, full depth. Same on both sides at spread 0;
    // opposite at 1.
    for(float spread : {0.f, 1.f})
    {
        phymo::Synth s;
        s.Init(kSr);
        s.SetVolume(1.f), s.SetDecay(20.f), s.SetKeyTrack(0.f);
        s.SetTremoloRate(4.f), s.SetTremoloDepth(1.f), s.SetTremoloSpread(spread);
        s.NoteOn(60);
        std::vector<float> l, r;
        Play(s, 1500, l, r);
        // each side's level, in 10 ms windows, from 250 ms
        std::vector<double> el, er;
        for(size_t w = 12000; w + 480 <= l.size(); w += 480)
            el.push_back(Rms(l, w, 480)), er.push_back(Rms(r, w, 480));
        // the level's swing, and the left side's share of it (0.5 throughout
        // when they move together; 0 to 1 when they're opposite)
        double lmax = 0, lmin = 1e9, smax = 0, smin = 1;
        for(size_t i = 0; i < el.size(); i++)
        {
            lmax = fmax(lmax, el[i]), lmin = fmin(lmin, el[i]);
            const double share = el[i] / (el[i] + er[i] + 1e-12);
            smax = fmax(smax, share), smin = fmin(smin, share);
        }
        char what[100];
        std::snprintf(what, sizeof what, "tremolo, spread %.0f: the level swings %.0f dB, the left's share %.2f-%.2f",
                      spread, 20.0 * log10(lmax / (lmin + 1e-9)), smin, smax);
        Check(lmax / (lmin + 1e-9) > 4.0 && (spread == 0.f ? smax - smin < 0.02 : smax - smin > 0.9), what);
    }

    // Tone: the tine's peak 1.5 octaves down at 0 (283 Hz), up at 1 (2.26
    // kHz); the wood's lowest resonance with it (35 Hz, 277 Hz).
    {
        auto pickup_gain = [](float tone, float hz) {
            phymo::Pickup p;
            p.Init(kSr);
            p.Set(PickupMode::Tine, 0.f, tone);
            std::vector<float> y(24000), x(24000);
            for(int i = 0; i < 24000; i++)
                x[i] = 0.01f * sinf(6.2831853f * hz * i / kSr), y[i] = p.Process(x[i]);
            return 20.0 * log10(Rms(y, 12000, 12000) / Rms(x, 12000, 12000));
        };
        auto body_gain = [](float tone, float hz) {
            phymo::Body b;
            b.Init(kSr);
            b.SetTone(tone), b.SetAmount(1.f);
            std::vector<float> l(48000), r(48000), in(48000);
            for(int i = 0; i < 48000; i++)
                in[i] = l[i] = r[i] = 0.1f * sinf(6.2831853f * hz * i / kSr);
            for(int i = 0; i < 48000; i += 48)
                b.Process(&l[i], &r[i], 48);
            return 20.0 * log10(Rms(l, 24000, 24000) / Rms(in, 24000, 24000));
        };
        const double dark = pickup_gain(0.f, 283.f) - pickup_gain(0.f, 2263.f);
        const double light = pickup_gain(1.f, 2263.f) - pickup_gain(1.f, 283.f);
        const double big = body_gain(0.f, 34.6f), small = body_gain(1.f, 277.f);
        char         what[140];
        std::snprintf(what, sizeof what, "tone: the tine's peak %+.1f dB at 283 Hz (dark), %+.1f at 2.26 kHz (bright); wood %+.1f dB at 35 Hz big, %+.1f at 277 small",
                      dark, light, big, small);
        Check(dark > 6.0 && light > 6.0 && big > 6.0 && small > 6.0, what);
    }

    // The vowel: its formants on Peterson & Barney's, each vowel's peaks
    // where they should be on a buzz, as loud as the dry, gliding.
    {
        static const float kF[5][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410}, {300, 870, 2240}};
        static const char* kName[5] = {"a", "e", "i", "o", "u"};
        // a buzz (a 110 Hz saw, all harmonics) through each vowel: its level
        // near each formant against halfway between them, and against the dry
        bool   peaks = true, level = true, tuned = true;
        double worst_peak = 1e9, worst_level = 0;
        for(int v = 0; v < 5; v++)
        {
            phymo::Vowel w;
            w.Init(kSr);
            w.SetVowel(v / 4.f), w.SetAmount(1.f);
            std::vector<float> l(72000), r(72000), in(72000);
            for(int i = 0; i < 72000; i++)
            {
                const float ph = fmodf(110.f * i / kSr, 1.f);
                in[i] = l[i] = r[i] = 0.1f * (2.f * ph - 1.f);
            }
            for(int i = 0; i < 72000; i += 48)
                w.Process(&l[i], &r[i], 48);
            for(int f = 0; f < 3; f++)
                tuned = tuned && fabsf(w.Formant(f) - kF[v][f]) < 0.5f;
            const std::vector<float> y(l.begin() + 24000, l.end()), x(in.begin() + 24000, in.end());
            // the harmonic nearest F2, against the one at the geometric middle
            // of F2 and whichever of F1 and F3 is further from it (o's F1 and
            // F2 are so close that between them both lift it)
            auto at = [&](const std::vector<float>& z, float hz) { return 20.0 * log10(Amp(z, 110.0 * fmax(1.0, round(hz / 110.0)))); };
            const bool   low  = kF[v][1] / kF[v][0] > kF[v][2] / kF[v][1];
            const float  mid  = sqrtf(kF[v][1] * (low ? kF[v][0] : kF[v][2]));
            const double lift = (at(y, kF[v][1]) - at(x, kF[v][1])) - (at(y, mid) - at(x, mid));
            worst_peak        = fmin(worst_peak, lift);
            peaks             = peaks && lift > 6.0;
            const double d    = 20.0 * log10(Rms(y, 0, y.size()) / Rms(x, 0, x.size()));
            worst_level       = fmax(worst_level, fabs(d));
            level             = level && fabs(d) < 1.5;
            std::printf("      %s: F2 lifted %.1f dB\n", kName[v], lift);
        }
        char what[160];
        std::snprintf(what, sizeof what, "vowel: a e i o u's formants on Peterson & Barney's, F2 lifted %.1f dB or more against between it and the next",
                      worst_peak);
        Check(tuned && peaks, what);
        std::snprintf(what, sizeof what, "vowel at full: as loud as the dry on every vowel (worst %.1f dB)", worst_level);
        Check(level, what);
        // a jump from a to u glides: a block in, F2 part of the way; 200 ms in, there
        phymo::Vowel w;
        w.Init(kSr);
        w.SetAmount(1.f);
        float l[48] = {}, r[48] = {};
        w.Process(l, r, 48);
        w.SetVowel(1.f);
        w.Process(l, r, 48);
        const float early = w.Formant(1);
        for(int b = 0; b < 200; b++)
            w.Process(l, r, 48);
        std::snprintf(what, sizeof what, "vowel: a to u glides (F2 %.0f Hz a block in, %.0f at 200 ms; 1090 to 870)", early, w.Formant(1));
        Check(early > 1050.f && fabsf(w.Formant(1) - 870.f) < 1.f, what);
    }

    // Bounded: six hard notes, each pickup driven all the way.
    for(PickupMode m : {PickupMode::Tine, PickupMode::Reed, PickupMode::Wood, PickupMode::Vowel})
    {
        phymo::Synth s;
        s.Init(kSr);
        s.SetVolume(1.f), s.SetBrightness(1.f), s.SetHardness(1.f), s.SetStrength(1.f), s.SetDecay(20.f);
        s.SetPickup(m), s.SetPickupAmount(1.f);
        float peak = 0.f;
        bool  finite = true;
        for(int rep = 0; rep < 4; rep++)
        {
            for(int n : {36, 43, 48, 55, 60, 64})
                s.NoteOn(n);
            std::vector<float> l, r;
            Play(s, 300, l, r);
            for(size_t i = 0; i < l.size(); i++)
                peak = fmaxf(peak, fmaxf(fabsf(l[i]), fabsf(r[i]))), finite = finite && std::isfinite(l[i]);
        }
        char what[80];
        std::snprintf(what, sizeof what, "%s at full drive, six hard notes: peak %.2f, no NaN",
                      m == PickupMode::Tine ? "tine" : m == PickupMode::Reed ? "reed" : m == PickupMode::Wood ? "wood" : "vowel", peak);
        Check(finite && peak < 1.f, what);
    }

    // The pickup's place: in the middle, the even harmonics gone (they've
    // a node there) and the odd kept; a quarter of the way, the 4th gone;
    // in every model with a string, about as loud as the whole string's
    {
        const phymo::Model models[] = {phymo::Model::String, phymo::Model::Bow, phymo::Model::Spectral};
        const char*        names[]  = {"String", "Bowed", "Spectral"};
        for(int i = 0; i < 3; i++)
        {
            auto play = [&](float place) {
                phymo::Synth s;
                s.Init(kSr);
                s.SetKeyMode(phymo::KeyMode::Mono);
                s.SetVolume(1.f), s.SetDecay(8.f), s.SetBrightness(0.7f);
                s.SetModel(models[i]), s.SetPickupPlace(place);
                s.NoteOn(48);
                std::vector<float> l, r;
                Play(s, 400, l, r);
                return l;
            };
            const double f = 130.8128;
            const auto   whole = play(0.f), mid = play(1.f), quarter = play(0.5f);
            const double even  = 20 * log10(Amp(mid, 2 * f) / Amp(mid, f) + 1e-9) - 20 * log10(Amp(whole, 2 * f) / Amp(whole, f));
            const double odd   = 20 * log10(Amp(mid, 3 * f) / Amp(mid, f)) - 20 * log10(Amp(whole, 3 * f) / Amp(whole, f));
            const double fourth = 20 * log10(Amp(quarter, 4 * f) / Amp(quarter, f) + 1e-9)
                                  - 20 * log10(Amp(whole, 4 * f) / Amp(whole, f));
            double lo = 0, hi = 0;
            for(float pl : {0.05f, 0.2f, 0.5f, 1.f})
            {
                const double d = 20 * log10(Rms(play(pl), 4800, 9600) / Rms(whole, 4800, 9600));
                lo = d < lo ? d : lo, hi = d > hi ? d : hi;
            }
            char what[160];
            std::snprintf(what, sizeof what,
                          "%s's pickup place: the middle takes the 2nd down %.0f dB, the 3rd %+.1f; a quarter the 4th "
                          "%.0f; level %+.1f to %+.1f dB",
                          names[i], even, odd, fourth, lo, hi);
            Check(even < -25 && fabs(odd) < 3 && fourth < -20 && lo > -4.5 && hi < 4.5, what);
        }
    }

    // Renders, each a few chords (C, Am, F, G), to listen to.
    struct Demo
    {
        const char* file;
        PickupMode  mode;
        float       amount, type, hardness, decay, bright, trem_depth, trem_spread;
        bool        chorus;
    };
    const Demo demos[] = {
        {"out/body_tine.wav", PickupMode::Tine, 0.6f, 0.f, 0.55f, 4.f, 0.35f, 0.5f, 1.f, true},  // autopan, chorus
        {"out/body_reed.wav", PickupMode::Reed, 0.5f, 0.1f, 0.6f, 2.5f, 0.45f, 0.4f, 0.f, false}, // tremolo
        {"out/body_wood.wav", PickupMode::Wood, 0.8f, 0.8f, 0.7f, 3.f, 0.6f, 0.f, 0.f, false},   // plucked
        {"out/body_clean_motor.wav", PickupMode::Clean, 0.f, 0.f, 0.4f, 6.f, 0.5f, 0.6f, 0.f, false},
    };
    const int chords[4][4] = {{48, 55, 60, 64}, {45, 52, 57, 60}, {41, 48, 53, 57}, {43, 50, 55, 59}};
    for(const auto& d : demos)
    {
        phymo::Synth s;
        s.Init(kSr);
        s.SetVolume(0.8f), s.SetDamper(0.3f);
        s.SetPickup(d.mode), s.SetPickupAmount(d.amount);
        s.SetType(d.type), s.SetHardness(d.hardness), s.SetDecay(d.decay), s.SetBrightness(d.bright);
        s.SetTremoloRate(5.f), s.SetTremoloDepth(d.trem_depth), s.SetTremoloSpread(d.trem_spread);
        if(d.chorus)
            s.SetChorus(phymo::Chorus::Mode::I);
        std::vector<float> l, r;
        for(const auto& ch : chords)
        {
            for(int k = 0; k < 4; k++)
            {
                s.NoteOn(ch[k]);
                Play(s, d.mode == PickupMode::Wood ? 90 : 10, l, r); // the guitar strums, the others play together
            }
            Play(s, d.mode == PickupMode::Wood ? 1100 : 1350, l, r);
            for(int k = 0; k < 4; k++)
                s.NoteOff(ch[k]);
            Play(s, 300, l, r);
        }
        Play(s, 800, l, r);
        WriteWav(d.file, l, r, 48000);
        std::printf("      wrote %s\n", d.file);
    }
    // The vowel: a bright plucked line, Tone stepping a, e, i, o, u, then
    // held notes with the LFO sweeping it.
    {
        phymo::Synth s;
        s.Init(kSr);
        s.SetVolume(0.8f), s.SetDamper(0.3f), s.SetDecay(4.f), s.SetBrightness(0.8f), s.SetType(1.f);
        s.SetHardness(0.8f), s.SetLife(0.f);
        s.SetPickup(PickupMode::Vowel), s.SetPickupAmount(1.f);
        s.SetChorus(phymo::Chorus::Mode::I);
        std::vector<float> l, r;
        const int line[] = {45, 52, 57, 52, 60, 57, 52, 45};
        for(int v = 0; v < 5; v++)
        {
            s.SetPickupTone(v / 4.f);
            for(int n : line)
                s.NoteOn(n), Play(s, 150, l, r), s.NoteOff(n);
        }
        s.SetPickupTone(0.5f), s.SetLfoRate(0.7f), s.SetLfoDepth(phymo::Dest::Tone, 1.f), s.SetDecay(12.f);
        for(int n : {45, 52, 57, 60})
            s.NoteOn(n), Play(s, 10, l, r);
        Play(s, 5000, l, r);
        for(int n : {45, 52, 57, 60})
            s.NoteOff(n);
        Play(s, 800, l, r);
        WriteWav("out/body_vowel.wav", l, r, 48000);
        std::printf("      wrote out/body_vowel.wav\n");
    }

    std::printf(g_fails ? "\n%d FAILED\n" : "\nall passed\n", g_fails);
    return g_fails ? 1 : 0;
}

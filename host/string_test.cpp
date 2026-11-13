// Checks the string voice (voice.h, string_res.h, exciter.h): its tuning
// across the keyboard, that a held note falls 60 dB in the decay's time,
// that the damper stops a let-go note, that brightness brightens and that
// a hard strike stays bounded. Then renders some notes to listen to:
//   make -C host string_test && (cd host && ./string_test)
#include "../src/synth.h"
#include "wav.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr float kSr     = 48000.f;
static int             g_fails = 0;

static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "pass" : "FAIL", what);
    g_fails += !ok;
}

/** `ms` milliseconds of the synth's left side. */
static std::vector<float> Play(phymo::Synth& s, int ms)
{
    std::vector<float> out(static_cast<size_t>(ms) * 48);
    float              l[48], r[48];
    for(int b = 0; b < ms; b++)
    {
        s.Process(l, r, 48);
        for(int i = 0; i < 48; i++)
            out[static_cast<size_t>(b) * 48 + i] = l[i];
    }
    return out;
}

/** A fresh synth: mono (one voice, in the middle), a long decay, no key
 *  tracking, the delay and reverb unset (InitSpace never called). */
static void Fresh(phymo::Synth& s)
{
    s.Init(kSr);
    s.SetKeyMode(phymo::KeyMode::Mono);
    s.SetDecay(8.f), s.SetKeyTrack(0.f), s.SetBrightness(0.5f);
    s.SetVolume(1.f);
}

/** The pitch of `x` from `from`, `n` samples, Hz: the strongest partial
 *  within a semitone of `expect` (a Hann-windowed DFT, searched in tenths
 *  of a cent near its peak). */
static float PitchOf(const std::vector<float>& x, size_t from, size_t n, float expect, double range = 100,
                     double lo = -100)
{
    auto mag = [&](double hz) {
        double re = 0, im = 0;
        for(size_t i = 0; i < n; i++)
        {
            const double win = 0.5 - 0.5 * cos(6.283185307 * i / n);
            const double ph  = 6.283185307 * hz * i / kSr;
            re += win * x[from + i] * cos(ph), im -= win * x[from + i] * sin(ph);
        }
        return re * re + im * im;
    };
    double best = expect, best_m = -1;
    for(double c = lo; c <= range; c += 5) // coarse, 5 cents
    {
        const double hz = expect * pow(2.0, c / 1200.0), m = mag(hz);
        if(m > best_m)
            best_m = m, best = hz;
    }
    const double centre = best;
    for(double c = -5; c <= 5; c += 0.1) // fine, a tenth of a cent
    {
        const double hz = centre * pow(2.0, c / 1200.0), m = mag(hz);
        if(m > best_m)
            best_m = m, best = hz;
    }
    return static_cast<float>(best);
}

/** RMS of `x` over `ms` milliseconds from `at_ms`, dB. */
static float RmsDb(const std::vector<float>& x, int at_ms, int ms)
{
    double e = 0;
    for(size_t i = static_cast<size_t>(at_ms) * 48; i < static_cast<size_t>(at_ms + ms) * 48; i++)
        e += static_cast<double>(x[i]) * x[i];
    return 10.f * log10f(static_cast<float>(e / (ms * 48)) + 1e-20f);
}

/** How much of `x`'s energy is in its highs: the first difference's energy over its own. */
static float Highs(const std::vector<float>& x, int at_ms, int ms)
{
    double e = 0, d = 0;
    for(size_t i = static_cast<size_t>(at_ms) * 48 + 1; i < static_cast<size_t>(at_ms + ms) * 48; i++)
        e += static_cast<double>(x[i]) * x[i], d += static_cast<double>(x[i] - x[i - 1]) * (x[i] - x[i - 1]);
    return static_cast<float>(d / (e + 1e-20));
}

int main()
{
    phymo::Synth s;

    // Tuning, C1 to C8: within 2 cents everywhere, dark and bright.
    for(float bright : {0.2f, 0.8f})
    {
        float worst = 0.f;
        int   worst_note = 0;
        for(int note = 24; note <= 108; note += 6)
        {
            Fresh(s);
            s.SetBrightness(bright);
            s.NoteOn(note);
            const auto  x      = Play(s, 1300);
            const float expect = 440.f * exp2f((note - 69) / 12.f);
            const float hz     = PitchOf(x, 200 * 48, 48000, expect);
            const float cents  = 1200.f * log2f(hz / expect);
            if(fabsf(cents) > fabsf(worst))
                worst = cents, worst_note = note;
        }
        char what[96];
        std::snprintf(what, sizeof what, "tuning C1-C8 at brightness %.1f: worst %+.2f cents (note %d)", bright,
                      worst, worst_note);
        Check(fabsf(worst) < 2.f, what);
    }

    // Decay: T60 2 s, held: 30 dB down a second later, for a low note and a high one.
    for(int note : {40, 76})
    {
        Fresh(s);
        s.SetDecay(2.f);
        s.NoteOn(note);
        const auto  x    = Play(s, 2000);
        const float drop = RmsDb(x, 400, 100) - RmsDb(x, 1400, 100);
        char        what[80];
        std::snprintf(what, sizeof what, "decay 2 s, note %d: %.1f dB down in a second (30)", note, drop);
        Check(fabsf(drop - 30.f) < 3.f, what);
    }

    // Key tracking at 1: an octave up, half as long (60 dB a second instead of 30).
    {
        Fresh(s);
        s.SetDecay(2.f), s.SetKeyTrack(1.f);
        s.NoteOn(72);
        const auto  x    = Play(s, 1500);
        const float drop = RmsDb(x, 300, 100) - RmsDb(x, 800, 100);
        char        what[80];
        std::snprintf(what, sizeof what, "key tracking 1, an octave up: %.1f dB down in half a second (30)", drop);
        Check(fabsf(drop - 30.f) < 3.f, what);
    }

    // The damper: let go after 300 ms, damper 0.1 s: silent (the voice free) within a second.
    {
        Fresh(s);
        s.SetDamper(0.1f);
        s.NoteOn(48);
        Play(s, 300);
        s.NoteOff(48);
        Play(s, 1000);
        Check(s.ActiveVoices() == 0, "damper: a let-go note ends, its voice free");
    }

    // Brightness: more highs at 1 than at 0.
    {
        Fresh(s);
        s.SetBrightness(0.f);
        s.NoteOn(48);
        const float dark = Highs(Play(s, 500), 200, 200);
        Fresh(s);
        s.SetBrightness(1.f);
        s.NoteOn(48);
        const float bright = Highs(Play(s, 500), 200, 200);
        char        what[80];
        std::snprintf(what, sizeof what, "brightness: %.1fx the highs at 1 as at 0", bright / dark);
        Check(bright > 4.f * dark, what);
    }

    // Bounded: six hard, bright, long notes, struck again and again.
    {
        s.Init(kSr);
        s.SetBrightness(1.f), s.SetHardness(1.f), s.SetStrength(1.f), s.SetDecay(20.f), s.SetVolume(1.f);
        float peak = 0.f;
        bool  finite = true;
        for(int rep = 0; rep < 8; rep++)
        {
            for(int n : {36, 43, 48, 55, 60, 64})
                s.NoteOn(n);
            for(float v : Play(s, 250))
                peak = fmaxf(peak, fabsf(v)), finite = finite && std::isfinite(v);
            for(int n : {36, 43, 48, 55, 60, 64})
                s.NoteOff(n);
        }
        char what[80];
        std::snprintf(what, sizeof what, "six hard strikes, eight times: peak %.2f, no NaN", peak);
        Check(finite && peak < 1.f, what);
    }

    // What a voice costs here (the CHOMPI's own meter is the one that counts).
    {
        Fresh(s);
        s.SetKeyMode(phymo::KeyMode::Poly1);
        for(int n : {36, 43, 48, 55, 60, 64})
            s.NoteOn(n);
        const auto t0 = std::chrono::steady_clock::now();
        Play(s, 10000);
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        std::printf("      six voices on this computer: %.1f ns a sample a voice\n", 1000.0 * us / (480000.0 * 6));
    }

    // Stiffness: the 4th partial sharp, more at full than half, none below
    // the middle (against the material's own: steel's, at the Table knob's
    // 0), the fundamental still in tune.
    {
        auto partial = [&](float stretch, float morph, int from_ms, int k, size_t n = 24000) {
            Fresh(s);
            s.SetStretch(stretch), s.SetMorph(morph), s.SetMorphTime(0.6f), s.SetBrightness(0.8f);
            s.NoteOn(48);
            const auto   x  = Play(s, 1400);
            const double f0 = 440.0 * pow(2.0, (48 - 69) / 12.0);
            // stiffness only sharpens: search from a little under the plain pitch up
            return 1200.0 * log2(PitchOf(x, static_cast<size_t>(from_ms) * 48, n, static_cast<float>(k * f0), 400, -40) / (k * f0));
        };
        // (against a plain string's own 4th, a little flat already from its loss filter)
        const double plain = partial(0.5f, 0.5f, 300, 4);
        const double sharp = partial(1.f, 0.5f, 300, 4) - plain, half = partial(0.75f, 0.5f, 300, 4) - plain;
        const double flat = partial(0.f, 0.5f, 300, 4) - plain;
        const double fund  = partial(1.f, 0.5f, 300, 1);
        const double early = partial(0.5f, 1.f, 0, 4, 4800) - plain, late = partial(0.5f, 1.f, 800, 4) - plain;
        char         what[160];
        std::snprintf(what, sizeof what, "stiffness: the 4th partial %+.0f cents at full, %+.0f half, %+.0f below (against plain); the 1st %+.1f",
                      sharp, half, flat, fund);
        Check(sharp > 100.0 && half > 15.0 && half < sharp && fabs(flat) < 1.0 && fabs(fund) < 2.0, what);
        (void)early, (void)late;
    }

    // The materials (MODEL knob 1): steel the brightest and longest, gut
    // the darkest and shortest; nylon a hard pluck's sharpness settling;
    // and the morph, a note struck as gut, settling as steel.
    {
        auto note = [&](float table, float morph, float strength, int ms) {
            Fresh(s);
            s.SetTable(table), s.SetMorph(morph), s.SetMorphTime(0.4f), s.SetStrength(strength);
            s.SetBrightness(0.6f), s.SetDecay(3.f);
            s.NoteOn(48);
            return Play(s, ms);
        };
        const auto   steel = note(0.f, 0.5f, 0.8f, 1500), gut = note(1.f, 0.5f, 0.8f, 1500);
        const double bright = Highs(steel, 100, 200) / Highs(gut, 100, 200);
        const double sustain = (RmsDb(steel, 1200, 200) - RmsDb(steel, 100, 200)) - (RmsDb(gut, 1200, 200) - RmsDb(gut, 100, 200));
        char         what[200];
        std::snprintf(what, sizeof what, "materials: steel's highs %.1fx gut's; over a second, gut falls %.1f dB more", bright, sustain);
        Check(bright > 3.0 && sustain > 3.0, what);
        // nylon (half way along), struck hard: sharp at first, in tune later
        const auto   nylon = note(0.5f, 0.5f, 1.f, 1500);
        const double f0    = 440.0 * pow(2.0, (48 - 69) / 12.0);
        const double sharp = 1200.0 * log2(PitchOf(nylon, 0, 2400, static_cast<float>(f0), 40, -40) / f0);
        const double later = 1200.0 * log2(PitchOf(nylon, 24000, 24000, static_cast<float>(f0), 40, -40) / f0);
        std::snprintf(what, sizeof what, "nylon struck hard: %+.1f cents in its first 50 ms, %+.1f from 0.5 s", sharp, later);
        Check(sharp > 3.0 && fabs(later) < 1.0, what);
        // the morph: steel at the knob, each note starting at gut (+7 tables: past its end), gliding back
        const auto   m  = note(0.f, 1.f, 0.8f, 1500);
        const double e  = Highs(m, 10, 60) / Highs(steel, 10, 60), l = Highs(m, 900, 200) / Highs(steel, 900, 200);
        // (settled as steel, it brightens against its start: the highs it lost
        // while dark don't come back, as on a real string, so not all the way)
        std::snprintf(what, sizeof what, "the morph on a string: struck as gut (highs %.2fx steel's), settling as steel (%.2fx)", e, l);
        Check(e < 0.6 && l > 1.5 * e, what);
    }

    // Renders, to listen to: a C major scale, hammer to pick and soft to hard,
    // then a chord through chorus I.
    {
        s.Init(kSr);
        s.SetVolume(0.8f), s.SetDecay(3.f), s.SetDamper(0.3f);
        std::vector<float> l, r;
        const int          scale[8] = {48, 50, 52, 53, 55, 57, 59, 60};
        float              lb[48], rb[48];
        for(int i = 0; i < 16; i++)
        {
            const float t = (i % 8) / 7.f;
            if(i < 8)
                s.SetType(t), s.SetHardness(0.5f);
            else
                s.SetType(0.3f), s.SetHardness(t);
            s.NoteOn(scale[i % 8] + (i < 8 ? 0 : 12));
            for(int b = 0; b < 400; b++)
            {
                if(b == 300)
                    s.NoteOff(scale[i % 8] + (i < 8 ? 0 : 12));
                s.Process(lb, rb, 48);
                l.insert(l.end(), lb, lb + 48), r.insert(r.end(), rb, rb + 48);
            }
        }
        s.SetChorus(phymo::Chorus::Mode::I);
        s.SetType(0.f), s.SetHardness(0.3f), s.SetDecay(6.f);
        for(int n : {48, 55, 60, 64, 67})
            s.NoteOn(n);
        for(int b = 0; b < 4000; b++)
        {
            if(b == 3000)
                for(int n : {48, 55, 60, 64, 67})
                    s.NoteOff(n);
            s.Process(lb, rb, 48);
            l.insert(l.end(), lb, lb + 48), r.insert(r.end(), rb, rb + 48);
        }
        WriteWav("out/string_demo.wav", l, r, 48000);
        std::printf("      wrote out/string_demo.wav\n");
    }

    // The second string's interval (CHOMPI + MODEL knob 2): at the fifth,
    // a partial at 3:2 the note that one string hasn't, in tune; moved to
    // it while the pair rings (from the octave), it's there too
    {
        auto amp = [](const std::vector<float>& x, size_t from, double hz) {
            const size_t n  = 9600;
            double       re = 0, im = 0;
            for(size_t i = 0; i < n; i++)
            {
                const double w = 0.5 - 0.5 * cos(6.283185307 * i / n), ph = 6.283185307 * hz * i / kSr;
                re += w * x[from + i] * cos(ph), im -= w * x[from + i] * sin(ph);
            }
            return sqrt(re * re + im * im);
        };
        const double f = 110.0, fifth = f * 1.5;
        auto play = [&](float at_strike, float later) {
            phymo::Synth s;
            Fresh(s);
            s.SetInterval(at_strike);
            s.NoteOn(45);
            auto a = Play(s, 200);
            s.SetInterval(later);
            auto b = Play(s, 600);
            a.insert(a.end(), b.begin(), b.end());
            return a;
        };
        const auto   one = play(0.f, 0.f), at = play(0.25f, 0.25f), moved = play(0.5f, 0.25f);
        const double lift  = 20 * log10(amp(at, 4800, fifth) / amp(one, 4800, fifth));
        const double live  = 20 * log10(amp(moved, 24000, fifth) / amp(one, 24000, fifth));
        const float  hz    = PitchOf(at, 4800, 9600, static_cast<float>(fifth), 50);
        const double cents = 1200 * log2(hz / fifth);
        char         what[160];
        std::snprintf(what, sizeof what,
                      "the second string at a fifth: its partial %+.0f dB, %+.1f cents; moved there ringing %+.0f dB",
                      lift, cents, live);
        Check(lift > 20 && fabs(cents) < 3 && live > 15, what);
    }

    std::printf(g_fails ? "\n%d FAILED\n" : "\nall passed\n", g_fails);
    return g_fails ? 1 : 0;
}

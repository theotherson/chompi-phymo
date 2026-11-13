// Checks each note's own strike (voice.h): MIDI velocity scaling the
// strength by its amount; Life varying the strikes a little, and not at
// all at 0; the course, a second string a few cents off, beating and still
// in tune, as loud as one; eight voices of it bounded. Then renders a
// course and a Life demo:
//   make -C host strike_test && (cd host && ./strike_test)
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

/** `ms` milliseconds of the synth's left side, added to `out`. */
static void Play(phymo::Synth& s, int ms, std::vector<float>& out)
{
    float l[48], r[48];
    for(int b = 0; b < ms; b++)
    {
        s.Process(l, r, 48);
        out.insert(out.end(), l, l + 48);
    }
}

/** A fresh synth: mono (one voice, in the middle), the string, a long
 *  decay, no key tracking, no Life. */
static void Fresh(phymo::Synth& s)
{
    s.Init(kSr);
    s.SetKeyMode(phymo::KeyMode::Mono);
    s.SetDecay(8.f), s.SetKeyTrack(0.f), s.SetBrightness(0.5f);
    s.SetVolume(1.f);
    s.SetLife(0.f);
}

/** RMS of `x` over `ms` milliseconds from `at_ms`, dB. */
static float RmsDb(const std::vector<float>& x, int at_ms, int ms)
{
    double e = 0;
    for(size_t i = static_cast<size_t>(at_ms) * 48; i < static_cast<size_t>(at_ms + ms) * 48; i++)
        e += static_cast<double>(x[i]) * x[i];
    return 10.f * log10f(static_cast<float>(e / (ms * 48)) + 1e-20f);
}

/** The strongest frequency of `x` from `from`, `n` samples, within ±range
 *  cents of `expect` (a Hann-windowed DFT, in tenths of a cent). */
static float PitchOf(const std::vector<float>& x, size_t from, size_t n, float expect, double range = 30)
{
    double best = expect, best_m = -1;
    for(double c = -range; c <= range; c += 0.1)
    {
        const double hz = expect * pow(2.0, c / 1200.0);
        double       re = 0, im = 0;
        for(size_t i = 0; i < n; i++)
        {
            const double win = 0.5 - 0.5 * cos(6.283185307 * i / n);
            const double ph  = 6.283185307 * hz * i / kSr;
            re += win * x[from + i] * cos(ph), im -= win * x[from + i] * sin(ph);
        }
        if(re * re + im * im > best_m)
            best_m = re * re + im * im, best = hz;
    }
    return static_cast<float>(best);
}

static float Cents(float hz, float ref) { return 1200.f * log2f(hz / ref); }

/** One note (A3) struck by MIDI at `vel`, 300 ms: its level, dB. */
static float VelocityLevel(float vel, float amount)
{
    phymo::Synth s;
    Fresh(s);
    s.SetVelocityAmount(amount);
    s.NoteOn(57, false, vel);
    std::vector<float> x;
    Play(s, 300, x);
    return RmsDb(x, 0, 300);
}

/** How far the level of `x` swings against `ref`'s, dB, from 0.2 s for
 *  2 s, in 50 ms windows (the same note's decay taken out). */
static float Swing(const std::vector<float>& x, const std::vector<float>& ref)
{
    float lo = 1e9f, hi = -1e9f;
    for(int ms = 200; ms < 2200; ms += 50)
    {
        const float d = RmsDb(x, ms, 50) - RmsDb(ref, ms, 50);
        lo = d < lo ? d : lo, hi = d > hi ? d : hi;
    }
    return hi - lo;
}

int main()
{
    char what[200];

    // Velocity: soft is quieter, by the amount; the keys (full) as before.
    {
        const float full = VelocityLevel(1.f, 1.f), soft = VelocityLevel(0.25f, 1.f);
        const float none = VelocityLevel(0.25f, 0.f), half = VelocityLevel(0.25f, 0.5f);
        std::snprintf(what, sizeof what, "velocity 0.25 against full: %.1f dB at amount 1, %.1f at 0.5, %.2f at 0",
                      soft - full, half - full, none - full);
        Check(soft - full < -8.f && half - full < -2.5f && half > soft && fabsf(none - full) < 0.05f, what);

        phymo::Synth s;
        Fresh(s);
        s.NoteOn(57); // a key: on / off, full
        std::vector<float> x;
        Play(s, 300, x);
        std::snprintf(what, sizeof what, "a CHOMPI key plays as full velocity (%.2f dB)", RmsDb(x, 0, 300) - full);
        Check(fabsf(RmsDb(x, 0, 300) - full) < 0.01f, what);
    }

    // Life: at 0, every strike the same; at 1, each a little different.
    for(float life : {0.f, 1.f})
    {
        phymo::Synth s;
        Fresh(s);
        s.SetLife(life);
        s.SetDamper(0.03f);
        s.SetNoise(0.f); // the noise burst is never the same twice anyway
        float lo = 1e9f, hi = -1e9f, plo = 1e9f, phi = -1e9f;
        for(int k = -1; k < 8; k++) // (one first, to let the controls settle from Init)
        {
            std::vector<float> x;
            s.NoteOn(57);
            Play(s, 400, x);
            s.NoteOff(57);
            Play(s, 600, x); // gone, so the next strikes a still string
            if(k < 0)
                continue;
            const float db = RmsDb(x, 0, 300), c = Cents(PitchOf(x, 4800, 9600, 220.f), 220.f);
            lo = db < lo ? db : lo, hi = db > hi ? db : hi;
            plo = c < plo ? c : plo, phi = c > phi ? c : phi;
        }
        std::snprintf(what, sizeof what, "Life %.0f: eight strikes' levels within %.2f dB, tuning %+.1f to %+.1f cents", life,
                      hi - lo, plo, phi);
        if(life == 0.f)
            Check(hi - lo < 0.1f && phi - plo < 0.15f, what);
        else
            Check(hi - lo > 0.3f && hi - lo < 5.f && phi - plo > 1.f && plo > -4.5f && phi < 4.5f, what);
    }

    // The course: two strings beating, still in tune, as loud as one.
    std::vector<float> one, two;
    for(float course : {0.f, 1.f})
    {
        phymo::Synth s;
        Fresh(s);
        s.SetCourse(course);
        s.NoteOn(57);
        Play(s, 2400, course > 0.f ? two : one);
    }
    {
        const float sw = Swing(two, one);
        std::snprintf(what, sizeof what, "the course beats: its level swings %.1f dB against one string's", sw);
        Check(sw > 4.f, what);
        // the two strings 15 cents apart, either side of the note
        const float a = Cents(PitchOf(two, 4800, 96000, 220.f * powf(2.f, -7.5f / 1200.f), 4), 220.f);
        const float b = Cents(PitchOf(two, 4800, 96000, 220.f * powf(2.f, 7.5f / 1200.f), 4), 220.f);
        std::snprintf(what, sizeof what, "the course's strings at %+.1f and %+.1f cents (-7.5 and +7.5)", a, b);
        Check(fabsf(a + 7.5f) < 1.f && fabsf(b - 7.5f) < 1.f, what);
        const float d = RmsDb(two, 0, 2400) - RmsDb(one, 0, 2400);
        std::snprintf(what, sizeof what, "the course as loud as one string over 2.4 s (%+.1f dB)", d);
        Check(fabsf(d) < 1.5f, what);
    }

    // Off at the next note, the second string's tail glides out, no jump.
    {
        phymo::Synth s;
        Fresh(s);
        s.SetCourse(1.f);
        s.NoteOn(57);
        std::vector<float> x;
        Play(s, 500, x);
        s.SetCourse(0.f);
        s.SetStrength(0.f); // so the strike itself adds next to nothing
        s.NoteOn(57);
        Play(s, 100, x);
        float jump = 0.f, step = 0.f;
        for(size_t i = 24000 - 480; i < 24000; i++)
            step = fmaxf(step, fabsf(x[i] - x[i - 1]));
        for(size_t i = 24000; i < 24000 + 960; i++)
            jump = fmaxf(jump, fabsf(x[i] - x[i - 1]));
        std::snprintf(what, sizeof what, "the course off at a new note: the biggest step %.4f against %.4f before", jump, step);
        Check(jump < 1.5f * step + 0.002f, what);
    }

    // Eight voices, the course and Life at full, struck hard: bounded.
    {
        phymo::Synth s;
        s.Init(kSr);
        s.SetKeyMode(phymo::KeyMode::Poly1);
        s.SetVolume(1.f);
        s.SetCourse(1.f), s.SetLife(1.f), s.SetStrength(1.f), s.SetHardness(1.f), s.SetBrightness(1.f);
        std::vector<float> x;
        for(int n = 0; n < 8; n++)
            s.NoteOn(36 + 7 * n, false, 1.f), Play(s, 20, x);
        Play(s, 1000, x);
        float peak = 0.f;
        bool  nan  = false;
        for(float v : x)
            peak = fmaxf(peak, fabsf(v)), nan = nan || std::isnan(v);
        std::snprintf(what, sizeof what, "eight voices, course and Life at full, hard: peak %.2f, no NaN", peak);
        Check(!nan && peak <= 1.f, what);

        // the cost, desktop (compare only): eight strings, one or two each
        for(float course : {0.f, 1.f})
        {
            s.SetCourse(course);
            for(int n = 0; n < 8; n++)
                s.NoteOn(36 + 7 * n, false, 1.f);
            float      l[48], r[48];
            const auto t0 = std::chrono::steady_clock::now();
            for(int b = 0; b < 5000; b++)
                s.Process(l, r, 48);
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
            std::printf("      eight voices, %s: %.1f ns a sample a voice (this computer)\n",
                        course > 0.f ? "two strings each" : "one string each", 1000.0 * us / (240000.0 * 8));
        }
    }

    // To listen to: a chord with the course, then repeated notes with Life.
    {
        phymo::Synth s;
        s.Init(kSr);
        s.SetKeyMode(phymo::KeyMode::Poly1);
        s.SetVolume(1.f), s.SetDecay(6.f), s.SetBrightness(0.45f), s.SetType(0.f), s.SetHardness(0.4f);
        std::vector<float> x;
        s.SetCourse(0.5f);
        for(int n : {48, 55, 60, 64})
            s.NoteOn(n), Play(s, 30, x);
        Play(s, 2500, x);
        for(int n : {48, 55, 60, 64})
            s.NoteOff(n);
        s.SetCourse(0.f), s.SetLife(1.f);
        for(int k = 0; k < 12; k++)
            s.NoteOn(60), Play(s, 200, x), s.NoteOff(60), Play(s, 50, x);
        Play(s, 1000, x);
        WriteWav("out/strike_demo.wav", x, x, static_cast<uint32_t>(kSr));
    }

    // To listen to: the latch loop, a chord held on the Spectral model's
    // tuned bar, then on nylon strings, each note striking again as it fades.
    {
        phymo::Synth s;
        s.Init(kSr);
        s.SetKeyMode(phymo::KeyMode::Poly1), s.SetVolume(0.9f), s.SetLife(0.5f), s.SetKeyTrack(0.7f);
        s.SetLatchLoop(true);
        std::vector<float> l, r;
        float              lb[48], rb[48];
        auto               play = [&](int ms) {
            for(int b = 0; b < ms; b++)
                s.Process(lb, rb, 48), l.insert(l.end(), lb, lb + 48), r.insert(r.end(), rb, rb + 48);
        };
        s.SetModel(phymo::Model::Spectral), s.SetTable(2.f / 8.f), s.SetDecay(2.5f), s.SetBrightness(0.5f);
        for(int n : {48, 55, 62, 64, 71})
            s.NoteOn(n), play(180);
        play(11000);
        s.AllNotesOff();
        play(1500);
        s.SetModel(phymo::Model::String), s.SetTable(0.5f), s.SetDecay(1.8f), s.SetType(0.8f), s.SetCourse(0.3f);
        for(int n : {45, 52, 57, 59, 64})
            s.NoteOn(n), play(140);
        play(11000);
        s.AllNotesOff();
        play(2000);
        WriteWav("out/latch_loop.wav", l, r, static_cast<uint32_t>(kSr));
        std::printf("      wrote out/latch_loop.wav\n");
    }

    // To listen to: one string through the materials, steel to gut, then
    // steel struck as gut by the morph.
    {
        phymo::Synth s;
        s.Init(kSr);
        s.SetKeyMode(phymo::KeyMode::Poly1), s.SetVolume(0.9f), s.SetLife(0.2f), s.SetDecay(3.f), s.SetType(0.85f);
        s.SetBrightness(0.6f), s.SetDamper(0.5f);
        std::vector<float> x;
        for(int m = 0; m < 5; m++)
        {
            s.SetTable(m / 4.f);
            for(int n : {48, 55, 60, 64})
                s.NoteOn(n), Play(s, 90, x);
            Play(s, 1600, x);
            for(int n : {48, 55, 60, 64})
                s.NoteOff(n);
            Play(s, 300, x);
        }
        s.SetTable(0.f), s.SetMorph(1.f), s.SetMorphTime(1.2f);
        for(int n : {48, 55, 60, 64})
            s.NoteOn(n), Play(s, 90, x);
        Play(s, 2500, x);
        WriteWav("out/materials.wav", x, x, static_cast<uint32_t>(kSr));
        std::printf("      wrote out/materials.wav\n");
    }

    std::printf(g_fails ? "\n%d FAILED\n" : "\nall passed\n", g_fails);
    return g_fails ? 1 : 0;
}

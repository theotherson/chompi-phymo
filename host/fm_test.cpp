// Checks the FM model (fm_core.h): a plain carrier's tuning; the sidebands
// where the ratio puts them; a scan across the list without clicks; the
// attack's time and the decay's; Carson's rule keeping a high, deep note
// clean; its level against the string's; bounded. Then renders some:
//   make -C host fm_test && (cd host && ./fm_test)
#include "../src/synth.h"
#include "wav.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr float kSr     = 48000.f;
static int             g_fails = 0;
using phymo::Model;

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

/** A fresh synth on FM: mono, a long decay, a fast attack, no key tracking.
 *  `depth` 0 to 1 (EXCITER knob 2), the list position 0 to 7. */
static void Fresh(phymo::Synth& s, float depth, float position, int list = 0)
{
    s.Init(kSr);
    s.SetKeyMode(phymo::KeyMode::Mono);
    s.SetModel(Model::Fm);
    s.SetVolume(1.f), s.SetDecay(20.f), s.SetKeyTrack(0.f), s.SetBrightness(1.f), s.SetStrength(1.f);
    s.SetAttack(0.002f), s.SetHardness(depth), s.SetNoise(0.f);
    s.SetTable(position / 7.f), s.SetFmList(list);
}

static std::vector<float> Note(phymo::Synth& s, int note, int ms)
{
    s.NoteOn(note);
    std::vector<float> l, r;
    Play(s, ms, l, r);
    return l;
}

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

static double Peak(const std::vector<float>& x, size_t from, size_t n, double expect)
{
    double best = expect, bm = -1;
    for(double c = -50; c <= 50; c += 5)
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

    // A plain carrier (depth 0): a sine at the note, C1 to C7.
    {
        double worst = 0;
        int    wn    = 24;
        for(int note = 24; note <= 96; note += 8)
        {
            Fresh(s, 0.f, 0.f);
            const auto   x = Note(s, note, 1100);
            const double c = 1200.0 * log2(Peak(x, 4800, 38400, Hz(note)) / Hz(note));
            if(fabs(c) > fabs(worst))
                worst = c, wn = note;
        }
        char what[80];
        std::snprintf(what, sizeof what, "tuning C1-C7: worst %+.2f cents (note %d)", worst, wn);
        Check(fabs(worst) < 0.5, what);
    }

    // Sidebands: ratio 2 (the harmonic list's 2nd entry), a moderate index:
    // the carrier ± 2: energy at 3f, none at 2f.
    {
        Fresh(s, 0.15f, 1.f);
        const auto   x  = Note(s, 48, 600);
        const double f  = Hz(48);
        const double s3 = 10.0 * log10(Mag(x, 4800, 19200, 3 * f) / Mag(x, 4800, 19200, f));
        const double s2 = 10.0 * log10(Mag(x, 4800, 19200, 2 * f) / Mag(x, 4800, 19200, f));
        char         what[96];
        std::snprintf(what, sizeof what, "ratio 2: the sideband at 3f %.0f dB, 2f %.0f dB (none)", s3, s2);
        Check(s3 > -20.0 && s2 < -60.0, what);
    }

    // A scan: the LFO sweeping the whole list under a held note. No clicks:
    // the sharpest change sample to sample no worse than a held note's, much.
    {
        auto roughness = [&](float lfo) {
            Fresh(s, 0.5f, 3.5f);
            s.SetLfoToMorph(lfo), s.SetLfoRate(0.7f), s.SetLfoFree(true);
            s.SetLfoShape(phymo::Lfo::Shape::Triangle);
            const auto x  = Note(s, 48, 3000);
            double     dd = 0;
            for(size_t i = 4800; i < x.size(); i++)
                dd = fmax(dd, fabs(x[i] - 2 * x[i - 1] + x[i - 2]));
            return dd / Rms(x, 4800, x.size() - 4800);
        };
        const double held = roughness(0.f), scan = roughness(1.f);
        char         what[96];
        std::snprintf(what, sizeof what, "a scan: its sharpest step %.2f against a held note's %.2f", scan, held);
        Check(scan < 2.0 * held, what);
    }

    // The envelope: attack 0.2 s (type ~0.4): at the top by then, not long
    // before; decay 2 s: 30 dB down a second later.
    {
        Fresh(s, 0.f, 0.f);
        s.SetAttack(0.2f), s.SetDecay(2.f);
        const auto x = Note(s, 60, 2000);
        size_t     top = 0;
        double     pk  = 0;
        for(size_t i = 0; i < 24000; i += 48) // 1 ms windows, the first half second
        {
            const double rr = Rms(x, i, 48);
            if(rr > pk)
                pk = rr, top = i;
        }
        const double drop = 20.0 * log10(Rms(x, 33600, 4800) / Rms(x, 81600, 4800));
        char         what[96];
        std::snprintf(what, sizeof what, "envelope: at its top after %.0f ms (200), %.1f dB down in a second (30)",
                      top / 48.0, drop);
        Check(top > 170 * 48 && top < 240 * 48 && fabs(drop - 30.0) < 3.0, what);
    }

    // Carson: a high note (C7), full depth, ratio 8: all its energy on the
    // note's harmonics (aliasing would land between them).
    {
        Fresh(s, 1.f, 7.f);
        const auto   x = Note(s, 96, 600);
        const double f = Hz(96);
        double       on = 0, all = 0;
        for(double hz = 50; hz < 24000; hz += 25)
        {
            const double m = Mag(x, 4800, 19200, hz);
            all += m;
            if(fabs(hz / f - round(hz / f)) * f < 30)
                on += m;
        }
        const double off = 10.0 * log10((all - on) / all);
        char         what[96];
        std::snprintf(what, sizeof what, "Carson, C7 at full depth and ratio 8: %.0f dB off the harmonics", off);
        Check(off < -30.0, what);
    }

    // Level: a moderate FM note as loud as the string's, within 6 dB.
    {
        phymo::Synth t;
        t.Init(kSr);
        t.SetKeyMode(phymo::KeyMode::Mono);
        t.SetVolume(1.f);
        std::vector<float> string;
        {
            t.NoteOn(60);
            std::vector<float> r;
            Play(t, 500, string, r);
        }
        Fresh(s, 0.3f, 1.f);
        s.SetStrength(0.8f), s.SetBrightness(0.5f), s.SetDecay(3.f);
        const auto   fm = Note(s, 60, 500);
        const double db = 20.0 * log10(Rms(fm, 0, 24000) / Rms(string, 0, 24000));
        char         what[80];
        std::snprintf(what, sizeof what, "level: %+.1f dB against the string", db);
        Check(fabs(db) < 6.0, what);
    }

    // Bounded: six notes at full depth and feedback, every list, scanning.
    {
        float peak   = 0.f;
        bool  finite = true;
        for(int list = 0; list < 4; list++)
        {
            Fresh(s, 1.f, 3.f, list);
            s.SetKeyMode(phymo::KeyMode::Poly1), s.SetNoise(1.f), s.SetMorph(1.f), s.SetLfoToMorph(1.f);
            for(int n : {36, 43, 48, 55, 60, 64})
                s.NoteOn(n);
            std::vector<float> l, r;
            Play(s, 400, l, r);
            for(float v : l)
                peak = fmaxf(peak, fabsf(v)), finite = finite && std::isfinite(v);
        }
        char what[80];
        std::snprintf(what, sizeof what, "six notes, full depth and feedback, every list: peak %.2f, no NaN", peak);
        Check(finite && peak < 1.f, what);
    }

    // Cost here.
    {
        Fresh(s, 0.5f, 2.f);
        s.SetKeyMode(phymo::KeyMode::Poly1);
        for(int n : {36, 43, 48, 55, 60, 64})
            s.NoteOn(n);
        const auto         t0 = std::chrono::steady_clock::now();
        std::vector<float> l, r;
        Play(s, 5000, l, r);
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        std::printf("      six voices on this computer: %.1f ns a sample a voice\n", 1000.0 * us / (240000.0 * 6));
    }

    // Renders.
    struct Demo
    {
        const char* file;
        int         list;
        float       position, morph, morph_time, depth, fb, attack, decay, bright, lfo_morph, lfo_rate;
        phymo::PickupMode pickup;
        float       drive, trem, spread;
        int         mode; // 0 chords, 1 an arpeggio, 2 a bass line
    };
    using PM          = phymo::PickupMode;
    const Demo demos[] = {
        // a pad scanning up and down the harmonic list
        {"out/fm_scan_pad.wav", 0, 2.f, 0.f, 0.5f, 0.35f, 0.2f, 0.6f, 15.f, 0.8f, 1.f, 0.1f, PM::Clean, 0.f, 0.f, 0.f, 0},
        // an electric piano: ratio 1, a short bright attack, the tine pickup
        {"out/fm_piano.wav", 0, 0.f, 0.15f, 0.25f, 0.25f, 0.f, 0.f, 3.f, 0.35f, 0.f, 1.f, PM::Tine, 0.5f, 0.4f, 1.f, 0},
        // a bass: ratio 1 with feedback
        {"out/fm_bass.wav", 0, 0.f, 0.f, 0.5f, 0.3f, 0.8f, 0.f, 1.2f, 0.4f, 0.f, 1.f, PM::Clean, 0.f, 0.f, 0.f, 2},
        // bells on the metal list, each note falling through it
        {"out/fm_bells.wav", 3, 1.f, 0.4f, 1.f, 0.45f, 0.f, 0.f, 5.f, 0.6f, 0.f, 1.f, PM::Clean, 0.f, 0.f, 0.f, 1},
        // a pluck whose spectrum sweeps down the odd list on each note
        {"out/fm_morph_pluck.wav", 1, 0.f, 1.f, 0.35f, 0.5f, 0.3f, 0.f, 1.5f, 0.5f, 0.f, 1.f, PM::Clean, 0.f, 0.f, 0.f, 1},
    };
    const int chords[4][4] = {{48, 55, 60, 64}, {45, 52, 57, 60}, {41, 48, 53, 57}, {43, 50, 55, 59}};
    for(const auto& d : demos)
    {
        phymo::Synth t;
        t.Init(kSr);
        t.SetModel(Model::Fm), t.SetVolume(0.8f), t.SetDamper(0.4f), t.SetKeyTrack(0.3f);
        t.SetFmList(d.list), t.SetTable(d.position / 7.f), t.SetMorph(0.5f + 0.5f * d.morph), t.SetMorphTime(d.morph_time);
        t.SetHardness(d.depth), t.SetNoise(d.fb), t.SetAttack(0.002f * powf(1000.f, d.attack)), t.SetDecay(d.decay), t.SetBrightness(d.bright);
        t.SetLfoToMorph(d.lfo_morph), t.SetLfoRate(d.lfo_rate), t.SetLfoFree(true), t.SetLfoShape(phymo::Lfo::Shape::Sine);
        t.SetPickup(d.pickup), t.SetPickupAmount(d.drive), t.SetTremoloRate(5.f), t.SetTremoloDepth(d.trem);
        t.SetTremoloSpread(d.spread);
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
        {
            const int arp[8]  = {48, 55, 60, 64, 67, 72, 67, 64};
            const int bass[8] = {36, 36, 48, 36, 39, 41, 43, 34};
            for(int rep = 0; rep < 2; rep++)
                for(int i = 0; i < 8; i++)
                {
                    const int n = d.mode == 2 ? bass[i] : arp[i];
                    t.NoteOn(n);
                    Play(t, d.mode == 2 ? 180 : 250, l, r);
                    t.NoteOff(n);
                    Play(t, d.mode == 2 ? 70 : 0, l, r);
                }
        }
        Play(t, 1500, l, r);
        WriteWav(d.file, l, r, 48000);
        std::printf("      wrote %s\n", d.file);
    }

    std::printf(g_fails ? "\n%d FAILED\n" : "\nall passed\n", g_fails);
    return g_fails ? 1 : 0;
}

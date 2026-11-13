// Checks the six voices: which voice each key mode gives a note, stealing
// when all six are busy, mono's legato, that six at once stay under the
// chorus's clip, that releases end in silence, and what six voices cost
// against one. Then renders some chords through chorus I:
//   make -C host poly_test && (cd host && ./poly_test)
#include "../src/synth.h"
#include "wav.h"
#include <algorithm>
#include <chrono>
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

/** Runs the synth for `ms` milliseconds, returning the output's peak. */
static float Run(phymo::Synth& s, int ms)
{
    float l[48], r[48], peak = 0.f;
    for(int b = 0; b < ms; b++)
    {
        s.Process(l, r, 48);
        for(int i = 0; i < 48; i++)
            peak = fmaxf(peak, fmaxf(fabsf(l[i]), fabsf(r[i])));
    }
    return peak;
}

/** The voice playing `note` with its key down, or -1. */
static int VoiceOf(const phymo::Synth& s, int note)
{
    for(int i = 0; i < phymo::Synth::kVoices; i++)
        if(s.GetVoice(i).Gated() && s.GetVoice(i).Note() == note)
            return i;
    return -1;
}

static void Fresh(phymo::Synth& s, phymo::KeyMode m)
{
    s.Init(kSr);
    s.SetKeyMode(m);
    s.SetDamper(0.3f); // tails long enough to overlap
}

int main()
{
    phymo::Synth s;

    // Poly 1: notes go round the voices; a released voice keeps ringing
    // while the next note takes the next voice.
    Fresh(s, phymo::KeyMode::Poly1);
    s.NoteOn(60), Run(s, 50), s.NoteOff(60), Run(s, 20);
    s.NoteOn(60);
    Check(VoiceOf(s, 60) == 1, "poly 1: the same note again takes the next voice (the last still ringing)");
    Check(s.ActiveVoices() == 2, "poly 1: both sounding");

    // Poly 1, nine notes held: the ninth steals the oldest.
    Fresh(s, phymo::KeyMode::Poly1);
    for(int n = 0; n < 8; n++)
        s.NoteOn(60 + n), Run(s, 5);
    Check(s.ActiveVoices() == 8, "poly 1: eight notes, eight voices");
    s.NoteOn(70);
    Check(VoiceOf(s, 70) == 0 && VoiceOf(s, 60) < 0, "all held: the 9th note takes the oldest held voice");

    // ... but a released one is taken before any held one.
    Fresh(s, phymo::KeyMode::Poly1);
    for(int n = 0; n < 8; n++)
        s.NoteOn(60 + n), Run(s, 5);
    s.NoteOff(63), Run(s, 5);
    s.NoteOn(70);
    Check(VoiceOf(s, 70) == 3 && VoiceOf(s, 60) == 0, "all busy: a released voice is taken before a held one");

    // Poly 2: a note goes back to its voice; else the lowest free one.
    Fresh(s, phymo::KeyMode::Poly2);
    s.NoteOn(60), s.NoteOn(64), Run(s, 20);
    s.NoteOff(60), Run(s, 20);
    s.NoteOn(67);
    Check(VoiceOf(s, 67) == 2, "poly 2: a new note takes the lowest silent voice");
    s.NoteOn(60);
    Check(VoiceOf(s, 60) == 0, "poly 2: a note goes back to the voice that last played it");

    // Mono: one voice, legato back to the key before.
    Fresh(s, phymo::KeyMode::Mono);
    s.NoteOn(60), Run(s, 20), s.NoteOn(64), Run(s, 20);
    Check(s.ActiveVoices() == 1 && VoiceOf(s, 64) == 0, "mono: one voice, the last key");
    s.NoteOff(64);
    Check(VoiceOf(s, 60) == 0, "mono: letting go falls back to the key still held");

    // Chord mode: the major triad until one's saved.
    auto gated = [&]() {
        std::vector<int> g;
        for(int i = 0; i < phymo::Synth::kVoices; i++)
            if(s.GetVoice(i).Gated())
                g.push_back(s.GetVoice(i).Note());
        std::sort(g.begin(), g.end());
        return g;
    };
    Fresh(s, phymo::KeyMode::Chord);
    s.NoteOn(60);
    Check(gated() == std::vector<int>({60, 64, 67}), "chord: a major triad on the key, before one's saved");
    s.NoteOff(60);
    // a saved chord, chosen as D F A (D minor), played on C: C Eb G
    const int dm[3] = {69, 62, 65};
    s.SetChord(dm, 3);
    s.NoteOn(60);
    Check(gated() == std::vector<int>({60, 63, 67}), "chord: a saved chord moves so its lowest note is the key");
    // one chord at a time: a second key replaces the first's chord, and
    // letting it go brings the first back
    s.NoteOn(63);
    Check(gated() == std::vector<int>({63, 66, 70}), "chord: a second key replaces the chord (no pile-up)");
    s.NoteOff(63);
    Check(gated() == std::vector<int>({60, 63, 67}), "chord: letting it go brings back the key still held");
    s.NoteOn(63), s.NoteOff(60);
    Check(gated() == std::vector<int>({63, 66, 70}), "chord: letting go of a key not sounding changes nothing");
    s.NoteOff(63);
    Check(gated().empty(), "chord: all keys up, silence");
    // choosing: single notes
    s.SetChordEdit(true);
    s.NoteOn(60);
    Check(gated() == std::vector<int>({60}), "chord: while choosing, keys play single notes");
    s.NoteOff(60), s.SetChordEdit(false);
    // the arp in chord mode: a chord a step
    s.SetArp(true);
    s.NoteOn(60), Run(s, 2);
    Check(gated().size() == 3, "chord + arp: each step is a chord");
    s.SetArp(false), s.NoteOff(60);

    // Spread: a chord fans out; at 0 it's mono; mono mode stays centred;
    // summed to mono it keeps its level.
    auto stereo = [&](phymo::KeyMode m, float spread, std::vector<int> notes, double& corr, double& mono_db,
                      double& l_db, double& r_db) {
        Fresh(s, m);
        s.SetSpread(spread);
        for(int k : notes)
            s.NoteOn(k);
        float  l[48], r[48];
        double ll = 0, rr = 0, lr = 0, mm = 0;
        for(int b = 0; b < 1000; b++)
        {
            s.Process(l, r, 48);
            if(b >= 300)
                for(int i = 0; i < 48; i++)
                    ll += l[i] * l[i], rr += r[i] * r[i], lr += l[i] * r[i],
                        mm += 0.25 * (l[i] + r[i]) * (l[i] + r[i]);
        }
        corr    = lr / sqrt(ll * rr);
        mono_db = 10 * log10(mm);
        l_db = 10 * log10(ll), r_db = 10 * log10(rr);
    };
    const std::vector<int> chord = {48, 55, 60, 64, 67, 72};
    double c0, m0, l0, r0, c6, m6, l6, r6, cm, mm, lm, rm, c1, m1, l1, r1;
    stereo(phymo::KeyMode::Poly1, 0.f, chord, c0, m0, l0, r0);
    stereo(phymo::KeyMode::Poly1, 0.6f, chord, c6, m6, l6, r6);
    stereo(phymo::KeyMode::Mono, 0.6f, {60}, cm, mm, lm, rm);
    stereo(phymo::KeyMode::Poly1, 0.6f, {60}, c1, m1, l1, r1);
    std::printf("      spread: L-R correlation %.2f at 0, %.2f at 0.6; summed to mono %.1f dB vs %.1f dB\n", c0, c6,
                m6, m0);
    Check(c0 > 0.999, "spread 0: mono, as the Juno");
    Check(c6 < 0.9, "spread 0.6: a chord across the field");
    Check(fabs(m6 - m0) < 1.5, "spread: summed to mono, the level holds (within 1.5 dB)");
    Check(fabs(lm - rm) < 0.1, "mono mode: in the middle, whatever the spread");
    Check(l1 > r1 + 3.0, "poly: the first voice sits left");

    // Level: six notes at once (full volume) stay under the chorus's clip.
    Fresh(s, phymo::KeyMode::Poly1);
    s.SetVolume(1.f);
    s.SetBrightness(1.f), s.SetStrength(1.f), s.SetDecay(20.f);
    for(int n : {36, 43, 48, 55, 60, 64})
        s.NoteOn(n);
    const float six = Run(s, 500);
    std::printf("      six voices, bright, struck hard: peak %.2f (%.1f dBFS)\n", six, 20.f * log10f(six));
    Check(six < 1.f, "six voices at once stay under full scale");

    // Releases end in silence, and silent voices drop out.
    for(int n : {36, 43, 48, 55, 60, 64})
        s.NoteOff(n);
    Run(s, 3000);
    Check(s.ActiveVoices() == 0, "after the releases, no voice is active");

    // The cost: six voices against one (desktop: compare only).
    auto cost = [&](int voices) {
        Fresh(s, phymo::KeyMode::Poly1);
        s.SetChorus(phymo::Chorus::Mode::I);
        for(int n = 0; n < voices; n++)
            s.NoteOn(48 + 4 * n);
        float      l[48], r[48];
        const auto t0 = std::chrono::steady_clock::now();
        for(int b = 0; b < 20000; b++)
            s.Process(l, r, 48);
        return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count()
               / (20000.0 * 48);
    };
    const double one = cost(1), all = cost(6);
    std::printf("      %.0f ns a sample with one voice, %.0f with six (x%.1f), chorus I\n", one, all, all / one);

    // A render: four chords, each held for a bar then let go into the next.
    Fresh(s, phymo::KeyMode::Poly1);
    s.SetVolume(0.5f);
    s.SetChorus(phymo::Chorus::Mode::I);
    s.SetDamper(0.6f);
    const int          chords[4][4] = {{48, 55, 60, 64}, {45, 52, 57, 60}, {41, 48, 53, 57}, {43, 50, 55, 59}};
    const int          n            = 9 * 48000;
    std::vector<float> l(n), r(n);
    for(int b = 0; b < n; b += 48)
    {
        const int t = b / 48000; // seconds
        if(b % 96000 == 0 && t / 2 < 4)
            for(int k : chords[t / 2])
                s.NoteOn(k);
        if(b % 96000 == 84000 && t / 2 < 4)
            for(int k : chords[t / 2])
                s.NoteOff(k);
        s.Process(&l[b], &r[b], 48);
    }
    // Fast taps on voices still ringing (a long damper): no click. With
    // a slow Attack, the old note fades out before the swell (no edge at
    // a note's start sharper than just before it); a string's new note,
    // its delay line's jump faded under (a soft strike's edge as a fresh
    // voice's). A quick re-pluck of the same note, no Attack, still lands
    // on the ringing string at once (its snap).
    {
        auto setup = [](phymo::Synth& s, phymo::KeyMode m, float attack, bool soft) {
            s.Init(kSr);
            s.SetKeyMode(m), s.SetVolume(1.f), s.SetDecay(8.f), s.SetDamper(3.f), s.SetBrightness(0.6f);
            s.SetAttack(attack), s.SetLife(0.f);
            if(soft)
                s.SetNoise(0.f), s.SetStrength(0.5f), s.SetType(0.f), s.SetHardness(0.f);
            else
                s.SetNoise(0.9f), s.SetStrength(1.f);
        };
        auto edge = [](const std::vector<float>& x, size_t from, size_t to) {
            double e = 0;
            for(size_t i = from + 2; i < to; i++)
                e = std::max(e, static_cast<double>(fabsf(x[i] - 2 * x[i - 1] + x[i - 2])));
            return e;
        };
        const int notes[] = {48, 55, 60, 52, 57, 64, 50, 59, 62, 53, 65, 47};
        float     lb[48], rb[48];
        auto      taps = [&](phymo::Synth& s, std::vector<float>& x, std::vector<size_t>& ons) {
            for(int k = 0; k < 36; k++)
            {
                ons.push_back(x.size());
                s.NoteOn(notes[k % 12]);
                for(int b = 0; b < 120; b++)
                {
                    if(b == 60)
                        s.NoteOff(notes[k % 12]);
                    s.Process(lb, rb, 48);
                    x.insert(x.end(), lb, lb + 48);
                }
            }
        };
        double swell = 0, moved = 0;
        for(auto m : {phymo::KeyMode::Poly1, phymo::KeyMode::Mono})
        {
            phymo::Synth s;
            setup(s, m, 0.3f, false);
            std::vector<float>  x;
            std::vector<size_t> ons;
            taps(s, x, ons);
            for(size_t k = 8; k < ons.size(); k++)
                swell = std::max(swell, edge(x, ons[k], ons[k] + 480) / edge(x, ons[k] - 960, ons[k]));
            phymo::Synth t;
            setup(t, m, 0.f, true);
            std::vector<float>  y;
            std::vector<size_t> on2;
            taps(t, y, on2);
            for(size_t k = 8; k < on2.size(); k++)
            {
                phymo::Synth f;
                setup(f, m, 0.f, true);
                std::vector<float> z;
                for(int b = 0; b < 100; b++)
                    f.Process(lb, rb, 48);
                f.NoteOn(notes[k % 12]);
                for(int b = 0; b < 16; b++)
                    f.Process(lb, rb, 48), z.insert(z.end(), lb, lb + 48);
                moved = std::max(moved, edge(y, on2[k], on2[k] + 720) / edge(z, 0, 720));
            }
        }
        // the snap: the same note again at once, struck on the ringing string
        phymo::Synth s;
        setup(s, phymo::KeyMode::Poly2, 0.f, false);
        std::vector<float> x;
        s.NoteOn(60);
        for(int b = 0; b < 80; b++)
            s.Process(lb, rb, 48), x.insert(x.end(), lb, lb + 48);
        const double before = edge(x, x.size() - 480, x.size());
        const size_t on     = x.size();
        s.NoteOn(60);
        for(int b = 0; b < 10; b++)
            s.Process(lb, rb, 48), x.insert(x.end(), lb, lb + 48);
        const double snap = edge(x, on, on + 240) / before;
        char         what[200];
        std::snprintf(what, sizeof what,
                      "fast taps on ringing voices: a slow Attack's start x%.1f the edge before it; a soft strike's "
                      "new note x%.2f a fresh voice's; a re-pluck's snap x%.0f",
                      swell, moved, snap);
        Check(swell < 4 && moved < 2 && snap > 5, what);
    }

    WriteWav("out/poly_chords.wav", l, r, 48000);
    std::printf("      wrote out/poly_chords.wav (C, Am, F, G through chorus I)\n");

    // The CPU's budget (Synth::ReportLoad, from the firmware's meter): a
    // block over 92% and a voice fewer sounds, the oldest let go first,
    // choked without a click (its 25 ms fade: no step larger than the
    // notes' own); three over 85% the same; half a second under 60%, one
    // more again. Unison fills only the budget.
    {
        phymo::Synth s;
        Fresh(s, phymo::KeyMode::Poly1);
        s.SetDecay(6.f), s.SetSustain(0.8f);
        for(int n : {48, 52, 55, 60, 64, 67, 72, 76})
            s.NoteOn(n), Run(s, 20);
        s.NoteOff(48);
        float l[48], r[48];
        double step = 0, step_before = 0, prev = 0;
        for(int b = 0; b < 100; b++) // (its own steps, first)
        {
            s.Process(l, r, 48);
            for(int i = 0; i < 48; i++)
                step_before = fmax(step_before, fabs(l[i] - prev)), prev = l[i];
        }
        s.ReportLoad(0.95f);
        const int b1 = s.VoiceBudget();
        for(int b = 0; b < 40; b++) // (past its fade)
        {
            s.Process(l, r, 48);
            for(int i = 0; i < 48; i++)
                step = fmax(step, fabs(l[i] - prev)), prev = l[i];
        }
        const int  after = s.ActiveVoices();
        const bool kept  = VoiceOf(s, 52) >= 0 && VoiceOf(s, 76) >= 0; // (the let-go C3 out, not a held note)
        for(int k = 0; k < 3; k++)
            s.ReportLoad(0.88f);
        const int b2 = s.VoiceBudget();
        for(int k = 0; k < phymo::Synth::kCalmBlocks; k++)
            s.ReportLoad(0.4f);
        const int b3 = s.VoiceBudget();

        phymo::Synth u;
        Fresh(u, phymo::KeyMode::Unison);
        u.SetVoiceBudget(4);
        u.NoteOn(57), Run(u, 20);
        char what[200];
        std::snprintf(what, sizeof what,
                      "the CPU's budget: over 92%%, %d voices (%d sounding, the let-go one out%s); its step %.4f (the notes' "
                      "own %.4f); three over 85%%, %d; calm, %d; unison at 4, %d voices",
                      b1, after, kept ? ", the held kept" : ", a HELD one out", step, step_before, b2, b3, u.ActiveVoices());
        Check(b1 == 7 && after == 7 && kept && step < 1.5 * step_before + 1e-4 && b2 == 6 && b3 == 7 && u.ActiveVoices() == 4,
              what);
    }
    return g_fails ? 1 : 0;
}

// The output stage (output.h): with it on, a single note should be as loud
// as with it off (kMatch), while chords come out denser (less difference
// between peak and average). Renders the same phrase off and on:
//   make -C host output_test && (cd host && ./output_test)
#include "../src/synth.h"
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

/** The panel's defaults (code/src/panel.h), at its default volume. */
static void Defaults(phymo::Synth& s)
{
    s.Init(kSr);
    s.SetVolume(powf(10.f, -48.f * (1.f - 0.65f) / 20.f));
}

struct Level
{
    float rms_db, peak_db;
};

static Level Play(bool stage, const std::vector<int>& notes)
{
    phymo::Synth s;
    Defaults(s);
    s.SetOutputStage(stage);
    for(int n : notes)
        s.NoteOn(n);
    float  l[48], r[48], peak = 0.f;
    double sq = 0;
    int    c  = 0;
    for(int b = 0; b < 1000; b++)
    {
        s.Process(l, r, 48);
        if(b >= 20) // from just after the strike (a plucked note has no sustain to wait for)
            for(int i = 0; i < 48; i++) // both sides (the voices are spread)
                sq += 0.5 * (l[i] * l[i] + r[i] * r[i]), c++, peak = fmaxf(peak, fmaxf(fabsf(l[i]), fabsf(r[i])));
    }
    const float rms = sqrtf(float(sq / c));
    return {20.f * log10f(rms), 20.f * log10f(peak)};
}

int main()
{
    const Level one_off = Play(false, {48}), one_on = Play(true, {48});
    std::printf("      one note:  off %.1f dB rms (peak %.1f), on %.1f dB rms (peak %.1f)\n", one_off.rms_db,
                one_off.peak_db, one_on.rms_db, one_on.peak_db);
    Check(fabsf(one_on.rms_db - one_off.rms_db) < 0.5f, "one note: as loud on as off (kMatch)");

    const std::vector<int> chord = {36, 48, 55, 60, 64, 67};
    const Level            ch_off = Play(false, chord), ch_on = Play(true, chord);
    const float            crest_off = ch_off.peak_db - ch_off.rms_db, crest_on = ch_on.peak_db - ch_on.rms_db;
    std::printf("      6 notes:   off %.1f dB rms (peak-to-average %.1f dB), on %.1f dB rms (%.1f dB)\n",
                ch_off.rms_db, crest_off, ch_on.rms_db, crest_on);
    Check(crest_on < crest_off, "6 notes: denser on (peaks closer to the average)");

    // A/B renders: the same phrase, off then on
    for(int stage = 0; stage < 2; stage++)
    {
        phymo::Synth s;
        Defaults(s);
        s.SetVolume(0.5f);
        s.SetOutputStage(stage == 1);
        s.SetChorus(phymo::Chorus::Mode::I);
        const int          chords[4][4] = {{48, 55, 60, 64}, {45, 52, 57, 60}, {41, 48, 53, 57}, {43, 50, 55, 59}};
        const int          n            = 8 * 48000;
        std::vector<float> l(n), r(n);
        for(int b = 0; b < n; b += 48)
        {
            const int t = b / 48000;
            if(b % 96000 == 0)
                for(int k : chords[t / 2])
                    s.NoteOn(k);
            if(b % 96000 == 84000)
                for(int k : chords[t / 2])
                    s.NoteOff(k);
            s.Process(&l[b], &r[b], 48);
        }
        WriteWav(stage ? "out/output_on.wav" : "out/output_off.wav", l, r, 48000);
    }
    std::printf("      wrote out/output_off.wav and output_on.wav (the same chords, brighter default)\n");
    return g_fails ? 1 : 0;
}

// Checks the modulation (env.h, lfo.h, voice.h's destinations, synth.h's
// triggers): the envelope's stages in their times; the envelope to the
// brightness, up and inverted; the LFO's depth inverted; NOTE restarting
// the LFO on every new note, the arp's too, FREE not; VOICE each voice its
// own, from its own note; the exciter (FM's index) and the tone moved; the
// RESONATOR's Attack (a swell) and Sustain (a hold); all of it bounded. Then renders a demo:
//   make -C host mod_test && (cd host && ./mod_test)
#include "../src/synth.h"
#include "wav.h"
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr float kSr     = 48000.f;
static int             g_fails = 0;
using phymo::Dest;
using phymo::LfoTrigger;

static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "pass" : "FAIL", what);
    g_fails += !ok;
}

static void Play(phymo::Synth& s, int ms, std::vector<float>& out)
{
    float l[48], r[48];
    for(int b = 0; b < ms; b++)
    {
        s.Process(l, r, 48);
        out.insert(out.end(), l, l + 48);
    }
}

/** The first difference's energy over the signal's: how bright. */
static double Highs(const std::vector<float>& x, int at_ms, int ms)
{
    double e = 0, d = 0;
    for(size_t i = static_cast<size_t>(at_ms) * 48 + 1; i < static_cast<size_t>(at_ms + ms) * 48; i++)
        e += static_cast<double>(x[i]) * x[i], d += static_cast<double>(x[i] - x[i - 1]) * (x[i] - x[i - 1]);
    return d / (e + 1e-20);
}

static double RmsDb(const std::vector<float>& x, int at_ms, int ms)
{
    double e = 0;
    for(size_t i = static_cast<size_t>(at_ms) * 48; i < static_cast<size_t>(at_ms + ms) * 48; i++)
        e += static_cast<double>(x[i]) * x[i];
    return 10.0 * log10(e / (ms * 48) + 1e-20);
}

/** A fresh synth: mono, the string, a long decay, no key tracking, no Life. */
static void Fresh(phymo::Synth& s)
{
    s.Init(kSr);
    s.SetKeyMode(phymo::KeyMode::Mono), s.SetVolume(1.f);
    s.SetDecay(8.f), s.SetKeyTrack(0.f), s.SetBrightness(0.3f), s.SetLife(0.f);
}

int main()
{
    char what[200];

    // The envelope: attack 100 ms, decay 200 ms to 0.5, release 300 ms.
    {
        phymo::Synth s;
        Fresh(s);
        s.SetEnv(0.1f, 0.2f, 0.5f, 0.3f);
        s.NoteOn(48);
        std::vector<float> x;
        const auto& v = s.GetVoice(0);
        Play(s, 50, x);
        const float half = v.EnvValue();
        Play(s, 52, x);
        const float top = v.EnvValue();
        Play(s, 400, x);
        const float held = v.EnvValue();
        s.NoteOff(48);
        Play(s, 300, x);
        const float gone = v.EnvValue();
        std::snprintf(what, sizeof what, "the envelope: %.2f at 50 ms, %.2f at 102 ms, holding %.2f, %.3f 300 ms after letting go",
                      half, top, held, gone);
        Check(half > 0.4f && half < 0.95f && top > 0.97f && fabsf(held - 0.5f) < 0.02f && gone < 0.01f, what);
    }

    // The envelope to the brightness: up brighter, inverted darker (sustain full).
    {
        auto highs = [](float depth) {
            phymo::Synth s;
            Fresh(s);
            s.SetEnv(0.005f, 1.f, 1.f, 0.3f);
            s.SetEnvDepth(Dest::Bright, depth);
            s.NoteOn(48);
            std::vector<float> x;
            Play(s, 600, x);
            return Highs(x, 300, 300);
        };
        const double up = highs(1.f), none = highs(0.f), down = highs(-1.f);
        std::snprintf(what, sizeof what, "the envelope to the brightness: up %.2fx the highs of none, inverted %.2fx",
                      up / none, down / none);
        Check(up > 1.5 * none && down < none / 1.2, what);
    }

    // The LFO inverted: a slow square (its first half up), NOTE-triggered,
    // to the brightness: up brighter in that half, inverted darker.
    {
        auto highs = [](float depth) {
            phymo::Synth s;
            Fresh(s);
            s.SetLfoShape(phymo::Lfo::Shape::Square), s.SetLfoRate(0.5f), s.SetLfoTrigger(LfoTrigger::Note);
            s.SetLfoDepth(Dest::Bright, depth);
            s.NoteOn(48);
            std::vector<float> x;
            Play(s, 600, x);
            return Highs(x, 200, 400);
        };
        const double up = highs(1.f), none = highs(0.f), down = highs(-1.f);
        std::snprintf(what, sizeof what, "the LFO to the brightness: up %.2fx the highs of none, inverted %.2fx", up / none,
                      down / none);
        Check(up > 1.2 * none && down < none / 1.1, what);
    }

    // NOTE: every new note restarts the LFO (a 1 Hz triangle starts at its
    // bottom), the arp's too; FREE: wherever it is.
    for(int free = 0; free < 2; free++)
    {
        phymo::Synth s;
        Fresh(s);
        s.SetKeyMode(phymo::KeyMode::Poly1);
        s.SetLfoRate(1.f), s.SetLfoTrigger(free ? LfoTrigger::Free : LfoTrigger::Note);
        s.SetArpRate(3.3f), s.SetArp(true);
        s.NoteOn(48), s.NoteOn(55), s.NoteOn(60);
        std::vector<float> x;
        int   last = -1, steps = 0;
        float worst = -1.f, lo = 1.f, hi = -1.f;
        for(int ms = 0; ms < 3000; ms++)
        {
            Play(s, 1, x);
            const int n = s.GetArpNote();
            if(n >= 0 && n != last)
            {
                const float v = s.LfoValue(); // a block after the note
                worst = fmaxf(worst, v), lo = fminf(lo, v), hi = fmaxf(hi, v), steps++;
            }
            last = n;
        }
        if(!free)
        {
            std::snprintf(what, sizeof what, "NOTE: each of %d arp notes restarts the LFO (at most %.3f from its bottom)", steps,
                          worst + 1.f);
            Check(steps >= 8 && worst < -0.98f, what);
        }
        else
        {
            std::snprintf(what, sizeof what, "FREE: the arp's notes find the LFO anywhere (%.2f to %.2f)", lo, hi);
            Check(steps >= 8 && hi - lo > 0.8f, what);
        }
    }

    // VOICE: each voice its own LFO, from its own note: two notes 250 ms
    // apart each start at the bottom, a quarter cycle (half the range) apart.
    {
        phymo::Synth s;
        Fresh(s);
        s.SetKeyMode(phymo::KeyMode::Poly1);
        s.SetLfoRate(1.f), s.SetLfoTrigger(LfoTrigger::Voice);
        std::vector<float> x;
        Play(s, 333, x); // (the synth's own LFO somewhere else)
        s.NoteOn(48);
        Play(s, 1, x);
        int a = -1, b = -1;
        for(int i = 0; i < phymo::Synth::kVoices; i++)
            if(s.GetVoice(i).Gated())
                a = i;
        const float a0 = s.GetVoice(a).LfoValue();
        Play(s, 249, x);
        s.NoteOn(55);
        Play(s, 1, x);
        for(int i = 0; i < phymo::Synth::kVoices; i++)
            if(s.GetVoice(i).Gated() && i != a)
                b = i;
        const float b0 = s.GetVoice(b).LfoValue(), a1 = s.GetVoice(a).LfoValue();
        std::snprintf(what, sizeof what, "VOICE: each note's LFO from its bottom (%.3f, %.3f), the first by then at %.2f",
                      a0, b0, a1);
        Check(a0 < -0.98f && b0 < -0.98f && fabsf(a1 - 0.f) < 0.05f, what);
    }

    // The exciter: FM's index, the envelope up brighter than inverted.
    {
        auto highs = [](float depth) {
            phymo::Synth s;
            Fresh(s);
            s.SetModel(phymo::Model::Fm), s.SetBrightness(0.5f);
            s.SetEnv(0.005f, 1.f, 1.f, 0.3f);
            s.SetEnvDepth(Dest::Exciter, depth);
            s.NoteOn(48);
            std::vector<float> x;
            Play(s, 600, x);
            return Highs(x, 300, 300);
        };
        const double up = highs(1.f), down = highs(-1.f);
        std::snprintf(what, sizeof what, "the exciter (FM's index): up %.2fx the highs of inverted", up / down);
        Check(up > 2.0 * down, what);
    }

    // The tone: half the range at full, from the newest note.
    {
        phymo::Synth s;
        Fresh(s);
        s.SetEnv(0.005f, 1.f, 1.f, 0.3f);
        s.SetEnvDepth(Dest::Tone, -1.f);
        s.NoteOn(48);
        std::vector<float> x;
        Play(s, 100, x);
        const float t = s.GetVoice(0).ToneMod();
        std::snprintf(what, sizeof what, "the tone: the envelope inverted at full, %.3f of its range", t);
        Check(fabsf(t + 0.5f) < 0.01f, what);
    }

    // Attack: the String model's swell, 300 ms: its first 20 ms well under
    // what it is by 300; with none, the strike first, as before. Over a
    // note still ringing, it fades out first (12 ms: no click), then swells.
    {
        auto rise = [](float attack) {
            phymo::Synth s;
            Fresh(s);
            s.SetAttack(attack);
            s.NoteOn(48);
            std::vector<float> x;
            Play(s, 400, x);
            return RmsDb(x, 0, 20) - RmsDb(x, 290, 20);
        };
        const double slow = rise(0.3f), none = rise(0.f);
        // again over the ring: it fades out (kHushSwell: 12 ms) and swells back
        phymo::Synth s;
        Fresh(s);
        s.SetAttack(0.3f);
        s.NoteOn(48);
        std::vector<float> x;
        Play(s, 800, x);
        s.NoteOff(48), s.NoteOn(48); // (mono: the same voice, ringing)
        Play(s, 400, x);
        const double dip = RmsDb(x, 813, 10) - RmsDb(x, 780, 20);
        std::snprintf(what, sizeof what,
                      "Attack: a 300 ms swell's first 20 ms %+.0f dB on its 300th (none: %+.0f); struck again ringing, it dips "
                      "%.0f dB and swells back", slow, none, dip);
        Check(slow < -12.0 && none > 0.0 && dip < -15.0, what);
    }

    // Sustain: a 1 s ring, held. At 0.5 it stops falling 24 dB under its
    // peak (half of kHoldOct's 48: 10 to 32 under its first 40 ms' level,
    // by how sharp the strike's peak is; eased in, it settles a little
    // over it: from 8) and stays; at 0, it rings away;
    // let go, the damper takes it. Spectral and FM hold too.
    for(int m = 0; m < 3; m++)
    {
        auto run = [m](float sustain, double& held, double& steady, double& after) {
            phymo::Synth s;
            Fresh(s);
            s.SetModel(static_cast<phymo::Model>(m)), s.SetDecay(1.f), s.SetDamper(0.1f), s.SetSustain(sustain);
            s.NoteOn(48);
            std::vector<float> x;
            Play(s, 4000, x);
            s.NoteOff(48);
            Play(s, 500, x);
            const double top = RmsDb(x, 20, 40);
            held   = RmsDb(x, 3000, 200) - top;
            steady = RmsDb(x, 3800, 200) - RmsDb(x, 2500, 200);
            after  = RmsDb(x, 4400, 100) - top;
        };
        double h, st, af, h0, st0, af0;
        run(0.5f, h, st, af);
        run(0.f, h0, st0, af0);
        static const char* kName[3] = {"String", "Spectral", "FM"};
        std::snprintf(what, sizeof what,
                      "Sustain (%s): at 0.5 held %.0f dB down, steady within %.1f dB over 1.3 s, %.0f dB let go; at 0, %.0f dB "
                      "by 3 s", kName[m], h, st, af, h0);
        // (-7: the string's offset now taken out by its period's mean, not a
        // blocker in its loop, it holds steadier, and a little higher: -8,
        // where it sagged on to -10)
        Check(h > -32.0 && h < -7.0 && fabs(st) < 1.5 && af < -50.0 && h0 < -60.0, what);
    }

    // Sustain eases into its level, gently when it's low: a 1 s ring at
    // Sustain 0.2, no corner where it's caught (each 100 ms's fall within
    // 1.5 dB of the one before), and held after; at 0.9 it's there quickly.
    {
        auto run = [&](float sustain) {
            phymo::Synth s;
            Fresh(s);
            s.SetDecay(1.f), s.SetSustain(sustain);
            for(int b = 0; b < 50; b++) // (the volume's glide settled)
            {
                float l[48], r[48];
                s.Process(l, r, 48);
            }
            s.NoteOn(48);
            std::vector<float> x;
            Play(s, 3000, x);
            return x;
        };
        const auto low = run(0.2f);
        double     corner = 0, prev = 0;
        for(int ms = 200; ms < 2800; ms += 100)
        {
            const double fall = RmsDb(low, ms + 100, 100) - RmsDb(low, ms, 100);
            if(ms > 200)
                corner = fmax(corner, fabs(fall - prev));
            prev = fall;
        }
        const double held = RmsDb(low, 2800, 200) - RmsDb(low, 2000, 200);
        const auto   high = run(0.9f);
        const double soon = RmsDb(high, 300, 100) - RmsDb(high, 900, 100);
        std::snprintf(what, sizeof what,
                      "Sustain eases in: at 0.2 its fall changes at most %.1f dB a 100 ms, then held (%+.1f dB); at 0.9 "
                      "there by 0.3 s (%+.1f dB on)",
                      corner, held, soon);
        Check(corner < 1.5 && fabs(held) < 1.5 && fabs(soon) < 2.5, what); // (2.5: the string held steadier, 2.1)
    }

    // Everything at full, every model, eight voices, the LFO fast: bounded.
    for(int m = 0; m < static_cast<int>(phymo::Model::Count); m++)
    {
        phymo::Synth s;
        s.Init(kSr);
        s.SetKeyMode(phymo::KeyMode::Poly1), s.SetVolume(1.f), s.SetModel(static_cast<phymo::Model>(m));
        s.SetLfoRate(20.f), s.SetLfoTrigger(LfoTrigger::Voice), s.SetEnv(0.002f, 0.1f, 0.6f, 0.2f);
        s.SetSustain(1.f), s.SetAttack(0.05f);
        s.SetPickup(phymo::PickupMode::Reed), s.SetPickupAmount(1.f);
        for(int d = 0; d < phymo::kDests; d++)
            s.SetLfoDepth(static_cast<Dest>(d), d % 2 ? 1.f : -1.f), s.SetEnvDepth(static_cast<Dest>(d), d % 2 ? -1.f : 1.f);
        std::vector<float> x;
        for(int n = 0; n < 8; n++)
            s.NoteOn(36 + 5 * n, false, 1.f), Play(s, 30, x);
        Play(s, 1500, x);
        float peak = 0.f;
        bool  nan  = false;
        for(float v : x)
            peak = fmaxf(peak, fabsf(v)), nan = nan || std::isnan(v);
        std::snprintf(what, sizeof what, "model %d: every depth at full, eight voices: peak %.2f, no NaN", m, peak);
        Check(!nan && peak <= 1.f, what);
    }

    // A demo: the arp, NOTE, the envelope opening each note's brightness
    // and pulling its pitch down a touch; then a held chord, VOICE, each
    // note's LFO sweeping its own morph.
    {
        phymo::Synth s;
        s.Init(kSr);
        s.SetKeyMode(phymo::KeyMode::Poly1), s.SetVolume(0.8f), s.SetDecay(3.f), s.SetBrightness(0.15f);
        s.SetEnv(0.01f, 0.25f, 0.f, 0.2f);
        s.SetEnvDepth(Dest::Bright, 0.9f), s.SetEnvDepth(Dest::Pitch, -0.12f);
        s.SetArpRate(6.f), s.SetArp(true);
        std::vector<float> x;
        s.NoteOn(48), s.NoteOn(55), s.NoteOn(63);
        Play(s, 4000, x);
        s.NoteOff(48), s.NoteOff(55), s.NoteOff(63), s.SetArp(false);
        Play(s, 600, x);
        s.SetEnvDepth(Dest::Bright, 0.f), s.SetEnvDepth(Dest::Pitch, 0.f), s.SetBrightness(0.5f);
        s.SetModel(phymo::Model::Spectral), s.SetDecay(10.f), s.SetLfoTrigger(LfoTrigger::Voice);
        s.SetLfoRate(0.4f), s.SetLfoShape(phymo::Lfo::Shape::Sine), s.SetLfoDepth(Dest::Morph, 1.f);
        for(int n : {43, 50, 58, 62})
            s.NoteOn(n), Play(s, 400, x);
        Play(s, 5000, x);
        for(int n : {43, 50, 58, 62})
            s.NoteOff(n);
        Play(s, 1500, x);
        WriteWav("out/mod_demo.wav", x, x, static_cast<uint32_t>(kSr));
        std::printf("      wrote out/mod_demo.wav\n");
    }

    std::printf(g_fails ? "\n%d FAILED\n" : "\nall passed\n", g_fails);
    return g_fails ? 1 : 0;
}

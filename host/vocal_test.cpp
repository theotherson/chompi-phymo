// The Vocal model (src/vocal.h): its vowels' formants where the table puts
// them; a high note still heard on a closed vowel (the first formant tuned
// to it); as loud as the String model; held, it sustains, let go, it fades;
// the ensemble's notes apart, none without it; the morph gliding a vowel
// into another; the vibrato its own. Renders out/vocal_*.wav.
//   make -C host vocal_test && (cd host && ./vocal_test)
#include "../src/synth.h"
#include "wav.h"
#include <cmath>
#include <cstdio>
#include <vector>

using phymo::Model;
using phymo::Synth;

static constexpr float kSr     = 48000.f;
static int             g_fails = 0;

static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "pass" : "FAIL", what);
    g_fails += !ok;
}

static void Play(Synth& s, int ms, std::vector<float>& l, std::vector<float>& r)
{
    float bl[48], br[48];
    for(int b = 0; b < ms; b++)
    {
        s.Process(bl, br, 48);
        l.insert(l.end(), bl, bl + 48), r.insert(r.end(), br, br + 48);
    }
}

/** |X(hz)| of `x` over `n` samples from `from`, Hann-windowed. */
static double Mag(const std::vector<float>& x, size_t from, size_t n, double hz)
{
    double c = 0, s = 0;
    for(size_t i = 0; i < n; i++)
    {
        const double w = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1)), ph = 2 * M_PI * hz * i / kSr;
        c += w * x[from + i] * cos(ph), s += w * x[from + i] * sin(ph);
    }
    return sqrt(c * c + s * s) / n;
}

static double Rms(const std::vector<float>& x, size_t from, size_t n)
{
    double e = 0;
    for(size_t i = from; i < from + n; i++)
        e += static_cast<double>(x[i]) * x[i];
    return sqrt(e / n);
}

static double Hz(int note) { return 440.0 * pow(2.0, (note - 69) / 12.0); }

static void Fresh(Synth& s, float vowel)
{
    s.Init(kSr);
    s.SetKeyMode(phymo::KeyMode::Mono);
    s.SetModel(Model::Vocal);
    s.SetVolume(1.f), s.SetNoise(0.05f), s.SetBrightness(0.5f);
    s.SetTable(vowel / static_cast<float>(phymo::kVowels - 1));
    s.SetSustain(1.f); // (held, it stays)
    s.SetType(0.f);    // (no consonant)
}

/** The harmonic (of note `note`) loudest between `lo` and `hi` Hz: a formant's. */
static double Formant(const std::vector<float>& x, int note, double lo, double hi)
{
    const double f0   = Hz(note);
    double       best = 0, at = 0;
    for(int k = 1; k * f0 < hi; k++)
        if(k * f0 >= lo)
        {
            const double m = Mag(x, 24000, 24000, k * f0);
            if(m > best)
                best = m, at = k * f0;
        }
    return at;
}

int main()
{
    Synth s;
    char  what[200];

    // Each vowel's first two formants: around each (out to the midpoints
    // with its neighbours), the loudest harmonic of A2 (110 Hz) a peak (both its
    // neighbours under it, not the window's edge; F1 and F2 resolved apart),
    // within a harmonic and a half of the table's.
    {
        double worst = 0;
        int    wv = 0, wk = 0;
        bool   peaks = true;
        for(int v = 0; v < phymo::kVowels; v++)
        {
            Fresh(s, static_cast<float>(v));
            std::vector<float> l, r;
            s.NoteOn(45);
            Play(s, 1000, l, r);
            for(int k = 0; k < 2; k++)
            {
                const double want = phymo::kVowelHz[v][k];
                const float*  f   = phymo::kVowelHz[v];
                const double  lo  = k == 0 ? 0.6 * want : 0.5 * (f[0] + f[1]); // (to the midpoints with its neighbours)
                const double  hi  = 0.5 * (f[k] + f[k + 1]);
                const double got  = Formant(l, 45, lo, hi);
                const double m    = Mag(l, 24000, 24000, got);
                if(f[1] - f[0] > 400.f) // (aw's F1 and F2, 270 Hz apart, one hump at 110 Hz's harmonics, as a voice's)
                    peaks = peaks && m > Mag(l, 24000, 24000, got - 110) && m > Mag(l, 24000, 24000, got + 110);
                const double off = fabs(got - want) / 110.0;
                if(off > worst)
                    worst = off, wv = v, wk = k + 1;
            }
        }
        std::snprintf(what, sizeof what,
                      "each vowel's F1 and F2: a peak%s within %.2f harmonics of the table's (vowel %d, F%d)",
                      peaks ? "" : " NOT always", worst, wv, wk);
        Check(peaks && worst <= 1.5, what);
    }

    // Sung high on "ee" (F1 270 Hz): A5 (880 Hz), its first formant tuned
    // up to it, within 12 dB of A3 on the same vowel.
    {
        double lv[2];
        for(int i = 0; i < 2; i++)
        {
            Fresh(s, 7.f);
            std::vector<float> l, r;
            s.NoteOn(i ? 81 : 57);
            Play(s, 800, l, r);
            lv[i] = 20 * log10(Rms(l, 19200, 19200));
        }
        std::snprintf(what, sizeof what, "sung high: A5 on ee %.1f dB against A3's %.1f (within 12)", lv[1], lv[0]);
        Check(lv[1] > lv[0] - 12.0, what);
    }

    // As loud as the String model (C3 held, 0.2 to 1 s): within 3 dB.
    {
        double lv[2];
        for(int m = 0; m < 2; m++)
        {
            s.Init(kSr);
            s.SetModel(m ? Model::Vocal : Model::String), s.SetSustain(1.f), s.SetDecay(8.f);
            std::vector<float> l, r;
            s.NoteOn(48, false, 0.8f);
            Play(s, 1000, l, r);
            lv[m] = 20 * log10(Rms(l, 9600, 38400));
        }
        std::snprintf(what, sizeof what, "level: Vocal %.1f dB, String %.1f (within 3)", lv[1], lv[0]);
        Check(fabs(lv[1] - lv[0]) < 3.0, what);
    }

    // Held, it sustains (2 s on within 1 dB of 0.5 s on); let go (Damper
    // 0.5 s), 60 dB down within it, and the voice free.
    {
        Fresh(s, 3.f);
        s.SetDamper(0.5f);
        std::vector<float> l, r;
        s.NoteOn(48);
        Play(s, 2200, l, r);
        const double a = Rms(l, 24000, 4800), b = Rms(l, 96000, 4800);
        s.NoteOff(48);
        std::vector<float> l2, r2;
        Play(s, 700, l2, r2);
        const double c = Rms(l2, 24000, 4800);
        std::snprintf(what, sizeof what, "held: %.1f dB at 2 s against 0.5 s's; let go, %.0f dB down by 0.5 s; free %s",
                      20 * log10(b / a), 20 * log10(c / b + 1e-12), s.GetVoice(0).Active() ? "NO" : "yes");
        Check(fabs(20 * log10(b / a)) < 1.0 && 20 * log10(c / b + 1e-12) < -50.0 && !s.GetVoice(0).Active(), what);
    }

    // The ensemble: two voices on one note, at full, apart (their sum's
    // level wavers as they beat); none, together (it doesn't).
    {
        double waver[2];
        for(int e = 0; e < 2; e++)
        {
            s.Init(kSr);
            s.SetKeyMode(phymo::KeyMode::Unison), s.SetModel(Model::Vocal), s.SetEnsemble(e ? 1.f : 0.f), s.SetNoise(0.f);
            s.SetSustain(1.f);
            s.SetTable(3.f / 7.f);
            std::vector<float> l, r;
            s.NoteOn(57);
            Play(s, 3000, l, r);
            double lo = 1e9, hi = 0;
            for(int w = 0; w < 25; w++)
            {
                const double v = Rms(l, 24000 + w * 4800, 4800);
                lo = fmin(lo, v), hi = fmax(hi, v);
            }
            waver[e] = 20 * log10(hi / lo);
        }
        std::snprintf(what, sizeof what, "ensemble: a unison's level wavers %.1f dB at full (beating), %.1f at none", waver[1], waver[0]);
        Check(waver[1] > 1.0 && waver[0] < waver[1], what);
    }

    // The morph: on "ee" (F2 2290), each note starting at "u" (F2 870) and
    // gliding over 0.4 s: F2 low at first, high once it's there.
    {
        Fresh(s, 7.f);
        s.SetMorph(0.5f - 0.5f), s.SetMorphTime(0.4f); // (all the way down: from the first vowel)
        std::vector<float> l, r;
        s.NoteOn(45);
        Play(s, 1200, l, r);
        auto f2 = [&](size_t from) {
            double best = 0, at = 0;
            for(int k = 6; k * 110.0 < 2800; k++)
            {
                const double m = Mag(l, from, 2400, k * 110.0);
                if(m > best)
                    best = m, at = k * 110.0;
            }
            return at;
        };
        const double a = f2(1200), b = f2(48000);
        std::snprintf(what, sizeof what, "morph: F2 %.0f Hz at the start (u: 870), %.0f once there (ee: 2290)", a, b);
        Check(a < 1300 && b > 1900, what);
    }

    // Sustain and Ring, held: Sustain 0.3, Ring 0.5 s, a note settled by
    // 1.5 s 10.5 dB (0.3) under the same note with Sustain at most, within
    // 2 dB; at most, no fall (1.5 s within half a dB of 0.1 s); at least,
    // gone (60 dB down); struck again
    // (legato, a tone up: its formants a little louder), up to full again
    // (within 3 dB of off's).
    {
        double level[2], early_off = 0, again = 0;
        for(int on = 0; on < 2; on++)
        {
            Fresh(s, 3.f);
            s.SetNoise(0.f), s.SetSustain(on ? 0.3f : 1.f), s.SetDecay(0.5f);
            std::vector<float> l, r;
            s.NoteOn(57);
            Play(s, 1600, l, r);
            level[on] = Rms(l, 72000, 2400);
            if(!on)
                early_off = Rms(l, 4800, 2400);
            else
            {
                s.NoteOn(59); // (legato, mono: the same voice)
                std::vector<float> l2, r2;
                Play(s, 30, l2, r2);
                again = 20 * log10(Rms(l2, 960, 480) / level[0]);
            }
        }
        double gone;
        {
            Fresh(s, 3.f);
            s.SetNoise(0.f), s.SetSustain(0.f), s.SetDecay(0.5f);
            std::vector<float> l, r;
            s.NoteOn(57);
            Play(s, 1600, l, r);
            gone = 20 * log10(Rms(l, 72000, 2400) / level[0] + 1e-12);
        }
        const double drop = 20 * log10(level[1] / level[0]), off = 20 * log10(level[0] / early_off);
        std::snprintf(what, sizeof what, "sustain and ring: settled %.1f dB (-10.5), at most %.1f, at least %.0f; struck again %.1f dB of most's",
                      drop, off, gone, again);
        Check(fabs(drop + 10.5) < 2.0 && fabs(off) < 0.5 && gone < -60.0 && fabs(again) < 3.0, what);
    }

    // The onset (Type, a consonant, the attack: none at 0, d at a third,
    // k at two, h at 1, each crossfading into the vowel): the vowel in
    // (its fundamental half in) later for k than d (by 15 ms), h's over
    // 70 ms; k's burst over none's start (6 dB, its first 5 ms); after 0.2
    // s, every one as the plain vowel (within 0.5 dB).
    {
        const float at[4] = {0.f, 1.f / 3.f, 2.f / 3.f, 1.f};
        double      voice[4], burst[4], later[4];
        for(int c = 0; c < 4; c++)
        {
            Fresh(s, 3.f);
            s.SetNoise(0.05f), s.SetType(at[c]);
            std::vector<float> l, r;
            s.NoteOn(45);
            Play(s, 400, l, r);
            const double f0 = Mag(l, 9600, 2400, 110.0);
            voice[c]        = -1;
            for(int w = 0; w < 60 && voice[c] < 0; w++)
                if(Mag(l, w * 240, 480, 110.0) > 0.5 * f0)
                    voice[c] = w * 5;
            double pk = 0;
            for(size_t i = 0; i < 240; i++)
                pk = fmax(pk, fabs(l[i]));
            burst[c] = 20 * log10(pk + 1e-12);
            later[c] = 20 * log10(Rms(l, 9600, 4800));
        }
        double worst = 0;
        for(int c = 1; c < 4; c++)
            worst = fmax(worst, fabs(later[c] - later[0]));
        std::snprintf(what, sizeof what, "onset: the vowel in at d %.0f ms, k %.0f, h %.0f; k's burst %+.1f dB over none's start; after, within %.2f dB",
                      voice[1], voice[2], voice[3], burst[2] - burst[0], worst);
        Check(voice[2] > voice[1] + 15 && voice[3] > 70 && burst[2] > burst[0] + 6 && worst < 0.5, what);
    }

    // The consonants' formant transitions (their join to the vowel): on
    // ah (F1 730, F2 1090), at the strike F1 closed (under 400) and F2 from
    // the locus' side: d's well over the vowel's (1400), k's over d's (the
    // velum's higher); all in the vowel by 200 ms; h's, none.
    {
        const float pos[4] = {0.f, 1.f / 3.f, 2.f / 3.f, 1.f};
        double      f1[4][3], f2[4][3]; // (at 0, 40 and 200 ms)
        for(int c = 0; c < 4; c++)
        {
            Fresh(s, 3.f);
            s.SetType(pos[c]);
            std::vector<float> l, r;
            s.NoteOn(45);
            const int at[3] = {1, 40, 200};
            int       t     = 0;
            for(int k = 0; k < 3; k++)
            {
                Play(s, at[k] - t, l, r), t = at[k];
                f1[c][k] = s.GetVoice(0).VocalFormant(0), f2[c][k] = s.GetVoice(0).VocalFormant(1);
            }
        }
        bool settled = true;
        for(int c = 0; c < 4; c++)
            settled = settled && fabs(f2[c][2] - 1090) < 30 && fabs(f1[c][2] - 730) < 30;
        std::snprintf(what, sizeof what, "transitions: F1 from %.0f (d), F2 from d %.0f, k %.0f (the vowel's 1090); h %.0f; all in the vowel by 200 ms%s",
                      f1[1][0], f2[1][0], f2[2][0], f2[3][0], settled ? "" : " NOT");
        Check(f1[1][0] < 400 && f2[1][0] > 1400 && f2[2][0] > f2[1][0] && fabs(f2[3][0] - 1090) < 10 && settled, what);
    }

    // The wheel's brightness: at its top, ah's F2 (1090 Hz) ~11% up
    // (within 5%), at its bottom ~10% down, found on A1 (55 Hz: a fine
    // grid of harmonics) as the vowels' test does; at its middle, where the
    // table puts it.
    {
        double f2[3];
        for(int g = 0; g < 3; g++)
        {
            Fresh(s, 3.f);
            s.SetBrightness(0.5f * g);
            std::vector<float> l, r;
            s.NoteOn(33);
            Play(s, 1000, l, r);
            const double sh = pow(2.0, 0.3 * (0.5 * g - 0.5)); // (the window with it)
            f2[g]           = Formant(l, 33, 0.5 * (730 + 1090) * sh, 0.5 * (1090 + 2440) * sh);
        }
        std::snprintf(what, sizeof what, "the wheel's brightness: ah's F2 %.0f Hz at its bottom, %.0f its middle, %.0f its top", f2[0], f2[1],
                      f2[2]);
        Check(fabs(f2[2] / f2[1] - 1.11) < 0.05 && fabs(f2[0] / f2[1] - 0.90) < 0.05 && fabs(f2[1] - 1090) < 80, what);
    }

    // A section (the ensemble): one note, at full, three voices beating
    // (its level wavers); none, steady.
    {
        double waver[2];
        for(int e = 0; e < 2; e++)
        {
            Fresh(s, 3.f);
            s.SetEnsemble(e ? 1.f : 0.f), s.SetNoise(0.f);
            std::vector<float> l, r;
            s.NoteOn(57);
            Play(s, 3000, l, r);
            double lo = 1e9, hi = 0;
            for(int w = 0; w < 50; w++)
            {
                const double v = Rms(l, 24000 + w * 2400, 2400);
                lo = fmin(lo, v), hi = fmax(hi, v);
            }
            waver[e] = 20 * log10(hi / lo);
        }
        std::snprintf(what, sizeof what, "a section: one note's level wavers %.1f dB with three voices, %.1f with one", waver[1], waver[0]);
        Check(waver[1] > 1.0 && waver[0] < 0.3, what);
    }

    // A section's singers somewhere new each strike: one note struck twice
    // (let go between), its level's rise and fall (50 ms steps, 0.2 to 2 s)
    // different each time (somewhere by 1 dB); none, the same.
    {
        double diff[2];
        for(int e = 0; e < 2; e++)
        {
            Fresh(s, 3.f);
            s.SetEnsemble(e ? 1.f : 0.f), s.SetNoise(0.f), s.SetDamper(0.1f);
            std::vector<double> env[2];
            for(int k = 0; k < 2; k++)
            {
                std::vector<float> l, r;
                s.NoteOn(57);
                Play(s, 2100, l, r);
                s.NoteOff(57);
                Play(s, 600, l, r);
                for(int w = 4; w < 40; w++)
                    env[k].push_back(20 * log10(Rms(l, w * 2400, 2400) + 1e-12));
            }
            diff[e] = 0;
            for(size_t i = 0; i < env[0].size(); i++)
                diff[e] = fmax(diff[e], fabs(env[0][i] - env[1][i]));
        }
        std::snprintf(what, sizeof what, "a section's singers new each strike: two strikes' levels apart by %.1f dB at most (none %.2f)", diff[1],
                      diff[0]);
        Check(diff[1] > 1.0 && diff[0] < 0.2, what);
    }

    // The tape: its pitch wobbling (A3's, 50 ms windows: more than 4
    // cents across, under 12) against none's (under 0.5); a held note out by
    // 9 s (its 8 s reel), none's still sounding; its highs (as a band,
    // against its level) at least 1 dB down against none's (gently: its
    // roll-off 6.5 kHz at full).
    {
        double spread[2], late[2], hi[2];
        for(int t = 0; t < 2; t++)
        {
            Fresh(s, 3.f);
            s.SetTape(t ? 1.f : 0.f), s.SetNoise(0.f), s.SetSustain(1.f), s.SetDecay(30.f), s.SetDamper(0.5f);
            std::vector<float> l, r;
            s.NoteOn(57);
            Play(s, 9500, l, r);
            // the pitch each 50 ms, 1 to 3.75 s: autocorrelation's peak
            // (A3's period, 218 samples), between lags by a parabola
            double lo = 1e9, top = -1e9;
            for(size_t w = 48000; w < 180000; w += 2400)
            {
                double best = -1e30, ac[3] = {};
                int    at   = 0;
                for(int k = 180; k < 260; k++)
                {
                    double c = 0;
                    for(int i = 0; i < 2400; i++)
                        c += static_cast<double>(l[w + i]) * l[w + i + k];
                    if(c > best)
                        best = c, at = k;
                }
                for(int d = -1; d <= 1; d++)
                {
                    double c = 0;
                    for(int i = 0; i < 2400; i++)
                        c += static_cast<double>(l[w + i]) * l[w + i + at + d];
                    ac[d + 1] = c;
                }
                const double frac = 0.5 * (ac[0] - ac[2]) / (ac[0] - 2 * ac[1] + ac[2]);
                const double c    = 1200 * log2(kSr / (at + frac) / 220.0);
                lo = fmin(lo, c), top = fmax(top, c);
            }
            spread[t] = top - lo;
            late[t]   = 20 * log10(Rms(l, 432000, 4800) + 1e-12);
            // the highs as a band, 5 to 7 kHz (short windows, a 10 Hz
            // comb: the wow moves the harmonics), against 300 Hz to 2 kHz
            double e = 0, e0 = 0;
            for(size_t w = 48000; w < 144000; w += 1024) // (short windows, a fine grid: each harmonic's lobe covered)
            {
                for(double f = 5000; f < 7000; f += 10)
                    e += pow(Mag(l, w, 1024, f), 2);
                for(double f = 300; f < 2000; f += 10)
                    e0 += pow(Mag(l, w, 1024, f), 2);
            }
            hi[t] = 10 * log10(e / e0);
        }
        std::snprintf(what, sizeof what, "tape: its pitch spread %.1f cents (none %.1f); at 9 s %.0f dB (none %.0f); its highs %.1f dB against none's",
                      spread[1], spread[0], late[1], late[0], hi[1] - hi[0]);
        Check(spread[1] > 4.0 && spread[1] < 12.0 && spread[0] < 0.5 && late[1] < -90 && late[0] > -60 && hi[1] < hi[0] - 1.0, what);
    }

    // Renders: a choir, chords on ah, oo, ee, ah (ensemble, vibrato, reverb).
    {
        static uint16_t tank[phymo::Space::kTankWords];
        static int16_t  dly[phymo::Space::kDelayWords];
        s.Init(kSr);
        s.InitSpace(tank, dly);
        s.SetModel(Model::Vocal), s.SetVolume(0.8f), s.SetKeyMode(phymo::KeyMode::Poly1);
        s.SetAttack(0.25f), s.SetDamper(1.2f), s.SetEnsemble(0.6f), s.SetLife(0.5f);
        s.SetVibrato(5.2f, 0.35f, 0.6f, -1.f), s.SetSend(0.35f), s.SetSpace(0.8f), s.SetTails(0.7f);
        s.SetChorus(phymo::Chorus::Mode::I), s.SetHardness(0.45f), s.SetNoise(0.12f), s.SetSustain(1.f);
        std::vector<float> l, r;
        const int   ch[4][4] = {{48, 55, 60, 64}, {45, 52, 57, 60}, {41, 48, 53, 57}, {43, 50, 55, 59}};
        const float vow[4]   = {3.f, 0.f, 7.f, 3.f};
        for(int c = 0; c < 4; c++)
        {
            s.SetTable(vow[c] / 7.f);
            for(int n : ch[c])
                s.NoteOn(n, false, 0.7f);
            Play(s, 1800, l, r);
            for(int n : ch[c])
                s.NoteOff(n, false);
            Play(s, 200, l, r);
        }
        Play(s, 2500, l, r);
        WriteWav("out/vocal_choir.wav", l, r, static_cast<int>(kSr));
        std::printf("      wrote out/vocal_choir.wav\n");
    }

    std::printf(g_fails ? "%d FAILED\n" : "all passed\n", g_fails);
    return g_fails ? 1 : 0;
}

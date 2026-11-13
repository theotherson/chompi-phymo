// Checks the Bowed model (bow_res.h, voice.h): each bow in tune C2 to C6
// and holding its note while the key's down; as loud as the string; the
// attack (Type) and the release; heavier pressure and bowing by the bridge
// brighter; eight hard voices bounded. Then renders each bow:
//   make -C host bow_test && (cd host && ./bow_test)
#include "../src/synth.h"
#include "wav.h"
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

static void Play(phymo::Synth& s, int ms, std::vector<float>& out)
{
    float l[48], r[48];
    for(int b = 0; b < ms; b++)
    {
        s.Process(l, r, 48);
        out.insert(out.end(), l, l + 48);
    }
}

static double RmsDb(const std::vector<float>& x, int at_ms, int ms)
{
    double e = 0;
    for(size_t i = static_cast<size_t>(at_ms) * 48; i < static_cast<size_t>(at_ms + ms) * 48; i++)
        e += static_cast<double>(x[i]) * x[i];
    return 10.0 * log10(e / (ms * 48) + 1e-20);
}

/** The first difference's energy over the signal's: how bright. */
static double Highs(const std::vector<float>& x, int at_ms, int ms)
{
    double e = 0, d = 0;
    for(size_t i = static_cast<size_t>(at_ms) * 48 + 1; i < static_cast<size_t>(at_ms + ms) * 48; i++)
        e += static_cast<double>(x[i]) * x[i], d += static_cast<double>(x[i] - x[i - 1]) * (x[i] - x[i - 1]);
    return d / (e + 1e-20);
}

/** The harmonic-to-noise ratio (dB) from sample `from` to `to`: each 20 ms
 *  against itself a period on (`P` samples, the best within ±2%). */
static double Hnr(const std::vector<float>& x, int from, int to, double P)
{
    double sig = 0, res = 0;
    for(int w = from; w + 960 + 2 * static_cast<int>(P) + 2 < to; w += 960)
    {
        double best = 1e30, e = 0;
        for(double lag = P * 0.98; lag <= P * 1.02; lag += 0.05)
        {
            const int    li = static_cast<int>(lag);
            const double fr = lag - li;
            double       d  = 0;
            for(int i = w; i < w + 960; i++)
            {
                const double z = x[i + li] + (x[i + li + 1] - x[i + li]) * fr - x[i];
                d += z * z;
            }
            best = fmin(best, d);
        }
        for(int i = w; i < w + 960; i++)
            e += x[i] * x[i];
        sig += e, res += best / 2;
    }
    return 10 * log10(sig / (res + 1e-30));
}

/** The energy between the harmonics (over a quarter of `f0` from each)
 *  against all, dB, in 4096 samples from `from` (Hann DFT). */
static double OffHarm(const std::vector<float>& x, size_t from, double f0)
{
    const int n = 4096;
    double    on = 0, off = 0;
    for(int k = 1; k < n / 2; k++)
    {
        double re = 0, im = 0;
        for(int i = 0; i < n; i++)
        {
            const double w = (0.5 - 0.5 * cos(6.283185307 * i / n)) * x[from + i], ph = 6.283185307 * k * i / n;
            re += w * cos(ph), im -= w * sin(ph);
        }
        const double hz = k * kSr / n, h = hz / f0, d = fabs(h - floor(h + 0.5));
        (d > 0.25 && h > 0.5 ? off : on) += re * re + im * im;
    }
    return 10 * log10(off / (on + off + 1e-30));
}

/** The strongest frequency within ±range cents of `f` (Hann DFT, tenths of a cent). */
static double Cents(const std::vector<float>& x, size_t from, size_t n, double f, double range = 40)
{
    double best = f, bm = -1;
    for(double c = -range; c <= range; c += 0.2)
    {
        const double hz = f * pow(2.0, c / 1200.0);
        double       re = 0, im = 0;
        for(size_t i = 0; i < n; i++)
        {
            const double w = 0.5 - 0.5 * cos(6.283185307 * i / n), ph = 6.283185307 * hz * i / kSr;
            re += w * x[from + i] * cos(ph), im -= w * x[from + i] * sin(ph);
        }
        if(re * re + im * im > bm)
            bm = re * re + im * im, best = hz;
    }
    return 1200.0 * log2(best / f);
}

/** A fresh synth bowing: mono, no Life, the bow `bow` (0 to 1 along them). */
static void Fresh(phymo::Synth& s, float bow)
{
    s.Init(kSr);
    s.SetKeyMode(phymo::KeyMode::Mono), s.SetVolume(1.f), s.SetModel(Model::Bow);
    s.SetDecay(3.f), s.SetBrightness(0.6f), s.SetLife(0.f), s.SetType(0.3f), s.SetAttack(0.002f * powf(1000.f, 0.3f));
    s.SetTable(bow);
}

int main()
{
    char        what[200];
    const char* name[4] = {"rosin", "horsehair", "synthetic", "e-bow"};

    // Tuning and holding, each bow, C2 to C6: within 2 cents to G4; above,
    // within 6 (the stick-slip breaks free on a whole sample, so a high
    // note's period can lock to a whole number of them: C6's 45.9 to 46).
    for(int b = 0; b < 4; b++)
    {
        double worst = 0, top = 0, drift = 0, lo = 1e9, hi = -1e9;
        for(int note : {36, 48, 60, 67, 72, 84})
        {
            phymo::Synth s;
            Fresh(s, b / 3.f);
            s.NoteOn(note);
            std::vector<float> x;
            Play(s, 2000, x);
            const double f = 440.0 * pow(2.0, (note - 69) / 12.0);
            const double c = fabs(Cents(x, 48000, 24000, f));
            (note <= 67 ? worst : top) = fmax(note <= 67 ? worst : top, c);
            drift          = fmax(drift, fabs(RmsDb(x, 1600, 400) - RmsDb(x, 1200, 400))); // (settled)
            const double l = RmsDb(x, 1000, 900);
            lo = fmin(lo, l), hi = fmax(hi, l);
        }
        std::snprintf(what, sizeof what,
                      "%s: C2 to G4 within %.1f cents, C5 and C6 %.1f; held, the level steady within %.1f dB; %.0f to %.0f dB",
                      name[b], worst, top, drift, lo, hi);
        Check(worst < (b == 3 ? 12.0 : 2.0) && top < (b == 3 ? 12.0 : 6.0) && drift < 2.0 && hi - lo < 12.0, what);
    }

    // As loud as the string (a C3 bowed, held, against one struck, its first 300 ms).
    {
        phymo::Synth s;
        Fresh(s, 1.f / 3.f);
        s.NoteOn(48);
        std::vector<float> bowed, struck;
        Play(s, 1500, bowed);
        phymo::Synth t;
        t.Init(kSr);
        t.SetKeyMode(phymo::KeyMode::Mono), t.SetVolume(1.f), t.SetDecay(3.f), t.SetBrightness(0.6f), t.SetLife(0.f);
        t.NoteOn(48);
        Play(t, 300, struck);
        const double d = RmsDb(bowed, 1000, 500) - RmsDb(struck, 0, 300);
        std::snprintf(what, sizeof what, "bowed as loud as struck: %+.1f dB", d);
        Check(fabs(d) < 4.0, what);
    }

    // The attack: 2 ms speaks at once (the bow catches the string: within
    // 8 dB of the held level by 25 ms); 200 ms well behind it.
    {
        auto at = [](float type) {
            phymo::Synth s;
            Fresh(s, 1.f / 3.f);
            s.SetType(type), s.SetAttack(0.002f * powf(1000.f, type));
            s.NoteOn(48);
            std::vector<float> x;
            Play(s, 1000, x);
            return RmsDb(x, 25, 20) - RmsDb(x, 800, 100);
        };
        const double fast = at(0.f), slow = at(0.67f);
        std::snprintf(what, sizeof what, "the attack: at 25 ms %+.1f dB of the held level at 2 ms, %+.1f at 200 ms", fast, slow);
        Check(fast > -8.0 && slow < fast - 6.0, what);
    }

    // It speaks at once, low notes too: C2, C3, C4 within 8 dB of their held
    // level in their first 20 ms, then within 3 dB by 150 ms.
    {
        double first = 0, bloom = 0;
        for(int note : {36, 48, 60})
        {
            phymo::Synth s;
            Fresh(s, 1.f / 3.f);
            s.SetAttack(0.002f);
            s.NoteOn(note);
            std::vector<float> x;
            Play(s, 1000, x);
            first = fmin(first, RmsDb(x, 0, 20) - RmsDb(x, 800, 100));
            bloom = fmin(bloom, RmsDb(x, 150, 30) - RmsDb(x, 800, 100));
        }
        std::snprintf(what, sizeof what, "speaks at once: C2 to C4 within %.1f dB of their held level in 20 ms, %.1f by 150 ms",
                      -first, -bloom);
        Check(first > -8.0 && bloom > -3.0, what);
    }

    // The vibrato (BODY knobs 3 and 4, Bowed: the tremolo's place): at
    // full, onset 0.5 s, the pitch still while it waits (its first third),
    // building, then swinging ± a semitone (its windows' spread, peak to
    // peak, 200, with its wander, the bow's grit and the tracker's jitter
    // on it); let go (a long damper, so it's heard), fading gently over
    // its release (3 s, as a player's takes time to stop): most of it still
    // there in its first 0.2 s, less but still swinging by 0.5 s; the
    // tremolo, at full, silent with the bow. With a decay (3 s, the
    // least), held, it fades once it's in.
    {
        phymo::Synth s;
        Fresh(s, 1.f / 3.f);
        s.SetAttack(0.002f), s.SetVibrato(5.f, 1.f, 0.5f, -1.f), s.SetTremoloDepth(1.f), s.SetTremoloRate(5.f);
        s.SetDamper(3.f);
        s.NoteOn(60);
        std::vector<float> x;
        Play(s, 2000, x);
        s.NoteOff(60);
        Play(s, 600, x);
        std::vector<float> y = x; // the fundamental alone (four one-poles at 300 Hz), for its zero crossings
        for(int pass = 0; pass < 4; pass++)
        {
            float z = 0.f;
            for(float& v : y)
                z += (v - z) * 0.0385f, v = z;
        }
        auto spread = [&](int from_ms, int to_ms) {
            double lo = 1e9, hi = -1e9;
            for(int ms = from_ms; ms < to_ms; ms += 10)
            {
                // the period from the zero crossings in 30 ms (rising, interpolated)
                const size_t a = static_cast<size_t>(ms) * 48, n = 30 * 48;
                double first = -1, last = -1, mean = 0;
                int    count = 0;
                for(size_t i = a; i < a + n; i++) // (crossings of its own mean: a bowed string let go drifts a little)
                    mean += y[i] / n;
                for(size_t i = a + 1; i < a + n; i++)
                    if(y[i - 1] < mean && y[i] >= mean)
                    {
                        const double t = (i - 1) + (y[i - 1] - mean) / (y[i - 1] - y[i]);
                        if(first < 0)
                            first = t;
                        last = t, count++;
                    }
                const double hz = (count - 1) * 48000.0 / (last - first);
                const double c  = 1200.0 * log2(hz / 261.6256);
                lo = fmin(lo, c), hi = fmax(hi, c);
            }
            return hi - lo;
        };
        const double before = spread(20, 140), building = spread(250, 400), after = spread(1000, 1900);
        const double fading = spread(2040, 2240), later = spread(2300, 2500); // (a whole swing each: 5 Hz)
        const double wobble = RmsDb(x, 1000, 100) - RmsDb(x, 1100, 100);
        std::snprintf(what, sizeof what,
                      "the vibrato: %.0f cents peak to peak waiting, %.0f building, %.0f in; let go %.0f, then %.0f; the tremolo "
                      "silent (%.1f dB)", before, building, after, fading, later, wobble);
        Check(before < 30.0 && building > before && building < after && after > 170.0 && after < 260.0
                  && fading > 0.65 * after && later < 0.9 * fading && later > 0.4 * after && fabs(wobble) < 1.5,
              what); // (the tracker's own jitter ~20)

        // held, with a decay: in, then fading while the key's still down
        phymo::Synth d;
        Fresh(d, 1.f / 3.f);
        d.SetAttack(0.002f), d.SetVibrato(5.f, 1.f, 0.5f, 0.3f); // (under the least: 3 s)
        d.NoteOn(60);
        x.clear();
        Play(d, 4000, x);
        y = x;
        for(int pass = 0; pass < 4; pass++)
        {
            float z = 0.f;
            for(float& v : y)
                z += (v - z) * 0.0385f, v = z;
        }
        const double in = spread(380, 600), mid = spread(1500, 1900), held = spread(3300, 3700);
        std::snprintf(what, sizeof what, "the vibrato's decay (at least 3 s), held: %.0f cents in, %.0f a second on, %.0f 3 s on",
                      in, mid, held);
        Check(in > 120.0 && mid > 0.2 * in && mid < 0.6 * in && held < 0.25 * in, what);
    }

    // A tremolo (strokes 0.06 s each way, ~17 bow changes a second): still
    // sounding (within 5 dB of one endless stroke: the bow slowing through
    // each change), each change heard (the level dipping 2 to 9 dB a stroke,
    // high notes the most, as the turns'), G3 to G5.
    {
        double worst_level = 0, lo_dip = 1e9, hi_dip = 0;
        for(int note : {55, 67, 79})
        {
            double level[2];
            for(int t = 0; t < 2; t++)
            {
                phymo::Synth s;
                Fresh(s, 1.f / 3.f);
                s.SetAttack(0.01f), s.SetHardness(0.55f), s.SetStrength(0.75f), s.SetPosition(0.3f), s.SetBrightness(0.62f);
                s.SetType(0.7f), s.SetBowStroke(t ? 0.06f : 0.f, 0.5f);
                s.NoteOn(note, false, 0.9f);
                std::vector<float> x;
                Play(s, 3000, x);
                const double P = kSr / (440.0 * pow(2.0, (note - 69) / 12.0));
                const int    N = static_cast<int>(P * fmax(1.0, floor(240.0 / P)) + 0.5);
                std::vector<double> v;
                for(size_t w = 48000; w + N < 144000; w += 240)
                {
                    double e = 0;
                    for(int i = 0; i < N; i++)
                        e += static_cast<double>(x[w + i]) * x[w + i];
                    v.push_back(10 * log10(e / N + 1e-30));
                }
                double m = 0;
                for(double y : v)
                    m += y / v.size();
                level[t] = m;
                if(t)
                {
                    double dips = 0;
                    int    n    = 0;
                    for(size_t a = 0; a + 12 <= v.size(); a += 12, n++) // (a stroke: 12 of 5 ms)
                    {
                        double l = 1e9, h = -1e9;
                        for(int i = 0; i < 12; i++)
                            l = fmin(l, v[a + i]), h = fmax(h, v[a + i]);
                        dips += h - l;
                    }
                    lo_dip = fmin(lo_dip, dips / n), hi_dip = fmax(hi_dip, dips / n);
                }
            }
            worst_level = fmax(worst_level, fabs(level[1] - level[0]));
        }
        std::snprintf(what, sizeof what, "a tremolo (0.06 s strokes): within %.1f dB of one stroke, each change dipping %.1f to %.1f dB",
                      worst_level, lo_dip, hi_dip);
        Check(worst_level < 5.0 && lo_dip > 2.0 && hi_dip < 9.0, what);
    }

    // Bow changes: E5, 0.45 s strokes, Life 0.4; each change's dip (windows a
    // whole number of periods, ~20 ms, every 5 ms) 3 to 8 dB, and the changes
    // not all alike (spread over 0.5 dB).
    {
        phymo::Synth s;
        Fresh(s, 1.f / 3.f);
        s.SetHardness(0.55f), s.SetStrength(0.75f), s.SetPosition(0.3f), s.SetBrightness(0.62f), s.SetType(0.75f);
        s.SetAttack(0.06f), s.SetDecay(4.f), s.SetLife(0.4f), s.SetBowStroke(0.45f, 0.f);
        s.NoteOn(75, false, 0.9f);
        std::vector<float> x;
        Play(s, 4000, x);
        const double P = kSr / (440.0 * pow(2.0, 6.0 / 12.0));
        const int    N = static_cast<int>(P * floor(960.0 / P) + 0.5);
        auto lv = [&](int ms) {
            double e = 0;
            for(int i = 0; i < N; i++)
                e += static_cast<double>(x[ms * 48 + i]) * x[ms * 48 + i];
            return 10 * log10(e / N + 1e-30);
        };
        double lo = 1e9, hi = 0;
        for(int c = 1; c <= 7; c++) // (the changes' middles: a stroke and a turn apart, 0.458 s)
        {
            const int mid = 454 + (c - 1) * 458; // (the first: the stroke, then half its 8 ms turn)
            double    m   = 1e9;
            for(int ms = mid - 40; ms < mid + 30; ms += 5)
                m = fmin(m, lv(ms));
            const double dip = 0.5 * (lv(mid - 120) + lv(mid + 120)) - m;
            lo = fmin(lo, dip), hi = fmax(hi, dip);
        }
        std::snprintf(what, sizeof what, "a bow change as a player's: each dipping %.1f to %.1f dB", lo, hi);
        Check(lo > 3.0 && hi < 8.0 && hi - lo > 0.5, what);
    }

    // Short strokes, no rattle at a note's start: A4, 0.06 and 0.12 s
    // strokes, Hardness 0.55 and 0.8, the first 400 ms's roughest 20 ms
    // (each period against the one before) no rougher than the notes' later
    // changes (2 dB). And at Life 0 the strokes in tune with each other
    // (0.25 s strokes: each one's pitch within 6 cents of the others).
    {
        double first = -1e9, later = -1e9, spread = 0;
        auto rough = [](const std::vector<float>& x, int ms) {
            const int P = 109; // (A4's period)
            double    e = 0, d = 0;
            for(int i = ms * 48; i < ms * 48 + 960; i++)
                e += static_cast<double>(x[i]) * x[i], d += static_cast<double>(x[i + P] - x[i]) * (x[i + P] - x[i]);
            return 10 * log10(d / 2 / (e + 1e-30) + 1e-30);
        };
        for(float st : {0.06f, 0.12f})
            for(float hard : {0.55f, 0.8f})
            {
                phymo::Synth s;
                Fresh(s, 1.f / 3.f);
                s.SetHardness(hard), s.SetStrength(0.75f), s.SetNoise(0.1f), s.SetPosition(0.3f), s.SetBrightness(0.62f);
                s.SetType(0.75f), s.SetDecay(4.f), s.SetAttack(0.03f), s.SetLife(0.f), s.SetBowStroke(st, 0.5f);
                s.NoteOn(69, false, 0.9f);
                std::vector<float> x;
                Play(s, 1400, x);
                for(int ms = 60; ms < 400; ms += 20) // (past the note's own start)
                    first = fmax(first, rough(x, ms));
                for(int ms = 600; ms < 1300; ms += 20)
                    later = fmax(later, rough(x, ms));
            }
        {
            phymo::Synth s;
            Fresh(s, 1.f / 3.f);
            s.SetHardness(0.55f), s.SetStrength(0.75f), s.SetNoise(0.1f), s.SetPosition(0.3f), s.SetBrightness(0.62f);
            s.SetType(0.75f), s.SetDecay(4.f), s.SetAttack(0.03f), s.SetLife(0.f), s.SetBowStroke(0.25f, 0.5f);
            s.NoteOn(69, false, 0.9f);
            std::vector<float> x;
            Play(s, 4000, x);
            const double per = 0.25 + 0.008; // (a stroke and its turn)
            double       lo = 1e9, hi = -1e9;
            for(double t = 0.25 + 0.004; t + per < 3.9; t += per)
            {
                const double c = Cents(x, static_cast<size_t>((t + 0.03) * kSr), static_cast<size_t>((per - 0.06) * kSr), 440.0, 30);
                lo = fmin(lo, c), hi = fmax(hi, c);
            }
            spread = hi - lo;
        }
        std::snprintf(what, sizeof what,
                      "short strokes: a note's first changes at most %.1f dB rough, its later ones %.1f; at Life 0 the strokes within "
                      "%.1f cents", first, later, spread);
        Check(first < later + 2.0 && spread < 6.0, what);
    }

    // The flageolet (CHOMPI + MODEL knob 2): a light finger at 1/n of the
    // string leaves n's harmonics, the rest gone: A3, at each of 1/2 to
    // 1/6, the harmonics not n's at least 30 dB under the strongest (1/6 the least), the
    // strongest one of n's, the level within 10 dB of no touch.
    {
        auto play = [&](float touch, std::vector<float>& x) {
            phymo::Synth s;
            Fresh(s, 1.f / 3.f);
            s.SetHardness(0.5f), s.SetStrength(0.75f), s.SetNoise(0.08f), s.SetPosition(0.3f), s.SetType(0.75f);
            s.SetAttack(0.05f), s.SetFlageolet(touch);
            s.NoteOn(57, false, 0.9f);
            Play(s, 2500, x);
        };
        auto amp = [&](const std::vector<float>& x, int h) {
            double re = 0, im = 0;
            for(int i = 0; i < 8192; i++)
            {
                const double w = (0.5 - 0.5 * cos(6.283185307 * i / 8192)) * x[72000 + i], ph = 6.283185307 * h * 220.0 * i / kSr;
                re += w * cos(ph), im -= w * sin(ph);
            }
            return sqrt(re * re + im * im);
        };
        std::vector<float> open;
        play(0.f, open);
        const double lvl0 = RmsDb(open, 1500, 400);
        double       worst_other = -1e9, worst_lvl = 0;
        bool         ok          = true;
        for(int n = 2; n <= 6; n++)
        {
            std::vector<float> x;
            play(phymo::Synth::kFlagOff + (n - 2 + 0.5f) * (1.f - phymo::Synth::kFlagOff) / 5.f, x);
            double a[9], mx = 0;
            int    top = 0;
            for(int h = 1; h <= 8; h++)
                if((a[h] = amp(x, h)) > mx)
                    mx = a[h], top = h;
            ok = ok && top % n == 0;
            for(int h = 1; h <= 8; h++)
                if(h % n)
                    worst_other = fmax(worst_other, 20 * log10(a[h] / mx + 1e-12));
            worst_lvl = fmax(worst_lvl, fabs(RmsDb(x, 1500, 400) - lvl0));
        }
        std::snprintf(what, sizeof what,
                      "the flageolet: at 1/2 to 1/6, the harmonics not touched's at most %.0f dB under the strongest%s; the level "
                      "within %.1f dB", worst_other, ok ? "" : " (the strongest NOT n's)", worst_lvl);
        Check(ok && worst_other < -30.0 && worst_lvl < 10.0, what); // (quieter up high, as a real one: the fundamental's gone)
    }

    // Sympathetic strings (Stretch, Bowed): up, a violin's open strings: a
    // G3 bowed and let go rings on (0.3 s after, within 15 dB of it held),
    // a G#3 dies (at least 25 dB lower than G3's ring); held, at most 6 dB
    // louder than without; at none, nothing rings.
    {
        auto run = [&](int note, float stretch, double& held, double& after) {
            phymo::Synth s;
            Fresh(s, 1.f / 3.f);
            s.SetHardness(0.5f), s.SetStrength(0.75f), s.SetPosition(0.3f), s.SetType(0.75f), s.SetDamper(0.15f);
            s.SetAttack(0.05f), s.SetStretch(0.5f + stretch);
            s.NoteOn(note, false, 0.9f);
            std::vector<float> x;
            Play(s, 1500, x);
            s.NoteOff(note, false);
            Play(s, 600, x);
            held = RmsDb(x, 1200, 100), after = RmsDb(x, 1800, 100);
        };
        double h0, a0, h1, a1, h2, a2;
        run(55, 0.f, h0, a0), run(55, 0.5f, h1, a1), run(56, 0.5f, h2, a2);
        std::snprintf(what, sizeof what,
                      "sympathetic strings: G3 rings on (%.0f dB 0.3 s after, held %.0f), G#3 dies (%.0f); held %+.1f dB; none, %.0f",
                      a1, h1, a2, h1 - h0, a0);
        Check(a1 > h1 - 15.0 && a2 < a1 - 25.0 && h1 - h0 < 6.0 && a0 < -100.0, what);
    }

    // No loop in a held note: by the bridge, a short Ring, pressed hard (where
    // a held note pumped, every 0.3 to 0.6 s, 6 to 10 dB: the catch forced
    // again on a string that was sounding), its level (each window a whole
    // number of periods, about 20 ms, 1 to 8 s) within 1.5 dB.
    {
        struct Case { float bow, press, speed; int note; };
        const Case cases[] = {{1.f / 3.f, 0.8f, 0.8f, 48}, {1.f / 3.f, 0.5f, 0.8f, 48}, {2.f / 3.f, 0.8f, 0.3f, 36}, {0.f, 0.8f, 0.8f, 36}}; // (to the clean range's top: past it, the scratch)
        double worst = 0;
        int    at    = 0;
        for(const auto& c : cases)
        {
            phymo::Synth s;
            Fresh(s, c.bow);
            s.SetHardness(c.press), s.SetStrength(c.speed), s.SetPosition(0.1f), s.SetDecay(0.1f * powf(200.f, 0.3f));
            s.SetNoise(0.1f), s.SetType(0.3f), s.SetLife(0.2f), s.SetBrightness(0.f), s.SetKeyTrack(0.5f); // (the panel's init)
            s.NoteOn(c.note, false, 0.8f);
            std::vector<float> x;
            Play(s, 8000, x);
            const double P = kSr / (440.0 * pow(2.0, (c.note - 69) / 12.0));
            const int    N = static_cast<int>(P * fmax(1.0, floor(960.0 / P)) + 0.5);
            double       lo = 1e9, hi = -1e9;
            for(size_t w = 48000; w + N < x.size(); w += 960)
            {
                double e = 0;
                for(int i = 0; i < N; i++)
                    e += static_cast<double>(x[w + i]) * x[w + i];
                const double db = 10 * log10(e / N + 1e-30);
                lo = fmin(lo, db), hi = fmax(hi, db);
            }
            if(hi - lo > worst)
                worst = hi - lo, at = c.note;
        }
        std::snprintf(what, sizeof what, "no loop held, by the bridge, a short Ring, pressed: within %.1f dB (note %d)", worst, at);
        Check(worst < 1.5, what);
    }

    // Done once let go: a bowed note at any Type (above its middle, no
    // bite: its strike's pulse once never ran out, and the voice was never
    // done, costing as much as a held one) silent and its voice free 3 s on.
    {
        int left = 0;
        for(float type : {0.2f, 0.7f, 1.f})
            for(float bow : {0.f, 1.f / 3.f, 1.f})
            {
                phymo::Synth s;
                Fresh(s, bow);
                s.SetType(type);
                for(int n : {48, 55, 60, 64})
                    s.NoteOn(n);
                std::vector<float> x;
                Play(s, 1000, x);
                for(int n : {48, 55, 60, 64})
                    s.NoteOff(n);
                Play(s, 3000, x);
                left += s.ActiveVoices();
            }
        std::snprintf(what, sizeof what, "let go, every bow and Type: %d voices still on 3 s later", left);
        Check(left == 0, what);
    }

    // Strokes (1.5 s each way, full weight): at each turn the bow's speed
    // passes through zero (the bow all but lifted), so a short dip (its 20
    // ms low under 14 dB: a bow change's articulation) and the grip's scrape;
    // 0.2 s on, it's back (within 1.5 dB of before) and in tune.
    // Life: a note starts a little off and settles, and drifts only a
    // few cents.
    {
        phymo::Synth s;
        Fresh(s, 1.f / 3.f);
        s.SetHardness(0.55f), s.SetStrength(0.75f), s.SetPosition(0.35f), s.SetNoise(0.08f);
        s.SetBowStroke(1.5f, 1.f);
        s.NoteOn(55);
        std::vector<float> x;
        Play(s, 3600, x);
        double dip = 0;
        for(int ms = 1450; ms < 1650; ms += 10)
            dip = fmin(dip, RmsDb(x, ms, 20) - RmsDb(x, 1350, 50));
        const double back = RmsDb(x, 1750, 100) - RmsDb(x, 1350, 50);
        const double tune = Cents(x, 1750 * 48, 9600, 195.9977);
        bool         nan  = false;
        for(float v : x)
            nan = nan || !std::isfinite(v);
        std::snprintf(what, sizeof what, "a bow stroke's turn: dips %.1f dB, 0.2 s on %+.1f dB, %+.1f cents%s", dip, back,
                      tune, nan ? ", NaN" : "");
        Check(dip < -1.0 && dip > -14.0 && fabs(back) < 1.5 && fabs(tune) < 3.0 && !nan, what);

        // and at every octave, C3 to C7, a turn's dip under 10 dB (its
        // speed through zero over 50 ms, the string sagged 10 to 25: a pump)
        double worst_dip = 0;
        int    at        = 0;
        for(int note : {48, 60, 72, 84, 96})
        {
            phymo::Synth t;
            Fresh(t, 1.f / 3.f);
            t.SetHardness(0.55f), t.SetStrength(0.55f), t.SetPosition(0.35f), t.SetNoise(0.08f);
            t.SetBowStroke(1.5f, 0.5f);
            t.NoteOn(note);
            std::vector<float> y;
            Play(t, 3600, y);
            for(int ms = 1450; ms < 1650; ms += 10)
                if(RmsDb(y, ms, 20) - RmsDb(y, 1350, 50) < worst_dip)
                    worst_dip = RmsDb(y, ms, 20) - RmsDb(y, 1350, 50), at = note;
        }
        std::snprintf(what, sizeof what, "a turn's dip, C3 to C7: at most %.1f dB (note %d)", -worst_dip, at);
        Check(worst_dip > -10.0, what);

        double worst = 0, start = 0;
        for(int k = 0; k < 6; k++)
        {
            phymo::Synth t;
            Fresh(t, 1.f / 3.f);
            t.SetLife(1.f), t.SetHardness(0.55f), t.SetStrength(0.75f);
            for(int j = 0; j < k; j++) // (other notes first: each its own draw)
                t.NoteOn(50), Play(t, 30, x), t.NoteOff(50), Play(t, 300, x);
            std::vector<float> y;
            t.NoteOn(55);
            Play(t, 1500, y);
            start = fmax(start, fabs(Cents(y, 2400, 2400, 195.9977, 30)));
            worst = fmax(worst, fabs(Cents(y, 48000, 19200, 195.9977, 30)));
        }
        std::snprintf(what, sizeof what, "Life: a note starts up to %.1f cents off, held within %.1f", start, worst);
        Check(start < 15.0 && worst < 6.0, what);
    }

    // A heavy bow stays clean: the top of the clean range (Hardness 0.8:
    // past it, the scratch), rosin and horsehair, slow and
    // quick, by the bridge to over the board, C3 to C6: the held tone's
    // harmonic-to-noise (each 20 ms against itself a period on) over 22 dB
    // (it crunched into a dusty, raucous motion: 10 to 20).
    {
        double worst = 1e9;
        char   where[80] = "";
        for(float bow : {0.f, 1.f / 3.f})
            for(float speed : {0.3f, 0.8f})
                for(float pos : {0.1f, 0.3f, 0.7f})
                    for(int note : {48, 60, 72, 84})
                    {
                        phymo::Synth s;
                        Fresh(s, bow);
                        s.SetAttack(0.f), s.SetDecay(2.4f), s.SetBrightness(0.5f), s.SetHardness(0.8f), s.SetStrength(speed);
                        s.SetNoise(0.1f), s.SetPosition(pos);
                        s.NoteOn(note, false, 0.8f);
                        std::vector<float> x;
                        Play(s, 1500, x);
                        const double P = kSr / (440.0 * pow(2.0, (note - 69) / 12.0));
                        const double hnr = Hnr(x, 24000, 72000, P);
                        if(hnr < worst)
                            worst = hnr, std::snprintf(where, sizeof where, "%s, speed %.1f, place %.1f, note %d",
                                                       bow == 0.f ? "rosin" : "horsehair", speed, pos, note);
                    }
        std::snprintf(what, sizeof what, "full pressure stays clean: the held tone at least %.1f dB over its noise (%s)",
                      worst, where);
        Check(worst > 22.0, what);
    }

    // The crunch: heavy, the bow digs in at the start, a scratch like a
    // hammer's or a pluck's, then the tone clean: at Hardness 0.8 (the top
    // of the clean range: past it, the scratch held) the
    // start (20 to 105 ms) rougher than at a light 0.3 (more of it
    // between the harmonics, C3 to C5: 4 dB on average, without the
    // crunch about the same; the high notes' least, settling quickest), and the
    // held tone (from 0.75 s) clean either way.
    {
        double dig = 1e9, mean = 0, held = -1e9;
        for(float bow : {0.f, 1.f / 3.f})
            for(int note : {48, 60, 72})
            {
                double start[2];
                for(int h = 0; h < 2; h++)
                {
                    phymo::Synth s;
                    Fresh(s, bow);
                    s.SetAttack(0.f), s.SetDecay(2.4f), s.SetBrightness(0.5f), s.SetHardness(h ? 0.3f : 0.8f);
                    s.SetStrength(0.8f), s.SetNoise(0.1f), s.SetPosition(0.3f);
                    s.NoteOn(note, false, 0.8f);
                    std::vector<float> x;
                    Play(s, 1000, x);
                    const double f0 = 440.0 * pow(2.0, (note - 69) / 12.0);
                    start[h]        = OffHarm(x, 960, f0);
                    held            = fmax(held, OffHarm(x, 36000, f0));
                }
                dig = fmin(dig, start[0] - start[1]), mean += (start[0] - start[1]) / 6;
            }
        std::snprintf(what, sizeof what,
                      "the crunch: a heavy start %.1f dB rougher than a light one (at least %.1f); held, at most %.1f dB "
                      "between the harmonics", mean, dig, held);
        Check(mean > 2.5 && dig > -2.0 && held < -33.0, what); // (each synth's voices their own random draws: the mean and least vary)
    }

    // The scratch: past Hardness 0.8, the bow ground in, held (not only at
    // the start): at full, rosin and horsehair, by the bridge to over the
    // board, G2 to G5, the held tone (1 to 3 s) at least 10 dB more of it
    // between the harmonics than at 0.8, and no loop in it: its level (in
    // windows a whole number of periods) wanders, as a scratch does, but
    // under 6 dB and with no repeat (its strongest under 0.85 of its
    // spread: the loops this guards against, 0.9 and more; a scratch's
    // wander, from its random draws, up to ~0.75).
    {
        double least = 1e9, swing = 0, repeat = 0;
        for(float bow : {0.f, 1.f / 3.f})
            for(float pos : {0.1f, 0.5f, 0.9f})
                for(int note : {43, 55, 67, 79})
                {
                    double off[2];
                    for(int h = 0; h < 2; h++)
                    {
                        phymo::Synth s;
                        Fresh(s, bow);
                        s.SetAttack(0.f), s.SetDecay(4.f), s.SetBrightness(0.6f), s.SetHardness(h ? 1.f : 0.8f);
                        s.SetStrength(0.8f), s.SetNoise(0.3f), s.SetPosition(pos), s.SetType(0.6f);
                        s.NoteOn(note, false, 0.9f);
                        std::vector<float> x;
                        Play(s, 3000, x);
                        const double f0 = 440.0 * pow(2.0, (note - 69) / 12.0);
                        off[h]          = (OffHarm(x, 48000, f0) + OffHarm(x, 96000, f0)) / 2;
                        if(h)
                        {
                            const double P = kSr / f0;
                            const int    N = static_cast<int>(P * fmax(1.0, floor(960.0 / P)) + 0.5);
                            std::vector<double> v;
                            for(size_t w = 48000; w + N < x.size(); w += 960)
                            {
                                double e = 0;
                                for(int i = 0; i < N; i++)
                                    e += static_cast<double>(x[w + i]) * x[w + i];
                                v.push_back(10 * log10(e / N + 1e-30));
                            }
                            double m = 0, lo = 1e9, hi = -1e9, var = 0;
                            for(double y : v)
                                m += y / v.size(), lo = fmin(lo, y), hi = fmax(hi, y);
                            for(double y : v)
                                var += (y - m) * (y - m);
                            for(int lag = 3; lag < 40; lag++) // (a peak in its autocorrelation, 60 to 800 ms)
                            {
                                double c[3] = {0, 0, 0};
                                for(int j = 0; j < 3; j++)
                                    for(size_t i = 0; i + lag + 1 < v.size(); i++)
                                        c[j] += (v[i] - m) * (v[i + lag - 1 + j] - m);
                                if(c[1] >= c[0] && c[1] >= c[2])
                                    repeat = fmax(repeat, c[1] / (var + 1e-30));
                            }
                            swing = fmax(swing, hi - lo);
                        }
                    }
                    least = fmin(least, off[1] - off[0]);
                }
        std::snprintf(what, sizeof what, "the scratch: at full Hardness, held, at least %.1f dB more between the harmonics than "
                      "at 0.8; its level within %.1f dB, its strongest repeat %.2f", least, swing, repeat);
        Check(least > 10.0 && swing < 6.0 && repeat < 0.85, what);
    }

    // A light bow speaks: no pressure to a little, the Ring short to long,
    // the bow by the bridge to over the board, every note C2 to C5 its
    // own velocity: each one sounding (the string not left sliding
    // steadily under the bow, nor too lossy for it to drive).
    {
        int    quiet = 0, total = 0;
        double worst = 0;
        for(float press : {0.f, 0.06f, 0.12f})
            for(float ring : {0.1f, 0.3f, 2.4f})
                for(float pos : {0.4f, 0.75f, 1.f})
                {
                    phymo::Synth s;
                    Fresh(s, 0.6f);
                    s.SetHardness(press), s.SetDecay(ring), s.SetPosition(pos), s.SetStrength(0.5f), s.SetNoise(0.4f);
                    std::vector<float> x;
                    for(int n = 36; n <= 72; n += 7)
                    {
                        x.clear();
                        s.NoteOn(n, false, 0.3f + 0.1f * static_cast<float>((n * 7) % 7));
                        Play(s, 300, x);
                        s.NoteOff(n);
                        std::vector<float> y;
                        Play(s, 60, y);
                        const double db = RmsDb(x, 60, 240);
                        worst = fmin(worst, db);
                        quiet += db < -60.0, total++;
                    }
                }
        std::snprintf(what, sizeof what, "a light bow speaks: %d of %d notes quiet (under -60 dB), the quietest %.0f dB",
                      quiet, total, worst);
        Check(quiet == 0, what);
    }

    // The release: let go, the bow lifts and the damper stops the string.
    {
        phymo::Synth s;
        Fresh(s, 1.f / 3.f);
        s.SetDamper(0.2f);
        s.NoteOn(48);
        std::vector<float> x;
        Play(s, 1000, x);
        s.NoteOff(48);
        Play(s, 600, x);
        const double d = RmsDb(x, 1450, 50) - RmsDb(x, 900, 100);
        std::snprintf(what, sizeof what, "the release: 0.45 s after letting go, %.0f dB", d);
        Check(d < -40.0, what);
    }

    // Brighter: heavier pressure, and bowing by the bridge.
    {
        auto highs = [](float hardness, float position) {
            phymo::Synth s;
            Fresh(s, 1.f / 3.f);
            s.SetHardness(hardness), s.SetPosition(position);
            s.NoteOn(48);
            std::vector<float> x;
            Play(s, 1500, x);
            return Highs(x, 1000, 400);
        };
        const double press = highs(1.f, 0.3f) / highs(0.f, 0.3f), place = highs(0.5f, 0.f) / highs(0.5f, 1.f);
        std::snprintf(what, sizeof what, "heavier pressure %.2fx the highs of light; by the bridge %.2fx over the board", press, place);
        Check(press > 1.15 && place > 1.05, what); // (by the bridge only a little brighter: the curve's steeper over the board)
    }

    // Eight voices, every bow, pressed hard and fast: bounded.
    for(int b = 0; b < 4; b++)
    {
        phymo::Synth s;
        s.Init(kSr);
        s.SetKeyMode(phymo::KeyMode::Poly1), s.SetVolume(1.f), s.SetModel(Model::Bow), s.SetTable(b / 3.f);
        s.SetStrength(1.f), s.SetHardness(1.f), s.SetNoise(1.f), s.SetBrightness(1.f), s.SetType(0.f), s.SetAttack(0.002f);
        std::vector<float> x;
        for(int n = 0; n < 8; n++)
            s.NoteOn(36 + 5 * n, false, 1.f), Play(s, 20, x);
        Play(s, 1500, x);
        float peak = 0.f;
        bool  nan  = false;
        for(float v : x)
            peak = fmaxf(peak, fabsf(v)), nan = nan || std::isnan(v);
        std::snprintf(what, sizeof what, "%s: eight voices, hard and fast: peak %.2f, no NaN", name[b], peak);
        Check(!nan && peak <= 1.f, what);
    }

    // Renders: each bow on a slow line, then a held chord swelling in.
    for(int b = 0; b < 4; b++)
    {
        phymo::Synth s;
        s.Init(kSr);
        s.SetKeyMode(phymo::KeyMode::Poly1), s.SetVolume(0.8f), s.SetModel(Model::Bow), s.SetTable(b / 3.f);
        s.SetDecay(4.f), s.SetDamper(0.6f), s.SetBrightness(0.55f), s.SetType(0.45f), s.SetAttack(0.002f * powf(1000.f, 0.45f)), s.SetLife(0.3f);
        s.SetPickup(phymo::PickupMode::Wood), s.SetPickupAmount(0.5f);
        std::vector<float> x;
        for(int n : {55, 57, 59, 62, 60, 59, 57, 55})
            s.NoteOn(n), Play(s, 600, x), s.NoteOff(n), Play(s, 60, x);
        s.SetType(0.75f), s.SetAttack(0.002f * powf(1000.f, 0.75f));
        for(int n : {43, 50, 55, 59})
            s.NoteOn(n), Play(s, 120, x);
        Play(s, 3500, x);
        for(int n : {43, 50, 55, 59})
            s.NoteOff(n);
        Play(s, 1500, x);
        char file[64];
        std::snprintf(file, sizeof file, "out/bow_%s.wav", b == 3 ? "ebow" : name[b]);
        WriteWav(file, x, x, static_cast<uint32_t>(kSr));
        std::printf("      wrote %s\n", file);
    }

    // A violin and a cello: horsehair, the violin body (the cello's at Tone
    // 0), a bow's stroke (no bite, a 120 ms swell), the vibrato in after
    // 0.3 s; a line, then a held double stop.
    for(int cello = 0; cello < 2; cello++)
    {
        phymo::Synth s;
        s.Init(kSr);
        s.SetKeyMode(phymo::KeyMode::Poly1), s.SetVolume(0.9f), s.SetModel(Model::Bow), s.SetTable(1.f / 3.f);
        s.SetDecay(4.f), s.SetDamper(0.25f), s.SetBrightness(0.62f), s.SetType(0.7f), s.SetLife(0.25f);
        s.SetHardness(0.55f), s.SetStrength(0.75f), s.SetPosition(0.35f), s.SetNoise(0.08f);
        s.SetAttack(0.12f), s.SetVibrato(5.6f, 0.42f, 0.6f, 0.4f); // (a bow's stroke: no bite, a 120 ms swell)
        s.SetPickup(phymo::PickupMode::Violin), s.SetPickupAmount(0.85f), s.SetPickupTone(cello ? 0.f : 0.5f);
        s.SetSpread(0.4f);
        const int base = cello ? 36 : 55; // C2, G3
        std::vector<float> x;
        for(int n : {0, 2, 4, 5, 7, 9, 7, 5, 4, 2})
            s.NoteOn(base + 12 + n), Play(s, n == 7 ? 900 : 420, x), s.NoteOff(base + 12 + n), Play(s, 40, x);
        s.NoteOn(base + 7), s.NoteOn(base + 14);
        Play(s, 3000, x);
        s.NoteOff(base + 7), s.NoteOff(base + 14);
        Play(s, 1200, x);
        const char* file = cello ? "out/bow_cello.wav" : "out/bow_violin.wav";
        WriteWav(file, x, x, static_cast<uint32_t>(kSr));
        std::printf("      wrote %s\n", file);
    }

    std::printf(g_fails ? "\n%d FAILED\n" : "\nall passed\n", g_fails);
    return g_fails ? 1 : 0;
}

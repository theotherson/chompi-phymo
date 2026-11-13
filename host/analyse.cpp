// Analyses a recording of an effect (a reverb, mostly) for fitting a model:
//   make -C host analyse && host/analyse <file.wav> [frame_ms]
//
// Prints, a frame at a time: each side's level (dB), the main frequency (from
// the autocorrelation's peak), how alike the two sides are (correlation: 1
// mono, 0 unrelated, below 0 opposite), and the level of the 2nd and 3rd
// harmonics of that frequency (distortion). Then the decays: each stretch
// where the level falls steadily for at least 0.3 s, its rate as a time to
// fall 60 dB (RT60), at the frequency playing just before.
#include "wav.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

static float Db(double x) { return x > 1e-12 ? 10.f * static_cast<float>(log10(x)) : -120.f; }

/** The strongest period between 40 Hz and 2 kHz in `x` (autocorrelation), Hz; 0 if none clear. */
static float Pitch(const float* x, size_t n, float sr)
{
    const size_t lo = static_cast<size_t>(sr / 2000.f), hi = std::min(n / 2, static_cast<size_t>(sr / 40.f));
    double       e0 = 0.0;
    for(size_t i = 0; i < n; i++)
        e0 += x[i] * x[i];
    if(e0 < 1e-9)
        return 0.f;
    double best = 0.0;
    size_t lag  = 0;
    std::vector<double> c(hi + 2, 0.0);
    for(size_t k = lo; k <= hi + 1; k++)
    {
        double s = 0.0;
        for(size_t i = 0; i + k < n; i++)
            s += x[i] * x[i + k];
        c[k] = s / e0 * n / (n - k);
    }
    // the first lag past the first dip whose correlation is near the best
    for(size_t k = lo + 1; k <= hi; k++)
        if(c[k] > best)
            best = c[k], lag = k;
    if(best < .3)
        return 0.f;
    for(size_t k = lo + 1; k <= hi; k++)
        if(c[k] > c[k - 1] && c[k] >= c[k + 1] && c[k] > .85 * best)
        {
            lag = k;
            break;
        }
    // parabolic interpolation round the peak
    const double a = c[lag - 1], b = c[lag], d = c[lag + 1];
    const double off = (a - d) / (2.0 * (a - 2.0 * b + d));
    return static_cast<float>(sr / (lag + (std::isfinite(off) ? off : 0.0)));
}

/** The level of frequency `hz` in `x` (a single-bin DFT, Hann window), as amplitude. */
static double Bin(const float* x, size_t n, float sr, float hz)
{
    double re = 0.0, im = 0.0, w = 0.0;
    for(size_t i = 0; i < n; i++)
    {
        const double h = .5 - .5 * cos(2.0 * M_PI * i / (n - 1));
        const double p = 2.0 * M_PI * hz * i / sr;
        re += x[i] * h * cos(p), im += x[i] * h * sin(p), w += h;
    }
    return 2.0 * sqrt(re * re + im * im) / w;
}

int main(int argc, char** argv)
{
    if(argc < 2)
    {
        printf("usage: analyse <file.wav> [frame_ms]\n");
        return 1;
    }
    std::vector<float> l, r;
    const uint32_t     sr = ReadWav(argv[1], l, r);
    if(!sr)
    {
        printf("can't read %s\n", argv[1]);
        return 1;
    }
    const float  frame_ms = argc > 2 ? static_cast<float>(atof(argv[2])) : 100.f;
    const size_t frame    = static_cast<size_t>(frame_ms * .001f * sr);
    printf("%s\n%u Hz, %.2f s, frames of %.0f ms\n", argv[1], sr, l.size() / static_cast<float>(sr), frame_ms);
    printf("%7s %7s %7s %8s %6s %6s %6s\n", "t (s)", "L dB", "R dB", "Hz", "corr", "H2 dB", "H3 dB");

    std::vector<float> lev, hz;
    for(size_t p = 0; p + frame <= l.size(); p += frame)
    {
        double el = 0.0, er = 0.0, lr = 0.0;
        std::vector<float> m(frame);
        for(size_t i = 0; i < frame; i++)
        {
            el += l[p + i] * l[p + i], er += r[p + i] * r[p + i], lr += l[p + i] * r[p + i];
            m[i] = .5f * (l[p + i] + r[p + i]);
        }
        const float  f    = Pitch(m.data(), frame, sr);
        const double corr = el > 1e-12 && er > 1e-12 ? lr / sqrt(el * er) : 0.0;
        float        h2 = -120.f, h3 = -120.f;
        if(f > 0.f)
        {
            const double h1 = Bin(m.data(), frame, sr, f);
            h2              = Db(pow(Bin(m.data(), frame, sr, 2.f * f) / h1, 2.0));
            h3              = Db(pow(Bin(m.data(), frame, sr, 3.f * f) / h1, 2.0));
        }
        const float dl = Db(el / frame), dr = Db(er / frame);
        lev.push_back(std::max(dl, dr));
        hz.push_back(f);
        printf("%7.2f %7.1f %7.1f %8.1f %6.2f %6.0f %6.0f\n", p / static_cast<float>(sr), dl, dr, f, corr, h2, h3);
    }

    // the decays: stretches falling steadily (each frame lower than the last, give or take 1 dB)
    printf("\ndecays (falling >= 0.3 s, from above -60 dB):\n");
    const size_t min_frames = static_cast<size_t>(300.f / frame_ms + .5f);
    for(size_t i = 1; i < lev.size();)
    {
        size_t j = i;
        while(j < lev.size() && lev[j] < lev[j - 1] + 1.f && lev[j] > -90.f)
            j++;
        if(j - i >= min_frames && lev[i - 1] > -60.f && lev[i - 1] - lev[j - 1] > 6.f)
        {
            // a straight line through the stretch (least squares), dB per second
            double sx = 0, sy = 0, sxx = 0, sxy = 0;
            const double nn = static_cast<double>(j - i + 1);
            for(size_t k = i - 1; k < j; k++)
            {
                const double x = k * frame_ms * .001, y = lev[k];
                sx += x, sy += y, sxx += x * x, sxy += x * y;
            }
            const double slope = (nn * sxy - sx * sy) / (nn * sxx - sx * sx);
            printf("  %6.2f s to %6.2f s: %5.1f dB to %5.1f dB, %6.1f dB/s, RT60 %5.2f s, at %7.1f Hz\n",
                   (i - 1) * frame_ms * .001f, (j - 1) * frame_ms * .001f, lev[i - 1], lev[j - 1], slope,
                   slope < 0 ? -60.0 / slope : 0.0, hz[i - 1]);
            i = j + 1;
        }
        else
            i = j + 1;
    }

    // the last tail: from the last note's end (the last frame within 12 dB of its level that the
    // level then falls from for good) down to the floor, in 10 ms frames: T20 (-5 to -25 dB) and
    // T30 (-5 to -35 dB) as RT60s, and how the two sides and the pitch behave in it
    {
        const size_t f10 = sr / 100;
        std::vector<float> e;
        for(size_t p = 0; p + f10 <= l.size(); p += f10)
        {
            double s2 = 0;
            for(size_t i = 0; i < f10; i++)
                s2 += .5 * (l[p + i] * l[p + i] + r[p + i] * r[p + i]);
            e.push_back(Db(s2 / f10));
        }
        // the last frame above -60 dB, then back to where the fall starts (the last local peak before it)
        size_t end = e.size();
        while(end > 0 && e[end - 1] < -60.f)
            end--;
        if(end > 10)
        {
            size_t start = end - 1;
            while(start > 0 && e[start - 1] >= e[start] - .5f)
                start--;
            // its peak: the highest point in the 300 ms before the fall
            size_t pk = start;
            for(size_t k = start >= 30 ? start - 30 : 0; k <= start; k++)
                if(e[k] > e[pk])
                    pk = k;
            const float top = e[pk];
            auto at = [&](float below) {
                for(size_t k = pk; k < e.size(); k++)
                    if(e[k] <= top - below)
                        return static_cast<float>(k - pk) * .01f;
                return -1.f;
            };
            const float t5 = at(5.f), t25 = at(25.f), t35 = at(35.f);
            printf("\nlast tail: from %.2f s at %.1f dB\n", pk * .01f, top);
            if(t5 >= 0.f && t25 >= 0.f)
                printf("  T20 (-5 to -25 dB): %.2f s -> RT60 %.2f s\n", t25 - t5, 3.f * (t25 - t5));
            if(t5 >= 0.f && t35 >= 0.f)
                printf("  T30 (-5 to -35 dB): %.2f s -> RT60 %.2f s\n", t35 - t5, 2.f * (t35 - t5));
            printf("  first 5 dB: %.3f s\n", t5);
            // the tail in 50 ms steps: level, pitch, correlation
            printf("  %6s %7s %8s %6s\n", "t", "dB", "Hz", "corr");
            const size_t f50 = sr / 20;
            for(size_t p = pk * f10; p + f50 <= l.size() && p < (pk + 200) * f10; p += f50)
            {
                double el = 0, er = 0, lr = 0;
                std::vector<float> m(f50);
                for(size_t i = 0; i < f50; i++)
                {
                    el += l[p + i] * l[p + i], er += r[p + i] * r[p + i], lr += l[p + i] * r[p + i];
                    m[i] = .5f * (l[p + i] + r[p + i]);
                }
                const float lvl = Db(.5 * (el + er) / f50);
                if(lvl < -70.f)
                    break;
                printf("  %6.2f %7.1f %8.1f %6.2f\n", (p / f10 - pk) * .01f, lvl, Pitch(m.data(), f50, sr),
                       el > 0 && er > 0 ? lr / sqrt(el * er) : 0.0);
            }
        }
    }
    return 0;
}

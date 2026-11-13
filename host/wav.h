// WAV files for the desktop renders: a 16-bit stereo writer,
// and a reader for 16 / 24-bit PCM or 32-bit float, mono or stereo.
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

inline void WriteWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, uint32_t sr)
{
    FILE* f = fopen(path.c_str(), "wb");
    if(!f)
    {
        perror(path.c_str());
        exit(1);
    }
    const uint32_t n = static_cast<uint32_t>(l.size()), data = n * 4, br = sr * 4, riff = 36 + data, fmt = 16;
    const uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&sr, 4, 1, f), fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
    for(uint32_t i = 0; i < n; i++)
    {
        auto c = [](float v) { return static_cast<int16_t>((v > 1.f ? 1.f : v < -1.f ? -1.f : v) * 32767.f); };
        int16_t s[2] = {c(l[i]), c(r[i])};
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

/** Reads `path` into l / r (a mono file fills both). @return its sample rate, 0 if unreadable. */
inline uint32_t ReadWav(const std::string& path, std::vector<float>& l, std::vector<float>& r)
{
    FILE* f = fopen(path.c_str(), "rb");
    if(!f)
        return 0;
    char     id[4];
    uint32_t size = 0, sr = 0;
    uint16_t fmt = 0, ch = 0, bits = 0;
    bool     ok  = fread(id, 1, 4, f) == 4 && fread(&size, 4, 1, f) == 1 && fread(id, 1, 4, f) == 4;
    while(ok && fread(id, 1, 4, f) == 4 && fread(&size, 4, 1, f) == 1)
    {
        if(!memcmp(id, "fmt ", 4))
        {
            std::vector<uint8_t> b(size);
            if(fread(b.data(), 1, size, f) != size || size < 16)
                break;
            memcpy(&fmt, &b[0], 2), memcpy(&ch, &b[2], 2), memcpy(&sr, &b[4], 4), memcpy(&bits, &b[14], 2);
            if(fmt == 0xFFFE && size >= 26) // WAVE_FORMAT_EXTENSIBLE: the real format's in the GUID
                memcpy(&fmt, &b[24], 2);
        }
        else if(!memcmp(id, "data", 4))
        {
            const size_t bytes = bits / 8, frames = bytes ? size / (bytes * (ch ? ch : 1)) : 0;
            std::vector<uint8_t> b(size);
            if(!ch || fread(b.data(), 1, size, f) != size)
                break;
            l.resize(frames), r.resize(frames);
            for(size_t i = 0; i < frames; i++)
                for(int c = 0; c < (ch > 1 ? 2 : 1); c++)
                {
                    const uint8_t* p = &b[(i * ch + c) * bytes];
                    float          v = 0.f;
                    if(fmt == 3 && bits == 32)
                        memcpy(&v, p, 4);
                    else if(fmt == 1 && bits == 16)
                        v = static_cast<int16_t>(p[0] | p[1] << 8) / 32768.f;
                    else if(fmt == 1 && bits == 24)
                        v = static_cast<int32_t>(uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24) / 2147483648.f;
                    else
                    {
                        fclose(f);
                        return 0;
                    }
                    (c ? r : l)[i] = v;
                }
            if(ch == 1)
                r = l;
            fclose(f);
            return sr;
        }
        else
            fseek(f, (size + 1) & ~1u, SEEK_CUR);
    }
    fclose(f);
    return 0;
}

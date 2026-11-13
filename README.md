# PHYMO

An eight-voice physical-modelling synth firmware for the CHOMPI. Five models
(String, Spectral, FM, Bowed and Vocal), each struck, plucked, bowed or sung,
through pickups and bodies, a chorus, delay and reverb, with an arpeggiator,
strum and a step sequencer.

- **[MANUAL.md](MANUAL.md)**: the controls.
- **[presets/README.md](presets/README.md)**: the factory presets.
- **[THIRD_PARTY.md](THIRD_PARTY.md)**: credits and licences.
- **[LICENSE](LICENSE)**: MIT.

## Installing

Copy the `presets/` banks into `/PHYMO/` on the SD card, then load
`build/CHOMPI.bin` the way you load any custom CHOMPI firmware.

## Building

Needs the Arm GNU Embedded Toolchain 10.3-2021.10.

```bash
make -C code/libs/libDaisy
```

```bash
make -C code/src
```

The output is `code/src/build/CHOMPI.bin`. Build options (run `make clean`
first when changing them):

| Option | Effect |
| --- | --- |
| `REVERB=0` / `1` / `2` | Reverb at full rate, half rate, or half rate with steeper filters (the default, 2) |
| `ITCM=0` | Don't run the hot loops from the ITCM |
| `DIAG=1` | Development diagnostics: the CPU meter on the white keys (CHOMPI held), an overload flash, `PROFILE.txt` on each preset save, `CRASH.txt` after a crash |

## Desktop tests

The synth (`src/`) is plain C++ and builds on a desktop. `host/` holds its
tests and WAV renders:

```bash
make -C host
```

Each `host/*_test` prints its checks and writes renders to `host/out/`.

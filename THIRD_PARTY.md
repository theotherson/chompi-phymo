# Third-party notices

PHYMO is released under the MIT license in `LICENSE`. It started as a copy of HEXA
(chompi-hexa, MIT): its engine frame, sequencer, arp, effects, panel and tests. The work below is
included under its own license. These notices, and the copyright attributions in the source files,
must be kept in any redistribution.

## The physical models

From the published literature only:

- **The string** (`src/string_res.h`): Karplus & Strong, "Digital synthesis of plucked-string and drum
  timbres" (1983); Jaffe & Smith, "Extensions of the Karplus-Strong plucked-string algorithm" (1983),
  for the allpass tuning; Välimäki et al., "Physical modeling of plucked string instruments with
  application to real-time sound synthesis" (1996), for the loss filter set by decay times; Van Duyne
  & Smith, "A simplified approach to modeling dispersion caused by stiffness in strings and plates"
  (1994), for the stiffness.
- **The bowed string** (`src/bow_res.h`): Smith, "Efficient simulation of the reed-bore and
  bow-string mechanisms" (1986); McIntyre, Schumacher & Woodhouse, "On the oscillations of musical
  instruments" (1983).
- **The Spectral model** (`src/modal_res.h`, `src/tables.h`): modal synthesis (Adrien 1991; Smith,
  "Physical Audio Signal Processing"); the tables computed from Fletcher & Rossing, "The Physics of
  Musical Instruments".
- **The Vocal model** (`src/vocal.h`): Klatt, "Software for a cascade/parallel formant synthesizer"
  (1980); vowel formants from Peterson & Barney, "Control methods used in a study of the vowels"
  (1952).
- **The FM model** (`src/fm_core.h`): Chowning, "The synthesis of complex audio spectra by means of
  frequency modulation" (1973).
- **The BBD** (`src/fx/lib/bbd.h`): after Holters & Parker, "A combined model for a bucket brigade
  device and its input and output filters" (DAFx-18).
- **The state-variable filters** (`src/dsp_util.h`): Zavalishin, "The Art of VA Filter Design".

No code, tables or settings are taken from any commercial instrument or Max for Live device.

## HEXA's measurements (`src/chorus.h`)

- **pendragon-andyh/Juno60** (© 2015 Andy Harman and Pendragon Software Limited, MIT): the Juno-60
  chorus measurements HEXA's chorus follows, kept here as it is.

- **The LFO delay** (`code/src/panel.h`, `SetLfoDelay`) follows the Juno-60 LFO delay slider
  timings from the same measurements.

- **peterall/junologue-chorus** (© 2020 Peter Allwin, MIT): the chorus's I | II mode, I and II run in
  parallel, is its idea. No code is taken from it.

## The CHOMPI platform

- **The backup SRAM region** in `chompi_sram.lds` (so libDaisy reads the bootloader version
  correctly) is the fix from sfaber02/CHOMPI-MULTI-FIRMWARE.
- **The SD card buffer alignment** (`storage.h`, `chompi_main.cpp`), the codec start-up order and
  the 4021 button-edge handling (`panel.h`) follow fixes found in POLY.
- **The quadrature decoder** (`quadrature.h`) replaces TEMPO's encoder edge test.

The hardware layer (`code/src/hardware.h`, `encoder.*`, `quadrature.h`, `temp_led_stuff.h`,
`chompi_sram.lds`), the startup sequence in `chompi_main.cpp`, and `code/src/panel.h`'s structure come
from chompi-fx and chompi-x0x, which took them from CHOMPI Club's TEMPO firmware (MIT; CHOMPI Club,
now part of Chase Bliss). Electrosmith is credited as the designer of the hardware and the author of
the original firmware and platform. The key, LED and knob tables in `panel.h` are from POLY
(sfaber02/chompi-poly, MIT).

## TEMPO's arp randomness (`src/arp.h`)

The arpeggiator's randomness (a chance a step of a rest or an octave jump) is the idea of TEMPO's
arpeggiator (CHOMPI Club, MIT). No code is taken from it.

## TEMPO's output stage (`src/output.h`)

After CHOMPI Club's TEMPO (MIT): its output compressor (`limiter.h`'s `ProcessComp`, itself from
Emilie Gillet's stmlib, MIT) and soft clip, at TEMPO's default settings, rewritten for HEXA.

## PORTA's reverb and send bus (`src/fx/`, `src/space.h`)

Copied unchanged from chompi-fx (`code/src/fx/`), which ported it from PORTA (a reworked TAPE,
MIT): `models/quadraverb.h` (the reverb), with `effect.h` and `lib/bbd.h` (its 2-pole filter).
`src/space.h` follows PORTA's send bus (its space and send curves). Within them:

- **The reverb's engine** (`lib/fx_engine.h`) and its tank are Emilie Gillet's (Mutable Instruments:
  Clouds / Rings), MIT, as Electrosmith ported them and CHOMPI's TAPE ships them. The file carries
  the full original notice. Do not remove it.
- **`SoftClip`** in `effect.h` is DaisySP's (Electrosmith, MIT).

## Vendored libraries

| Component | Copyright | License | Where |
|---|---|---|---|
| **libDaisy** | © Electrosmith | MIT | `code/libs/libDaisy/`, with its `LICENSE` |

libDaisy is the CHOMPI adaptation vendored by TEMPO (via chompi-x0x and chompi-fx), not a stock
release: keep this copy rather than replacing it. chompi-x0x's `THIRD_PARTY.md` lists how it differs
from upstream v5.4.0.

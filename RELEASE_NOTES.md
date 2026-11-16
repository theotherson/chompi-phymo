# PHYMO 1.0

The first release of PHYMO: an eight-voice physical-modelling synth for the
CHOMPI. Every note is an exciter (a strike, a pluck, a bow or a voice)
setting a resonator ringing, heard through a pickup or body, then a chorus,
delay and reverb.

## Five models

- **String**: plucked and struck strings in steel, bronze, nylon, nylgut or
  gut, with stiffness, and an optional second string, detuned or tuned to an
  interval (twelve-string, harpsichord ranks).
- **Spectral**: up to 24 partials from nine tables (harmonic, odd, tuned bar,
  tine, reed, free bar, bell, plate, membrane), with Stretch, Snap, a morph
  between tables, and a tonebar for electric-piano bloom and beating.
- **FM**: a carrier and modulator pair scanning four ratio lists, with
  feedback and its own envelope.
- **Bowed**: a bowed string with rosin, horsehair, synthetic or e-bow;
  pressure, bow position, strokes, flageolet harmonics, sympathetic strings
  and a player-style vibrato.
- **Vocal**: a singer across eight vowels, with size, tension, breath,
  consonant attacks (d, k, h), a choir ensemble and tape wow and flutter.

## Sound shaping

- **Exciter**: hammer to pick, hardness, noise and strength, with Life
  (each strike a little different) and MIDI velocity.
- **Resonator**: attack, ring, damper and sustain for every model, with key
  tracking.
- **Body**: tine and reed pickups, a wooden body, a violin body, a vowel
  filter, pickup position along the string, and tremolo.
- **Modulation**: an LFO (six shapes, delay, sync, per-note or per-voice) and
  a per-note envelope, each to pitch, brightness, morph, exciter, tone and
  pickup position.
- **Effects**: a Juno-style chorus (I, II, I + II, I | II), a stereo delay
  with ping-pong, and a plate reverb, sharing one send.

## Playing

- Mono, two poly modes, unison and chord memory.
- Portamento, strum and glissando, cycle and the latch loop.
- An arpeggiator with five patterns, 1-4 octaves and randomness.
- A step sequencer: 32 steps of up to four notes, recording and step entry,
  per-step length and strum, swing, transpose, patterns and banks.
- Eight preset banks of 15 slots, with five factory banks: the models'
  range, instruments, bowed, electric pianos, and voices.
- MIDI over DIN and USB: notes, CCs, clock in and out.

## Installing

Copy the `presets/` banks into `/PHYMO/` on the SD card and load
`CHOMPI.bin` as you would any custom CHOMPI firmware. The controls are in
`MANUAL.md`.

## Known limits

Very heavy patches (many held Bowed notes with modulation and the sequencer)
can need more CPU than the CHOMPI has. PHYMO then fades out the oldest
voices rather than glitching, so some notes may drop out.

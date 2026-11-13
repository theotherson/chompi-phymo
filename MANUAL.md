# PHYMO manual

PHYMO is an eight-voice physical-modelling synth for the CHOMPI. Each note is
an exciter (a strike, a pluck or a bow) setting a resonator ringing, heard
through a pickup or body, then a chorus, delay and reverb.

There are five models:

| Model | Colour | What rings |
| --- | --- | --- |
| String | orange | A plucked or struck string: steel, bronze, nylon, nylgut or gut |
| Spectral | red | A bank of up to 24 partials: harmonic, odd, tuned bar, tine, reed, free bar, bell, plate, membrane |
| FM | teal | A sine carrier and a pair of modulators scanning a ratio list |
| Bowed | purple | A bowed string, with rosin, horsehair, synthetic or e-bow |
| Vocal | pink | A singer: a glottal source through formants, eight vowels |

## Playing

- **Keys** play notes, C3 at the lowest key plus the octave. Their LEDs show
  notes by pitch, including held and latched ones.
- **CHOMPI + low C# / D#**: octave down / up (-2 to +2).
- **CHOMPI + a black key** picks the view knobs 1-4 edit (below). The view
  stays until another is picked. Tapping some view keys again cycles a
  choice: the model (MODEL), the pickup (BODY), LFO / ENV.
- **Knobs 1-4** edit the view. Most have a second control under
  **CHOMPI + turn**. A knob's **click** resets it to its default; knobs with
  a click action of their own keep that, and **CHOMPI + click** resets them.
  Knobs accelerate when turned quickly.
- **Wheel**: brightness. **CHOMPI + wheel**: strike position.
  **Pressed + turn**: pitch bend. **CHOMPI + pressed + turn**: mod (right: LFO
  to brightness; left: LFO to pitch). Bend and mod return to centre on release.
- **Knob 6**: level, -48 dB to 0, fully down silent.
- **LOOP**: hold. Released keys keep sounding until hold is off or they're
  pressed again. With the arp off, a held note that has faded strikes again
  (the latch loop), each on its own decay.
- **CHOMPI + LOOP**: chord memory. The LOOP LED flashes teal; press keys to
  add or remove them from the chord, then CHOMPI + LOOP again to save it and
  switch to chord mode.
- **PLAY**: the arpeggiator on / off (green running, amber paused by MIDI
  stop). Its notes light red on the keys; the CHOMPI LED blinks yellow on
  each step.
- **Toggle**: performance / sequencer mode.

## The views

| Key | View |
| --- | --- |
| low F# | MODEL (tap again: next model) |
| low G# | EXCITER |
| low A# | RESONATOR |
| mid C# | BODY (tap again: next pickup) |
| mid D# | LFO (tap again: ENV) |
| high F# | FX |
| high G# | KBD |
| high A# | PRESET (held a second: back to init) |

### MODEL

| Knob | Turn | CHOMPI + turn |
| --- | --- | --- |
| 1 | Spectral: table. String: material. Bowed: bow. FM: position along the ratio list (click: next list). Vocal: vowel (u U aw ah ae eh ih ee) | Spectral: number of partials. String: second string, off to 15 cents apart. Bowed: bow position. |
| 2 | Stretch, centred: squeezed or spread partials; String: stiffness above the middle. Vocal: size, bass to child. Bowed: sympathetic strings (up: G D A E; down: tuned to each note) | Snap towards whole ratios. String: second string's interval, unison to two octaves. Bowed: flageolet harmonic. |
| 3 | Morph, centred: where each note's spectrum starts, gliding back | |
| 4 | Morph time, 20 ms to 8 s | Spectral: tonebar (a second fundamental a few cents up: beating and bloom) |

Between two tables, materials, bows or vowels, knob 1's LED mixes their colours.

### EXCITER

| Knob | Turn | CHOMPI + turn |
| --- | --- | --- |
| 1 | Type: hammer to pick. Bowed: bite as it catches. Vocal: consonant attack (none, d, k, h). FM: none | Life: each strike a little different. Vocal: tape ("magnetism": wow, flutter, an 8 s reel) |
| 2 | Hardness. FM: depth. Bowed: pressure (past ~0.8, a held scratch). Vocal: tension | Bowed: weight along the bow |
| 3 | Noise. FM: feedback. Bowed: grit. Vocal: breath | Bowed: stroke length, 0.04 s to 6 s, then one endless stroke |
| 4 | Strength. Bowed: speed. Vocal: effort | How much MIDI velocity scales strength |

### RESONATOR

| Knob | Turn |
| --- | --- |
| 1 | Attack: none to 2 s (a swell; FM and Bowed: their attack, 2 ms to 2 s) |
| 2 | Ring: how long a note rings |
| 3 | Damper: how fast it stops once released |
| 4 | Sustain: off, then the level a held note settles to and holds (Vocal: least fades out over Ring, most stays) |

### BODY

Pickups (tap mid C# again to cycle): clean, tine (orange), reed (pink), wood
(cream), vowel (teal), violin (yellow).

| Knob | Turn | CHOMPI + turn |
| --- | --- | --- |
| 1 | Amount: pickup drive, or how much body / vowel | Pickup position: off (whole string), then bridge (thin) to middle (hollow). Not FM |
| 2 | Tone: pickup voicing or body size; with vowel, the vowel | Bowed / Vocal: vibrato decay |
| 3 | Tremolo rate. Bowed / Vocal: vibrato rate | Bowed / Vocal: vibrato onset |
| 4 | Tremolo depth. Bowed / Vocal: vibrato depth (to ± a semitone) | Tremolo spread: same both sides to autopan |

### LFO

| Knob | Turn | CHOMPI + turn | Click |
| --- | --- | --- | --- |
| 1 | Rate (free) | Synced to the arp, 1/8 to 2 cycles a step | |
| 2 | Shape: triangle, sine, square, saw up, saw down, sample & hold | Delay | Trigger: NOTE, FREE, VOICE |
| 3 | Envelope depth to the destination, centred (below: inverted) | | Next destination |
| 4 | LFO depth to the destination, centred | | Next destination |

Destinations: pitch (white), brightness (green), morph (the model's colour),
exciter (orange for FM depth and bow pressure, purple for Stretch), tone
(red), pickup position (purple). The routing shows on white keys C to A for a
moment after a change and while CHOMPI is held. CHOMPI + a destination's key
picks it; CHOMPI + click returns a depth to none.

### ENV

A per-note envelope used for modulation only (loudness comes from the strike).

| Knob | Turn | Click |
| --- | --- | --- |
| 1 | Attack, 1 ms to 5 s | |
| 2 | Decay, 5 ms to 10 s | Cycle: held notes restrike after each attack and decay |
| 3 | Sustain | |
| 4 | Release, 5 ms to 10 s | |

### FX

| Knob | Turn | CHOMPI + turn | Click |
| --- | --- | --- | --- |
| 1 | Chorus level | Stereo spread of the voices | Chorus mode: off, I, II, I + II, I \| II |
| 2 | Send into the delay and reverb | | |
| 3 | Space: delay (green), both (white), reverb (blue) | Delay ping-pong | |
| 4 | Time: delay time, with the reverb's pre-delay | Tails: delay feedback and reverb decay | Time changes crossfade (yellow) or glide (orange) |

### KBD

| Knob | Turn | CHOMPI + turn | Click |
| --- | --- | --- | --- |
| 1 | Key tracking: at full, the ring halves each octave up | Detune: unison spread and per-voice drift | |
| 2 | Portamento: off, then 5 ms to 2 s | Strum: notes played together spaced up to 1 s apart | |
| 3 | Arp pattern: up, down, up & down, random, as played (also the strum order) | Arp octaves, 1 to 4 (strumming: a glissando through each inversion) | |
| 4 | Arp rate: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 of the tempo | Arp randomness: rests and octave jumps | Sync to MIDI clock |

CHOMPI + the lowest white keys in this view pick the key mode: C mono,
D poly 1, E poly 2, F unison, G chord.

## Sequencer mode

Flip the toggle down. The keys play notes; stopped, they enter notes into the
selected step; recording, into the running sequence.

- **White keys**: 16 steps (keys 1-7 steps 1-7, 9-15 steps 10-16, middle C
  step 8 or 9). The wheel scrolls the pages. CHOMPI + a white key selects a
  step; the selected one again turns it on or off.
- **Wheel**: selects the step. CHOMPI + wheel: its note length. Pressed and
  turned: the pattern length. Clicked while armed: a rest. CHOMPI + click:
  the metronome.
- **PLAY** starts / stops; **LOOP** arms recording (red).
- **Knobs 1-4**: step size (1/4 to 1/32), glide, direction, tempo
  (CHOMPI + turn: swing). Knob 6's click: the MIX page (sequence against keys).
- **CHOMPI + mid C#**: transpose (every key sets it; middle C none).
- **Mid D#**: QUANTIZE page (white keys 1-4: as played, 1/16, 1/8, 1/4).
- **High F#**: PATTERN view (a bank's 15 patterns; the wheel picks the bank;
  held 2 s, write protect). **High G#**: COPY (held, a pattern key gets the
  playing pattern). **High A#**: CLEAR the selected step (held: the pattern;
  with COPY: undo).
- **Low F#**: LENGTH page (white keys 1-15: a new note's length in steps;
  hold a key 2 s to set every step to it).
- **Low G#**: STRUM track (each step key strums that step on or off; the
  wheel's click switches synced, spread over the step, or free spacing).

In performance mode, CHOMPI + PLAY starts / stops the sequence.

## Presets

CHOMPI + high A# opens the PRESET view: eight banks of 15 slots. The wheel
steps through slots, playing each saved sound as it arrives; empty slots play
the current sound. The white keys show the bank's slots, the black keys the
banks.

- **Wheel click**: keep the sound and leave.
- **CHOMPI + PLAY**: save to the selected slot (green).
- **CHOMPI + LOOP**: delete it (red).
- **High A# held a second**: back to init.
- Leaving any other way returns to the sound the view opened with.

Factory presets are described in `presets/README.md`.

## MIDI

DIN and USB, channels set in `/PHYMO/options.txt`. CCs: brightness 74,
strike position 71, attack 73, ring 75, damper 72, LFO rate 76, tremolo depth
92, chorus level 93, send 91, portamento time 5, volume 7, mod wheel 1,
hold 64, portamento on / off 65. Turning a mapped knob sends its CC. MIDI
clock can drive the arp and sequencer.

## Files on the SD card

All in `/PHYMO`:

| File | Contents |
| --- | --- |
| `BANK1/` ... `BANK8/` | Preset banks, `P01.txt` to `P15.txt` each (plain `name=value` text) |
| `SEQ1.txt` ... `SEQ3.txt` | Sequencer pattern banks |
| `options.txt` | `midi_in=1`, `midi_out=1` |

# Factory presets

Five banks, the first five of the CHOMPI's eight preset banks.

## Bank 1: how far the models go

Real and imagined sounds, for trying out each model's range. Most started
from the desktop demo renders (`host/out/`).

| Slot | Name | What it is |
|---|---|---|
| 01 | Nylon Guitar | String: nylon, picked, through the wood body |
| 02 | Twelve String | String: steel, each note a course with its octave string (CHOMPI + MODEL knob 2) |
| 03 | Endless Steel | String held at its peak (Sustain), a slow swell, the pickup's place wandering on the LFO |
| 04 | Talking Strings | String through the vowel body, the LFO sweeping its vowel |
| 05 | Tine Piano | Spectral: the tine, through the tine pickup, autopanned, chorus I |
| 06 | Bell to Pad | Spectral: a bell striking, its spectrum settling into a harmonic pad (Morph) |
| 07 | Stretched Gong | Spectral: a plate, stretched, the morph falling from the membrane |
| 08 | FM E-Piano | FM: ratio 1, a short bright attack, the tine pickup and its tremolo |
| 09 | FM Bass | FM: ratio 1 with feedback, mono, gliding when played legato |
| 10 | FM Bells | FM: the metal list, each note falling through it |
| 11 | FM Scan Pad | FM: a pad, the LFO scanning slowly up and down the harmonic list |
| 12 | Cello | Bowed: horsehair, the cello body, a bow's stroke, vibrato coming in late (play it low) |
| 13 | Violin | Bowed: horsehair, the violin body, quicker vibrato (play it high) |
| 14 | Rosin Crunch | Bowed: rosin dug in hard (the crunch at each start), short strokes, a heavy frog |
| 15 | E-Bow Drone | Bowed: the e-bow over the fingerboard, its place drifting on the LFO, a long reverb |

## Bank 2: instruments

Each aims at a real instrument, to show what the models can imitate. Each
was measured against what defines its instrument: the attack, how fast it
decays, which harmonics are strong, any inharmonic partials, breath noise,
and the vibrato. For example, the clarinet has only odd harmonics, the
horns brighten as they speak, and the Rhodes has its tine partial (6.27×
the note).

| Slot | Name | What it is |
|---|---|---|
| 01 | Violin | Bowed: horsehair, the violin body; vibrato ±25 cents at 6 Hz, in after a moment (play G3 up) |
| 02 | Cello | Bowed: horsehair, the body down (Tone 0); vibrato ±20 cents at 5 Hz (play C2 up) |
| 03 | Upright Bass | String: steel plucked by a finger (soft, by the middle), a big wooden body, short (play low) |
| 04 | Acoustic Guitar | String: bronze, a pick near the bridge, the wooden body |
| 05 | Harp | String: nylon plucked by a fingertip, long and round, no body |
| 06 | Harpsichord | String: steel, a quill's hard pluck by the end, an 8' and a 4' rank (the octave course) |
| 07 | Clavinet | String: steel struck by a tangent, its magnetic pickup by the bridge, damped quickly |
| 08 | Rhodes | Spectral: the tine (its bell partial fading quickly), the tine pickup barking when played hard |
| 09 | Wurlitzer | Spectral: the Reed table (a tip-weighted reed) through the reed pickup, reedier and shorter, its tremolo |
| 10 | Vibraphone | Spectral: the tuned bar, a soft mallet, the motor's tremolo |
| 11 | Marimba | Spectral: the tuned bar, a yarn mallet, short (its resonator tube) |
| 12 | French Horns | FM: ratio 1, the index swelling with the envelope (the brass brightening as it speaks) |
| 13 | Flute | Bowed: a smooth bow far over the board (few harmonics), its breath (grit), vibrato ±15 cents |
| 14 | Clarinet | Spectral: the odd harmonics, held (Sustain), a breath's swell in |
| 15 | Organ | Spectral: the harmonics, held, at once; a rotary speaker's swirl (tremolo, wide) |

## Bank 3: bowed, dry

The Bowed model alone, with no chorus, delay or reverb, so you hear exactly
what the bow and string are doing. The settings that matter for bowing are
listed below each one, as knob positions, so you can rebuild a sound or
start from the nearest one and move a control at a time.

| Slot | Name | What it is |
|---|---|---|
| 01 | Violin | Horsehair, the violin body; vibrato ±25 cents at 6 Hz, in after a moment |
| 02 | Violin Detache | The violin, each note bowed back and forth (0.35 s strokes) |
| 03 | Violin Tremolo | The violin in tremolo: strokes 0.06 s, about 17 bow changes a second |
| 04 | Violin Scratchy | Rosin pressed hard at the bridge, grit up, quick 0.6 s strokes: scratch at each turn |
| 05 | Violin Grind | As Scratchy, but past the clean range (Hardness full): the scratch held through |
| 06 | Viola | Horsehair, a bigger body (Tone down), vibrato a little slower and wider |
| 07 | Cello | Horsehair, the biggest body (Tone 0), vibrato ±20 cents at 5 Hz (play C2 up) |
| 08 | Double Bass | Rosin, dark, slow and heavy; a slow, narrow vibrato (play low) |
| 09 | Sul Tasto | Over the fingerboard, a light bow: soft, hollow, flute-like (flautando) |
| 10 | Sul Ponticello | Right at the bridge, a light quick bow: glassy, the high harmonics ringing |
| 11 | Viol | A viol's: horsehair, no vibrato, long gentle strokes (1.5 s), a little Life |
| 12 | Folk Fiddle | Rosin, quick 0.25 s strokes, a bite at each start, little vibrato |
| 13 | Erhu | A smooth synthetic bow, wide vibrato (±40 cents), mono, sliding between notes |
| 14 | Bowed Saw | A smooth synthetic bow far over the board, dark and pure, a slow, wide, singing vibrato |
| 15 | Hurdy-Gurdy | Rosin on an endless wheel (one stroke): a steady, buzzing grit, no vibrato |

### How each is set

Percentages are knob positions, from fully left. Strokes are the length of
each bow stroke before it turns; the top of that knob is one endless stroke.
Everything not listed is at its init value. Vibrato is its rate, its width
(± cents), and how long it takes to come in.

| Slot | Name | Bow (MODEL 1) | Pressure (EXC 2) | Speed (EXC 4) | Grit (EXC 3) | Bite (EXC 1) | Strokes (CHOMPI + EXC 3) | Bowing point (CHOMPI + wheel) | Brightness (wheel) | Ring (RES 2) | Vibrato (BODY 3, 4; CHOMPI + 3) | Body (BODY) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 01 | Violin | horsehair | 55% | 70% | 8% | 75% | endless | 30% | 62% | 4.0 s (70%) | 6.0 Hz, ±25 c, in 0.4 s | violin 90%, tone 55% |
| 02 | Violin Detache | horsehair | 60% | 75% | 12% | 60% | 0.35 s (42%) | 30% | 65% | 4.0 s (70%) | 6.0 Hz, ±20 c, in 0.5 s | violin 90%, tone 55% |
| 03 | Violin Tremolo | horsehair | 55% | 75% | 12% | 70% | 0.06 s (8%) | 30% | 62% | 4.0 s (70%) | 5.8 Hz, ±20 c, in 0.4 s | violin 85%, tone 50% |
| 04 | Violin Scratchy | rosin | 80% | 50% | 100% | 10% | 0.6 s (52%) | 0% | 70% | 4.0 s (70%) | 5.8 Hz, ±15 c, in 1.2 s | violin 90%, tone 50% |
| 05 | Violin Grind | rosin | 100% | 30% | 70% | 10% | 0.6 s (52%) | 0% | 70% | 4.0 s (70%) | 5.8 Hz, ±15 c, in 1.2 s | violin 90%, tone 50% |
| 06 | Viola | horsehair | 55% | 65% | 8% | 75% | endless | 32% | 58% | 4.5 s (72%) | 5.5 Hz, ±25 c, in 0.4 s | violin 90%, tone 30% |
| 07 | Cello | horsehair | 55% | 50% | 6% | 75% | endless | 35% | 55% | 5.0 s (74%) | 5.0 Hz, ±20 c, in 0.4 s | violin 90%, tone 0% |
| 08 | Double Bass | rosin | 60% | 25% | 15% | 60% | endless | 35% | 45% | 6.0 s (77%) | 4.6 Hz, ±15 c, in 0.6 s | violin 90%, tone 0% |
| 09 | Sul Tasto | horsehair | 30% | 70% | 5% | 85% | endless | 100% | 40% | 4.0 s (70%) | 5.6 Hz, ±18 c, in 0.4 s | violin 80%, tone 50% |
| 10 | Sul Ponticello | horsehair | 35% | 85% | 15% | 80% | endless | 0% | 85% | 4.0 s (70%) | 6.0 Hz, ±12 c, in 0.4 s | violin 85%, tone 60% |
| 11 | Viol | horsehair | 45% | 60% | 10% | 80% | 1.5 s (70%) | 40% | 55% | 4.0 s (70%) | none | violin 85%, tone 35% |
| 12 | Folk Fiddle | rosin | 65% | 50% | 35% | 25% | 0.25 s (35%) | 20% | 70% | 3.0 s (64%) | 6.5 Hz, ±8 c, in 0.6 s | violin 90%, tone 60% |
| 13 | Erhu | synthetic | 55% | 100% | 10% | 80% | endless | 25% | 60% | 4.0 s (70%) | 6.0 Hz, ±40 c, in 0.3 s | vowel 5%, tone 25% |
| 14 | Bowed Saw | synthetic | 50% | 100% | 10% | 90% | endless | 80% | 40% | 6.0 s (77%) | 4.5 Hz, ±60 c, in 0.8 s | violin 70%, tone 40% |
| 15 | Hurdy-Gurdy | rosin | 70% | 65% | 85% | 50% | endless | 15% | 65% | 4.0 s (70%) | none | wood 60%, tone 50% |

### What each control does to a bowed sound

- **Pressure** (EXCITER knob 2): light is soft and airy, heavier is brighter
  and fuller. Up to 80% the held tone stays clean; past it, the bow grinds
  in and the scratch is held through the note.
- **Speed** (EXCITER knob 4): mostly loudness. A slow, heavy bow is the
  grittiest combination.
- **Grit** (EXCITER knob 3): the rosin's roughness, strongest as the bow
  meets the string and at each turn.
- **Bite** (EXCITER knob 1): below the middle, an accented, struck start;
  above it, a smooth start.
- **Bowing point** (CHOMPI + wheel): fully left is right at the bridge
  (glassy and bright, sul ponticello); fully right is over the fingerboard
  (soft and hollow, sul tasto).
- **Strokes** (CHOMPI + EXCITER knob 3): the bow changing direction. Low is
  quick bowing, down to a tremolo; the top is one endless stroke.
- **Sympathetic strings** (MODEL knob 2, Stretch): strings that aren't
  played, ringing along. Up from the middle, a violin's open strings (G, D,
  A, E): notes on them, or their harmonics, ring on after you let go. Down
  from the middle, strings tuned to each note you play, a sitar-like halo.
- **Flageolet** (CHOMPI + MODEL knob 2): a light finger on the string for
  natural harmonics. Off at the bottom, then the octave, the twelfth, two
  octaves, and two higher, snapped.
- **Brightness** (the wheel): the string's and hair's own colour, the
  highs tilted against the lows, dark (gut, old hair) to bright (steel).
- **Body** (BODY: the violin pickup): Tone is its size, from a small,
  bright fiddle at the top through the violin in the middle to a cello at
  the bottom; Amount is how much of it you hear, its bridge hill (the
  violin's bright ring round 3 kHz) included.

## Putting them on the CHOMPI

Each bank is its own folder on the SD card, `PHYMO/BANK1` to `PHYMO/BANK8`,
with that bank's 15 slots as `P01.txt` to `P15.txt`. Copy the `BANK1`,
`BANK2` and `BANK3` folders here into `PHYMO` on the card. That replaces any
presets already saved in those banks, so copy the card's own `BANK1` to
`BANK3` somewhere first if you want to keep them, or rename them to free
banks, say `BANK4` to `BANK6`. A whole bank can be copied, swapped or shared
the same way.

The CHOMPI makes the folders itself at startup. Presets saved before there
were banks (`P01.txt` to `P15.txt` loose in `PHYMO`) are moved into `BANK1`
then, never over a file already there.

In the PRESET view (CHOMPI + high A#), turn the wheel to step through the
slots, bank by bank. Each saved one plays as you reach it, and the keys
keep playing so you can try it. Click the wheel to keep the sound and leave
the view; leaving any other way (tapping high A# again, or a view key) goes
back to the sound you had before. CHOMPI + PLAY saves into the wheel's
slot, and CHOMPI + LOOP deletes it.

The white keys show the bank's slots and the black keys show the banks, in
each bank's colour: warm white, light blue, magenta, green, orange, teal,
pink, purple. A slot's file is its bank's folder and its place in the bank:
`BANK2/P01.txt` is bank 2's first slot.

Each file is plain text, one `name=value` a line (see
`code/src/preset.h`), so you can edit it by hand. The CHOMPI ignores the `name=` and `about=` lines,
and drops them when you save over the slot.

## Checking them

`host/preset_test` loads each file through the panel's own code, so every
knob means what it does on the CHOMPI. It plays each preset a short phrase
and checks that:

- every line is a name the CHOMPI knows (it skips unknown ones, so a typo
  would otherwise go unnoticed),
- each one sounds and stays under clipping,
- they're about as loud as each other.

It writes `host/out/preset_B_NN.wav` (bank, slot) for listening:

    make -C host preset_test && (cd host && ./preset_test)

## Bank 4: electric pianos

The Spectral model's Tine and Reed tables, each through its own pickup. A
struck tine or reed rings a nearly pure tone: its overtones are a short
"tink" on the strike (gone in 60 ms for the tine, 120 ms for the reed), and
the bark is the pickup's, growing as you play harder. The tine's pickup adds
mostly even harmonics; the reed's, odd ones. The tine presets also have the
tonebar (CHOMPI + MODEL knob 4): the tine's partner, a few cents apart, so the
note swells gently and rings on.

| Slot | Name | What it is |
|---|---|---|
| 01 | Tine Stage | The tine through the tine pickup, barking when played hard |
| 02 | Tine Suitcase | The tine, its amp's tremolo panning side to side |
| 03 | Tine Bark | The pickup driven hard: clean when soft, growling when struck |
| 04 | Tine Bright | A hotter hammer and the pickup's peak up, chorus I |
| 05 | Tine Bells | The overtones long and bright, a bell-like piano, delay and reverb |
| 06 | Reed 200 | The reed through the reed pickup, the amp's tremolo |
| 07 | Reed Dirty | The pickup driven to the edge, no tremolo |
| 08 | Reed Soft | Played gently: rounder, less drive, chorus I |
| 09 | Reed Plucked | Short, damped quickly, a plucked reed |
| 10 | Reed to Tine | Each note struck as a reed, settling onto the tine within half a second |
| 11 | Tine Mellow | Dark and round: a soft hammer, the pickup's peak down, little drive |
| 12 | Tine Pad | Swelling in and held (Sustain), chorus II, a long reverb |
| 13 | Reed Bass | Mono, short and punchy (play it low) |
| 14 | Tine Chorus | Bright-ish, chorus II and a touch of delay |
| 15 | Reed Tremolo | The tremolo deep and quick, panning side to side |

## Bank 5: voices

The Vocal model: a singer as a glottal source and five formants, its
vowel on MODEL knob 1 (oo, ʊ, aw, ah, a(t), eh, ih, ee), its size on knob
2, each note's glide from another vowel on knobs 3 and 4, Hardness its
tension, Noise its breath, the BODY view's knobs 3 and 4 its vibrato.
CHOMPI + EXCITER knob 1 is its tape, "magnetism" (a tape choir's wow,
flutter and colour, each held note 8 s), and CHOMPI + the wheel its
ensemble, which also gives each note a section of three voices; the wheel
itself is its brightness (its formants up and down). EXCITER's Type is the
onset, its attack: none at the bottom, then d, k and h (blends between).

| Slot | Name | What it is |
|---|---|---|
| 01 | Choir Aah | A choir on ah, each voice its own, a late vibrato, a hall |
| 02 | Choir Ooh | On oo: darker, rounder |
| 03 | Choir Ee | On ee: bright and forward |
| 04 | Monks | Deep voices on aw, slow and close together, a long hall |
| 05 | Soprano | One voice, small and bright, a full vibrato (play high) |
| 06 | Breath Pad | Mostly breath, slow to swell, a wide ensemble |
| 07 | Wah Voice | Each note starts on oo and opens to ah in a quarter second |
| 08 | Eye | Each note sung ah to ee: a diphthong |
| 09 | Bass Voice | One big low voice on aw (play low) |
| 10 | Child Choir | Small voices between oo and ah, light and airy |
| 11 | Tape Choir Aah | A tape choir: a section on each key, the tape's wow, flutter and colour, each note 8 s |
| 12 | Tape Choir Ooh | The tape choir on oo: darker, its 8 s reel the same |
| 13 | Slow Bloom | Each note opening from oo to ee over three seconds |
| 14 | Unison Choir | Unison: a section on each key (one note at a time) |
| 15 | Talking Choir | Through the vowel body as well, its vowel on the LFO |

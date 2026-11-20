# Mono SWAVE

The SWAVE machines of the Elektron Monomachine (SWAVE SAW, SWAVE PULS and
SWAVE ENS) as three CLAP instruments for Bitwig (macOS, universal binary),
each with the track's AMP envelope, and ENS with its chorus.

The DSP is a fixed-point C port of these machines and of the track kernel's
oscillator, pitch and AMP sections in Monomachine OS 1.32B, written from its
DSP56300 code and checked sample for sample against that code running in an
emulator. No firmware code is in this repository. Its tables are generated
from formulas at start-up, except the oscillators' band-limited impulse (515
words), which no formula reproduces and which is copied from the firmware
(the same table Mono Ringmod ships).

The bundle `Mono SWAVE.clap` holds three plug-ins: **Mono SWAVE SAW**,
**Mono SWAVE PULS** and **Mono SWAVE ENS**. Each is a monophonic synth, as a
Monomachine track is.

## Controls

Every plug-in has the machine's knobs (page "Machine"), seven of the AMP
page's (page "Amp") and three pitch parameters. The knobs are 0–127 like
the unit's knobs, can be automated and modulated in Bitwig, and default to
the unit's values.

**Mono SWAVE SAW** (five saws and two sines):

| Parameter    | On the unit | What it does |
|--------------|-------------|--------------|
| Unison Level | UNIL | Two more saws, detuned up and down by Unison Width, at UNIL/128 of the main saw's level. |
| Unison Width | UNIW | Their detune: the period × (1 ± UNIW/4096), 52.9 cents down and 54.5 up at 127. The display shows both. |
| Unison Inner | UNIX | Two further saws detuned by half as much, at Unison Level × UNIX/128. |
| Sub Saw      | SUBX | Makes every other edge of the main saw taller (up to 9 times at 127), which adds a saw an octave down. |
| Sub 1        | SUB1 | A sine an octave below the note, at up to 1/16 of full scale. |
| Sub 2        | SUB2 | A sine two octaves below, the same way. |
| Tune         | TUNE | The pitch, ±2 semitones (−200 to +197 cents). |

The unit's fourth SAW knob does nothing; the plug-in keeps it as a hidden
parameter.

**Mono SWAVE PULS** (three pulses and two sines):

| Parameter    | On the unit | What it does |
|--------------|-------------|--------------|
| Unison Level | UNIL | Two more pulses, detuned up and down by Unison Width, at UNIL/128 of the main pulse's level. |
| Unison Width | UNIW | Their detune: the period × (1 ± UNIW/8192), 26.6 cents down and 27.0 up at 127. |
| Sub 1        | SUB1 | A sine an octave below the note, at up to 1/4 of full scale. |
| Sub 2        | SUB2 | A sine two octaves below, the same way. |
| Pulse Width  | PW   | The width, 1% (0) to 98% (127) of the cycle, 50% at 64. |
| PW Sweep     | PWAD | Sweeps the width through its whole range (1% to 99%) and back, a triangle starting from Pulse Width: one cycle in 47.6/PWAD seconds (2.7 Hz at 127). At 0 the width stays where the sweep left it. |
| PW Restart   | PWRS | At 64 and above, each note restarts the sweep from Pulse Width; below, it runs on. |
| Tune         | TUNE | The pitch, ±2 semitones. |

**Mono SWAVE ENS** (four oscillators and a chorus):

| Parameter    | On the unit | What it does |
|--------------|-------------|--------------|
| Pitch 2, 3, 4 | PCH2, PCH3, PCH4 | The other three oscillators' intervals to the first: −12 to +12 semitones in 33 steps, equal-tempered, with just intervals among them (2/3, 3/4, 4/5 and 5/6 below; 6/5, 5/4, 4/3 and 3/2 above). 61 to 63 is unison, which mutes that oscillator. The display shows the interval. |
| Wave         | WAVE | Each oscillator from a saw at the note's pitch (0) to a pulse an octave lower (127). |
| Pulse Width  | PW   | Where the cycle's second edge falls, 1% (0) to 98% (127), 50% at 64: the pulse's width; at Wave 0 with 50% the saw's teeth are even, elsewhere it alternates long and short teeth. |
| Chorus Level | CHRL | The chorus's level; the dry signal falls by as much (1 − CHRL/128). Both follow the knob within about 20 blocks (7 ms). |
| Chorus Width | CHRW | How far apart the chorus's two voices sit: each side gets its own voice in full and the other's at 1 − CHRW/256, so both go to both sides at 0, and the other's comes in at half at 127. |
| Tune         | TUNE | The pitch, ±2 semitones. |

The Amp page, in all three:

| Parameter | On the unit | What it does |
|-----------|-------------|--------------|
| Attack    | ATK  | From where the level is up to full: 0.7 ms at 0, 152 ms at 64, 30 s at 127. |
| Hold      | HOLD | How long the level stays full, in beats of the host's tempo: Hold/32 beats (127 is about a bar). |
| Decay     | DEC  | The fall after the hold: −60 dB in 23 ms at 0, 1.7 s at 64, 112 s at 126; 127 holds. |
| Release   | REL  | The fall after the note ends, with the same times. |
| Volume    | VOL  | The level, (VOL/128)². |
| Pan       | PAN  | Equal-power panning (−64 to +63 on the display). In the centre the left side is 0.11 dB lower than the right, as on the unit. |
| Portamento | PORT | The glide to a new note, in beats of the host's tempo, whatever the interval: 0 is instant, 32 about 1/30 beat, 64 a third of a beat, 96 three beats, 127 about 27 beats (2^(13 Portamento/128)/276 beats). |

The pitch parameters (module "Pitch"), in all three:

| Parameter   | On the unit | What it does |
|-------------|-------------|--------------|
| Pitch Mod   | where the LFOs aimed at PTCH land (block word 30) | 64 is no change; each step is 2.06 semitones (352/2048 octave), and fractions of a step are kept. Modulate it in Bitwig as the unit's LFOs modulate pitch. 0 and 127 are −132 and +130 semitones; the pitch itself stops at the kernel's 11-octave range. |
| Bend Range  | KIT > ASSIGN, JOY R/L, PAGE PTCH, DEST | How far MIDI pitch bend goes at its ends: 1/12, 2/12 (the default, ±2.01 semitones), 7/12, 1OCT, 2OCT, 4OCT, 8OCT or 16OC. |
| Master Tune | GLOBAL > AUDIO > MASTER TUNE | 400.0 to 499.9 Hz in steps of 0.1 Hz, default 440.0. |

MIDI pitch bend works as the joystick's left and right do on the unit with
KIT > ASSIGN set to MIRR on, PAGE PTCH, DEST the Bend Range and ADD +63.
Only the bend's top 8 bits count, as on the unit: 128 steps each way.

The Monomachine's envelope has no sustain: the level falls with Decay even
while the note is held (use Decay 127 to hold it), and a note's end starts
the Release from wherever the level is.

## How it behaves

This is what the firmware does; the plug-in keeps all of it.

- **The oscillators.** All three machines use the kernel's oscillator, at
  44.1 kHz. Each edge of a wave is a band-limited impulse (a windowed sinc
  at 32 sub-sample positions), added into a ring of 64 samples that all the
  machine's oscillators share; a leaky integrator turns the impulses into
  the waves, and a DC blocker follows. The integrator's leak follows the
  pitch in SAW and ENS and is fixed in PULS; the DC blocker's corner follows
  the pitch in SAW and PULS and is fixed in ENS. A saw is equal impulses
  integrated; a pulse is impulses of alternating sign. When the main saw's
  pitch changes, SAW moves all five saws' next edges in proportion, so they
  stay at the same point of their cycles; moving Unison Width does not.
- **The sines** (SAW and PULS) are read from an 8192-point table, their
  frequency ramped across each 16-sample block. Every second sample takes
  the table's next entry instead of the interpolated value, as the original
  does.
- **ENS** runs its oscillators an octave below the note. At Wave 0 each
  cycle has two equal edges, so the saw sounds at the note's pitch; at 127
  the edges have opposite signs, a pulse an octave down. Its pitch stops
  rising above note 114 (at Tune 64).
- **ENS's chorus** runs after the AMP section, on the panned stereo signal,
  before the track's delay. A mono sum goes into a 1024-sample delay line,
  read at 9.1 ± 7.7 ms by two taps moved by sine LFOs a quarter cycle apart,
  at 0.2 Hz; the taps are panned like the dry signal. The chorus is silent
  for the first 64 blocks (23 ms) after the machine is assigned; the plug-in
  runs those at every reset, so it never starts silent. Between the AMP
  section and the chorus the track's SRR stage, at its default, delays the
  signal by two samples, and the plug-in keeps that too.
- **Notes.** Each note restarts the AMP envelope's attack, from where its
  level is, and in PULS the width's sweep when PW Restart is on. With
  Portamento at 0 the pitch reaches a new note in two 16-sample blocks.
  Above 0 it glides in a straight line (in pitch), covering the interval
  from the glide's start in the Portamento time. A note that comes mid-glide
  in the same direction keeps that start; one in the other direction turns
  from where the pitch is, at the speed for the interval from the note it
  was heading to. At Master Tune 440.0, A4 is 440.14 Hz (+0.55 cents) in
  SAW and ENS, whose periods come from the pitch table, and 439.95 Hz
  (−0.19 cents) in PULS, whose period is the kernel's frequency word
  divided out.
- **Knobs.** Parameter changes reach the machine the way the Monomachine's
  CPU sends a knob: every 384 samples the value moves a quarter of the way to
  where you set it. Modulation is added on top, unsmoothed, every 48
  samples, as the unit's LFOs are. Notes and the tempo are taken every 48
  samples too.
- **The pitch** is the kernel's sum, once per block: the note's glide,
  then Master Tune, Tune, Pitch Mod and the bend, clamped to the kernel's
  11 octaves. The glide does not smooth Pitch Mod or the bend.
  - Master Tune is the main OS's table, trunc(2048 log2(f/440)) in 1/2048
    octaves (0 at 440.0 Hz, −281 at 400.0 Hz).
  - A pitch bend at its end adds 127 × 63 × the range's step to word 30.
    The steps are 4, 8, 28, 48, 96, 192, 384 and 768 (the main OS's table).
    The left side's ADD is −63, as the unit's mirror makes it. The result is
    kept to 0..$7EFFFF, on the side it moves.
- **The AMP gains** move in a straight line across each 16-sample block.

## Differences from the hardware

- **The track filter.** On the unit the machine's output goes through the
  track's filter and distortion before the AMP section. The plug-in leaves
  them out ([Mono Filter Distortion](https://github.com/nerazin/mono-filter-distortion-clap)
  is that filter, as an effect) and stands in for them with the filter's
  plain level at its default settings, 0.4455 (−7.0 dB), without its
  inversion, as Mono FM+ does. The SWAVE machines each set the filter's
  drive to a level of their own (FM+ leaves it at 0.25; SAW sets 0.078,
  PULS 0.2 and ENS 0.3125); at the default Distortion this changes the
  filter's level by less than 0.00001, measured in the firmware.
- **The rest of the track.** EQ, SRR (ENS keeps the two samples its default
  delays by), delay, the LFOs and the track's LEVEL are not in the plug-in.
  Bitwig's own modulators can take the LFOs' place: modulating Pitch Mod is
  what an LFO on the PTCH page does. The unit's LFO takes a new value every
  96 samples and moves a quarter of the way to it each time; a Bitwig
  modulator is read every 48 samples, unsmoothed.
- **Pitch bend** follows one KIT ASSIGN setting (above). On the unit the
  joystick and bend can also go to other parameters with other ADD amounts,
  and velocity and key position can be assigned too. Those are left out.
- **The MULTI TRIG transpose** (SEQ START / SEQ TRNSP) reaches the kernel as
  a second glide (block words 34 and 28). It needs the unit's sequencer, so
  it is left out; with it at rest the kernel adds nothing.
- **Master tune in 0.1.0** was the word Monomodule's CPU model sends, −1 at
  440 Hz. The main OS's table gives 0, so notes are now 1/2048 octave
  (0.59 cents) higher than in 0.1.0.
- **Notes.** One voice: the newest note held sounds. Releasing it returns to
  the previous note still held, with a new trig; releasing the last ends the
  note. Velocity is ignored. This note handling is the plug-in's; the unit's
  own MIDI handling was not studied.
- **Start.** The plug-in starts as a track does when the machine is assigned
  (the machine's init routine runs; ENS then runs its first 64 blocks with
  no note), with the AMP envelope idle at 0. On a unit just switched on, the
  firmware's envelope starts with an attack of its own.
- **Other sample rates.** The machines run at 44.1 kHz, the unit's rate. At
  other rates the plug-in resamples its output (windowed sinc), so it is no
  longer bit-exact, but the pitch is the same. The resampler reads ahead, so
  the plug-in reports a latency: 0 samples at 44.1 kHz, 40 at 22.05 kHz, 44
  at 48 kHz and 87 at 96 kHz. Notes land on the unit's 48-sample control
  grid, so their timing has that much spread anyway.
- **Levels.** 0 dBFS in the plug-in is full scale after the AMP section. At
  their default knobs a held A4 peaks at −21.3 dBFS (SAW), −24.4 dBFS
  (PULS) and −18.9 dBFS (ENS) at Volume 64, and 12 dB higher at 127. With
  the unison and sub knobs up the machines get much louder (SAW with all its
  knobs at 127 peaks at −2.4 dBFS before the AMP section), and the AMP
  section then clips at full scale, as the firmware's does.

## Accuracy

The reference is Monomachine OS 1.32B running in the DSP56300 emulator of
[Monomodule](https://github.com/shnolk/monomodule), with the track's controls
and notes built by Monomodule's model of the Monomachine's CPU. It has not
been compared with a physical Monomachine, and the plug-ins have not been
opened in Bitwig as part of this work.

| Check | Result |
|-------|--------|
| Core vs. firmware, for each machine on a synth track: random notes on and off, knob moves through the CPU model (Portamento included), raw control words anywhere from 0 to $7FFFFF (also for the AMP page, the trig word and Pitch Mod's word 30), Master Tune words from the whole table, tempos from 30 to 300 BPM; 400,000 blocks each, 18,500 to 31,800 of them mid-glide per 50,000 | every machine output sample identical; every machine state word identical after every block (for ENS also its chorus's delay line, LFOs and gate); the AMP envelope's state, its gain ramps and its products with the firmware's own filter output identical in every block; ENS's chorus output identical in every block, and its input equal to the AMP section's products two samples earlier; the glide's state, the pitch index and the frequency word identical to the kernel's in every block |
| Built plug-in code vs. firmware, with the firmware's filter output replaced by the machine's output × 0.4455 as in the plug-in: knob moves on both pages (Portamento included) through the CPU model, modulation, overlapping notes, tempo changes, Pitch Mod moves and modulation, MIDI pitch bends, Bend Range and Master Tune changes (240,000 blocks per plug-in: about 24,120 moves, 1,831 of them modulation, 3,735 pitch-parameter changes and about 1,876 bends; 4,694 notes on and 3,690 off) | every output sample identical (for ENS, the chorus's output) |
| The main CPU's pitch words vs. the main OS (the OS image's tables, not the DSP's) | identical: all 1,000 Master Tune words and the 8 bend-range steps |
| Tables vs. firmware memory | identical, every entry the machines, the chorus and the AMP section read |

The tables: the sine is the boot code's recurrence, reproduced exactly; the
exponential pitch table is 2^(22 + k/2048) with a mask for its 416 entries
one LSB lower in the firmware; the integrator's leak is 1 − e^(−2π f/44100)
for f from 4.003922 Hz over 12.5 octaves, with five entries one LSB higher,
and the DC blocker the track filters' one-pole formula, both as in
[Mono Ringmod](https://github.com/nerazin/mono-ringmod-clap); SAW's detune
tables are 1 ± k/4096 and their reciprocals, rounded; ENS's intervals are the
semitones and just ratios above, rounded to 2^−21; the AMP section's attack,
decay and portamento tables are Mono FM+'s formulas. The band-limited
impulse is copied.

`tests/test_core.c` pins this without the firmware. Its expected hashes were
produced by feeding the same stimulus into the firmware's machines, AMP
section and ENS's chorus, which the core matched from its own start. Its
stimulus also moves Pitch Mod's word and the Master Tune word. It also
hashes the Master Tune words and every bend value against a hash of the
main OS's own tables.

## Build and install

```bash
./build_macos.sh
```

```bash
cp -R "build/Mono SWAVE.clap" ~/Library/Audio/Plug-Ins/CLAP/
```

The build script needs Xcode's command-line tools.

## Tests

```bash
cc -std=c99 -O2 -Wno-unused-function tests/test_core.c -lm -o tests/test_core && tests/test_core
```

```bash
xcrun clang -std=c11 -O2 -Wno-unused-function -Wno-missing-field-initializers -I vendor/clap/include tests/clap_test.c -o tests/clap_test && tests/clap_test "build/Mono SWAVE.clap"
```

`test_core` checks the tables, the main CPU's pitch words and 12,000 blocks
of each machine's output and AMP output (and ENS's chorus output) against
the firmware-derived hashes.
`clap_test` loads the built bundle and checks:

- the three plug-ins, each with its eighteen parameters (SAW's unused one
  hidden);
- at 44.1 kHz, each plug-in's output equal to the core's, for notes on and
  off at and between control frames, with the machines' knobs away from
  their defaults, Portamento at 0 and at 50 (and that 50 glides), and
  Pitch Mod and Master Tune away from theirs;
- SAW as a single saw at the core's frequency for A4 at 44.1 and 48 kHz,
  and 197 cents higher with Tune at 127;
- SAW's pitch at the core's frequency with a full bend up and down
  (±201 cents at 2/12), Pitch Mod 65 (+206 cents) and Master Tune 415.3 Hz
  (−100 cents);
- releasing the newer of two held notes returning to the other, and the
  release ending in silence;
- ENS's chorus changing the sound, and its width separating left and right;
- a note's onset no earlier than the reported latency and at most one
  control frame and one block later, at 22.05, 44.1, 48 and 96 kHz;
- state (also a version 1 state, from 0.1.0, loading with the pitch
  parameters at their defaults; another machine's, a short and a bad state
  rejected) and the parameter text.

Both tests also pass with AddressSanitizer and UndefinedBehaviorSanitizer,
and as Intel builds under Rosetta. The bundle passes clap-trap's `validate`
(all three plug-ins, at 44.1, 48 and 96 kHz) and `state --roundtrip`.

## Source

- `source/mono_swave.h`: the three machines, the kernel's oscillator, pitch
  and AMP section, ENS's chorus, and the main CPU's master tune and bend
  words (C99 and libm).
- `source/mono_swave_clap.c`: the three CLAP plug-ins around it.
- `vendor/clap`: CLAP 1.2 headers (MIT).

## License

GPL-3.0-only for this project's code. The DSP is an independent
reimplementation of the Monomachine's SWAVE machines and track kernel,
derived from analysis of Elektron's firmware; no firmware code is included,
and the one table copied from it is the oscillators' band-limited impulse.
Monomachine and Elektron are trademarks of Elektron Music Machines; this
project is not affiliated with Elektron.

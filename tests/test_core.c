/* Regression test for mono_swave.h that needs no firmware.
 *
 * The expected hashes come from Monomachine OS 1.32B itself: the tables were
 * read from the emulated DSP's memory, and the same stimulus was fed into the
 * firmware's SWAVE SAW, SWAVE PULS and SWAVE ENS machines on a synth track
 * running in an emulator (see the README), the AMP section starting idle and
 * the track filter's output replaced by the machine's output x 0.4455, as
 * the plug-in has it. The stimulus plays and releases notes over the whole
 * range, moves every control, including to the ends of its range and the
 * portamento, the pitch modulation (word 30) and the master tune (word 38),
 * and changes the tempo. Each block hashes the machine's output
 * and the AMP section's left and right output; for ENS, also its chorus's
 * output (its input being the AMP's output two samples later, as the track's
 * SRR stage passes it at its default).
 *
 * The main CPU's pitch words (master tune, pitch bend) are hashed against the
 * main OS's own tables: lab/pitch/maincpu_hash.py prints that hash from the
 * OS image. */
#include <stdio.h>
#include "../source/mono_swave.h"

static uint32_t seed = 1;
static uint32_t rnd(uint32_t n) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % n; }

/* The block words of one block: the SYN and AMP pages, the trig word (40),
 * the note word (41), the tick (42, 24 x BPM; word 35 is 0x800000 / tick),
 * the pitch modulation (30) and the master tune (38). Notes, the tempo and
 * those two change only every third block, where the Monomachine's CPU sends
 * them. */
typedef struct { int32_t syn[8], amp[8], trig, note, tick, pmod, mtune; } Words;
static void stimulus(int block, Words *w) {
   static const int amp_words[7] = {MS_ATK, MS_HOLD, MS_DEC, MS_REL, MS_VOL, MS_PAN, MS_PORT};
   if (block == 0) {
      memset(w, 0, sizeof *w);
      for (int k = 0; k < 8; ++k) w->syn[k] = 64 << 16;
      w->amp[MS_DEC] = w->amp[MS_REL] = w->amp[MS_VOL] = w->amp[MS_PAN] = 64 << 16;
      w->note = (60 << 11) / 12;
      w->tick = 2880;
      w->pmod = 64 << 16;
   }
   w->trig = block == 0 ? 0x80 : 0;   /* the machine's assignment */
   if (block % 23 == 0 && block) {                  /* knob-like moves, also between steps and to the ends */
      const int k = (int)rnd(15);
      const int32_t v = rnd(4) == 0 ? (rnd(2) ? 0x7fffff : 0) : (int32_t)rnd(0x7f0000);
      if (k < 8) w->syn[k] = v; else w->amp[amp_words[k - 8]] = k == 10 && v > 0x700000 ? 0x700000 : v;
   }
   if (block % 3 == 0 && block) {
      const uint32_t r = rnd(100);
      if (r < 6) { w->trig = 0x81; w->note = ((rnd(5) ? 24 + (int)rnd(85) : (int)rnd(128)) << 11) / 12; }
      else if (r < 10) w->trig = 0x02;
      else if (r < 11) w->tick = 720 + (int)rnd(6481);
      else if (r < 16) w->pmod = rnd(4) ? (64 << 16) + (int32_t)rnd(0x300000) - 0x180000 : (int32_t)rnd(0x7f0000);
      else if (r < 17) w->mtune = ms_master_tune(4000 + (int)rnd(1000));
   }
}

static uint64_t hash = 1469598103934665603ULL;
static void mix(const int32_t *v, int n) {
   for (int i = 0; i < n; ++i)
      for (int b = 0; b < 4; ++b) hash = (hash ^ (((uint32_t)v[i] >> (8 * b)) & 0xff)) * 1099511628211ULL;
}

#define TEST_BLOCKS 12000
#define FILTER_LEVEL 0x3905a5

#ifndef TEST_CORE_NO_MAIN
int main(void) {
   static const uint64_t want[MS_MACHINES] = {0xc9e48eeabb2bf2a3ULL, 0xddac181655d07098ULL, 0x1ee68d8698797c11ULL};
   const uint64_t want_tables = 0x69294719a6fbb8a6ULL;
   ms_tables();
   mix(ms_sine, 0x2000); mix(ms_exp, 0x800); mix(ms_leak, 256); mix(ms_dc, 1600); mix(ms_impulse, 515);
   for (int t = 0; t < 4; ++t) mix(ms_detune[t], 129);
   mix(ms_interval[0], 128); mix(ms_interval[1], 128);
   mix(ms_attack, 128); mix(ms_decay, 129); mix(ms_port, 128);
   int fail = hash != want_tables;
   printf("tables %016llx\n", (unsigned long long)hash);
   hash = 1469598103934665603ULL;
   for (int t = 4000; t < 5000; ++t) { const int32_t v = ms_master_tune(t); mix(&v, 1); }
   for (int r = 0; r < 8; ++r) for (int v = 0; v < 256; ++v) { const int32_t b = ms_bend(v, r); mix(&b, 1); }
   fail |= hash != 0x3449da86dbec4126ULL;   /* want_main */
   printf("main cpu %016llx\n", (unsigned long long)hash);
   for (int m = 0; m < MS_MACHINES; ++m) {
      static MonoSwave s;
      MsAmp env = {3, 0, 0, {0, 0}};
      MsPitch glide;
      Words w;
      seed = 1 + (uint32_t)m;
      hash = 1469598103934665603ULL;
      ms_init(&s, m);
      for (int block = 0; block < TEST_BLOCKS; ++block) {
         stimulus(block, &w);
         if (block == 0) ms_pitch_reset(&glide, w.note);
         int32_t out[MS_FRAMES], g[2][MS_FRAMES], lr[2][MS_FRAMES];
         ms_block(&s, w.syn, ms_pitch(&glide, w.note, w.syn[7], w.amp[MS_PORT], w.tick, w.pmod, w.mtune), w.trig & 0x80, out);
         ms_amp(&env, w.trig & 0x7f, w.amp, 0x800000 / w.tick, g);
         for (int c = 0; c < 2; ++c)
            for (int i = 0; i < MS_FRAMES; ++i) lr[c][i] = ms_apply(g[c][i], ms_mpy(out[i], FILTER_LEVEL));
         mix(out, MS_FRAMES); mix(lr[0], MS_FRAMES); mix(lr[1], MS_FRAMES);
         if (m == MS_ENS) {
            ms_after_amp(&s, w.syn, w.amp[MS_PAN], lr);
            mix(lr[0], MS_FRAMES); mix(lr[1], MS_FRAMES);
         }
      }
      printf("machine %d audio %016llx\n", m, (unsigned long long)hash);
      fail |= hash != want[m];
   }
   if (fail) { puts("FAIL: output differs from the firmware"); return 1; }
   puts("PASS core tables and audio match the firmware");
   return 0;
}
#endif

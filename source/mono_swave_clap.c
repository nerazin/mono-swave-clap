/* Mono SWAVE: the Monomachine's SWAVE SAW, SWAVE PULS and SWAVE ENS machines
 * as three CLAP instruments in one bundle.
 *
 * The machines, the AMP section and ENS's chorus are mono_swave.h, a
 * fixed-point port of OS 1.32B. This file supplies what surrounds them on the
 * hardware and in a DAW:
 *  - the notes: one voice, the last note held wins; releasing it returns to
 *    the previous note still held (a new trig), or ends the note;
 *  - the main CPU's timing: notes, knobs and tempo are taken every 48 frames,
 *    each knob's word moves a quarter of the way to its value every 384
 *    frames, and modulation is added on top, unsmoothed, every 48 frames (as
 *    the Monomachine's LFOs are);
 *  - the pitch words: Pitch Mod is block word 30 (where the LFOs aimed at
 *    PTCH land), MIDI pitch bend is added to it as KIT ASSIGN JOY R/L would
 *    add it (MIRR on, PAGE PTCH, DEST Bend Range, ADD +63), and Master Tune
 *    is word 38 from the main OS's table;
 *  - the track filter, which sits between the machine and the AMP section on
 *    the unit: left out (Mono Filter Distortion is that filter), and stood in
 *    for by its plain level at its defaults, 0.4455;
 *  - ENS: a start as the machine has after its first 64 blocks (its chorus
 *    is muted for those), which the plug-in runs on silence at every reset;
 *  - sample-rate conversion when the host does not run at 44.1 kHz.
 * SPDX-License-Identifier: GPL-3.0-only */
#include <clap/clap.h>
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mono_swave.h"

/* The machine's eight knobs, then the AMP page's. */
enum { SYN, TUNE = SYN + 7, ATK = 8, HOLD, DEC, REL, VOL, PAN, PORT, PMOD, BEND, MTUNE, NPARAM };
#define KNOBS BEND      /* the parameters that are knobs: slewed, modulatable, 0..127 */
static const int amp_word[7] = {MS_ATK, MS_HOLD, MS_DEC, MS_REL, MS_VOL, MS_PAN, MS_PORT};
#define FILTER_LEVEL 0x3905a5   /* 0.4455: the track filter's level at its defaults, without its inversion */

typedef struct {
   const char *id, *name, *magic;
   const char *label[8];
   double def[8];
   unsigned unused;    /* knobs the machine does not read (hidden) */
} Machine;
static const Machine machines[MS_MACHINES] = {
   {"works.reconstruction.monoswavesaw", "Mono SWAVE SAW", "MSWS",
    {"Unison Level", "Unison Width", "Unison Inner", "-", "Sub Saw", "Sub 1", "Sub 2", "Tune"},
    {0, 0, 0, 0, 0, 0, 0, 64}, 0x08},
   {"works.reconstruction.monoswavepuls", "Mono SWAVE PULS", "MSWP",
    {"Unison Level", "Unison Width", "Sub 1", "Sub 2", "Pulse Width", "PW Sweep", "PW Restart", "Tune"},
    {0, 0, 0, 0, 64, 0, 0, 64}, 0},
   {"works.reconstruction.monoswaveens", "Mono SWAVE ENS", "MSWE",
    {"Pitch 2", "Pitch 3", "Pitch 4", "Wave", "Pulse Width", "Chorus Level", "Chorus Width", "Tune"},
    {63, 63, 63, 0, 64, 0, 127, 64}, 0},
};
static const char *const amp_label[7] = {"Attack", "Hold", "Decay", "Release", "Volume", "Pan", "Portamento"};
static const double amp_def[7] = {0, 0, 64, 64, 64, 64, 0};
static const char *const pitch_label[3] = {"Pitch Mod", "Bend Range", "Master Tune"};
static const double pitch_def[3] = {64, 1, 440}, pitch_lo[3] = {0, 0, 400}, pitch_hi[3] = {127, 7, 499.9};
static const char *const bend_name[8] = {"1/12", "2/12", "7/12", "1OCT", "2OCT", "4OCT", "8OCT", "16OC"};

static const char *const features[] = {
   CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, NULL
};
#define DESCRIPTOR(m, text) { .clap_version = CLAP_VERSION_INIT, .id = machines[m].id, .name = machines[m].name, \
   .vendor = "Independent DSP", .url = "", .manual_url = "", .support_url = "", .version = "0.2.0", \
   .description = text, .features = features }
static const clap_plugin_descriptor_t descriptors[MS_MACHINES] = {
   DESCRIPTOR(MS_SAW, "The Monomachine SWAVE SAW machine and AMP envelope, ported from its DSP code"),
   DESCRIPTOR(MS_PULS, "The Monomachine SWAVE PULS machine and AMP envelope, ported from its DSP code"),
   DESCRIPTOR(MS_ENS, "The Monomachine SWAVE ENS machine, AMP envelope and chorus, ported from its DSP code"),
};

#define NATIVE 44100.0
#define HISTORY 4096        /* resampler input ring */
#define PHASES 128
#define ENS_START 64   /* blocks of ENS's start, its chorus muted */

static double clamp(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }
static double def_of(int m, int id) { return id < 8 ? machines[m].def[id] : id < PMOD ? amp_def[id - 8] : pitch_def[id - PMOD]; }
static double lo_of(int id) { return id < PMOD ? 0 : pitch_lo[id - PMOD]; }
static double hi_of(int id) { return id < PMOD ? 127 : pitch_hi[id - PMOD]; }

/* ---- sample-rate conversion (windowed sinc, as in Mono Delay) -------------- */

typedef struct { double x[2]; } Frame;
typedef struct {
   float *kernel;
   Frame history[HISTORY];
   double ratio;   /* output n is the input at n x ratio */
   int half, taps;
   int64_t last;
   uint64_t index;
} Resampler;

static bool rs_prepare(Resampler *r, double from, double to) {
   r->ratio = from / to;
   r->half = (int)ceil(40.0 * fmax(1.0, r->ratio));
   r->taps = 2 * r->half;
   if (r->taps + 2 >= HISTORY) return false;
   r->kernel = calloc((size_t)(PHASES + 1) * r->taps, sizeof(float));
   if (!r->kernel) return false;
   const double cutoff = 0.95 * fmin(1.0, to / from), pi = 3.14159265358979323846;
   for (int p = 0; p <= PHASES; ++p) {
      float *k = r->kernel + (size_t)p * r->taps;
      double sum = 0.0;
      for (int i = 0; i < r->taps; ++i) {
         const double d = (double)(i - r->half + 1) - (double)p / PHASES, x = d / r->half, u = pi * cutoff * d;
         const double window = fabs(x) <= 1.0 ? 0.42 + 0.5 * cos(pi * x) + 0.08 * cos(2.0 * pi * x) : 0.0;
         k[i] = (float)(cutoff * (fabs(u) < 1e-14 ? 1.0 : sin(u) / u) * window);
         sum += k[i];
      }
      for (int i = 0; i < r->taps; ++i) k[i] /= (float)sum;
   }
   return true;
}
static void rs_reset(Resampler *r) { memset(r->history, 0, sizeof r->history); r->last = -1; r->index = 0; }
static void rs_push(Resampler *r, Frame f) { r->history[(size_t)(++r->last) & (HISTORY - 1)] = f; }
static bool rs_ready(const Resampler *r) { return (int64_t)floor((double)r->index * r->ratio) + r->half <= r->last; }
static Frame rs_pop(Resampler *r) {
   const double pos = (double)r->index * r->ratio, pp = (pos - floor(pos)) * PHASES;
   r->index++;
   const int64_t base = (int64_t)floor(pos);
   const int phase = (int)fmin(PHASES - 1, (int)pp);
   const float *a = r->kernel + (size_t)phase * r->taps, *b = a + r->taps;
   Frame out = {{0, 0}};
   for (int i = 0; i < r->taps; ++i) {
      const int64_t at = base - r->half + 1 + i;
      if (at < 0) continue;
      const Frame *f = &r->history[(size_t)at & (HISTORY - 1)];
      const double w = a[i] + (b[i] - a[i]) * (pp - phase);
      for (int c = 0; c < 2; ++c) out.x[c] += f->x[c] * w;
   }
   return out;
}

/* ---- plug-in --------------------------------------------------------------- */

typedef struct {
   clap_plugin_t plugin;
   const clap_host_t *host;
   int machine;
   _Atomic uint64_t value[NPARAM];    /* parameter values (double bits): host thread and audio thread */
   double mod[NPARAM];
   /* audio thread */
   bool active, direct, settled;
   uint32_t latency, blocks;
   int pos;
   int32_t knob[KNOBS], syn[8], amp[8], pitch_word, tick, pmod, mtune;
   int bend;                          /* the pitch bend's top 8 bits, 128 at the centre */
   int trig_word;                     /* block word 40 for the next block */
   double tempo;
   /* notes: the keys held, in the order pressed; the note sounding; the frame's news (1 on, 2 off) */
   int held[128], nheld, note, pending;
   MsPitch glide;
   MonoSwave core;
   MsAmp env;
   double out[2][MS_FRAMES];
   Resampler down;
} Plugin;

static double bits_to(uint64_t u) { double x; memcpy(&x, &u, sizeof x); return x; }
static uint64_t to_bits(double x) { uint64_t u; memcpy(&u, &x, sizeof u); return u; }
static double value(const Plugin *s, int id) { return bits_to(atomic_load_explicit(&s->value[id], memory_order_relaxed)); }
static void set_value(Plugin *s, int id, double x) { atomic_store_explicit(&s->value[id], to_bits(x), memory_order_relaxed); }

/* Block word 42 for a tempo, as the CPU sends it: 24 x BPM, the tempo kept to 30..300 (word 35 is 0x800000 / it). */
static int32_t tick_of(double bpm) { return (int32_t)clamp(lround(24.0 * bpm), 720, 7200); }

/* Start of a control frame (three blocks): the words the main CPU sends. */
static void control_frame(Plugin *s) {
   const uint32_t frame = s->blocks / 3;
   for (int p = 0; p < KNOBS; ++p) {
      const int32_t target = (int32_t)lrint(clamp(value(s, p), 0, 127) * 65536.0);
      if (!s->settled) s->knob[p] = target;
      else if ((frame & 7) == 1) s->knob[p] = (3 * s->knob[p] + target) >> 2;
      const int32_t word = s->mod[p] == 0 ? s->knob[p] : (int32_t)lrint(clamp(s->knob[p] + s->mod[p] * 65536.0, 0, 0x7effff));
      if (p < 8) s->syn[p] = word; else if (p < PMOD) s->amp[amp_word[p - 8]] = word; else s->pmod = word;
   }
   s->pmod = ms_add_mod(s->pmod, ms_bend(s->bend, (int)lrint(value(s, BEND))));
   s->mtune = ms_master_tune((int)lrint(clamp(value(s, MTUNE) * 10, 4000, 4999)));
   s->pitch_word = (s->note << 11) / 12;   /* the note word */
   s->tick = tick_of(s->tempo);
   s->trig_word = s->pending == 1 ? 0x81 : s->pending == 2 ? 0x02 : 0;
   s->pending = 0;
   if (!s->settled) {   /* the machine's assignment: its init and trig routines, the glide at the note, the AMP idle */
      ms_init(&s->core, s->machine);
      ms_pitch_reset(&s->glide, s->pitch_word);
      memset(&s->env, 0, sizeof s->env);
      s->env.stage = 3;
      s->trig_word |= 0x80;
   }
   s->settled = true;
}

/* One frame at 44.1 kHz; a block is computed at its first frame. */
static void native_frame(Plugin *s, double out[2]) {
   if (s->pos == 0) {
      if (s->blocks % 3 == 0) control_frame(s);
      s->blocks++;
      int32_t m[MS_FRAMES], g[2][MS_FRAMES], y[2][MS_FRAMES];
      ms_block(&s->core, s->syn, ms_pitch(&s->glide, s->pitch_word, s->syn[7], s->amp[MS_PORT], s->tick, s->pmod, s->mtune), s->trig_word & 0x80, m);
      ms_amp(&s->env, s->trig_word & 0x7f, s->amp, 0x800000 / s->tick, g);
      s->trig_word = 0;
      for (int c = 0; c < 2; ++c)
         for (int i = 0; i < MS_FRAMES; ++i) y[c][i] = ms_apply(g[c][i], ms_mpy(m[i], FILTER_LEVEL));
      if (s->machine == MS_ENS) ms_after_amp(&s->core, s->syn, s->amp[MS_PAN], y);
      for (int c = 0; c < 2; ++c)
         for (int i = 0; i < MS_FRAMES; ++i) s->out[c][i] = y[c][i] / 8388608.0;
   }
   out[0] = s->out[0][s->pos];
   out[1] = s->out[1][s->pos];
   s->pos = (s->pos + 1) % MS_FRAMES;
}

static void host_frame(Plugin *s, double out[2]) {
   if (s->direct) { native_frame(s, out); return; }
   while (!rs_ready(&s->down)) {
      Frame f;
      native_frame(s, f.x);
      rs_push(&s->down, f);
   }
   const Frame f = rs_pop(&s->down);
   out[0] = f.x[0];
   out[1] = f.x[1];
}

static void reset(Plugin *s) {
   s->settled = false;
   s->blocks = 0;
   s->pos = 0;
   s->nheld = 0;
   s->note = 60;
   s->pending = 0;
   s->bend = 128;
   rs_reset(&s->down);
   if (s->machine == MS_ENS) {   /* the machine's start, no note playing */
      double ignored[2];
      for (int n = 0; n < ENS_START * MS_FRAMES; ++n) native_frame(s, ignored);
   }
}

static void note_on(Plugin *s, int key) {
   for (int i = 0; i < s->nheld; ++i)
      if (s->held[i] == key) { memmove(s->held + i, s->held + i + 1, (size_t)(--s->nheld - i) * sizeof *s->held); break; }
   s->held[s->nheld++] = key;
   s->note = key;
   s->pending = 1;
}
static void note_off(Plugin *s, int key) {
   for (int i = 0; i < s->nheld; ++i)
      if (s->held[i] == key) {
         memmove(s->held + i, s->held + i + 1, (size_t)(--s->nheld - i) * sizeof *s->held);
         if (key != s->note) return;
         if (s->nheld) { s->note = s->held[s->nheld - 1]; s->pending = 1; }
         else s->pending = 2;
         return;
      }
}

static bool plug_init(const clap_plugin_t *p) { (void)p; return true; }
static void plug_destroy(const clap_plugin_t *p) {
   Plugin *s = p->plugin_data;
   free(s->down.kernel);
   free(s);
}
static bool plug_activate(const clap_plugin_t *p, double rate, uint32_t min, uint32_t max) {
   (void)min; (void)max;
   Plugin *s = p->plugin_data;
   if (s->active || !isfinite(rate) || rate < 8000 || rate > 768000) return false;
   s->direct = rate == NATIVE;
   uint32_t latency = 0;
   if (!s->direct) {
      if (!rs_prepare(&s->down, NATIVE, rate)) { free(s->down.kernel); s->down.kernel = NULL; return false; }
      latency = (uint32_t)lround(s->down.half * rate / NATIVE);   /* the resampler reads that far ahead */
   }
   reset(s);
   s->active = true;
   if (latency != s->latency) {
      s->latency = latency;
      const clap_host_latency_t *hl = s->host->get_extension ? s->host->get_extension(s->host, CLAP_EXT_LATENCY) : NULL;
      if (hl && hl->changed) hl->changed(s->host);
   }
   return true;
}
static void plug_deactivate(const clap_plugin_t *p) {
   Plugin *s = p->plugin_data;
   free(s->down.kernel);
   s->down.kernel = NULL;
   s->active = false;
}
static bool plug_start(const clap_plugin_t *p) { (void)p; return true; }
static void plug_stop(const clap_plugin_t *p) { (void)p; }
static void plug_reset(const clap_plugin_t *p) { reset(p->plugin_data); }
static void plug_on_main(const clap_plugin_t *p) { (void)p; }

static void event(Plugin *s, const clap_event_header_t *h) {
   if (!h || h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
   if (h->type == CLAP_EVENT_PARAM_VALUE && h->size >= sizeof(clap_event_param_value_t)) {
      const clap_event_param_value_t *e = (const void *)h;
      if (e->param_id < NPARAM && e->note_id == -1 && e->port_index == -1 && e->channel == -1 && e->key == -1 && isfinite(e->value))
         set_value(s, (int)e->param_id, clamp(e->value, lo_of((int)e->param_id), hi_of((int)e->param_id)));
   } else if (h->type == CLAP_EVENT_PARAM_MOD && h->size >= sizeof(clap_event_param_mod_t)) {
      const clap_event_param_mod_t *e = (const void *)h;
      if (e->param_id < KNOBS && e->note_id == -1 && e->port_index == -1 && e->channel == -1 && e->key == -1 && isfinite(e->amount))
         s->mod[e->param_id] = clamp(e->amount, -127, 127);
   } else if ((h->type == CLAP_EVENT_NOTE_ON || h->type == CLAP_EVENT_NOTE_OFF) && h->size >= sizeof(clap_event_note_t)) {
      const clap_event_note_t *e = (const void *)h;
      if (e->key >= 0 && e->key < 128) { if (h->type == CLAP_EVENT_NOTE_ON) note_on(s, e->key); else note_off(s, e->key); }
   } else if (h->type == CLAP_EVENT_MIDI && h->size >= sizeof(clap_event_midi_t)) {
      const clap_event_midi_t *e = (const void *)h;
      const int status = e->data[0] & 0xf0, key = e->data[1] & 0x7f;
      if (status == 0x90 && e->data[2] > 0) note_on(s, key);
      else if (status == 0x80 || status == 0x90) note_off(s, key);
      else if (status == 0xe0) s->bend = (e->data[2] & 0x7f) * 2 + ((e->data[1] & 0x7f) >> 6);
   }
}

static clap_process_status plug_process(const clap_plugin_t *p, const clap_process_t *pr) {
   Plugin *s = p->plugin_data;
   if (!s->active || !pr || pr->audio_outputs_count < 1) return CLAP_PROCESS_ERROR;
   clap_audio_buffer_t *out = &pr->audio_outputs[0];
   if (out->channel_count < 2 || (!out->data32 && !out->data64)) return CLAP_PROCESS_ERROR;
   s->tempo = pr->transport && (pr->transport->flags & CLAP_TRANSPORT_HAS_TEMPO) && isfinite(pr->transport->tempo) ? pr->transport->tempo : 120.0;
   const clap_input_events_t *ev = pr->in_events;
   const uint32_t count = ev && ev->size && ev->get ? ev->size(ev) : 0;
   uint32_t next = 0;
   for (uint32_t n = 0; n < pr->frames_count; ++n) {
      for (; next < count; ++next) {
         const clap_event_header_t *h = ev->get(ev, next);
         if (h && h->time > n) break;
         event(s, h);
      }
      double y[2];
      host_frame(s, y);
      for (int c = 0; c < 2; ++c)
         if (out->data32) out->data32[c][n] = (float)y[c]; else out->data64[c][n] = y[c];
   }
   out->constant_mask = 0;
   return CLAP_PROCESS_CONTINUE;
}

/* ---- extensions ------------------------------------------------------------ */

static uint32_t ports_count(const clap_plugin_t *p, bool input) { (void)p; return input ? 0 : 1; }
static bool ports_get(const clap_plugin_t *p, uint32_t i, bool input, clap_audio_port_info_t *port) {
   if (i >= ports_count(p, input) || !port) return false;
   memset(port, 0, sizeof *port);
   port->id = 0;
   snprintf(port->name, sizeof port->name, "Stereo Out");
   port->flags = CLAP_AUDIO_PORT_IS_MAIN | CLAP_AUDIO_PORT_SUPPORTS_64BITS | CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_SIZE;
   port->channel_count = 2;
   port->port_type = CLAP_PORT_STEREO;
   port->in_place_pair = CLAP_INVALID_ID;
   return true;
}
static const clap_plugin_audio_ports_t ports = {ports_count, ports_get};

static uint32_t params_count(const clap_plugin_t *p) { (void)p; return NPARAM; }
static bool params_info(const clap_plugin_t *p, uint32_t i, clap_param_info_t *pi) {
   const Plugin *s = p->plugin_data;
   if (i >= NPARAM || !pi) return false;
   memset(pi, 0, sizeof *pi);
   pi->id = i;
   pi->flags = i < 8 && (machines[s->machine].unused >> i & 1) ? CLAP_PARAM_IS_HIDDEN
             : CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS | (i < KNOBS ? CLAP_PARAM_IS_MODULATABLE : 0)
               | (i == BEND ? CLAP_PARAM_IS_STEPPED : 0);
   snprintf(pi->name, sizeof pi->name, "%s", i < 8 ? machines[s->machine].label[i] : i < PMOD ? amp_label[i - 8] : pitch_label[i - PMOD]);
   snprintf(pi->module, sizeof pi->module, "%s", i < 8 ? "Machine" : i < PMOD ? "Amp" : "Pitch");
   pi->min_value = lo_of((int)i);
   pi->max_value = hi_of((int)i);
   pi->default_value = def_of(s->machine, (int)i);
   return true;
}
static bool params_get(const clap_plugin_t *p, clap_id id, double *v) {
   if (id >= NPARAM || !v) return false;
   *v = value(p->plugin_data, (int)id);
   return true;
}
/* The knob value with what it means where that is plain. */
static bool params_to_text(const clap_plugin_t *p, clap_id id, double v, char *out, uint32_t cap) {
   const Plugin *s = p->plugin_data;
   if (id >= NPARAM || !out || !cap || !isfinite(v)) return false;
   const int32_t w = (int32_t)lrint(clamp(v, 0, 127) * 65536.0);
   const int m = s->machine;
   if (id == PMOD) snprintf(out, cap, "%.1f (%+.2f st)", v, (ms_mpy(0xb000, w) / 16777216.0 - 0x5800) * 12 / 2048);
   else if (id == BEND) {
      const int r = (int)lrint(clamp(v, 0, 7));
      snprintf(out, cap, "%s (+-%.2f st)", bend_name[r], ms_bend(255, r) * (0xb000 * 2 / 16777216.0) * 12 / 2048);
   }
   else if (id == MTUNE) snprintf(out, cap, "%.1f Hz", lrint(clamp(v, 400, 499.9) * 10) / 10.0);
   else if (id == TUNE) snprintf(out, cap, "%.1f (%+.0f cents)", v, (ms_mpy(0x2ab, w) / 16777216.0 - 341.5) * 1200 / 2048);
   else if (id == PAN) snprintf(out, cap, "%.1f (%+.0f)", v, floor(v) - 64);
   else if ((m == MS_SAW || m == MS_PULS) && id == 1) {   /* the outer pair's periods x (1 +- d); SAW's inner pair has half */
      const double d = m == MS_SAW ? (int)lrint(fmin(v, 127.5)) / 4096.0 : w / 8388608.0 / 64;
      snprintf(out, cap, "%.1f (-%.1f +%.1f cents)", v, 1200 * log2(1 + d), -1200 * log2(1 - d));
   }
   else if (m == MS_PULS && id == 6) snprintf(out, cap, "%.1f (%s)", v, w >= 0x400000 ? "on" : "off");
   else if (m == MS_ENS && id < 3) {
      int k = w >> 16, step = 0;
      while (k >= ms_ens_steps[step].run) k -= ms_ens_steps[step++].run;
      if (ms_ens_steps[step].num) snprintf(out, cap, "%.1f (%d/%d)", v, ms_ens_steps[step].num, ms_ens_steps[step].den);
      else if (ms_ens_steps[step].semi) snprintf(out, cap, "%.1f (%+d st)", v, ms_ens_steps[step].semi);
      else snprintf(out, cap, "%.1f (unison: off)", v);
   } else if ((m == MS_PULS || m == MS_ENS) && id == 4) snprintf(out, cap, "%.1f (%.0f%%)", v, 1 + 98 * w / 8388608.0);
   else snprintf(out, cap, "%.1f", v);
   return true;
}
static bool params_from_text(const clap_plugin_t *p, clap_id id, const char *text, double *v) {
   (void)p;
   if (id >= NPARAM || !text || !v) return false;
   if (id == BEND)
      for (int r = 0; r < 8; ++r)
         if (!strncmp(text, bend_name[r], strlen(bend_name[r]))) { *v = r; return true; }
   char *end = NULL;
   const double x = strtod(text, &end);
   if (end == text || !isfinite(x)) return false;
   *v = clamp(x, lo_of((int)id), hi_of((int)id));
   return true;
}
static void params_flush(const clap_plugin_t *p, const clap_input_events_t *in, const clap_output_events_t *out) {
   (void)out;
   const uint32_t n = in && in->size && in->get ? in->size(in) : 0;
   for (uint32_t i = 0; i < n; ++i) {
      const clap_event_header_t *h = in->get(in, i);
      if (h && (h->type == CLAP_EVENT_PARAM_VALUE || h->type == CLAP_EVENT_PARAM_MOD)) event(p->plugin_data, h);
   }
}
static const clap_plugin_params_t params = {params_count, params_info, params_get, params_to_text, params_from_text, params_flush};

static bool state_save(const clap_plugin_t *p, const clap_ostream_t *st) {
   const Plugin *s = p->plugin_data;
   uint8_t buf[8 + 8 * NPARAM] = {0};
   memcpy(buf, machines[s->machine].magic, 4);
   buf[7] = 2;
   for (int i = 0; i < NPARAM; ++i)
      for (int b = 0; b < 8; ++b) buf[8 + 8 * i + b] = (uint8_t)(to_bits(value(s, i)) >> (8 * b));
   for (uint64_t done = 0; done < sizeof buf;) {
      const int64_t n = st && st->write ? st->write(st, buf + done, sizeof buf - done) : -1;
      if (n <= 0) return false;
      done += (uint64_t)n;
   }
   return true;
}
static bool state_load(const clap_plugin_t *p, const clap_istream_t *st) {
   Plugin *s = p->plugin_data;
   uint8_t buf[8 + 8 * NPARAM];
   uint64_t size = 8;
   for (uint64_t done = 0; done < size;) {
      const int64_t n = st && st->read ? st->read(st, buf + done, size - done) : -1;
      if (n <= 0) return false;
      done += (uint64_t)n;
      if (done == 8 && size == 8) {   /* the header: version 1 had no pitch parameters */
         if (memcmp(buf, machines[s->machine].magic, 4) || buf[4] || buf[5] || buf[6] || buf[7] < 1 || buf[7] > 2) return false;
         size = 8 + 8 * (uint64_t)(buf[7] == 1 ? PMOD : NPARAM);
      }
   }
   double v[NPARAM];
   for (int i = PMOD; i < NPARAM; ++i) v[i] = def_of(s->machine, i);
   for (int i = 0; i < (int)(size - 8) / 8; ++i) {
      uint64_t u = 0;
      for (int b = 0; b < 8; ++b) u |= (uint64_t)buf[8 + 8 * i + b] << (8 * b);
      v[i] = bits_to(u);
      if (!isfinite(v[i]) || v[i] < lo_of(i) || v[i] > hi_of(i)) return false;
   }
   for (int i = 0; i < NPARAM; ++i) set_value(s, i, v[i]);
   const clap_host_params_t *hp = s->host->get_extension ? s->host->get_extension(s->host, CLAP_EXT_PARAMS) : NULL;
   if (hp && hp->rescan) hp->rescan(s->host, CLAP_PARAM_RESCAN_VALUES);
   return true;
}
static const clap_plugin_state_t state = {state_save, state_load};

static uint32_t remote_count(const clap_plugin_t *p) { (void)p; return 2; }
static bool remote_get(const clap_plugin_t *p, uint32_t i, clap_remote_controls_page_t *page) {
   const Plugin *s = p->plugin_data;
   if (i > 1 || !page) return false;
   memset(page, 0, sizeof *page);
   snprintf(page->section_name, sizeof page->section_name, "%s", machines[s->machine].name);
   snprintf(page->page_name, sizeof page->page_name, "%s", i ? "Amp" : "Machine");
   page->page_id = i;
   for (int k = 0; k < CLAP_REMOTE_CONTROLS_COUNT; ++k)
      page->param_ids[k] = i == 0 ? (machines[s->machine].unused >> k & 1 ? CLAP_INVALID_ID : (clap_id)k) : k < 7 ? (clap_id)(ATK + k) : CLAP_INVALID_ID;
   return true;
}
static const clap_plugin_remote_controls_t remote = {remote_count, remote_get};
static uint32_t latency_get(const clap_plugin_t *p) { return ((const Plugin *)p->plugin_data)->latency; }
static const clap_plugin_latency_t latency = {latency_get};
static uint32_t notes_count(const clap_plugin_t *p, bool input) { (void)p; return input ? 1 : 0; }
static bool notes_get(const clap_plugin_t *p, uint32_t i, bool input, clap_note_port_info_t *port) {
   (void)p;
   if (i || !input || !port) return false;
   memset(port, 0, sizeof *port);
   port->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
   port->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
   snprintf(port->name, sizeof port->name, "Notes");
   return true;
}
static const clap_plugin_note_ports_t notes = {notes_count, notes_get};

static const void *plug_extension(const clap_plugin_t *p, const char *id) {
   (void)p;
   if (!id) return NULL;
   if (!strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &ports;
   if (!strcmp(id, CLAP_EXT_PARAMS)) return &params;
   if (!strcmp(id, CLAP_EXT_STATE)) return &state;
   if (!strcmp(id, CLAP_EXT_REMOTE_CONTROLS) || !strcmp(id, CLAP_EXT_REMOTE_CONTROLS_COMPAT)) return &remote;
   if (!strcmp(id, CLAP_EXT_LATENCY)) return &latency;
   if (!strcmp(id, CLAP_EXT_NOTE_PORTS)) return &notes;
   return NULL;
}

/* ---- factory and entry ----------------------------------------------------- */

static uint32_t factory_count(const clap_plugin_factory_t *f) { (void)f; return MS_MACHINES; }
static const clap_plugin_descriptor_t *factory_desc(const clap_plugin_factory_t *f, uint32_t i) { (void)f; return i < MS_MACHINES ? &descriptors[i] : NULL; }
static const clap_plugin_t *factory_create(const clap_plugin_factory_t *f, const clap_host_t *host, const char *id) {
   (void)f;
   if (!host || !id || !clap_version_is_compatible(host->clap_version)) return NULL;
   int m = 0;
   while (m < MS_MACHINES && strcmp(id, descriptors[m].id)) ++m;
   if (m == MS_MACHINES) return NULL;
   Plugin *s = calloc(1, sizeof *s);
   if (!s) return NULL;
   s->host = host;
   s->machine = m;
   s->tempo = 120.0;
   for (int i = 0; i < NPARAM; ++i) atomic_init(&s->value[i], to_bits(def_of(m, i)));
   s->plugin = (clap_plugin_t){
      .desc = &descriptors[m], .plugin_data = s, .init = plug_init, .destroy = plug_destroy,
      .activate = plug_activate, .deactivate = plug_deactivate, .start_processing = plug_start,
      .stop_processing = plug_stop, .reset = plug_reset, .process = plug_process,
      .get_extension = plug_extension, .on_main_thread = plug_on_main,
   };
   return &s->plugin;
}
static const clap_plugin_factory_t factory = {factory_count, factory_desc, factory_create};

static bool entry_init(const char *path) { (void)path; ms_tables(); return true; }   /* read-only from here on */
static void entry_deinit(void) {}
static const void *entry_factory(const char *id) { return id && !strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &factory : NULL; }
CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, entry_init, entry_deinit, entry_factory};

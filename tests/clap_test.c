/* Loads the built bundle like a host and checks what the core test cannot:
 * the wrapper around mono_swave.h. usage: clap_test "build/Mono SWAVE.clap"
 *  - the bundle offers the three machines, each with its knobs and the AMP page
 *  - at 44.1 kHz each plug-in's output is the core's, exactly, for notes
 *    taken on the 48-frame control grid (ENS after its 64-block start)
 *  - SAW as a single saw sounds a note at the core's frequency (A4 0.04
 *    cents under 440 Hz) at 44.1 and 48 kHz, and TUNE at 127 raises it by
 *    197 cents
 *  - releasing the last note fades out; releasing the newest of two held
 *    notes returns to the other
 *  - ENS's chorus changes the sound, and its width separates left and right
 *  - a note's onset comes no earlier than the reported latency, and at most
 *    one control frame and one block after it, at 22.05 to 96 kHz
 *  - state round trip, rejected bad states, parameter text, the hidden knob */
#include <clap/clap.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../source/mono_swave.h"

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); puts(""); exit(1); } } while (0)
#define N (192 * 512)   /* whole process calls */
#define NP 15
#define PORT 14
#define MAXEV 16

typedef union { clap_event_header_t h; clap_event_param_value_t v; clap_event_note_t n; clap_event_midi_t m; } Event;
typedef struct { clap_input_events_t api; Event v[NP + MAXEV]; uint32_t n; } Events;
static uint32_t ev_size(const clap_input_events_t *e) { return ((const Events *)e->ctx)->n; }
static const clap_event_header_t *ev_get(const clap_input_events_t *e, uint32_t i) { return &((const Events *)e->ctx)->v[i].h; }
static const void *host_ext(const clap_host_t *h, const char *id) { (void)h; (void)id; return NULL; }
static void host_cb(const clap_host_t *h) { (void)h; }
static const clap_host_t host = {CLAP_VERSION_INIT, NULL, "test", "", "", "1", host_ext, host_cb, host_cb, host_cb};
static const clap_plugin_factory_t *factory;
static const char *const ids[3] = {"works.reconstruction.monoswavesaw", "works.reconstruction.monoswavepuls", "works.reconstruction.monoswaveens"};

typedef struct { uint8_t data[256]; uint64_t size, pos; } Buffer;
static int64_t buf_write(const clap_ostream_t *s, const void *p, uint64_t n) { Buffer *b = s->ctx; if (b->size + n > sizeof b->data) return -1; memcpy(b->data + b->size, p, n); b->size += n; return (int64_t)n; }
static int64_t buf_read(const clap_istream_t *s, void *p, uint64_t n) { Buffer *b = s->ctx; if (n > b->size - b->pos) n = b->size - b->pos; memcpy(p, b->data + b->pos, n); b->pos += n; return (int64_t)n; }

/* A plug-in with these parameter values (NAN: the default), activated at rate. */
static const clap_plugin_t *make(int m, const double v[NP], double rate) {
   const clap_plugin_t *p = factory->create_plugin(factory, &host, ids[m]);
   CHECK(p && p->init(p), "create");
   const clap_plugin_params_t *params = p->get_extension(p, CLAP_EXT_PARAMS);
   Events ev = {{NULL, ev_size, ev_get}, {{{0}}}, 0};
   for (uint32_t i = 0; i < NP; ++i)
      if (!isnan(v[i]))
         ev.v[ev.n++].v = (clap_event_param_value_t){{sizeof ev.v[0].v, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0}, i, NULL, -1, -1, -1, -1, v[i]};
   ev.api.ctx = &ev;
   params->flush(p, &ev.api, NULL);
   CHECK(p->activate(p, rate, 1, 512) && p->start_processing(p), "activate");
   return p;
}
/* Notes: frame, on (1) or off (0), key; sorted by frame. */
typedef struct { int at, on, key; } Note;
static void run(const clap_plugin_t *p, const Note *notes, int count, double out[2][N]) {
   Events ev = {{NULL, ev_size, ev_get}, {{{0}}}, 0};
   ev.api.ctx = &ev;
   int next = 0;
   for (uint32_t at = 0; at < N; at += 512) {
      ev.n = 0;
      for (; next < count && notes[next].at < (int)at + 512; ++next)
         ev.v[ev.n++].n = (clap_event_note_t){{sizeof ev.v[0].n, (uint32_t)(notes[next].at - (int)at), CLAP_CORE_EVENT_SPACE_ID,
            (uint16_t)(notes[next].on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), 0}, -1, 0, 0, (int16_t)notes[next].key, 1.0};
      double *o[2] = {out[0] + at, out[1] + at};
      clap_audio_buffer_t ao = {NULL, o, 2, 0, 0};
      clap_process_t pr = {0};
      pr.frames_count = 512; pr.audio_outputs = &ao; pr.audio_outputs_count = 1; pr.in_events = &ev.api;
      CHECK(p->process(p, &pr) == CLAP_PROCESS_CONTINUE, "process");
   }
}
static uint32_t latency_of(const clap_plugin_t *p) { return ((const clap_plugin_latency_t *)p->get_extension(p, CLAP_EXT_LATENCY))->get(p); }
static void done(const clap_plugin_t *p) { p->stop_processing(p); p->deactivate(p); p->destroy(p); }
/* The frequency of the left output over [from, to), from its upward crossings of half its peak (the saw's
 * edges; the band-limited edge rings around zero). */
static double frequency(double out[2][N], int from, int to, double rate) {
   double top = 0;
   for (int n = from; n < to; ++n) top = fmax(top, out[0][n]);
   int first = -1, last = -1, count = 0;
   for (int n = from + 1; n < to; ++n)
      if (out[0][n - 1] < top / 2 && out[0][n] >= top / 2) { if (first < 0) first = n; last = n; ++count; }
   return count > 1 ? (count - 1) * rate / (last - first) : 0;
}
/* SAW's main saw for a settled note at TUNE 64: one edge a period, 2^24 a sample. */
static double note_hz(int note) {
   MsPitch g;
   ms_pitch_reset(&g, (note << 11) / 12);
   const int32_t p = ms_pitch(&g, (note << 11) / 12, 64 << 16, 0, 2880);
   return 44100 / ((double)ms_mpy(0x17a6, ms_sat(ms_w(ms_exp[0x7ff - (p & 0x7ff)]) >> (p >> 11))) / 16777216.0);
}
static double peak(double out[2][N], int c, int from, int to) {
   double x = 0;
   for (int n = from; n < to; ++n) x = fmax(x, fabs(out[c][n]));
   return x;
}
static double rms_diff(double out[2][N], int from, int to) {
   double d = 0, s = 0;
   for (int n = from; n < to; ++n) { d += pow(out[0][n] - out[1][n], 2); s += pow(out[0][n] + out[1][n], 2) / 4; }
   return sqrt(d / s);
}

static double out[2][N];

int main(int argc, char **argv) {
   CHECK(argc == 2, "usage: clap_test bundle.clap");
   char path[1024];
   snprintf(path, sizeof path, "%s/Contents/MacOS/Mono SWAVE", argv[1]);
   void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
   CHECK(lib, "dlopen %s", path);
   const clap_plugin_entry_t *entry = dlsym(lib, "clap_entry");
   CHECK(entry && entry->init(argv[1]), "entry");
   factory = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
   CHECK(factory && factory->get_plugin_count(factory) == 3, "three plug-ins");
   for (uint32_t m = 0; m < 3; ++m) CHECK(!strcmp(factory->get_plugin_descriptor(factory, m)->id, ids[m]), "descriptor %u", m);
   ms_tables();
   const double defaults[NP] = {NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN};

   /* exact against the core at 44.1 kHz: notes on and off at and between control frames, without and with
    * portamento, the machines' knobs away from their defaults */
   static const Note seq[] = {{100, 1, 48}, {6000, 0, 48}, {9000, 1, 67}, {9047, 1, 30}, {15000, 0, 67}, {20000, 0, 30}, {30001, 1, 90}, {60000, 0, 90}, {70000, 1, 60}};
   static const double knobs[3][8] = {{90, 70, 100, 0, 60, 80, 50, 64}, {90, 70, 40, 60, 30, 20, 100, 64}, {70, 100, 40, 60, 90, 80, 100, 64}};
   const int nseq = (int)(sizeof seq / sizeof seq[0]);
   for (int t = 0; t < 6; ++t) {
      const int m = t % 3, start = m == MS_ENS ? 64 : 0;
      double v[NP];
      memcpy(v, defaults, sizeof v);
      memcpy(v, knobs[m], sizeof knobs[m]);
      if (t >= 3) v[PORT] = 50;   /* about 170 blocks at 120 BPM */
      const clap_plugin_t *p = make(m, v, 44100);
      CHECK(latency_of(p) == 0, "latency at 44.1 kHz");
      const clap_plugin_params_t *params = p->get_extension(p, CLAP_EXT_PARAMS);
      CHECK(params->count(p) == NP, "parameter count");
      int32_t syn[8], amp[8] = {0}, note = 60, word = (60 << 11) / 12, pending = 0, k = 0, bad = 0;
      for (uint32_t i = 0; i < NP; ++i) {
         double x;
         CHECK(params->get_value(p, i, &x), "value");
         const int32_t w = (int32_t)x << 16;
         if (i < 8) syn[i] = w;
         else amp[(const int[]){MS_ATK, MS_HOLD, MS_DEC, MS_REL, MS_VOL, MS_PAN, MS_PORT}[i - 8]] = w;
      }
      run(p, seq, nseq, out);
      done(p);
      static MonoSwave f;
      MsAmp env = {3, 0, 0, {0, 0}};
      MsPitch glide;
      int32_t from = 0, mid = 0;
      ms_init(&f, m);
      ms_pitch_reset(&glide, (60 << 11) / 12);
      for (int b = 0; b < start + N / MS_FRAMES; ++b) {
         for (; k < nseq && seq[k].at <= (b - start) * MS_FRAMES; ++k)   /* no release here falls back to a note still held */
            if (seq[k].on) { pending = 1; note = seq[k].key; }
            else if (seq[k].key == note) pending = 2;
         int trig = 0;
         if (b % 3 == 0) { trig = b ? (pending == 1 ? 0x81 : pending == 2 ? 0x02 : 0) : 0x80 | (pending == 1 ? 0x81 : 0); pending = 0; word = (note << 11) / 12; }
         int32_t y[MS_FRAMES], g[2][MS_FRAMES], lr[2][MS_FRAMES];
         const int32_t pitch = ms_pitch(&glide, word, syn[7], amp[MS_PORT], 2880);
         if (b - start == 30001 / MS_FRAMES + 3 + 60) mid = pitch;   /* 60 blocks into the glide from 30 to 90 */
         if (b - start == 30001 / MS_FRAMES - 3) from = pitch;
         ms_block(&f, syn, pitch, trig & 0x80, y);
         ms_amp(&env, trig & 0x7f, amp, 0x800000 / 2880, g);
         for (int c = 0; c < 2; ++c)
            for (int i = 0; i < MS_FRAMES; ++i) lr[c][i] = ms_apply(g[c][i], ms_mpy(y[i], 0x3905a5));
         if (m == MS_ENS) ms_after_amp(&f, syn, amp[MS_PAN], lr);
         if (b >= start)
            for (int c = 0; c < 2; ++c)
               for (int i = 0; i < MS_FRAMES; ++i) bad += out[c][(b - start) * MS_FRAMES + i] != lr[c][i] / 8388608.0;
      }
      CHECK(bad == 0, "machine %d, PORT %g: %d frames differ from the core", m, v[PORT], bad);
      CHECK(t < 3 || (mid > from + 100 && mid < from + (60 << 11) / 12 - 100), "PORT 50 does not glide (%d from %d)", mid, from);
      CHECK(peak(out, 0, 0, N) > 0.05, "machine %d is silent", m);
   }

   /* pitch: SAW at its defaults is one saw */
   const double plain[NP] = {NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, 127, NAN, 127, NAN, NAN};
   static const Note a4[] = {{0, 1, 69}};
   for (int r = 0; r < 2; ++r) {
      const double rate = r ? 48000 : 44100;
      const clap_plugin_t *p = make(MS_SAW, plain, rate);
      run(p, a4, 1, out);
      done(p);
      const double hz = frequency(out, N / 4, N, rate);
      CHECK(fabs(hz - note_hz(69)) < 0.01 && fabs(1200 * log2(hz / 440)) < 1, "A4 at %.0f Hz is %.3f Hz, not %.3f", rate, hz, note_hz(69));
   }
   double tuned[NP];
   memcpy(tuned, plain, sizeof tuned);
   tuned[7] = 127;
   {
      const clap_plugin_t *p = make(MS_SAW, tuned, 44100);
      run(p, a4, 1, out);
      done(p);
      const double cents = 1200 * log2(frequency(out, N / 4, N, 44100) / 440);
      CHECK(fabs(cents - 197) < 1, "TUNE 127 is %+.1f cents", cents);
   }

   /* the note stack and the release */
   {
      double v[NP];
      memcpy(v, plain, sizeof v);
      v[11] = 0;   /* Release 0 */
      static const Note stack[] = {{0, 1, 57}, {20000, 1, 69}, {40000, 0, 69}, {70000, 0, 57}};
      const clap_plugin_t *p = make(MS_SAW, v, 44100);
      run(p, stack, 4, out);
      done(p);
      CHECK(fabs(frequency(out, 5000, 20000, 44100) - note_hz(57)) < 0.05, "first note");
      CHECK(fabs(frequency(out, 25000, 40000, 44100) - note_hz(69)) < 0.05, "second note");
      CHECK(fabs(frequency(out, 45000, 70000, 44100) - note_hz(57)) < 0.05, "back to the first note");
      CHECK(peak(out, 0, 50000, 70000) > 0.05 && peak(out, 0, 75000, N) < 1e-6, "release to silence (%g)", peak(out, 0, 75000, N));
   }

   /* ENS's chorus: its level changes the sound; at width 0 left and right stay as close as the centre pan
    * leaves them (1.3%), at full width they come apart */
   {
      double v[NP], side[3];
      static double dry[2][N];
      for (int t = 0; t < 3; ++t) {
         memcpy(v, plain, sizeof v);
         v[5] = t ? 127 : 0;
         v[6] = t == 2 ? 127 : 0;
         const clap_plugin_t *p = make(MS_ENS, v, 44100);
         run(p, a4, 1, t ? out : dry);
         done(p);
         side[t] = rms_diff(t ? out : dry, N / 4, N);
      }
      double d = 0, e = 0;
      for (int n = N / 4; n < N; ++n) { d += pow(out[0][n] - dry[0][n], 2); e += pow(dry[0][n], 2); }
      CHECK(sqrt(d / e) > 0.1 && side[0] < 0.02 && side[1] < 0.02 && side[2] > 0.3, "chorus %g, sides %g %g %g", sqrt(d / e), side[0], side[1], side[2]);
   }

   /* onset against the reported latency */
   static const double rates[] = {22050, 44100, 48000, 96000};
   for (int r = 0; r < 4; ++r) {
      const double rate = rates[r], ratio = rate / 44100;
      for (int t = 0; t < 4; ++t) {
         const int at = 10000 + 37 * t;
         const Note one[] = {{at, 1, 45}};
         const clap_plugin_t *p = make(MS_PULS, plain, rate);   /* a square: no part of it near zero for long */
         const int latency = (int)latency_of(p);
         run(p, one, 1, out);
         done(p);
         const double level = peak(out, 0, at + 4000, at + 8000);
         int onset = 0;
         while (onset < N && fabs(out[0][onset]) < 0.05 * level) ++onset;
         CHECK(onset >= at + latency - 2 && onset <= at + latency + (int)ceil(66 * ratio) + 2,
               "%.0f Hz: note at %d, latency %d, onset %d", rate, at, latency, onset);
      }
   }

   /* state, parameter text */
   for (int m = 0; m < 3; ++m) {
      double v[NP];
      for (int i = 0; i < NP; ++i) v[i] = (i * 37 + m * 11) % 128;
      const clap_plugin_t *p = make(m, v, 44100), *q = make(m, defaults, 44100), *other = make((m + 1) % 3, defaults, 44100);
      const clap_plugin_state_t *st = p->get_extension(p, CLAP_EXT_STATE);
      Buffer b = {{0}, 0, 0};
      clap_ostream_t os = {&b, buf_write};
      CHECK(st->save(p, &os), "save");
      clap_istream_t is = {&b, buf_read};
      CHECK(((const clap_plugin_state_t *)q->get_extension(q, CLAP_EXT_STATE))->load(q, &is), "load");
      const clap_plugin_params_t *pp = q->get_extension(q, CLAP_EXT_PARAMS);
      for (uint32_t i = 0; i < NP; ++i) { double x; CHECK(pp->get_value(q, i, &x) && x == v[i], "restored value %u", i); }
      b.pos = 0;
      CHECK(!((const clap_plugin_state_t *)other->get_extension(other, CLAP_EXT_STATE))->load(other, &is), "another machine's state accepted");
      b.pos = 0; b.size -= 8;
      CHECK(!st->load(p, &is), "short state accepted");
      b.pos = 0; b.size += 8; b.data[7] = 2;
      CHECK(!st->load(p, &is), "unknown version accepted");
      b.pos = 0; b.data[7] = 1; b.data[8] ^= 0xff; b.data[15] = 0x7f;
      CHECK(!st->load(p, &is), "bad value accepted");
      char text[64];
      CHECK(pp->value_to_text(q, 7, 127, text, sizeof text) && strstr(text, "+197 cents"), "TUNE text %s", text);
      done(p); done(q); done(other);
   }
   {
      const clap_plugin_t *p = make(MS_ENS, defaults, 44100), *q = make(MS_PULS, defaults, 44100), *r = make(MS_SAW, defaults, 44100);
      const clap_plugin_params_t *pp = p->get_extension(p, CLAP_EXT_PARAMS), *qp = q->get_extension(q, CLAP_EXT_PARAMS), *rp = r->get_extension(r, CLAP_EXT_PARAMS);
      char text[64];
      CHECK(pp->value_to_text(p, 0, 63, text, sizeof text) && !strcmp(text, "63.0 (unison: off)"), "interval text %s", text);
      CHECK(pp->value_to_text(p, 1, 100, text, sizeof text) && !strcmp(text, "100.0 (+7 st)"), "interval text %s", text);
      CHECK(pp->value_to_text(p, 2, 105, text, sizeof text) && !strcmp(text, "105.0 (3/2)"), "interval text %s", text);
      CHECK(pp->value_to_text(p, 2, 0, text, sizeof text) && !strcmp(text, "0.0 (-12 st)"), "interval text %s", text);
      CHECK(pp->value_to_text(p, 13, 64, text, sizeof text) && !strcmp(text, "64.0 (+0)"), "pan text %s", text);
      CHECK(qp->value_to_text(q, 4, 64, text, sizeof text) && !strcmp(text, "64.0 (50%)"), "width text %s", text);
      CHECK(qp->value_to_text(q, 6, 64, text, sizeof text) && !strcmp(text, "64.0 (on)"), "restart text %s", text);
      CHECK(rp->value_to_text(r, 1, 127, text, sizeof text) && !strcmp(text, "127.0 (-52.9 +54.5 cents)"), "detune text %s", text);
      clap_param_info_t info;
      CHECK(rp->get_info(r, 3, &info) && (info.flags & CLAP_PARAM_IS_HIDDEN) && !(info.flags & CLAP_PARAM_IS_AUTOMATABLE), "SAW's unused knob");
      CHECK(rp->get_info(r, 4, &info) && !(info.flags & CLAP_PARAM_IS_HIDDEN), "SAW's Sub Saw");
      double x;
      CHECK(pp->text_to_value(p, 3, "100", &x) && x == 100, "text to value");
      done(p); done(q); done(r);
   }
   entry->deinit();
   puts("PASS");
   return 0;
}

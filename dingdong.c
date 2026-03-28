#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define PLUGIN_URI    "urn:dingdong:DingDongDelay"
#define MAX_DELAY_SEC 2.5f
#define MAX_TAPS      8
#define GRAIN_SIZE    8192u
#define SH_BUF_SIZE   (GRAIN_SIZE * 2u)

#define VOICE_B_DETUNE  1.007f
#define STEREO_DETUNE   1.003f
#define LFO_DEPTH       0.004f
#define LFO_RATE_HZ     0.15f
#define VOICE_B_LEVEL   0.35f
#define WOW_RATE_HZ     2.3f
#define WOW_DEPTH       0.0006f
#define RPONG_ATTEN     0.84f
#define SAT_DRIVE       1.8f

typedef enum {
    PORT_AUDIO_IN_L    = 0,
    PORT_AUDIO_IN_R    = 1,
    PORT_AUDIO_OUT_L   = 2,
    PORT_AUDIO_OUT_R   = 3,
    PORT_MIDI_IN       = 4,
    PORT_LEVEL         = 5,
    PORT_MIX           = 6,
    PORT_DECAY         = 7,
    PORT_HP_FREQ       = 8,
    PORT_LP_FREQ       = 9,
    PORT_DELAY_MS      = 10,
    PORT_SHIMMER_MIX   = 11,
    PORT_SHIMMER_PITCH = 12,
    PORT_STEREO        = 13,
} PortIndex;

/* Single all-pass filter stage for shimmer diffusion */
typedef struct {
    float    buf[2048];
    uint32_t pos;
    uint32_t delay;
    float    g;
} AllPass;

static inline float allpass_run(AllPass *ap, float x)
{
    float d = ap->buf[ap->pos];
    float y = -ap->g * x + d;
    ap->buf[ap->pos] = x + ap->g * d;
    if (++ap->pos >= ap->delay) ap->pos = 0;
    return y;
}

/* Soft saturation using a fast tanh approximation */
static inline float soft_clip(float x)
{
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

typedef struct {
    float   *delay_buf;
    uint32_t buf_size;
    uint32_t write_pos;

    float    sh_buf[SH_BUF_SIZE];
    uint32_t sh_write;
    float    sh_phase_a[2];
    float    sh_phase_b[2];

    AllPass  ap[4];

    float hp_state;
    float lp_state;
    float prev_in;
    float age_lp;
} Channel;

typedef struct {
    const float            *audio_in_l;
    const float            *audio_in_r;
    float                  *audio_out_l;
    float                  *audio_out_r;
    const LV2_Atom_Sequence *midi_in;
    const float            *p_level;
    const float            *p_mix;
    const float            *p_decay;
    const float            *p_hp_freq;
    const float            *p_lp_freq;
    const float            *p_delay_ms;
    const float            *p_shimmer_mix;
    const float            *p_shimmer_pitch;
    const float            *p_stereo;

    LV2_URID_Map *map;
    LV2_URID      urid_midi;

    Channel  ch[2];
    float    current_delay;
    float    target_delay;

    uint64_t tap_times[MAX_TAPS];
    int      tap_count;
    uint64_t sample_pos;

    float    lfo_phase[2];
    float    lfo_dp;
    float    wow_phase;
    float    wow_dp;

    double   sample_rate;
    float    smooth_coeff;
} DingDong;

static void tap_register(DingDong *self, uint64_t t)
{
    if (self->tap_count > 0) {
        uint64_t gap = t - self->tap_times[0];
        if (gap < (uint64_t)(0.10 * self->sample_rate)) return;
        if (gap > (uint64_t)(3.0  * self->sample_rate)) self->tap_count = 0;
    }

    for (int i = MAX_TAPS - 1; i > 0; i--)
        self->tap_times[i] = self->tap_times[i - 1];
    self->tap_times[0] = t;
    if (self->tap_count < MAX_TAPS) self->tap_count++;
    if (self->tap_count < 2) return;

    double total = 0.0;
    int pairs = self->tap_count - 1;
    for (int i = 0; i < pairs; i++)
        total += (double)(self->tap_times[i] - self->tap_times[i + 1]);

    float avg = (float)(total / pairs);
    float lo  = 0.10f * (float)self->sample_rate;
    float hi  = 2.00f * (float)self->sample_rate;
    if (avg >= lo && avg <= hi)
        self->target_delay = avg;
}

static inline float grain_read(Channel *ch, float ratio, float ph[2], float dp)
{
    const float gf  = (float)GRAIN_SIZE;
    const float bsz = (float)SH_BUF_SIZE;
    float out = 0.0f;

    for (int g = 0; g < 2; g++) {
        float p = ph[g];
        float w = sinf((float)M_PI * p);
        w = w * w;

        float rp = (float)ch->sh_write - gf * (1.0f + (1.0f - ratio) * p);
        while (rp <  0.0f) rp += bsz;
        while (rp >= bsz)  rp -= bsz;

        uint32_t ri  = (uint32_t)rp;
        float    frc = rp - (float)ri;
        uint32_t ri2 = (ri + 1u < SH_BUF_SIZE) ? ri + 1u : 0u;

        out += (ch->sh_buf[ri] * (1.0f - frc) + ch->sh_buf[ri2] * frc) * w;

        p += dp;
        if (p >= 1.0f) p -= 1.0f;
        ph[g] = p;
    }
    return out;
}

static inline float shimmer_read(Channel *ch, float ratio_a, float ratio_b)
{
    float dp = 1.0f / (float)GRAIN_SIZE;
    float a  = grain_read(ch, ratio_a, ch->sh_phase_a, dp);
    float b  = grain_read(ch, ratio_b, ch->sh_phase_b, dp);
    float raw = a + b * VOICE_B_LEVEL;

    float diff = raw;
    for (int k = 0; k < 4; k++)
        diff = allpass_run(&ch->ap[k], diff);

    return raw * 0.5f + diff * 0.5f;
}

static inline float delay_read(Channel *ch, float samples)
{
    float bsz = (float)ch->buf_size;
    float rp  = (float)ch->write_pos - samples;
    if (rp <  0.0f) rp += bsz;
    if (rp >= bsz)  rp -= bsz;

    uint32_t ri  = (uint32_t)rp;
    float    frc = rp - (float)ri;
    uint32_t ri2 = (ri + 1u < ch->buf_size) ? ri + 1u : 0u;
    return ch->delay_buf[ri] * (1.0f - frc) + ch->delay_buf[ri2] * frc;
}

static inline void channel_process(
    Channel *ch,
    float in, float delayed, int feed_dry,
    float ra, float rb, float shimmer,
    float a_hp, float a_lp, float one_m_lp,
    float decay, float mix, float level,
    float dry, float age_a, float atten,
    float *out, float *write)
{
    /* HP → LP filter chain on feedback signal */
    float hp = a_hp * (ch->hp_state + delayed - ch->prev_in);
    ch->hp_state = hp;
    ch->prev_in  = delayed;
    ch->lp_state = one_m_lp * hp + a_lp * ch->lp_state;
    float filtered = ch->lp_state;

    /* Age filter: darker repeats at higher decay values */
    ch->age_lp = age_a * ch->age_lp + (1.0f - age_a) * filtered;
    float aged = filtered * (1.0f - decay * 0.4f) + ch->age_lp * (decay * 0.4f);

    /* Soft saturation for analog warmth */
    float sat = soft_clip(aged * SAT_DRIVE) / SAT_DRIVE;

    /* Shimmer */
    ch->sh_buf[ch->sh_write] = sat;
    if (++ch->sh_write >= SH_BUF_SIZE) ch->sh_write = 0;

    float sh = (shimmer > 0.001f) ? shimmer_read(ch, ra, rb) : 0.0f;

    *write = (feed_dry ? in : 0.0f) + sat * decay + sh * shimmer * decay;
    *out   = (dry * (1.0f - mix) + sat * mix * level + sh * shimmer * level) * atten;
}

static void channel_init_allpass(Channel *ch)
{
    static const uint32_t delays[4] = { 347, 569, 811, 1213 };
    static const float    gains[4]  = { 0.55f, 0.50f, 0.45f, 0.40f };
    for (int k = 0; k < 4; k++) {
        memset(ch->ap[k].buf, 0, sizeof(ch->ap[k].buf));
        ch->ap[k].pos   = 0;
        ch->ap[k].delay = delays[k];
        ch->ap[k].g     = gains[k];
    }
}

static LV2_Handle instantiate(
    const LV2_Descriptor *descriptor, double rate,
    const char *bundle_path, const LV2_Feature *const *features)
{
    (void)descriptor; (void)bundle_path;

    DingDong *self = (DingDong *)calloc(1, sizeof(DingDong));
    if (!self) return NULL;

    self->sample_rate = rate;

    for (int i = 0; features[i]; i++)
        if (!strcmp(features[i]->URI, LV2_URID__map))
            self->map = (LV2_URID_Map *)features[i]->data;

    if (!self->map) { free(self); return NULL; }
    self->urid_midi = self->map->map(self->map->handle, LV2_MIDI__MidiEvent);

    uint32_t bsz = (uint32_t)(MAX_DELAY_SEC * rate) + 4;
    for (int c = 0; c < 2; c++) {
        self->ch[c].delay_buf = (float *)calloc(bsz, sizeof(float));
        if (!self->ch[c].delay_buf) {
            for (int j = 0; j < c; j++) free(self->ch[j].delay_buf);
            free(self);
            return NULL;
        }
        self->ch[c].buf_size    = bsz;
        self->ch[c].sh_phase_a[0] = 0.0f;
        self->ch[c].sh_phase_a[1] = 0.5f;
        self->ch[c].sh_phase_b[0] = 0.25f;
        self->ch[c].sh_phase_b[1] = 0.75f;
        channel_init_allpass(&self->ch[c]);
    }

    self->current_delay = 0.5f * (float)rate;
    self->target_delay  = 0.5f * (float)rate;
    self->smooth_coeff  = 1.0f - expf(-1.0f / (0.04f * (float)rate));
    self->lfo_phase[0]  = 0.0f;
    self->lfo_phase[1]  = (float)M_PI * 0.5f;
    self->lfo_dp        = 2.0f * (float)M_PI * LFO_RATE_HZ / (float)rate;
    self->wow_phase     = 0.0f;
    self->wow_dp        = 2.0f * (float)M_PI * WOW_RATE_HZ / (float)rate;

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void *data)
{
    DingDong *self = (DingDong *)instance;
    switch ((PortIndex)port) {
        case PORT_AUDIO_IN_L:    self->audio_in_l      = (const float *)data;              break;
        case PORT_AUDIO_IN_R:    self->audio_in_r      = (const float *)data;              break;
        case PORT_AUDIO_OUT_L:   self->audio_out_l     = (float *)data;                    break;
        case PORT_AUDIO_OUT_R:   self->audio_out_r     = (float *)data;                    break;
        case PORT_MIDI_IN:       self->midi_in         = (const LV2_Atom_Sequence *)data;  break;
        case PORT_LEVEL:         self->p_level         = (const float *)data;              break;
        case PORT_MIX:           self->p_mix           = (const float *)data;              break;
        case PORT_DECAY:         self->p_decay         = (const float *)data;              break;
        case PORT_HP_FREQ:       self->p_hp_freq       = (const float *)data;              break;
        case PORT_LP_FREQ:       self->p_lp_freq       = (const float *)data;              break;
        case PORT_DELAY_MS:      self->p_delay_ms      = (const float *)data;              break;
        case PORT_SHIMMER_MIX:   self->p_shimmer_mix   = (const float *)data;              break;
        case PORT_SHIMMER_PITCH: self->p_shimmer_pitch = (const float *)data;              break;
        case PORT_STEREO:        self->p_stereo        = (const float *)data;              break;
    }
}

static void activate(LV2_Handle instance)
{
    DingDong *self = (DingDong *)instance;

    for (int c = 0; c < 2; c++) {
        Channel *ch = &self->ch[c];
        memset(ch->delay_buf, 0, ch->buf_size * sizeof(float));
        memset(ch->sh_buf, 0, sizeof(ch->sh_buf));
        ch->hp_state = ch->lp_state = ch->prev_in = ch->age_lp = 0.0f;
        ch->write_pos = ch->sh_write = 0;
        ch->sh_phase_a[0] = 0.0f;  ch->sh_phase_a[1] = 0.5f;
        ch->sh_phase_b[0] = 0.25f; ch->sh_phase_b[1] = 0.75f;
        channel_init_allpass(ch);
    }

    self->tap_count     = 0;
    self->sample_pos    = 0;
    self->lfo_phase[0]  = 0.0f;
    self->lfo_phase[1]  = (float)M_PI * 0.5f;
    self->wow_phase     = 0.0f;
    self->current_delay = 0.5f * (float)self->sample_rate;
    self->target_delay  = self->current_delay;
}

static void run(LV2_Handle instance, uint32_t n_samples)
{
    DingDong *self = (DingDong *)instance;

    const float level    = *self->p_level;
    const float mix      = *self->p_mix;
    const float decay    = *self->p_decay;
    const float delay_ms = *self->p_delay_ms;
    const int   shpitch  = (int)(*self->p_shimmer_pitch + 0.5f);
    const int   stereo   = (int)(*self->p_stereo + 0.5f);
    float       hp_fc    = *self->p_hp_freq;
    float       lp_fc    = *self->p_lp_freq;

    /* Shimmer mix uses a squared curve for a more gradual feel */
    float shimmer = *self->p_shimmer_mix;
    shimmer = shimmer * shimmer;

    float sh_base;
    switch (shpitch) {
        case 1:  sh_base = 1.4983f; break;  /* perfect fifth  */
        case 2:  sh_base = 0.5000f; break;  /* octave down    */
        default: sh_base = 2.0000f; break;  /* octave up      */
    }

    float age_a = decay * 0.9f;

    /* Reset to knob value if no tap has come in for 5 seconds */
    if (self->tap_count > 0 &&
        self->sample_pos > self->tap_times[0] + (uint64_t)(5.0 * self->sample_rate))
        self->tap_count = 0;

    if (self->tap_count == 0)
        self->target_delay = (delay_ms / 1000.0f) * (float)self->sample_rate;

    float dmax = (float)self->ch[0].buf_size - 2.0f;
    float dmin = 0.002f * (float)self->sample_rate;
    if (self->target_delay > dmax) self->target_delay = dmax;
    if (self->target_delay < dmin) self->target_delay = dmin;

    /* MIDI tap tempo */
    if (self->midi_in) {
        LV2_ATOM_SEQUENCE_FOREACH(self->midi_in, ev) {
            if (ev->body.type != self->urid_midi) continue;
            const uint8_t *msg    = (const uint8_t *)(ev + 1);
            const uint8_t  status = msg[0] & 0xF0u;
            uint64_t       t      = self->sample_pos + (uint64_t)ev->time.frames;
            if      (status == 0x90u && msg[2] > 0)   tap_register(self, t);
            else if (status == 0xB0u && msg[2] > 63u) tap_register(self, t);
            else if (status == 0xC0u)                  tap_register(self, t);
        }
    }

    /* Filter coefficients */
    float fs = (float)self->sample_rate;
    if (hp_fc <   20.0f) hp_fc = 20.0f;
    if (hp_fc > 2000.0f) hp_fc = 2000.0f;
    if (lp_fc <  500.0f) lp_fc = 500.0f;
    if (lp_fc > fs * 0.45f) lp_fc = fs * 0.45f;

    const float a_hp     = expf(-2.0f * (float)M_PI * hp_fc / fs);
    const float a_lp     = expf(-2.0f * (float)M_PI * lp_fc / fs);
    const float one_m_lp = 1.0f - a_lp;

    Channel *chL = &self->ch[0];
    Channel *chR = &self->ch[1];

    for (uint32_t i = 0; i < n_samples; i++) {

        /* Smooth delay time changes to avoid clicks */
        self->current_delay +=
            (self->target_delay - self->current_delay) * self->smooth_coeff;

        /* Wow/flutter modulates delay time slightly for an analog feel */
        float wow = 1.0f + WOW_DEPTH * sinf(self->wow_phase);
        self->wow_phase += self->wow_dp;
        if (self->wow_phase >= 2.0f * (float)M_PI)
            self->wow_phase -= 2.0f * (float)M_PI;

        float d = self->current_delay * wow;
        if (d < dmin) d = dmin;
        if (d > dmax) d = dmax;

        float in_l = self->audio_in_l[i];
        float in_r = self->audio_in_r[i];

        /* Shimmer LFO — L and R out of phase for width */
        float lfo_l = 1.0f + LFO_DEPTH * sinf(self->lfo_phase[0]);
        float lfo_r = 1.0f + LFO_DEPTH * sinf(self->lfo_phase[1]);
        self->lfo_phase[0] += self->lfo_dp;
        self->lfo_phase[1] += self->lfo_dp;
        if (self->lfo_phase[0] >= 2.0f * (float)M_PI) self->lfo_phase[0] -= 2.0f * (float)M_PI;
        if (self->lfo_phase[1] >= 2.0f * (float)M_PI) self->lfo_phase[1] -= 2.0f * (float)M_PI;

        float ra_l = sh_base * lfo_l;
        float ra_r = sh_base * STEREO_DETUNE * lfo_r;
        float rb_l = sh_base * VOICE_B_DETUNE * lfo_l;
        float rb_r = sh_base * VOICE_B_DETUNE * STEREO_DETUNE * lfo_r;

        float out_l, out_r, write_l, write_r;

        if (stereo == 0) {
            float in_m    = (in_l + in_r) * 0.5f;
            float delayed = delay_read(chL, d);

            channel_process(chL, in_m, delayed, 1,
                ra_l, rb_l, shimmer,
                a_hp, a_lp, one_m_lp,
                decay, mix, level, in_m, age_a, 1.0f,
                &out_l, &write_l);

            chL->delay_buf[chL->write_pos] = write_l;
            if (++chL->write_pos >= chL->buf_size) chL->write_pos = 0;

            self->audio_out_l[i] = out_l;
            self->audio_out_r[i] = out_l;

        } else {
            /* Ping-pong: L feeds dry signal, R receives only L's echo.
               Signal path: in → L → R → L → R ...                    */
            float in_m = (in_l + in_r) * 0.5f;

            channel_process(chL, in_m, delay_read(chR, d), 1,
                ra_l, rb_l, shimmer,
                a_hp, a_lp, one_m_lp,
                decay, mix, level, in_m, age_a, 1.0f,
                &out_l, &write_l);

            channel_process(chR, 0.0f, delay_read(chL, d), 0,
                ra_r, rb_r, shimmer,
                a_hp, a_lp, one_m_lp,
                decay, mix, level, in_r, age_a, RPONG_ATTEN,
                &out_r, &write_r);

            chL->delay_buf[chL->write_pos] = write_l;
            chR->delay_buf[chR->write_pos] = write_r;
            if (++chL->write_pos >= chL->buf_size) chL->write_pos = 0;
            if (++chR->write_pos >= chR->buf_size) chR->write_pos = 0;

            self->audio_out_l[i] = out_l;
            self->audio_out_r[i] = out_r;
        }
    }

    self->sample_pos += n_samples;
}

static void deactivate(LV2_Handle instance) { (void)instance; }

static void cleanup(LV2_Handle instance)
{
    DingDong *self = (DingDong *)instance;
    for (int c = 0; c < 2; c++) free(self->ch[c].delay_buf);
    free(self);
}

static const void *extension_data(const char *uri) { (void)uri; return NULL; }

static const LV2_Descriptor descriptor = {
    PLUGIN_URI, instantiate, connect_port,
    activate, run, deactivate, cleanup, extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor *lv2_descriptor(uint32_t index)
{
    return (index == 0) ? &descriptor : NULL;
}

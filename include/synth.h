#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>
#include <stddef.h>

#define SAMPLE_RATE 44100
#define MAX_VOICES  16

typedef enum
{
    OSC_SINE,
    OSC_SQUARE,
    OSC_SAW,
    OSC_TRIANGLE
} Oscillator_type;

typedef struct
{
    Oscillator_type type;
    double          frequency;
    double          phase;
    double          phase_increment;
} Oscillator;

typedef enum
{
    ENV_IDLE,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
} Envelope_state;

typedef struct
{
    double attack_time;
    double decay_time;
    double sustain_level;
    double release_time;
    
    Envelope_state state;
    double         current_level;
    double         time_in_state;
} ADSR_Envelope;

typedef struct
{
    uint8_t        midi_note;
    uint8_t        velocity;
    uint8_t        active;
    
    Oscillator     osc;
    ADSR_Envelope  env;
} Voice;

typedef struct
{
    Voice  voices[MAX_VOICES];
    double master_volume;
} Synth;

void oscillator_init(Oscillator *osc, Oscillator_type type, double frequency);
void oscillator_set_frequency(Oscillator *osc, double frequency);
float oscillator_next_sample(Oscillator *osc);

void envelope_init(ADSR_Envelope *env, double attack, double decay, double sustain, double release);
void envelope_trigger(ADSR_Envelope *env);
void envelope_release(ADSR_Envelope *env);
float envelope_next_value(ADSR_Envelope *env, double sample_rate);

void synth_init(Synth *synth);
void synth_note_on(Synth *synth, uint8_t note, uint8_t velocity);
void synth_note_off(Synth *synth, uint8_t note);
void synth_render(Synth *synth, float *buffer, size_t frame_count);

double midi_note_to_frequency(uint8_t note);

#endif /* SYNTH_H */

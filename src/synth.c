#include "synth.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double midi_note_to_frequency(uint8_t note)
{
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

void oscillator_init(Oscillator *osc, Oscillator_type type, double frequency)
{
    osc->type = type;
    osc->frequency = frequency;
    osc->phase = 0.0;
    osc->phase_increment = frequency / SAMPLE_RATE;
}

void oscillator_set_frequency(Oscillator *osc, double frequency)
{
    osc->frequency = frequency;
    osc->phase_increment = frequency / SAMPLE_RATE;
}

float oscillator_next_sample(Oscillator *osc)
{
    float sample = 0.0f;
    
    switch (osc->type)
    {
    case OSC_SINE:
        sample = (float)sin(2.0 * M_PI * osc->phase);
        break;
        
    case OSC_SQUARE:
        sample = osc->phase < 0.5 ? 1.0f : -1.0f;
        break;
        
    case OSC_SAW:
        sample = (float)(2.0 * osc->phase - 1.0);
        break;
        
    case OSC_TRIANGLE:
        if (osc->phase < 0.5)
            sample = (float)(4.0 * osc->phase - 1.0);
        else
            sample = (float)(3.0 - 4.0 * osc->phase);
        break;
    }
    
    osc->phase += osc->phase_increment;
    if (osc->phase >= 1.0)
        osc->phase -= 1.0;
    
    return sample;
}

void envelope_init(ADSR_Envelope *env, double attack, double decay, double sustain, double release)
{
    env->attack_time = attack;
    env->decay_time = decay;
    env->sustain_level = sustain;
    env->release_time = release;
    env->state = ENV_IDLE;
    env->current_level = 0.0;
    env->time_in_state = 0.0;
}

void envelope_trigger(ADSR_Envelope *env)
{
    env->state = ENV_ATTACK;
    env->time_in_state = 0.0;
}

void envelope_release(ADSR_Envelope *env)
{
    if (env->state != ENV_IDLE)
    {
        env->state = ENV_RELEASE;
        env->time_in_state = 0.0;
    }
}

float envelope_next_value(ADSR_Envelope *env, double sample_rate)
{
    double dt = 1.0 / sample_rate;
    env->time_in_state += dt;
    
    switch (env->state)
    {
    case ENV_IDLE:
        env->current_level = 0.0;
        break;
        
    case ENV_ATTACK:
        if (env->attack_time > 0.0)
        {
            env->current_level = env->time_in_state / env->attack_time;
            if (env->current_level >= 1.0)
            {
                env->current_level = 1.0;
                env->state = ENV_DECAY;
                env->time_in_state = 0.0;
            }
        }
        else
        {
            env->current_level = 1.0;
            env->state = ENV_DECAY;
            env->time_in_state = 0.0;
        }
        break;
        
    case ENV_DECAY:
        if (env->decay_time > 0.0)
        {
            double decay_progress = env->time_in_state / env->decay_time;
            env->current_level = 1.0 - (1.0 - env->sustain_level) * decay_progress;
            if (decay_progress >= 1.0)
            {
                env->current_level = env->sustain_level;
                env->state = ENV_SUSTAIN;
                env->time_in_state = 0.0;
            }
        }
        else
        {
            env->current_level = env->sustain_level;
            env->state = ENV_SUSTAIN;
            env->time_in_state = 0.0;
        }
        break;
        
    case ENV_SUSTAIN:
        env->current_level = env->sustain_level;
        break;
        
    case ENV_RELEASE:
        if (env->release_time > 0.0)
        {
            double release_level = env->current_level * (1.0 - env->time_in_state / env->release_time);
            if (release_level <= 0.0)
            {
                env->current_level = 0.0;
                env->state = ENV_IDLE;
            }
            else
            {
                env->current_level = release_level;
            }
        }
        else
        {
            env->current_level = 0.0;
            env->state = ENV_IDLE;
        }
        break;
    }
    
    return (float)env->current_level;
}

void synth_init(Synth *synth)
{
    memset(synth, 0, sizeof(Synth));
    synth->master_volume = 0.15;
    
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        oscillator_init(&synth->voices[i].osc, OSC_SAW, 440.0);
        envelope_init(&synth->voices[i].env, 0.002, 0.3, 0.3, 0.5);
        synth->voices[i].active = 0;
    }
}

void synth_note_on(Synth *synth, uint8_t note, uint8_t velocity)
{
    int free_voice = -1;
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        if (!synth->voices[i].active)
        {
            free_voice = i;
            break;
        }
    }
    
    if (free_voice == -1)
    {
        for (int i = 0; i < MAX_VOICES; ++i)
        {
            if (synth->voices[i].env.state == ENV_RELEASE || 
                synth->voices[i].env.state == ENV_IDLE)
            {
                free_voice = i;
                break;
            }
        }
        
        if (free_voice == -1)
        {
            free_voice = 0;
        }
    }
    
    Voice *v = &synth->voices[free_voice];
    v->midi_note = note;
    v->velocity = velocity;
    v->active = 1;
    
    double freq = midi_note_to_frequency(note);
    oscillator_set_frequency(&v->osc, freq);
    envelope_trigger(&v->env);
}

void synth_note_off(Synth *synth, uint8_t note)
{
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        Voice *v = &synth->voices[i];
        if (v->active && v->midi_note == note)
        {
            envelope_release(&v->env);
        }
    }
}

void synth_render(Synth *synth, float *buffer, size_t frame_count)
{
    memset(buffer, 0, frame_count * sizeof(float));
    
    for (size_t i = 0; i < frame_count; ++i)
    {
        float mix = 0.0f;
        int active_voices = 0;
        
        for (int v = 0; v < MAX_VOICES; ++v)
        {
            Voice *voice = &synth->voices[v];
            
            if (voice->active)
            {
                float osc_sample = oscillator_next_sample(&voice->osc);
                float env_value = envelope_next_value(&voice->env, SAMPLE_RATE);
                float velocity_scale = voice->velocity / 127.0f;
                
                mix += osc_sample * env_value * velocity_scale;
                active_voices++;
                
                if (voice->env.state == ENV_IDLE)
                {
                    voice->active = 0;
                }
            }
        }
        
        float output = mix * (float)synth->master_volume;
        
        if (output > 1.0f) output = 1.0f;
        if (output < -1.0f) output = -1.0f;
        
        buffer[i] = output;
    }
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/midi_parser.h"
#include "include/json_generator.h"
#include "include/midi_preprocessor.h"
#include "include/synth.h"

#define MINIAUDIO_IMPLEMENTATION
#include "include/miniaudio.h"

static void print_usage(const char *prog)
{
    printf("Usage: %s <input.mid> [-o output.json] [-a output.wav]\n", prog);
    printf("  -o : Parse MIDI and write to JSON file\n");
    printf("  -a : Generate audio WAV file from MIDI\n");
    printf("  At least one option (-o or -a) must be specified\n");
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        print_usage(argv[0]);
        return 1;
    }

    char *input_file = argv[1];
    char *json_output = NULL;
    char *audio_output = NULL;

    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
        {
            json_output = argv[++i];
        }
        else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc)
        {
            audio_output = argv[++i];
        }
        else
        {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!json_output && !audio_output)
    {
        printf("Error: At least one option (-o or -a) must be specified\n");
        print_usage(argv[0]);
        return 1;
    }

    FILE *fp = fopen(input_file, "rb");
    if (!fp)
    {
        printf("Error: Could not open MIDI file '%s'\n", input_file);
        return 1;
    }

    int status;
    MIDI_file midi = get_MIDI_file(fp, &status);
    fclose(fp);

    if (status != 0)
    {
        printf("Error: Failed to parse MIDI file\n");
        return 1;
    }

    if (json_output)
    {
        if (!write_MIDI_to_JSON_file(&midi, json_output))
        {
            printf("Error: Failed to write JSON file\n");
            free_MIDI_file(&midi);
            return 1;
        }
        printf("Generated JSON: %s\n", json_output);
    }

    if (audio_output)
    {
        Tempo_map tmap = build_tempo_map(&midi, &status);
        if (status != 0)
        {
            printf("Error: Failed to build tempo map\n");
            free_MIDI_file(&midi);
            return 1;
        }

        Timeline timeline = merge_tracks_to_timeline(&midi, &tmap, &status);
        if (status != 0)
        {
            printf("Error: Failed to merge tracks\n");
            free_tempo_map(&tmap);
            free_MIDI_file(&midi);
            return 1;
        }

        if (timeline.count == 0)
        {
            printf("Error: No events to process\n");
            free_timeline(&timeline);
            free_tempo_map(&tmap);
            free_MIDI_file(&midi);
            return 1;
        }

        double duration_ms = timeline.events[timeline.count - 1].timestamp_ms + 1000.0;
        size_t total_samples = (size_t)((duration_ms / 1000.0) * SAMPLE_RATE);
        
        float *audio_buffer = (float*)malloc(total_samples * sizeof(float));
        if (!audio_buffer)
        {
            printf("Error: Failed to allocate audio buffer\n");
            free_timeline(&timeline);
            free_tempo_map(&tmap);
            free_MIDI_file(&midi);
            return 1;
        }

        Synth synth;
        synth_init(&synth);

        size_t event_idx = 0;
        double current_time_ms = 0.0;
        double ms_per_sample = 1000.0 / SAMPLE_RATE;

        for (size_t i = 0; i < total_samples; ++i)
        {
            while (event_idx < timeline.count && timeline.events[event_idx].timestamp_ms <= current_time_ms)
            {
                Timed_event *te = &timeline.events[event_idx];
                MTrk_event *ev = te->event;

                if (ev->kind == CH)
                {
                    uint8_t type = ev->channel_ev.type;
                    uint8_t note = ev->channel_ev.param1;
                    uint8_t velocity = ev->channel_ev.param2;

                    if (type == 0x9 && velocity > 0)
                    {
                        synth_note_on(&synth, note, velocity);
                    }
                    else if (type == 0x8 || (type == 0x9 && velocity == 0))
                    {
                        synth_note_off(&synth, note);
                    }
                }

                event_idx++;
            }

            synth_render(&synth, &audio_buffer[i], 1);
            current_time_ms += ms_per_sample;

            if (i % (SAMPLE_RATE * 2) == 0 || i == total_samples - 1)
            {
                printf("\rRendering: %.1f%%", (i * 100.0) / total_samples);
                fflush(stdout);
            }
        }
        printf("\rRendering: 100.0%%\n");

        ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1, SAMPLE_RATE);
        ma_encoder encoder;

        if (ma_encoder_init_file(audio_output, &config, &encoder) != MA_SUCCESS)
        {
            printf("Error: Failed to initialize audio encoder\n");
            free(audio_buffer);
            free_timeline(&timeline);
            free_tempo_map(&tmap);
            free_MIDI_file(&midi);
            return 1;
        }

        ma_uint64 frames_written = 0;
        ma_encoder_write_pcm_frames(&encoder, audio_buffer, total_samples, &frames_written);
        ma_encoder_uninit(&encoder);

        printf("Generated audio: %s (%.2f seconds)\n", audio_output, duration_ms / 1000.0);

        free(audio_buffer);
        free_timeline(&timeline);
        free_tempo_map(&tmap);
    }

    free_MIDI_file(&midi);
    return 0;
}

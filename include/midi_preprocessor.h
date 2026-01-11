#ifndef MIDI_PREPROCESSOR_H
#define MIDI_PREPROCESSOR_H

#include <stdint.h>
#include <stddef.h>
#include "midi_parser.h"


typedef struct
{
    uint64_t tick;
    uint32_t us_per_qn;
    double bpm;
} Tempo_change;

typedef struct
{
    Tempo_change *changes;
    size_t        count;
    size_t        cap;
} Tempo_map;

typedef struct
{
    double      timestamp_ms;
    uint8_t     track_idx;
    MTrk_event *event;
} Timed_event;

typedef struct
{
    Timed_event *events;
    size_t       count;
    size_t       cap;
} Timeline;


Tempo_map build_tempo_map(const MIDI_file *midi, int *status);
void      free_tempo_map(Tempo_map *tmap);

double tick_to_milliseconds(uint64_t tick, const MThd *mthd, const Tempo_map *tmap);

Timeline merge_tracks_to_timeline(const MIDI_file *midi, const Tempo_map *tmap, int *status);
void     free_timeline(Timeline *timeline);


#endif /* MIDI_PREPROCESSOR_H */

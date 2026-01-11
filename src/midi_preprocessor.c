#include <stdlib.h>
#include <string.h>
#include "midi_preprocessor.h"


static int tempo_map_grow(Tempo_map *tmap, size_t min_needed)
{
    size_t cap = tmap->cap ? tmap->cap : 16;
    while (cap < min_needed)
    {
        if (cap > SIZE_MAX / 2) return 0;
        cap *= 2;
    }
    if (cap > SIZE_MAX / sizeof(Tempo_change)) return 0;

    Tempo_change *tc = realloc(tmap->changes, cap * sizeof(*tc));
    if (!tc) return 0;

    tmap->changes = tc;
    tmap->cap = cap;
    return 1;
}

static inline int tempo_map_ensure_one(Tempo_map *tmap)
{
    if (tmap->count < tmap->cap) return 1;
    return tempo_map_grow(tmap, tmap->count + 1);
}

static int compare_tempo_tick(const void *a, const void *b)
{
    const Tempo_change *ta = (const Tempo_change*)a;
    const Tempo_change *tb = (const Tempo_change*)b;
    if (ta->tick < tb->tick) return -1;
    if (ta->tick > tb->tick) return  1;
    return 0;
}

Tempo_map build_tempo_map(const MIDI_file *midi, int *status)
{
    uint32_t ntracks = midi->mthd.ntracks;
    Tempo_map tmap = { 0 };
    for (uint32_t i = 0; i < ntracks; ++i)
    {
        uint64_t cum_delta = 0;
        MTrk curr_track = midi->mtrk[i];
        size_t nevents  = curr_track.count;
        for (size_t k = 0; k < nevents; ++k)
        {
            MTrk_event curr_ev = curr_track.events[k];
            cum_delta += curr_ev.delta_time;
            if (curr_ev.kind == META && curr_ev.meta_ev.type == 0x51)
            {
                uint8_t *data = (uint8_t*)curr_ev.meta_ev.data;
                uint32_t us_per_qn = (data[0] << 16) | (data[1] << 8) | data[2];
                double bpm = 60000000.0 / us_per_qn;

                Tempo_change tchange;
                tchange.tick      = cum_delta;
                tchange.us_per_qn = us_per_qn;
                tchange.bpm       = bpm;
                
                if (!tempo_map_ensure_one(&tmap))
                {
                    *status = -1;
                    free_tempo_map(&tmap);
                    return tmap;
                }
                tmap.changes[tmap.count++] = tchange;
            }
        }
    }
    
    if (tmap.count == 0)
    {
        if (!tempo_map_ensure_one(&tmap))
        {
            *status = -1;
            return (Tempo_map){ 0 };
        }
        Tempo_change default_tempo;
        default_tempo.tick      = 0;
        default_tempo.us_per_qn = 500000;
        default_tempo.bpm       = 120.0;

        tmap.changes[tmap.count++] = default_tempo;
    }

    qsort(tmap.changes, tmap.count, sizeof(Tempo_change), compare_tempo_tick);
    
    *status = 0;
    return tmap;
}

void free_tempo_map(Tempo_map *tmap)
{
    if (tmap && tmap->changes)
    {
        free(tmap->changes);
        tmap->changes = NULL;
        tmap->count = 0;
        tmap->cap = 0;
    }
}

static double tick_to_seconds_smpte(uint64_t tick, int8_t smpte, uint8_t ticks_per_frame)
{
    int frame_rate = -smpte;
    return (double)tick / (frame_rate * ticks_per_frame);
}

static double tick_to_ms_metrical(uint64_t tick, const Tempo_map *tmap, uint16_t ticks_per_beat)
{
    if (tmap->count == 0) return 0.0;
    if (tick == 0)        return 0.0;
    
    double ms_accumulated = 0.0;
    uint64_t prev_tick = 0;
    uint32_t current_us_per_qn = tmap->changes[0].us_per_qn;
    for (size_t i = 0; i < tmap->count; ++i)
    {
        Tempo_change *tc = &tmap->changes[i];
        if (tc->tick >= tick) break;
        if (tc->tick > prev_tick)
        {
            uint64_t delta_ticks = tc->tick - prev_tick;
            double ms_interval = (double)(delta_ticks * current_us_per_qn) / (ticks_per_beat * 1000.0);
            ms_accumulated += ms_interval;
        }
        prev_tick = tc->tick;
        current_us_per_qn = tc->us_per_qn;
    }
    uint64_t final_delta = tick - prev_tick;
    double ms_final = (double)(final_delta * current_us_per_qn) / (ticks_per_beat * 1000.0);
    ms_accumulated += ms_final;
    
    return ms_accumulated;
}

double tick_to_milliseconds(uint64_t tick, const MThd *mthd, const Tempo_map *tmap)
{
    if (!mthd->is_fps)
    {
        return tick_to_ms_metrical(tick, tmap, mthd->timediv.ticks_per_beat);
    }
    else
    {
        return tick_to_seconds_smpte(tick,
                                     mthd->timediv.frames_per_sec.smpte,
                                     mthd->timediv.frames_per_sec.ticks) * 1000.0;
    }
}

static int timeline_grow(Timeline *tl, size_t min_needed)
{
    size_t cap = tl->cap ? tl->cap : 256;
    while (cap < min_needed)
    {
        if (cap > SIZE_MAX / 2) return 0;
        cap *= 2;
    }
    if (cap > SIZE_MAX / sizeof(Timed_event)) return 0;

    Timed_event *te = realloc(tl->events, cap * sizeof(*te));
    if (!te) return 0;

    tl->events = te;
    tl->cap = cap;
    return 1;
}

static inline int timeline_ensure_one(Timeline *tl)
{
    if (tl->count < tl->cap) return 1;
    return timeline_grow(tl, tl->count + 1);
}

static int compare_timed_event(const void *a, const void *b)
{
    const Timed_event *ta = (const Timed_event*)a;
    const Timed_event *tb = (const Timed_event*)b;
    if (ta->timestamp_ms < tb->timestamp_ms) return -1;
    if (ta->timestamp_ms > tb->timestamp_ms) return 1;
    return 0;
}

Timeline merge_tracks_to_timeline(const MIDI_file *midi, const Tempo_map *tmap, int *status)
{
    Timeline timeline = { 0 };
    uint16_t ntracks = midi->mthd.ntracks;
    for (uint16_t i = 0; i < ntracks; ++i)
    {
        MTrk *curr_track = &midi->mtrk[i];
        uint64_t cum_ticks = 0;
        for (size_t k = 0; k < curr_track->count; ++k)
        {
            MTrk_event *curr_ev = &curr_track->events[k];
            cum_ticks += curr_ev->delta_time;
            double timestamp_ms = tick_to_milliseconds(cum_ticks, &midi->mthd, tmap);
            if (!timeline_ensure_one(&timeline))
            {
                *status = -1;
                free_timeline(&timeline);
                return timeline;
            }
            Timed_event tev;
            tev.timestamp_ms = timestamp_ms;
            tev.track_idx    = i;
            tev.event        = curr_ev;

            timeline.events[timeline.count++] = tev;
        }
    }
    qsort(timeline.events, timeline.count, sizeof(Timed_event), compare_timed_event);

    *status = 0;
    return timeline;
}

void free_timeline(Timeline *timeline)
{
    if (timeline && timeline->events)
    {
        free(timeline->events);
        timeline->events = NULL;
        timeline->count = 0;
        timeline->cap = 0;
    }
}
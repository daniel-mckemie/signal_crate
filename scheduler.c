#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduler.h"

static ScheduledEvent sched_events[MAX_EVENTS];
static int sched_count = 0;
static double scheduler_time_ms = 0.0;

// forward declare ScriptEvent to avoid circular include
typedef struct ScriptEvent {
    char script_line[256];
    void* scriptbox_ptr;
} ScriptEvent;

void scheduler_add(void (*callback)(void*), void* userdata, double interval_ms) {
    ScriptEvent* new_e = (ScriptEvent*)userdata;

    // Parse alias + param for new event
    char new_alias[64] = {0}, new_param[64] = {0};
    sscanf(new_e->script_line, "%*[^ (](%*f,%*f,%63[^,],%63[^,]", new_alias, new_param);

    // --- Cancel any existing event for same alias/param (no free yet) ---
    for (int i = 0; i < sched_count; i++) {
        ScriptEvent* existing_e = (ScriptEvent*)sched_events[i].userdata;
        if (!existing_e) continue;

        char ex_alias[64] = {0}, ex_param[64] = {0};
        sscanf(existing_e->script_line, "%*[^ (](%*f,%*f,%63[^,],%63[^,]", ex_alias, ex_param);

        if (strcmp(ex_alias, new_alias) == 0 && strcmp(ex_param, new_param) == 0) {
            // just mark as cancelled, do NOT free yet
            sched_events[i].cancelled = 1;
        }
    }

    if (sched_count >= MAX_EVENTS) {
        fprintf(stderr, "Scheduler full!\n");
        return;
    }

    // --- Add new event ---
    ScheduledEvent* e = &sched_events[sched_count++];
    e->callback = callback;
    e->userdata = userdata;
    e->interval_ms = interval_ms;
    e->next_trigger = scheduler_time_ms + interval_ms;
    e->cancelled = 0;
}

void scheduler_tick(double block_ms) {
    scheduler_time_ms += block_ms;

    for (int i = 0; i < sched_count; ) {
        ScheduledEvent* e = &sched_events[i];

        // Clean up cancelled events
        if (e->cancelled) {
            ScriptEvent* se = (ScriptEvent*)e->userdata;
            if (se) free(se);   // safe to free now
            // compact array
            for (int j = i; j < sched_count - 1; j++)
                sched_events[j] = sched_events[j + 1];
            sched_count--;
            continue;
        }

        if (scheduler_time_ms >= e->next_trigger) {
            if (e->callback) e->callback(e->userdata);
            e->next_trigger += e->interval_ms;
        }
        i++;
    }
}


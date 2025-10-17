#ifndef SCHEDULER_H
#define SCHEDULER_H

#define MAX_EVENTS 128

typedef struct {
    double interval_ms;
    double next_trigger;
    int cancelled;
	void (*callback)(void* userdata);
    void* userdata;
} ScheduledEvent;

void scheduler_add(void (*callback)(void*), void* userdata, double interval_ms);
void scheduler_tick(double block_ms);

#endif


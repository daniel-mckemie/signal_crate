#ifndef E_RECORDER_H
#define E_RECORDER_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include "module.h"

typedef enum {
    EREC_IDLE = 0,
    EREC_RECORDING
} ERecState;

typedef struct {
    float sample_rate;

    ERecState state;
    uint64_t sample_counter;
    unsigned int take_id;

    float** buffers;
    uint64_t* buffer_sizes;
    uint64_t buffer_capacity;

    int num_inputs;

	float* mix_buffer;
	uint64_t mix_capacity;
	uint64_t mix_size;
	
	int fading_in;
	int fading_out;
	uint32_t fade_count;

    /* UI */
    double display_seconds;

    /* sync */
    pthread_mutex_t lock;

    /* writer thread */
    pthread_t writer_thread;
    pthread_mutex_t writer_lock;
    pthread_cond_t  writer_cv;
    bool writer_running;

    /* job */
    bool job_pending;
    unsigned int job_take_id;
    int job_num_inputs;
    float** job_buffers;
    uint64_t* job_sizes;
} ERecorder;

#endif

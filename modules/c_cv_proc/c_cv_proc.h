#ifndef C_CV_PROC_H
#define C_CV_PROC_H

#include <pthread.h>

typedef struct {
    float k;        // Gain for V_a ([-2.0, 2.0])
    float m;        // Crossfade between V_b and V_c ([0.0, 1.0])
    float offset;   // Offset added to result ([-1.0, 1.0])

	float display_va;
	float display_vb;
	float display_vc;

    float output;   // Final output value
    pthread_mutex_t lock;
} CCVProc;

#endif // C_CV_PROC_H

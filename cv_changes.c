Completed:

amp_mod
bit_crush
c_asr
// c_clock_s - check CV or general architecture of this
// c_clock_u - same as above
c_cv_monitor
c_cv_proc
c_env_fol
c_fluct
c_function
c_input
c_lfo
c_output
// c_random
// c_sh
delay
fm_mod
freeverb
input
looper
moog_filter
noise_source
pm_mod
// res bank
ring_mod
// scriptbox
spec_hold
vca (in progress)



/*
1. Two threads
UI/OSC thread: writes raw params, then clamp_params().
Audio thread: never writes params, never locks in the sample loop.

2. One block pattern (audio thread)
At start of process():
lock
copy raw params to locals
unlock
smooth locals once per block
cache pointers to CV/control inputs

3. One sample pattern (audio thread)
for each sample:
start from smoothed locals
read CV sample(s) (or 0 if missing)
apply CV directly (no smoothing)
hard-clamp result to rails
run DSP
write output

4. Clamp rules
clamp_params() clamps raw params on every write.

5. Sample-rate usage
Allowed only inside DSP math (phase increment, filters, delays, integrators).
Never used in CV or clamp logic.

6. Control modules
May be stepped or continuous.
Must write control_output every sample (or every frame), no smoothing unless the module itself is a slewer.

7. Non-negotiables
No locks in per-sample loop.
No smoothing in per-sample loop.
No writing raw params in audio thread.
*/

/*
Extra to-dos:
1. Finish building S&H, can skip for now...
2. When building c_random and c_sh, look at PCG32/xorshift, etc.
*/

static void spec_tilt_process(Module* m, float* in, unsigned long frames)
{
    SpecHold* state = (SpecHold*)m->state;
    float* input = (m->num_inputs > 0) ? m->inputs[0] : in;
    float* out   = m->output_buffer;

    pthread_mutex_lock(&state->lock);
    float base_pivot = state->pivot_hz;
    float base_tilt  = state->tilt;
    int   freeze     = state->freeze;
    float sample_rate = state->sample_rate;
    pthread_mutex_unlock(&state->lock);

    float pivot_s = process_smoother(&state->smooth_pivot_hz, base_pivot);
    float tilt_s  = process_smoother(&state->smooth_tilt,     base_tilt);

    float disp_pivot = pivot_s;
    float disp_tilt  = tilt_s;

    for (unsigned long i = 0; i < frames; i++) {

        float pivot = pivot_s;
        float tilt  = tilt_s;

        for (int j = 0; j < m->num_control_inputs; j++) {
            if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

            const char* param = m->control_input_params[j];
            float control = m->control_inputs[j][i];
            control = fminf(fmaxf(control, -1.0f), 1.0f);

            if (strcmp(param, "pivot") == 0) {
                pivot += control * base_pivot;
            } else if (strcmp(param, "tilt") == 0) {
                tilt += control * (1.0f - fabsf(base_tilt));
            }
        }

        clampf(&pivot, 1.0f, sample_rate * 0.45f);
        clampf(&tilt, -1.0f, 1.0f);

        disp_pivot = pivot;
        disp_tilt  = tilt;

        /* ---- FFT hop handling (unchanged DSP) ---- */

        memmove(state->input_buffer,
                state->input_buffer + 1,
                sizeof(float) * (FFT_SIZE - 1));
        state->input_buffer[FFT_SIZE - 1] = input ? input[i] : 0.0f;

        state->hop_write_index++;

        if (state->hop_write_index >= HOP_SIZE) {
            state->hop_write_index = 0;

            for (int k = 0; k < FFT_SIZE; k++)
                state->time_buffer[k] = state->input_buffer[k] * hann[k];

            fftwf_execute(state->fft_plan);

            int bins = FFT_SIZE / 2 + 1;
            float nyquist = sample_rate * 0.5f;

            for (int k = 0; k < bins; k++) {
                float mag, phase;

                if (!freeze) {
                    mag = hypotf(state->freq_buffer[k][0], state->freq_buffer[k][1]);
                    phase = atan2f(state->freq_buffer[k][1], state->freq_buffer[k][0]);
                    state->frozen_mag[k] = mag;
                    state->frozen_phase[k] = phase;
                } else {
                    mag = state->frozen_mag[k];
                    phase = state->frozen_phase[k];
                }

                float hz = ((float)k / (float)bins) * nyquist;
                if (hz < 1.0f) hz = 1.0f;

                float gain_db = tilt * 3.0f * log2f(hz / pivot);
                float gain = powf(10.0f, gain_db / 20.0f);

                state->freq_buffer[k][0] = gain * mag * cosf(phase);
                state->freq_buffer[k][1] = gain * mag * sinf(phase);
            }

            fftwf_execute(state->ifft_plan);

            float dc = 0.0f;
            for (int k = 0; k < FFT_SIZE; k++) dc += state->time_buffer[k];
            dc /= FFT_SIZE;
            for (int k = 0; k < FFT_SIZE; k++) state->time_buffer[k] -= dc;

            for (int k = 0; k < FFT_SIZE; k++)
                state->output_buffer[k] += state->time_buffer[k] / FFT_SIZE;
        }

        out[i] = state->output_buffer[i];
    }

    memmove(state->output_buffer,
            state->output_buffer + frames,
            sizeof(float) * (FFT_SIZE - frames));
    memset(state->output_buffer + (FFT_SIZE - frames), 0, sizeof(float) * frames);

    pthread_mutex_lock(&state->lock);
    state->display_pivot = disp_pivot;
    state->display_tilt  = disp_tilt;
    pthread_mutex_unlock(&state->lock);
}


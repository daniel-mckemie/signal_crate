Completed:

amp_mod
bit_crush
c_asr
c_clock_s
c_clock_u
c_cv_monitor





static void c_cv_proc_process_control(Module* m, unsigned long frames) {
    CCVProc* s = (CCVProc*)m->state;
    float* out = m->control_output;

    // ---- snapshot base params (UI / OSC only) ----
    float base_k, base_m, base_offset;
    pthread_mutex_lock(&s->lock);
    base_k      = s->k;
    base_m      = s->m;
    base_offset = s->offset;
    pthread_mutex_unlock(&s->lock);

    // ---- smooth base params only ----
    float k_s      = process_smoother(&s->smooth_k,      base_k);
    float m_s      = process_smoother(&s->smooth_m,      base_m);
    float offset_s = process_smoother(&s->smooth_offset, base_offset);

    float disp_k      = k_s;
    float disp_m      = m_s;
    float disp_offset = offset_s;
    float disp_va = 0.0f, disp_vb = 0.0f, disp_vc = 0.0f;
    float disp_out = 0.0f;

    for (unsigned long i = 0; i < frames; i++) {

        float k      = k_s;
        float m_amt  = m_s;
        float offset = offset_s;

        // ---- per-sample CV modulation of params ----
        for (int j = 0; j < m->num_control_inputs; j++) {
            if (!m->control_inputs[j] || !m->control_input_params[j])
                continue;

            const char* param = m->control_input_params[j];
            float cv = m->control_inputs[j][i];
            cv = fminf(fmaxf(cv, -1.0f), 1.0f);

            if (strcmp(param, "k") == 0) {
                float range = fabsf(base_k) > 0.0f ? fabsf(base_k) : 2.0f;
                k += cv * range;
            }
            else if (strcmp(param, "m") == 0) {
                m_amt += cv * (1.0f - base_m);
            }
            else if (strcmp(param, "offset") == 0) {
                offset += cv * (1.0f - fabsf(base_offset));
            }
        }

        // ---- clamp effective params ----
        clampf(&k,      -2.0f,  2.0f);
        clampf(&m_amt,   0.0f,  1.0f);
        clampf(&offset, -1.0f,  1.0f);

        // ---- read signal inputs ----
        float va = (m->num_control_inputs > 0 && m->control_inputs[0])
                     ? m->control_inputs[0][i] : 0.0f;
        float vb = (m->num_control_inputs > 1 && m->control_inputs[1])
                     ? m->control_inputs[1][i] : 0.0f;
        float vc = (m->num_control_inputs > 2 && m->control_inputs[2])
                     ? m->control_inputs[2][i] : 0.0f;

        // ---- core CV processing ----
        float val = va * k + vb * (1.0f - m_amt) + vc * m_amt + offset;
        val = fminf(fmaxf(val, -1.0f), 1.0f);

        out[i] = val;

        // ---- last-sample display capture ----
        disp_k      = k;
        disp_m      = m_amt;
        disp_offset = offset;
        disp_va     = va;
        disp_vb     = vb;
        disp_vc     = vc;
        disp_out    = val;
    }

    // ---- publish display state ----
    pthread_mutex_lock(&s->lock);
    s->output         = disp_out;
    s->display_va     = disp_va;
    s->display_vb     = disp_vb;
    s->display_vc     = disp_vc;
    s->display_k      = disp_k;
    s->display_m_amt  = disp_m;
    s->display_offset = disp_offset;
    pthread_mutex_unlock(&s->lock);
}


static void c_env_fol_process_control(Module* m, unsigned long frames) {
    if (!m->inputs[0]) {
        endwin();
        fprintf(stderr, "[c_env_fol] Error: No audio input connected.\n");
        exit(1);
    }

    CEnvFol* s = (CEnvFol*)m->state;

    // ---- snapshot base params (UI / OSC only) ----
    float base_attack, base_decay, base_sens, base_depth;
    pthread_mutex_lock(&s->lock);
    base_attack = s->attack_ms;   // fixed, but still treated as base
    base_decay  = s->decay_ms;
    base_sens   = s->sens;
    base_depth  = s->depth;
    pthread_mutex_unlock(&s->lock);

    // ---- smooth base params only ----
    float att_s   = process_smoother(&s->smooth_attack, base_attack);
    float dec_s   = process_smoother(&s->smooth_decay,  base_decay);
    float sens_s  = process_smoother(&s->smooth_gain,   base_sens);
    float depth_s = process_smoother(&s->smooth_depth,  base_depth);

    // ---- display capture (effective values used; last sample) ----
    float disp_att   = att_s;
    float disp_dec   = dec_s;
    float disp_sens  = sens_s;
    float disp_depth = depth_s;
    float disp_env   = s->smoothed_env;

    float sr = s->sample_rate;

    for (unsigned long i = 0; i < frames; i++) {

        float dec   = dec_s;
        float sens  = sens_s;
        float depth = depth_s;

        // ---- per-sample CV modulation (unsmoothed) ----
        for (int j = 0; j < m->num_control_inputs; j++) {
            if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

            const char* param = m->control_input_params[j];
            float cv = m->control_inputs[j][i];
            cv = fminf(fmaxf(cv, -1.0f), 1.0f);

            if (strcmp(param, "dec") == 0) {
                dec += cv * (5000.0f - base_decay);
            } else if (strcmp(param, "sens") == 0) {
                sens += cv * (1.0f - base_sens);
            } else if (strcmp(param, "depth") == 0) {
                depth += cv * (1.0f - base_depth);
            }
        }

        // ---- clamp effective params ----
        clampf(&dec,   1.0f,  5000.0f);
        clampf(&sens,  0.01f, 1.0f);
        clampf(&depth, 0.0f,  1.0f);

        // ---- compute coeffs from effective params (per-sample) ----
        float atk_coeff = expf(-1.0f / (0.001f * att_s * sr));
        float dec_coeff = expf(-1.0f / (0.001f * dec   * sr));

        float in = fabsf(m->inputs[0][i] * sens);

        if (in > s->env)
            s->env = atk_coeff * (s->env - in) + in;
        else
            s->env = dec_coeff * (s->env - in) + in;

        s->smoothed_env += 0.05f * (s->env - s->smoothed_env);

        float out = fminf(s->smoothed_env, 1.0f) * depth;
        m->control_output[i] = out;

        // ---- last-sample display ----
        disp_att   = att_s;
        disp_dec   = dec;
        disp_sens  = sens;
        disp_depth = depth;
        disp_env   = s->smoothed_env;
    }

    // ---- publish display ----
    pthread_mutex_lock(&s->lock);
    s->display_att   = disp_att;
    s->display_dec   = disp_dec;
    s->display_gain  = disp_sens;
    s->display_depth = disp_depth;
    s->display_env   = disp_env;
    pthread_mutex_unlock(&s->lock);
}


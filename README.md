# C Modules

## **Modules**
- **System Input** (mono)  
  *(no `control_input_params` listed)*

- **Amplitude Modulator** (single input, sine as mod)  
  - `gain` (via `output` module or internal)

- **Delay**
  - `time`
  - `fb`
  - `mix`

- **Frequency Modulator** (single input, sine as mod)
  - `freq`
  - `index`
  - `mix`

- **Looper** (10 Seconds Total)
  - `rec`
  - `play`
  - `speed`

- **Moog Ladder Filter** (LP/HP/BP/Notch/Res)
  - `cutoff`
  - `res`

- **Noise Source** (White, Pink, Brown)  
  *(no `control_input_params` listed)*

- **Ring Modulator** (single user input, sine tone as modulator)
  - `freq`
  - `mix`

- **Spectral Hold** (tilt/freeze)
  - `thresh`

- **Spectral Tilt**
  - `pivot`
  - `tilt`

- **VCA**
  - `gain`

- **VCO**
  - `freq`
  - `amp`

- **Wavefolder**
  - `gain`
  - `fold`
  - `bias`

- **Wave Player** (variable speed, mono)  
  *(params not listed)*

---

## **Control Modules**

- **ASR Envelope**
  - `trigger`
  - `cycle`
  - `att`
  - `sus`
  - `rel`

- **Envelope Follower**
  - `att`
  - `dec`
  - `gain`
  - `depth`

- **LFO**
  - `freq`
  - `amp`
  - `depth`

- **CV Processor**
  - `in`
  - `vb`
  - `vc`
  - `k`
  - `m`
  - `offset`

---

## **OSC Instructions**

OSC routes are assigned by alias/param.  
When building a layout in TouchOSC, for the **VCO**, for example:
- Create two sliders and a button (`freq`, `amp`, and `waveform`)
- `slider1` route = `/vco1/freq`
- `slider2` route = `/vco1/amp`
- `button1` route = `/vco1/waveform`

Repeat for `vco2`, but use:
- `/vco2/freq`
- `/vco2/amp`
- etc.

Then in `./SignalCrate`:
```text
vco as vco1  
vco as vco2  
output(vco1,vco2) as out


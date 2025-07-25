# Signal Crate
Signal Crate is a terminal application written entirely in C. It's purpose
is in creating a modular environment for live performance that is lightweight
and easily expandable. Blending the worlds of scripted event programming with
modular synthesizer design. All modules are controllable via the computer keyboard,
one another, or using OSC.

This does not yet have a build file, but if you are interested in using or developing
for the environment, I can certainly make one and help set it up. Currently only runs
on MacOS with Linux support to follow. I will get to this if/when interest arises!

Notes on the environment:
- There are dedicated audio, control, and UI threads
- Modules follow similar design patterns for processing audio, UI, OSC, and control functions, with
a central engine to wire it together. This allows for new modules to be created as they are needed.
- All modules run with a mono input and mono out
- Multiple modules can sum into one module's input, and they do just that,
sum into a mono signal.
- All control outputs are 0.0-1.0 32-float values and each control module has a depth parameter
to control its output strength. Similarly, audio modules have amp parameters to do the same.
- All modules take -1.0-1.0 control values and use of the CV Processor can allow for inversion, attenuation,
and offset of CV, along with several other mixing capabilities. Modules do not have attenuvertable inputs, 
but the input scale is built and again, if that feature is desired, use an intermediary CV Processor.
- See below on instructions on scripts and aliases for OSC usage. OSC inputs are all scaled to accept 0-1
float, so any work on building an interface requires no additional scaling assignments beyond that, unless
you want to.

##  Audio Modules

### **System Input**
Mono input from external sources.  
- `amp`

---

### **Amplitude Modulator**
Single input, sine wave as internal modulator.  
- `freq`
- `car_amp`  
- `depth`  

---

### **Delay**
Basic delay line.  
- `time`  
- `mix`  
- `fb`  

---

### **Frequency Modulator**
Single input, sine wave as internal modulator.  
- `mod_freq`
- `index`  

---

### **Looper**
10-second mono looper.  
- `speed`
- `start`
- `end`  
- `record`  
- `play`  
- `overdub`  
- `stop`  

---

### **Moog Ladder Filter**
Multi-mode resonant filter (LP/HP/BP/Notch).  
- `cutoff`  
- `res`  
- `type`

---

### **Noise Source**
Generates white, pink, or brown noise.  
- `amp`  
- `type`

---

### **Ring Modulator**
Single input with sine carrier.  
- `mod_freq`
- `car_amp`  
- `mod_amp`  

---

### **Spectral Hold**
Freezes spectrum in-place.  
- `tilt`
- `pivot`  
- `freeze`  

---

### **VCA**
Voltage-controlled amplifier.  
- `gain`

---

### **VCO**
Waveform oscillator.  
- `freq`  
- `amp`  
- `wave`
- `range` = toggles freq range (low=20-2000Hz, mid=20-8000Hz, full=20-20000Hz, super=20-nyquist)
---

### **Wavefolder**
Adds wavefolding distortion.  
- `fold`  
- `blend`  
- `drive`  

---

### **Wave Player**
Mono WAV playback.  
- `file` - Specify relative file location, enclose in [ ]...`wav_player([file=sound.wav], speed=ctrl) as out`
- `speed`

---

## Control Modules

### **ASR Envelope**
Attack-Sustain-Release envelope generator.  
- `att`  
- `cycle`  
- `rel`  
- `trig`
- `depth`  
- `long/short` toggles maximum att/rel time (short=10s max, long=no upper bounds)
---

### **Envelope Follower**
Extracts amplitude envelope.  
- `in_gain`
- `dec`  
- `depth`  

---

### **LFO**
Low-frequency waveform modulator.  
- `freq`  
- `amp`  
- `wave`
- `depth`  

---

### **CV Processor**
Flexible control signal processor. Modeled after Buchla 257
- `in` (va)
- `vb`
- `vc`

Params controllable via OSC
- `k`  
- `m`  
- `offset`

---

## Loading a Signal Crate
Arm Signal Crate in the directory with ./SignalCrate or relevant bash script

There are two ways to load an environment, declaratively line by line in the terminal or as a .txt file
passed in as an argument upon launch. (ie. ./SignalCrate mypatch.txt). The language is the same for either
method.

For example, this would patch two oscillators into a filter and out. `out` is a keyword to output to your system output
as determined by your local settings. This could be saved in a .txt file all the same:

```bash
vco as vco1
vco as vco2
moog_filter(vco1,vco2) as out
```

This will produce...

```bash
[VCO:vco1] Freq: 440.0 Hz | Amp: 0.50 | Wave: Sine | Range: Low
Real-time keys: -/= (freq), _/+ (amp), w (wave), r (range)
Command mode: :1 [freq], :2 [amp]

[VCO:vco2] Freq: 440.0 Hz | Amp: 0.50 | Wave: Sine | Range: Low
Real-time keys: -/= (freq), _/+ (amp), w (wave), r (range)
Command mode: :1 [freq], :2 [amp]

[Moog Filter:out] Cutoff: 440.00 | Res: 0.50 | Type: LP
Real-time keys: -/= (cutoff), _/+ (resonance)
Command mode: :1 [cutoff], :2 [resonance] f: [filter type]
```

Modules can have aliases which are used for routing and CV/OSC assignment. They are assigned with the `as` keyword.
This is true for either control or audio rate modules.

```bash
c_lfo as lfo1
c_lfo as lfo2
vco(freq=lfo1, amp=lfo2) as out
```
Control can be done with the keyboard. Each module has scrolling control and command mode. 
- `-/=, _/=`, etc...are keys that can scroll the params. Each module has which keys control which param
directly in the UI.
- Command mode is entered by hitting `:` followed by a character, also indicated in the UI. For example,
`:1` will change the `freq` of the VCO.
- `:q` quits the environment

The control parameters for the modules listed above are the exact labels to assign control. Again not all of these
are assignable via CV, mostly for ease of architecture and lack of musical purpose to build it. They are all, however,
controllable via OSC. `wave` for example, is not assignable via CV but is as a button/toggle via OSC.

---
## OSC Instructions

OSC routes are assigned using `alias/param`.  
When building a TouchOSC layout:

For **VCO**:
- Add sliders for `freq`, `amp`, and `wave`
- Routes:
  - `/vco1/freq`
  - `/vco1/amp`
  - `/vco1/wave`

For `vco2`:
- `/vco2/freq`, etc.

Example usage in `./SignalCrate`:
```bash
vco as vco1  
vco as vco2  
output(vco1,vco2) as out
```
Your layout will target each oscillator. But the alias must match your layout!

All modules expect -1.0f-1.0f and all OSC params are designed to work with 0-1.
For example, if you have a slider `/vco1/freq` setting 0-1 automatically gives you
the required range as a lograthmic control.

- An extra note:
For OSC compatibility. Firewall must allow incoming connections to terminal.
Depending on your settings, this may silently block incoming UDP connections.
Add terminal/iterm to firewall allowance, and if needed run these commands:
- sudo /usr/libexec/ApplicationFirewall/socketfilterfw --remove path/to/SignalCrate
- sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add path/to/SignalCrate
- sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp path/to/SignalCrate


# SignalCrate Modules

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

---

### **Wavefolder**
Adds wavefolding distortion.  
- `fold`  
- `blend`  
- `drive`  

---

### **Wave Player**
Mono WAV playback.  
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

Your layout will target each oscillator. But the alias must match your layout!

For OSC compatibility. Firewall must allow incoming connections to terminal.
Depending on your settings, this may silently block incoming UDP connections.
Add terminal/iterm to firewall allowance, and if needed run these commands:
- sudo /usr/libexec/ApplicationFirewall/socketfilterfw --remove path/to/SignalCrate
- sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add path/to/SignalCrate
- sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp path/to/SignalCrate


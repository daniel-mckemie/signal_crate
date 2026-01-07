# Signal Crate
Signal Crate is a terminal application written entirely in C for live audio processing
and performance that is lightweight and easily expandable. Blending the worlds of control voltage,
scripted event programming with modular synthesizer design. All modules are controllable via the 
computer keyboard, one another, or using OSC.

This does not yet have a build file, but if you are interested in using or developing
for the environment, I can certainly make one and help set it up. Currently only runs
on MacOS with Linux support to follow. I will get to this if/when interest arises!

There is no current build or dmg file, but in the interim to install and run Signal
Crate, there are just a few C libraries needed to install. Homebrew is likely easiest,
but however you like to do it!

To install Signal Crate:
1. Clone this repo
2. Install Homebrew
3. brew install portaudio liblo fftw libsndfile
5. In the `signal_crate` directory, type `make it`
6. Run `./SignalCrate`

Notes on the environment:
- There are dedicated audio, control, and UI threads
- Modules follow similar design patterns for processing audio, UI, OSC, and control functions, with
a central engine to wire it together. This allows for new modules to be created as they are needed.
- The `input` module can take in multichannel audio independently.
- The `vca` module can output multichannel audio independently.
- All other modules run with a mono input and mono out.
- Multiple modules can sum into one module's input.
- All control outputs are 0.0-1.0 32-float values and each control module has a depth parameter
to control its output strength. Similarly, audio modules have amp parameters to do the same.
- All modules take -1.0-1.0 control values and use of the CV Processor can allow for inversion, attenuation,
and offset of CV, along with several other mixing capabilities. Modules do not have attenuvertable inputs, 
but the input scale is built and again, if that feature is desired, use an intermediary CV Processor.
- See below on instructions on scripts and aliases for OSC usage. OSC inputs are all scaled to accept 0-1
float, so any work on building an interface requires no additional scaling assignments beyond that, unless
you want to.

## Audio Modules

### **System Audio Input**
`input`
Multi-channel enabled input, assigned by param input `([ch=#])`
`input([ch=1]) as in1` = input from channel 1 with alias in1
`input([ch=4]) as in4` = outputs to channel 4 with alias in4

Takes input 8 and outputs it to channel 3
```bash
input([ch=8]) as in8
vca(in8) as out3
```
- `amp` - amplitude of signal

---

### **Amplitude Modulator**
`amp_mod`
Dual input with control over amp of each signal.
`amp_mod(in1,in2) as am1`
- `car_amp` - amplitude of carrier signal upon input 
- `mod_amp` - amplitude of modulator signal upon input  
- `depth` - depth of am 

---

### **Bit Crusher**
`bit_crush`
Bit crusher quantized between 2-16 bits with rate to control how often the signal is sampled
- `bits` - number of bits quantized 
- `rate` - frequency/rate in which signal is sampled/held 

---

### **Delay**
`delay`
Basic delay line.  
- `time` - delay time 
- `mix` - mix of dry signal with delayed signal 
- `fb` - feedback 

---

### **Freeverb**
`freeverb`
Schroeder reverb
- `fb` - feedback
- `damp` - dampness of reveberated signal
- `wet` - mix of amount of reverated signal

---

### **Frequency Modulator**
`fm_mod`
Single input, sine wave as internal modulator.  
- `mod_freq` - frequency of internal modulator
- `idx` - index of fm 

---

### **Looper**
`looper`
10-second (default) looper
- `speed` = passable as constructor
- `start` - start time of the tape
- `end` = passable as constructor (length)
- `record`
- `play`  
- `overdub`  
- `stop`  

---

### **Moog Ladder Filter**
`moog_filter`
Multi-mode resonant filter (Lowpass/Highpass/Bandpass/Notch/Resonant).  
- `cutoff` - cutoff frequency of the filter
- `res` - resonance [0.0 - 4.2] 
- `filt_type` (LP, HP, BP, notch, res) - lowpass/highpass/bandpass/notch/resonant

---

### **Noise Source**
`noise_source`
Generates white, pink, or brown noise.  
- `amp` - amplitude of signal 
- `type` - noise type

---

### **Phase Modulator**
`pm_mod`
Dual input, classic digial PM with base freq setting
`pm_mod(in1,in2) as pm1`
- `car_amp` - amplitude of carrier signal
- `mod_amp` - amplitude of modulator signal
- `base_freq` - base frequency of the oscillator rate of the accumulator
- `idx` - index

---

### **Resonant Filter Bank**
`res_bank`
Parallel band-pass filter bank.
- `mix`
- `q`  
- `lo`
- `hi`
- `bands`
- `tilt`
- `odd`
- `drive`
- `regen`

---

### **Ring Modulator**
`ring_mod`
Dual input with mono output
`ring_mod(in1,in2) as out`
- `car_amp` - amplitude of the carrier signal
- `mod_amp` - amplitude of the modulator signal 
- `depth` - depth of modulation

---

### **Spectral Hold**
`spec_hold`
Freezes spectrum in-place.  
- `tilt` - spectral tilt low to high
- `pivot` - frequency of tilt's center 
- `freeze` - freezes the signal in place

---

### **Spectral Ring Modulator**
`spec_ringmod` - Dual Input
Performs ring modulation in the frequency domain by multiplying 
the spectra of two signals...RM bin-by-bin versus per sample.
- `band_low` - lower freq bound of the active spectral region
- `band_high` - higher freq bound of the active spectral region
- `car_amp` - carrier input level
- `mod_amp` - modulator input level (very sensitive!) 
- `mix` - wet/dry blend 

---

### **VCA**
`vca`
Voltage-controlled amplifier. When used as `vca as out`, can take in two signals and output hard panned stereo.
Panning enabled [-1,1] for either single or dual signals.
`vca(in1,in2) as out`

Multi-channel enabled by appending channel output number to `out#`
`vca(in1) as out3` = outputs to channel 3
`vca(in2) as out5` = outputs to channel 5

- `pan`
- `gain`

---

### **VCO**
`vco`
Waveform oscillator.  
- `freq` - frequency 
- `amp` - amplitude 
- `wave` - waveform (sine, triangle, sawtooth, square)
- `range` = toggles freq range (low=20-2000Hz, mid=20-8000Hz, full=20-20000Hz, super=20-nyquist)
---

### **Wavefolder**
`wavefolder`
Adds wavefolding distortion.  
- `fold` - fold amount 
- `blend` - blend of original and folded signals
- `drive` - intensity of fold 

---

### **Wave Player**
`wav_player`
Mono WAV playback.  
- `file` - Specify relative file location, enclose in [ ]...`wav_player([file=sound.wav], speed=ctrl) as out`
- `speed` - playback speed

---

## Control Modules

### **ASR Envelope**
`c_asr`
Attack-Sustain-Release envelope generator.  
- `att` - attack 
- `rel` - release 
- `trig` - trigger mode (one-shot)
- `cycle` - cycle mode (repeats env)
- `depth` - range of output 
- `long/short` toggles maximum att/rel time (short=10s max, long=no upper bounds)

---

### **Clock (Syncable)**
`c_clock_s`
Clock that can be synced to a primary clock, sharing bpm and on/off actions.
- `bpm` - beats per minute (tempo)
- `mult` - multiply or divide clock output (ie. 3.00 = x3 or 0.5 = /2)
- `pw` - pulse width of gate
- `run` - on/off
One primary clock can send its bpm and switch behavior to any number of secondary clocks.
Secondary clocks each have their own mult, pw, and switch behavior, but are always tied to the primary bpm.
The primary clock switch will always control the secondary clocks' switch.

This example has one `primeclk` controlling two clocks. You can only have ONE primary clock, as opposed to families
of clocks. If you want indepdendent clocks, see `c_clock_u`.
```bash
c_clock_s as primeclk
c_clock_s(in=primeclk) as secondclk
c_clock_s(in=primeclk) as thirdclk
```
---

### **Clock (Un-Syncable)**
`c_clock_u`
Clock that sends a pulse out independent of other clocks on the grid. Can be used alongside `c_clock_s`
but has no inputs in which to sync.
- `bpm` - beats per minute (tempo)
- `mult` - multiply or divide clock output (ie. 3.00 = x3 or 0.5 = /2)
- `pw` - pulse width of gate
- `run` - on/off

This example has two clocks synced together, controlled by the `primeclk`. And one independently controlled
`indyclk` which has its own bpm and all other behaviors. The `secondclk` can have its own mult, pw, and switch
behavior, but is always tied in tempo to the primeclk. You can have as many unsynchronized clocks as you wish,
but there is always only one primary clock bpm for synched clocks (`c_clock_s`).
```bash
c_clock_s as primeclk
c_clock_s(in=primeclk) as secondclk
c_clock_u as indyclk
```

---

### **CV Monitor**
`c_cv_monitor`
Monitors incoming and outgoing signal, and gives extra utils
- `in` (va) = to pass in control signal, must call `c_cv_monitor(in=alias)`
- `att` - attenuvert 
- `off` - offset

---

### **CV Processor**
`c_cv_proc`
Flexible control signal processor. Modeled after Buchla 257
`V_a * K + V_b * (1 - M) + M * V_c + V_offset = V_out`
- `in` (va) = to pass in control signal, must call `c_cv_proc(in=alias)`
- `vb` - input to crossfade with `vc`
- `vc` - input to crossfade with `vb`

Params controllable via OSC
- `k` - scale factor 
- `m` - crossface between `vb` and `vc` 
- `offset` - offset input or built-in offset

---

### **Envelope Follower**
`c_env_fol`
Extracts amplitude envelope.  
- `in_gain`
- `dec` - decay
- `depth` - range of output 

---

### **Fluctating Random Voltages**
`c_fluct`
Fluctuating Random Voltages after the Buchla 266
- `rate` - rate of fluctuation 
- `depth` - range of output 
- `mode` - noise and random walk

---

### **Function Generator**
`c_function`
Attack-Release slope generator. No sustain, only trig to fire 
- `att` - attack 
- `rel` - release 
- `gate` - gate threshold, min to fire generator (takes in as control, ie. gate=clk)
- `depth` - range of output 
- `long/short` toggles maximum att/rel time (short=10s max, long=no upper bounds)

---

### **LFO**
`c_lfo`
Low-frequency waveform modulator running on the control thread
- `freq` - frequency/rate [0.001 - 100.0]
- `amp` - strength of signal output
- `wave` - waveform (sine, sawtooth, square, triangle)
- `depth` - range of output 
- `polarity` - output is either unipolar or bipolar

---

### **Random**
`c_random`
Random control signal generator
- `rate` - frequency/rate [0.001 - 100.0]
- `rmin` - minimum of random 
- `rmax` - maximum of random
- `type` - noise type (white, brown, pink)
- `depth` - range of output 

---

### **Sample and Hold**
`c_sh`
Sample and Hold requires audio input and outputs control signal
- `rate` - frequency/rate of triggered sample
- `depth` - range of output 

---

### **Control Input**
`c_input`
Multi-channel enabled control input, assigned by param input `([ch=#])`
Takes in audio from system and outputs control signal into Signal Crate
`c_input([ch=1]) as in1` = input from channel 1 sends to control layer with alias in1
`c_input([ch=4]) as in4` = outputs to channel 4 sends to control layer with alias in4

Takes control input on channel 8 and controls the [VCO:v1]'s frequency param
and is output to stereo from the VCA
```bash
c_input([ch=8]) as cin8
c_input([ch=17]) as cin17
vco(freq=in8) as v1
vca(pan=cin17,v1) as out
```
- `val` - amplitude of signal

---

### **Control Output**
`c_output`
Output module for DC control voltage. Takes in control input
and routes out as audio, set to output channel similar to VCA.
- `val` - output value of CV
```bash
c_lfo as l1
c_output(l1) as out3` // output lfo from Signal Crate to channel 3 of interface
```
*To output audio as CV, use `audio -> c_env_fol -> c_output`*

---

## Script Box
The Script Box module allows for functions to be declared and targeted throughout the system. The Script Box is armed
and goes into a separate editor, and here functions can be declared line by line. In the main Signal Crate UI view, the
module can be run using the `Ctrl+R` command. Every function is equipped to either run as a one-shot or on a cycle. To
use the cycle feature, the last argument is preceded with a `~` and the value to cycle in milliseconds.

Each function is on their own scheduler and uses OSC to target modules and their params within Signal Crate. A list of 
available functions is below, along with their arguments.

- `rand` - random number generator (min,max,alias,param)

```bash
rand(100,1000,vco1,freq)    // Sends a one-shot random value to vco1's frequency param
rand(0.3,0.6,vco1,amp,~500) // Sends a random value to vco1's amp param every 500ms
```


## Loading a Signal Crate
Arm Signal Crate in the directory with ./SignalCrate or relevant bash script

There are two ways to load an environment, declaratively line by line in the terminal or as a .txt file
passed in as an argument upon launch. (ie. ./SignalCrate mypatch.txt). The language is the same for either
method.

For example, this would patch two oscillators into a filter and out. `out` is a keyword to output to your system output
as determined by your local settings. If you want multichannel audio output, using a `vca` with the `out#` nomenclature
can control specific channel routing out of Signal Crate. More info in the VCA section above.

This could be saved in a .txt file all the same:

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
- `tab` and the `arrow keys` highlight the active module, opening control via the keyboard. Scroll through the UI
as needed to change params. No need to have the module active if using CV or OSC.
- Command mode is entered by hitting `:` followed by a character, also indicated in the UI. For example,
`:1` will change the `freq` of the VCO.
- `:q` quits the environment

You can also set the params via constructor arguments, which are indicated with `[ ]`, as well as use both a constructor and control assignment:

`vco([freq=212, amp=0.1, wave=saw]) as out` will produce:
```bash
[VCO] Freq: 212.0 Hz | Amp: 0.10 | Wave: Saw | Range: Low
Real-time keys: -/= (freq), _/+ (amp), w (wave), r (range)
Command mode: :1 [freq], :2 [amp]
```

You can also combine them. The below will give you a VCO with the starting/center freq at 100, with lfo modulation input. Note that
the constructor MUST precede the control input. Otherwise the constructor will override control assignment.

```bash
c_lfo as lfo1
vco([freq=100], freq=lfo1) as out
```
Another example:
```bash
c_lfo as a
c_cv_proc([k=0.5, m=0.4, offset=0.7], in=a) as u
vco([freq=220], freq=u) as out
```

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
vca(vco1,vco2) as out
```
Your layout will target each oscillator. But the alias must match your layout!

All modules expect -1.0f - 1.0f and all OSC params are designed to work with 0-1.
For example, if you have a slider `/vco1/freq` setting 0-1 automatically gives you
the required range as a lograthmic control.

- An extra note:
For OSC compatibility. Firewall must allow incoming connections to terminal.
Depending on your settings, this may silently block incoming UDP connections.
Add terminal/iterm to firewall allowance, and if needed run these commands:
- sudo /usr/libexec/ApplicationFirewall/socketfilterfw --remove path/to/SignalCrate
- sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add path/to/SignalCrate
- sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp path/to/SignalCrate


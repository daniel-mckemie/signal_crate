# C Modules

**Modules**
- System Input (mono)
- VCO
- Moog Ladder Filter (LP/HP/BP/Notch/Res)
- Wavefolder
- Ring Modulator (single user input, sine tone as modulator)
- Noise Source (White, Pink, Brown)
- Frequency Modulator (single input, sine as mod)
- Amplitude Modulator (single input, sine as mod)
- Looper (10 Seconds Total)
- Spectral Tilt/Freeze
- System Output (mono)

**OSC instructions:**
OSC routes are assigned by alias/param. 
So when building a layout in TouchOSC, for the VCO for example:
- create two sliders and a button (freq, amp, and waveform)
- slider1 route = /vco1/freq
- slider2 route = /vco1/amp
- button1 route = /vco1/waveform

Repeat the process for vco2, but as vco2/freq, vco2/amp, etc.
Then in ./SignalCrate:
- vco as vco1
- vco as vco2
- output(vco1,vco2) as out

Your layout will target each oscillator. But the alias must match your layout!

For OSC compatibility. Firewall must allow incoming connections to terminal.
Depending on your settings, this may silently block incoming UDP connections.
Add terminal/iterm to firewall allowance, and if needed run these commands:
- sudo /usr/libexec/ApplicationFirewall/socketfilterfw --remove path/to/SignalCrate
- sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add path/to/SignalCrate
- sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp path/to/SignalCrate


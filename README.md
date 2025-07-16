# C Modules

- System Input
- VCO
- Moog Ladder Filter (LP/HP/BP/Notch/Res)
- Wavefolder
- Ring Modulator (1 input user, 1 "input" sine tone)
- Noise Source (White, Pink, Brown)
- Frequency Modulator
- Amplitude Modulator
- Looper (10 Seconds Total)
- Spectral Tilt/Freeze



For OSC compatibility. Firewall must allow incoming connections to terminal.
Depending on your settings, this may silently block incoming UDP connections.
Add terminal/iterm to firewall allowance, and if needed run these commands:
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --remove path/to/SignalCrate
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add path/to/SignalCrate
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp path/to/SignalCrate


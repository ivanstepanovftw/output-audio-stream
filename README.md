# StreamSome
The media streaming library (PC-to-PC yet, android coming ASAP).

## StreamSome::audio
Server side is compatible with SoundWire Android client (not fully, but it working at least).

## How to launch
0. Get linux with PortAudio
0. Get cmake
0. Compile like ``mkdir build && cd build && cmake .. && cd audio``
0. Launch `./streamsome_audio_server`
0. Launch `./streamsome_audio_client` or SoundWire Android client
0. ...
0. PROFIT!!!

## Known issues
0. PortAudio built-in monitor device of current output device have a big latency like 0.4 sec, maybe moar
0. PortAudio's monitor device have big latency
0. Do not know how to reduce monitor device latency
0. Very large PortAudio's monitor device latency

About the last issue: even microphone device latency over WiFi 5 is pretty damn good.

## Troubleshooting (LEGACY)
### Debian/Ubuntu:
Launch server, open "Volume Control", select tab named "Recording", locale something like "**ALSA plug-in [linux-audiowire-server]**: ALSA Capture *from*: " and select something like "Monitor of Built-in Audio Analog Stereo".
### Arch/Manjaro:
Launch "Audio Volume — System Settings Module", go to "Advanced". In second dropdown list named "Built-in Audio" set the same Profile as in first one.
How this should looks like:
![imgur.com/0i6t1EW](https://i.imgur.com/0i6t1EW.png?1)

# output-audio-stream
## What?
Contains 2 different programs:
AudioWire that compatible with SoundWire and WifiSound that compatible with WifiAudio. 

## How to launch
It is not ready for end user. Actually you can try it in CLion. First: launch server, then: launch client.

## It doesn't work
### Debian/Ubuntu:
Launch server, open "Volume Control", select tab named "Recording", locale something like "**ALSA plug-in [linux-audiowire-server]**: ALSA Capture *from*: " and select something like "Monitor of Built-in Audio Analog Stereo".
### Arch/Manjaro:
Launch "Audio Valume — System Settings Module", go to "Advanced". In second dropdown list named "Built-in Audio" set the same Profile as in first one.
How this should looks like:
![imgur.com/0i6t1EW](https://i.imgur.com/0i6t1EW.png?1)

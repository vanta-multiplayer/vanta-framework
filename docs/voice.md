# Voice

Proximity talk rides the **same RakNet UDP** as the rest of the session.
There is no second socket and no voice server. The dedicated server only
sets the hear distance (`SetProximityRange(40)` in `server.cpp`).

**V** is push-to-talk (Win32 `0x56`). Settings can change the key; it is
saved as `voice_ptt_key` in `VantaMP.json`.

## What the client does

`core/voice_io.cpp` lists WASAPI endpoints, opens the chosen pair, and
applies output gain. `"default"` means whatever the OS currently prefers.

The listener follows the local ped. In a vehicle the chase camera sits
metres behind the cabin and springs every frame; pan is measured from
`listener.position`, so that motion chops the mix. Distance still uses
the ped. When seated, both positions are the ped.

`InvalidateVoiceSpeakerCache` runs before each publish so a player who
just entered or left a car is not still mixed as if they were on foot.

## Mic test

Hear-yourself loopback on the chosen endpoints. It works from the menu
before a connect. Starting it **pauses** the live voice session so the
same WASAPI device can open twice; stopping it (or leaving the menu /
disconnecting) tears the test down and reports `settings:currentVolume:running`
false.

## Settings keys

| Key | Meaning |
|-----|---------|
| `voiceInputDevice` / `voiceOutputDevice` | Endpoint id, or `"default"` |
| `voiceVolume` | Playback gain for other players. 1 is unity; 2 is the slider top |
| `voicePttKey` | Win32 virtual-key |

A live change writes the file and calls `ApplyVoiceSettings`. Display
resolution is not like this — that waits for the next launch.

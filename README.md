# DingDong Delay

Stereo ping-pong delay LV2 plugin for Raspberry Pi and PiPedal.

## Features

- Stereo ping-pong or mono delay
- MIDI tap tempo (Note On, CC ≥ 64, Program Change)
- Shimmer effect with pitch options (+Octave, +5th, −Octave)
- HP and LP filters in the feedback path
- Analog warmth: soft saturation, wow/flutter, age filter

## Controls

| Parameter | Description |
|-----------|-------------|
| Level | Delay output volume |
| Mix | Dry/wet balance |
| Decay | Feedback amount |
| HP Freq | High-pass filter cutoff (removes mud) |
| LP Freq | Low-pass filter cutoff (softens highs) |
| Delay Time | 50–2000 ms (overridden by tap tempo) |
| Shimmer Mix | Shimmer amount |
| Shimmer Pitch | +Octave / +5th / −Octave |
| Stereo | Mono or Stereo/Ping-pong |

## Installation

```bash
git clone https://github.com/fe-su/dingdong-delay.git
cd dingdong-delay
sudo apt install lv2-dev gcc make
make deploy
```

## Updating

```bash
cd ~/dingdong-delay
git pull && make deploy
```

## License

MIT

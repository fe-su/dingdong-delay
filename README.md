# DingDong Delay — LV2 Guitar Delay Plugin

Stereo ping-pong delay Raspberry Pi:lle ja PiPedalille.  
Tap tempo MIDI-pedaalilla, shimmer, analoginen lämpö.

## Ominaisuudet

| Parametri | Kuvaus |
|-----------|--------|
| **Delay Time** | 50–2000 ms, tap tempo MIDI:llä |
| **Level** | Delay-signaalin voimakkuus |
| **Mix** | Kuiva/märkä -suhde |
| **Decay** | Feedback (kaiun sammuminen) |
| **HP Freq** | High-pass feedback-ketjussa (poistaa mutaa) |
| **LP Freq** | Low-pass feedback-ketjussa (pehmentää korkeat) |
| **Shimmer Mix** | Shimmer-efektin määrä (x²-skaalaus) |
| **Shimmer Pitch** | +Octave / +5th / −Octave |
| **Stereo** | Mono tai Stereo/Ping-pong |

**Analoginen sävy:** tanh-saturaatio, wow/flutter, age-suodatin  
**MIDI tap:** Note On, CC ≥ 64, Program Change

## Asennus

```bash
git clone https://github.com/SINUN-TUNNUS/dingdong-delay.git
cd dingdong-delay
chmod +x install.sh && ./install.sh
```

## Manuaalinen asennus

```bash
sudo apt install lv2-dev gcc make
make deploy   # käännä + asenna + restart pipedald
```

## Päivitys

```bash
git pull && make deploy
```

## Lisenssi

MIT

# Universal IR Remote — Godrej AC (ESPHome + Home Assistant)

Control a **Godrej AC** with an **ESP32-C3 Super Mini** over IR, exposed to Home
Assistant as a full **thermostat card**. Everything compiles/flashes on your
machine via **Docker** — no local toolchain, no slow Pi builds.

Godrej isn't a built-in ESPHome brand, so its IR protocol was reverse-engineered
and reimplemented as a custom component that **synthesizes** any frame
(temp/fan/swing/power + checksum) — verified bit-for-bit against captures and on
the real AC.

## Layout
```
Makefile                       # compile / flash / logs / etc. (see `make help`)
docker-compose.yml             # ESPHome in Docker (CLI + web dashboard)
esphome/
  secrets.yaml                 # wifi + keys (gitignored) — FILL THIS IN
  godrej-climate.yaml          # the device: thermostat + swing-angle select
  capture.yaml                 # reusable IR "learning" tool (dump codes from any remote)
  components/godrej_ac/        # custom climate component (the decoded protocol)
reference-ir-smart-remote/     # Cian911's repo, reference only
```

## Quick start
```bash
make help                       # list all targets
make config                     # validate the config
make flash                      # first flash over USB (ESP32-C3 = /dev/ttyACM0)
make logs                       # watch serial logs
make run                        # compile + upload + logs in one go
```
Override the config or port:
```bash
make flash YAML=capture.yaml
make logs  DEVICE=/dev/ttyACM1
```

## Hardware / wiring
- **IR transmitter** on **GPIO4**: `GPIO4 → 1kΩ → transistor base`;
  `5V → 22Ω → IR LED(+) → IR LED(−) → collector`; `emitter → GND`.
  (Use **22Ω**, not 100Ω — 100Ω is too dim for usable range. 2N2222 preferred.)
- **IR receiver** (VS1838B) on **GPIO5**, powered from **3V3** (not 5V), with
  `inverted: true` + internal pullup.
- ESP32-C3 is **3.3V only**; avoid strapping pins GPIO2/8/9.

## Home Assistant
1. Put real values in `esphome/secrets.yaml` (wifi + API key).
2. `make flash` (USB first time; later flashes can be OTA).
3. HA → Settings → Devices & Services → ESPHome auto-discovers `godrej-ac`.
   You get a thermostat card (16–30°C, Cool/Off, fan Auto/Low/Med/High) plus a
   "Godrej Swing" select with the 10 angles (rename them in HA to taste).

## Protocol (for reference)
LG-extended, 38 kHz, 67 data bits in two blocks. Fields (LSB-first data bits):
temp = bits 8–11 (`°C − 16`), fan = bits 4–5, swing = bits {6,35–38},
power = bits 3 & 22, checksum = bits 63–66 = `(nibble0 + temp_nibble + 12) & 0xF`.
Full details + timings are in `esphome/components/godrej_ac/godrej_ac.h`.

## Notes
- `capture.yaml` is kept as a reusable tool: flash it, point any IR remote at
  the receiver, and read the codes in the logs to learn a new device.
- The dashboard UI (optional): `make dashboard` → http://localhost:6052

# universal-ir — ESPHome IR blaster (Home Assistant)

A single **ESP32-C3 Super Mini** acting as a universal IR blaster + receiver,
exposed to Home Assistant via ESPHome. The node owns the shared plumbing
(Wi-Fi / API / OTA / IR hardware); each appliance it controls is a self-contained
**device package**. Everything compiles/flashes on your machine via **Docker** —
no local toolchain, no slow Pi builds.

## Devices
| Device | Package | What you get |
|--------|---------|--------------|
| **Godrej AC** | `devices/godrej-ac.yaml` | Full thermostat card (temp/mode/fan), a 10-position swing select, automatic state sync when the physical remote is used, and an IR-repeater switch. Protocol reverse-engineered into a custom component that synthesizes + decodes frames. |

Add a new device by dropping a package under `devices/` and listing it under
`packages:` in the node config.

## Layout
```
Makefile                     # compile / flash / logs / etc. (see `make help`)
docker-compose.yml           # ESPHome in Docker (CLI + web dashboard)
universal-ir.yaml            # the node — single-file builder config (pulls device
                             #   packages + components from GitHub)
universal-ir-local.yaml      # the node — local-build twin (default for `make`)
devices/
  godrej-ac.yaml             # Godrej AC device package
components/
  godrej_ac/                 # Godrej AC custom climate component (the protocol)
capture.yaml                 # reusable IR "learning" tool (dump codes from any remote)
secrets.yaml                 # wifi + keys (gitignored) — FILL THIS IN
.esphome/                    # ESPHome build cache (gitignored)
```
The repo root is the ESPHome config dir (mounted as `/config` in Docker).

## Two ways to build
- **ESPHome dashboard / "Builder"**: paste **`universal-ir.yaml`** — it pulls the
  component and every device package straight from this GitHub repo, so it's the
  only file you need (plus `secrets.yaml`).
- **Local (default)**: `make` targets build **`universal-ir-local.yaml`** from the
  on-disk tree — use this when editing components.

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
make run   YAML=universal-ir.yaml     # build the GitHub-sourced node instead
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
3. HA → Settings → Devices & Services → ESPHome auto-discovers **`universal-ir`**.
   The Godrej AC device gives you a thermostat card (16–30°C, Cool/Off, fan
   Auto/Low/Med/High), a "Godrej Swing" select (10 angles), and an "IR Repeater"
   switch. Using the physical Godrej remote updates these automatically.

## Godrej AC protocol (for reference)
LG-extended, 38 kHz, 67 data bits in two blocks. Fields (LSB-first data bits):
temp = bits 8–11 (`°C − 16`), fan = bits 4–5, swing = bits {6,35–38},
power = bits 3 & 22, checksum = bits 63–66 = `(nibble0 + temp_nibble + 12) & 0xF`.
Full details + timings are in `esphome/components/godrej_ac/godrej_ac.h`.

## Notes
- `capture.yaml` is kept as a reusable tool: flash it, point any IR remote at
  the receiver, and read the codes in the logs to learn a new device.
- The dashboard UI (optional): `make dashboard` → http://localhost:6052

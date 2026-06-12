# TACSCOPE — Live Air Traffic Radar

A military-style PPI radar scope for the **ESP32-2424S012** round display board,
built with **ESP-IDF**. Real aircraft around your location appear as phosphor-green
blips with callsigns, heading-oriented symbols, and position trails under a
rotating sweep with afterglow — all from a free, no-registration ADS-B API.

## Features

- Live ADS-B traffic — no API key, no account required
- Phosphor-green military aesthetic with rotating sweep and glow trail
- Tap any blip to see callsign, flight level, ground speed, bearing and distance
- Tap empty scope area to cycle display range: 10 / 25 / 50 / 100 km
- Up to 6-step position trail per contact; closest 24 contacts always kept
- Streaming JSON parser — RAM usage stays bounded regardless of traffic density
- UTC (Zulu) clock synced via SNTP
- Link-loss alert with automatic Wi-Fi and API reconnect

## Hardware

| Component | Details |
|-----------|---------|
| Board | ESP32-2424S012 |
| MCU | ESP32-C3-MINI-1U (RISC-V, single-core, ~320 KB SRAM) |
| Display | 1.28″ round 240×240 GC9A01 (SPI) |
| Touch | CST816D capacitive (I2C) |
| Flash | 4 MB |

### Pinout

| Signal | GPIO |
|--------|------|
| LCD SCLK | 6 |
| LCD MOSI | 7 |
| LCD DC | 2 |
| LCD CS | 10 |
| LCD Backlight | 3 (LEDC PWM, active-high) |
| Touch SDA | 4 |
| Touch SCL | 5 |
| Touch RST | 1 |
| Touch INT | 0 (strapping pin — polled, not IRQ) |

## Data source

The app polls a free community ADS-B aggregator. **No registration or API key is needed.**

| Service | Base URL |
|---------|----------|
| adsb.lol (default) | `https://api.adsb.lol` |
| airplanes.live (fallback) | `https://api.airplanes.live` |

Endpoint: `GET <base>/v2/point/<lat>/<lon>/<radius_nm>`

Please be polite to these free services: keep `REFRESH_INTERVAL_S` at 5 seconds or
more (default is 10). If one service is down, switch `ADSB_BASE_URL` in
`app_config.h` to the other.

## Setup

### 1. Configure

Copy the example config and fill in your settings:

```sh
cp main/app_config.h.example main/app_config.h
```

Edit `main/app_config.h` — this is the **only file you need to touch**:

| Setting | Description |
|---------|-------------|
| `WIFI_SSID` | Your 2.4 GHz network name |
| `WIFI_PASS` | Your Wi-Fi password |
| `HOME_LAT` / `HOME_LON` | Scope center in decimal degrees — right-click your location in Google Maps and choose *Copy coordinates* |
| `ADSB_BASE_URL` | API base URL (see table above) |
| `FETCH_RADIUS_NM` | Radius to request from the API in nautical miles (default 60, max 250) |
| `REFRESH_INTERVAL_S` | Seconds between refreshes (default 10, minimum 5) |
| `RANGE_STEPS_KM` | Tap-to-cycle display ranges in km |
| `RANGE_STEP_DEFAULT` | Index into the list above used on boot (default `2` = 50 km) |
| `BACKLIGHT_PCT` | Display brightness, 1–100% |
| `DISPLAY_BGR` | Set to `0` if red and blue look swapped on your panel |
| `SHOW_GROUND_TARGETS` | Show aircraft on the ground (default `0` — hidden) |

> **Note:** `app_config.h` is gitignored because it contains your Wi-Fi credentials
> and home location. The committed `app_config.h.example` is the safe template.

### 2. Build and flash

Requires **ESP-IDF v5.x**. On Windows, open the *ESP-IDF PowerShell* shortcut, or
source `export.ps1` from your IDF installation before building.

```sh
idf.py set-target esp32c3
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with your device's serial port (`COM11` on Windows, `/dev/ttyUSB0`
on Linux/macOS).

## Controls

| Action | Effect |
|--------|--------|
| Tap an aircraft blip | Select it — displays callsign, flight level, ground speed, bearing and distance |
| Tap the selected blip again | Deselect |
| Tap empty scope area | Cycle display range: 10 → 25 → 50 → 100 km |

## Display legend

| Symbol / Label | Meaning |
|---------------|---------|
| Triangle blip | Aircraft with track data, oriented along its heading |
| Round blip | Aircraft without heading data |
| Dots trailing a blip | Position history (one dot per refresh cycle) |
| Amber colour | Selected contact |
| `CON nn` | Number of contacts shown (closest 24 kept when airspace is busy) |
| `LNK -xxdBm` | Wi-Fi signal strength |
| `** LINK LOST **` | Wi-Fi or API outage — retries automatically |

## Project structure

```
Air-Traffic/
├── main/
│   ├── app_config.h.example   Template — copy to app_config.h and edit
│   ├── app_config.h           Your config (gitignored)
│   ├── main.c                 Boot sequence and main render loop
│   ├── display.c/h            GC9A01 via esp_lcd + LEDC backlight
│   ├── touch.c/h              CST816D polled I2C driver
│   ├── gfx.c/h                Software renderer (banded RGB565 DMA framebuffer)
│   ├── font5x7.c/h            5×7 bitmap font, 95 printable ASCII characters
│   ├── adsb.c/h               HTTPS fetch task + streaming JSON parser
│   ├── radar.c/h              PPI scope rendering, sweep, touch interaction
│   └── wifi.c/h               Wi-Fi STA with auto-reconnect
├── partitions.csv             Custom partition table (3 MB app slot)
├── sdkconfig.defaults         Build configuration baseline
├── CMakeLists.txt
└── README.md
```

## Troubleshooting

**Colors look wrong (red/blue swapped)**
Set `DISPLAY_BGR 0` in `app_config.h` and reflash.

**No blips visible despite good Wi-Fi**
Verify `HOME_LAT` / `HOME_LON` are correct and `FETCH_RADIUS_NM` is large enough
for your area. Try bumping it to 100 and cycling to the 100 km range.

**`LINK LOST` banner is flashing**
The API endpoint is unreachable. Check that Wi-Fi credentials are correct. If the
network is fine, try switching `ADSB_BASE_URL` to the alternate service.

**Scope shows data but no blips at my range**
Your nearest aircraft may be outside the current display range. Tap the empty scope
area to step through to a larger range.

**Build fails with Python or environment errors on Windows**
Pre-set the IDF Python env path before sourcing `export.ps1`:

```powershell
$env:IDF_PYTHON_ENV_PATH = "C:\Users\<you>\.espressif\python_env\idf5.5_py3.11_env"
. "C:\Users\<you>\esp\v5.5.1\esp-idf\export.ps1"
```

## License

MIT — see [LICENSE](LICENSE).

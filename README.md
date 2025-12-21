# ESP32 Stock Display (HUB75 64x32)

ESP32 + HUB75 64x32 LED matrix “stock ticker” that cycles through tickers and shows:
- Symbol + last price + daily % change (from Twelve Data `/quote`)
- Intraday 1‑minute sparkline (from Twelve Data `/time_series`)

It also includes a built-in **phone config page** (LAN-only) to edit tickers and persist them across reboots.

## Setup

### 1) Secrets (required)
Create `src/app_secrets.h` (this file is git-ignored).

You can start from the template:
- Copy `src/app_secrets.example.h` → `src/app_secrets.h`
- Fill in:
  - `WIFI_SSID`, `WIFI_PASSWORD`
  - `TWELVEDATA_API_KEY`

### 2) Build / upload
This is a PlatformIO project.

- Build:

```bash
pio run
```

- Upload:

```bash
pio run -t upload
```

- Serial monitor:

```bash
pio device monitor -b 115200
```

## Configure tickers from your phone

After the ESP32 is connected to WiFi:
- Open `http://<esp32-ip>/`
- (If mDNS works on your network) open `http://stock-display.local/`

The page lets you save a comma-separated ticker list (max 10). The list is stored in ESP32 NVS and restored after reboot.

## Notes
- This project uses NTP to determine New York time and regular session open/close behavior.
- Twelve Data free tier is rate-limited, so the firmware schedules at most one API operation per spacing window.



# xDrip — Pebble Time 2 Watchface

A CGM watchface for xDrip+ on the **Pebble Time 2 (Emery, 200×228)**, redesigned with a clean minimal aesthetic.

> **Based on** [consp/xDrip-Pebble-E](https://github.com/consp/xDrip-Pebble-E), which is itself based on the Nightscout community version.

---

## Screenshot

<img width="200" height="228" alt="pebble_screenshot" src="https://github.com/user-attachments/assets/95401ed5-f179-4bec-be73-6792f783bf11" />
<img width="1555" height="1525" alt="image" src="https://github.com/user-attachments/assets/4556a5a3-8cd4-4a8f-b748-525a7dd6ef5f" />


---

## Features

- **Blood glucose** value, large and prominently displayed — color-coded:
  - 🔴 Red = low
  - 🟢 Green = in range
  - 🔵 Blue = high
- **Trend arrow** — sleek custom-drawn vector arrows (↑↑ ↑ ↗ → ↘ ↓ ↓↓)
- **Time since last reading** — top-right of glucose section
- **Large time display** — Gotham Bold 60pt, center of the face
- **Date** — below the time
- **Live weather temperature** — via Open-Meteo API (no API key required), fetched via PebbleKit JS using phone GPS
- **Heart rate** — from Pebble Health sensor (Emery only)
- **Phone battery** and **watch battery** — with drawn phone/watch icons
- Clean minimal layout — no divider lines

---

## Requirements

- **Pebble Time 2** (Emery platform, 200×228 color display)
- **xDrip+** — version later than January 2025
- **Pebble app** — version 1.0.10.8 or later
- Before installing, ensure the **"Pebble Trend Clay Version (Test)"** watchface has been installed first — this registers the xDrip communication protocol

---

## Build

```bash
pebble build
```

Install on device:
```bash
pebble install --phone <PHONE_IP>
```

Install on emulator:
```bash
pebble install --emulator emery
```

**Requirements:** Pebble SDK 4.9+, Node.js (for Clay config)

---

## Settings

Configured via the Pebble app (Clay):

| Setting | Default | Description |
|---|---|---|
| Display Seconds | Off | Appends seconds to the clock — `HH:MM:SS` |
| Re-raise BT alert | On | Periodic vibration until Bluetooth reconnects |
| Silence all vibrations | Off | Disables all watchface vibrations |
| Light on charge | Off | Keeps backlight on while charging |
| Message timer | 15s | Interval for message/delta display updates |

Settings are stored on the watch and persist across watchface transitions.

---

## Data displayed

| Element | Source |
|---|---|
| BG value + trend arrow | xDrip+ via AppMessage |
| Time since last reading | xDrip+ timestamp |
| Heart rate | Pebble Health API |
| Weather temperature | Open-Meteo API via PebbleKit JS |
| Phone battery | xDrip+ |
| Watch battery | Pebble battery service |

---

## Change Log

**pt2 branch — Visual redesign (2026-06-11)**
- Complete visual rewrite targeting Pebble Time 2 (Emery)
- Clean black background, single-color design
- Large Gotham Bold 60pt time display
- BG glucose color-coded (red/green/blue)
- Custom vector trend arrows replacing legacy bitmaps
- Consistent 4-cell data grid with drawn icons (thermometer, heart, phone, watch)
- Live weather temperature via Open-Meteo + PebbleKit JS
- Heart rate via Pebble Health API
- Persisted weather temperature across restarts
- Removed all section dividers for minimal look
- Emery-only target

**20260609**
- Refactored code, fixed outstanding issues and made it nicer to watch on a PT2 display
- Fixed issues with wrong x/y width/height values on PT2, PD2, Gabbro
- Added 60pt font, dynamic 40pt/60pt font for PT2
- Moved seconds timer and message display timer into their own functions

**20260227**
- Refactor to build with SDK v4.9.127
- Initial support for Gabbro (Core Round 2)

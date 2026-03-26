# PlatformIO quick reference (smart-scale)

Run these from the project folder (`smart-scale`), or use the PlatformIO UI in VS Code / Cursor.

## Clean build artifacts

```bash
pio run -t clean
```

Removes the `seeed_xiao_esp32c3` build directory under `.pio/build/`. Next `pio run` is a full recompile.

## Build (after clean or anytime)

```bash
pio run
```

## Upload firmware

```bash
pio run -t upload
```

Ensure the XIAO ESP32C3 is connected by USB and the correct serial port is available (PlatformIO usually auto-picks it).

## Clean + build + upload (one shot)

```bash
pio run -t clean && pio run -t upload
```

## Serial monitor (115200 baud)

`platformio.ini` sets `monitor_speed = 115200`.

```bash
pio device monitor
```

Or: **PlatformIO: Upload and Monitor** from the command palette (builds/uploads/opens monitor if your environment supports it).

## If upload fails

- Hold **BOOT** on the XIAO while plugging USB or when upload starts, if the chip won’t enter download mode.
- List ports: `pio device list`

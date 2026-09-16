# Lightweight split logging

These diagnostic builds add a USB serial console to both halves while avoiding
per-key log output. Key transitions are counted with atomic counters and only
summarized every five seconds.

The normal firmware is unchanged by the diagnostic module. Do not leave the
logging firmware installed after the capture.

## Capture procedure on Windows (recommended)

WSL does not receive Windows USB serial devices automatically. Run the capture
from native PowerShell instead of adding USB/IP forwarding to the test.

1. Flash `skreecustom_left_light_logging.uf2` to the left half and
   `skreecustom_right_light_logging.uf2` to the right half.
2. Connect both halves with data-capable USB cables.
3. List the COM ports in PowerShell:

   ```powershell
   Get-CimInstance Win32_SerialPort | Format-Table DeviceID, Name
   ```

4. Start the capture, substituting the two COM port names:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\tools\capture_split_logs.ps1 `
     -Left COM3 `
     -Right COM4
   ```

5. Keep the PowerShell window open while reproducing the lag. Press `M`
   immediately after it happens to add a timestamped marker. Press `Q` to stop.

## Capture procedure on macOS or Linux

1. Flash `skreecustom_left_light_logging.uf2` to the left half and
   `skreecustom_right_light_logging.uf2` to the right half.
2. Connect both halves with data-capable USB cables.
3. Identify the ports by connecting one half at a time:
   - macOS: `ls /dev/cu.usbmodem*`
   - Linux: `ls /dev/ttyACM*`
4. Start the capture, substituting the two port names:

   ```sh
   python3 tools/capture_split_logs.py \
     --left /dev/cu.usbmodemLEFT \
     --right /dev/cu.usbmodemRIGHT
   ```

5. Keep the capture running continuously while reproducing the lag. Immediately
   after an occurrence, focus the terminal, type a short note such as
   `MARK lag while typing hello`, and press Enter.
6. After capturing at least one event, press Ctrl-C and flash both normal
   firmware images back.

The script writes `left.log`, `right.log`, and a host-timestamped `combined.log`.
It automatically reopens a serial port if a controller resets or disconnects.

## Reading the result

- A new `BOOT` line or `CAPTURE_RETRY` on the right during the lag indicates a
  controller reset, USB/power interruption, or physical connection problem.
- Increasing right `local_events` while left `remote_events` stops increasing
  indicates an inter-half BLE/transport problem.
- Right `local_events` stopping while right heartbeats continue indicates the
  matrix/SPI/input path rather than BLE.
- Both halves and event counters continuing normally while the host misses input
  points to the left-to-host connection or host-side processing.
- `DISCONNECTED ... reason=0x08` is a BLE supervision timeout.

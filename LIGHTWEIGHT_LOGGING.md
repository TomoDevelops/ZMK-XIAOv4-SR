# Lightweight split logging on macOS

The diagnostic work runs on the keyboard. Each half reports its boot/reset
cause, Bluetooth connection state, key-transition counters, and a heartbeat
every five seconds. It does not print every key event.

The Mac only saves the two USB serial streams. The capture below uses the
standard macOS shell directly; it does not require Python, a helper script, or
an installed package.

The normal firmware is unchanged by the diagnostic module. Flash the normal
images back after collecting the logs.

## Capture procedure

1. Flash `skreecustom_left_light_logging.uf2` to the left half and
   `skreecustom_right_light_logging.uf2` to the right half.
2. Connect both halves directly to the Mac with data-capable USB cables.
3. Open Terminal on the Mac itself, not WSL or an SSH session, and list the
   ports:

   ```sh
   ls -1 /dev/cu.usbmodem*
   ```

   Two simultaneously connected halves must produce two different paths. If
   no paths appear, reset each half once and check the cables and flashed
   images.

4. Paste the block below into the same Terminal window. Change only `PORT_A`
   and `PORT_B` to the two paths shown by the previous command.

   ```sh
   PORT_A=/dev/cu.usbmodem101
   PORT_B=/dev/cu.usbmodem1101
   LOG_DIR="$HOME/Desktop/skree-logs-$(date +%Y%m%d-%H%M%S)"
   mkdir -p "$LOG_DIR"

   capture_port() {
     local port="$1"
     local file="$2"
     : > "$file"

     while true; do
       printf '[HOST] opening %s\n' "$port" >> "$file"
       while IFS= read -r line; do
         printf '%s\n' "$line" >> "$file"
       done < "$port"
       printf '[HOST %s] port closed; retrying\n' \
         "$(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$file"
       sleep 1
     done
   }

   capture_port "$PORT_A" "$LOG_DIR/port-a.log" &
   PID_A=$!
   capture_port "$PORT_B" "$LOG_DIR/port-b.log" &
   PID_B=$!

   cleanup_capture() {
     kill "$PID_A" "$PID_B" 2>/dev/null
     wait "$PID_A" "$PID_B" 2>/dev/null
   }
   trap cleanup_capture EXIT INT TERM

   echo "Recording to $LOG_DIR"
   echo "After lag: focus this window and press Return. To stop: type q and press Return."
   while IFS= read -r command; do
     if [[ "$command" == q ]]; then
       break
     fi
     marker="[HOST_MARK $(date -u +%Y-%m-%dT%H:%M:%SZ)] lag observed"
     printf '%s\n' "$marker" >> "$LOG_DIR/port-a.log"
     printf '%s\n' "$marker" >> "$LOG_DIR/port-b.log"
     echo "$marker"
   done

   trap - EXIT INT TERM
   cleanup_capture
   echo "Saved logs in $LOG_DIR"
   ```

5. Reproduce the lag. Immediately after it happens, focus Terminal and press
   Return to insert the same `HOST_MARK` into both files. Type `q` and press
   Return after capturing at least one event.
6. Send both files from the new `skree-logs-*` folder on the Desktop. The first
   `HEARTBEAT side=...` line identifies which physical half produced each file.

## Reading the result

- A new `BOOT` line or a host `port closed` line near the marker indicates a
  controller reset, USB/power interruption, or physical connection problem.
- Increasing right `local_events` while left `remote_events` stops increasing
  indicates an inter-half Bluetooth/transport problem.
- Right `local_events` stopping while right heartbeats continue indicates the
  matrix/SPI/input path rather than Bluetooth.
- Both halves and event counters continuing normally while the host misses input
  points to the left-to-host connection or host-side processing.
- `DISCONNECTED ... reason=0x08` is a Bluetooth supervision timeout.

#!/usr/bin/env python3
"""Continuously capture both Skree USB logging ports with host timestamps.

The reader reopens a port after a controller reset or USB disconnect. It uses
only Python's standard library and is intended for macOS and Linux.
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import pathlib
import sys
import threading
import time


OUTPUT_LOCK = threading.Lock()
STOP = threading.Event()


def timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="milliseconds")


def emit(side: str, message: str, side_file, combined_file) -> None:
    line = f"[{timestamp()}] [{side}] {message.rstrip()}\n"
    with OUTPUT_LOCK:
        side_file.write(line)
        side_file.flush()
        combined_file.write(line)
        combined_file.flush()
        sys.stdout.write(line)
        sys.stdout.flush()


def capture(side: str, port: str, output_dir: pathlib.Path, combined_file) -> None:
    side_path = output_dir / f"{side}.log"
    with side_path.open("a", encoding="utf-8", buffering=1) as side_file:
        while not STOP.is_set():
            try:
                with open(port, "rb", buffering=0) as serial_port:
                    emit(side, f"CAPTURE_OPEN port={port}", side_file, combined_file)
                    pending = b""
                    while not STOP.is_set():
                        chunk = serial_port.read(512)
                        if not chunk:
                            raise OSError("end of stream")
                        pending += chunk
                        while b"\n" in pending:
                            raw_line, pending = pending.split(b"\n", 1)
                            emit(
                                side,
                                raw_line.rstrip(b"\r").decode("utf-8", errors="replace"),
                                side_file,
                                combined_file,
                            )
            except (FileNotFoundError, PermissionError, OSError) as error:
                emit(side, f"CAPTURE_RETRY port={port} error={error}", side_file, combined_file)
                STOP.wait(0.5)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture left and right ZMK USB logs, reopening ports after resets."
    )
    parser.add_argument("--left", required=True, help="Left serial device, e.g. /dev/cu.usbmodem1101")
    parser.add_argument("--right", required=True, help="Right serial device, e.g. /dev/cu.usbmodem1201")
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=pathlib.Path("split-logs") / dt.datetime.now().strftime("%Y%m%d-%H%M%S"),
        help="Output directory (default: split-logs/<timestamp>)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    combined_path = args.output / "combined.log"

    with combined_path.open("a", encoding="utf-8", buffering=1) as combined_file:
        workers = [
            threading.Thread(
                target=capture,
                args=("left", args.left, args.output, combined_file),
                daemon=True,
            ),
            threading.Thread(
                target=capture,
                args=("right", args.right, args.output, combined_file),
                daemon=True,
            ),
        ]
        for worker in workers:
            worker.start()

        print(f"Capturing to {args.output.resolve()}")
        print("Type a note and press Enter to add a MARK; press Ctrl-C to stop.")
        try:
            while not STOP.is_set():
                note = input()
                if note.strip():
                    with OUTPUT_LOCK:
                        line = f"[{timestamp()}] [MARK] {note.strip()}\n"
                        combined_file.write(line)
                        combined_file.flush()
                        sys.stdout.write(line)
                        sys.stdout.flush()
        except (KeyboardInterrupt, EOFError):
            STOP.set()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

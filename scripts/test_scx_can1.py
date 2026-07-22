#!/usr/bin/env python3
"""Test an SCX Hub connected through a Linux SocketCAN interface.

The SCX Hub interface uses Classic CAN at 500 kbit/s:
  * Receive 0x121 (DLC 8): function/button state bitmap.
  * Transmit 0x212 (DLC 8): current engine RPM, big-endian in bytes 6-7.

Examples:
  # Configure can1, listen for button states, and keep the SCX LEDs at 0 RPM.
  sudo python3 scripts/test_scx_can1.py --configure

  # Drive the SCX LEDs at 3,500 RPM while printing button state changes.
  sudo python3 scripts/test_scx_can1.py --configure --rpm 3500

    # Continuously sweep the SCX LEDs from 800 to 6,500 RPM.
    sudo python3 scripts/test_scx_can1.py --configure --rpm-sweep

  # Receive only; do not transmit RPM frames.
  python3 scripts/test_scx_can1.py --no-rpm
"""

import argparse
import os
import select
import socket
import struct
import subprocess
import sys
import time

CAN_SFF_MASK = 0x000007FF
CAN_EFF_FLAG = 0x80000000
CAN_RTR_FLAG = 0x40000000
CAN_RAW = 1
CAN_RAW_FILTER = 1
CAN_FRAME_FORMAT = "=IB3x8s"
CAN_FILTER_FORMAT = "=II"
SCX_BUTTON_STATE_ID = 0x121
SCX_RPM_ID = 0x212
SCX_FUNCTION_COUNT = 30
SCX_RPM_SWEEP_MIN = 800
SCX_RPM_SWEEP_MAX = 6500
SCX_RPM_SWEEP_STEP = 100


def configure_interface(interface: str) -> None:
    """Configure the selected SocketCAN interface for the SCX Hub."""
    if os.geteuid() != 0:
        raise RuntimeError("--configure requires root; rerun with sudo")

    commands = (
        ("ip", "link", "set", interface, "down"),
        (
            "ip",
            "link",
            "set",
            interface,
            "up",
            "type",
            "can",
            "bitrate",
            "500000",
            "sample-point",
            "0.625",
            "restart-ms",
            "100",
        ),
    )
    for command in commands:
        subprocess.run(command, check=True)


def open_scx_socket(interface: str) -> socket.socket:
    """Open a SocketCAN raw socket that accepts only standard ID 0x121."""
    can_socket = socket.socket(socket.PF_CAN, socket.SOCK_RAW, CAN_RAW)

    # Match a standard, non-RTR 0x121 frame exactly.  RPM transmissions from
    # this process are not received because SocketCAN disables loopback here.
    can_filter = struct.pack(
        CAN_FILTER_FORMAT,
        SCX_BUTTON_STATE_ID,
        CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG,
    )
    can_socket.setsockopt(socket.SOL_CAN_RAW, CAN_RAW_FILTER, can_filter)
    can_socket.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_RECV_OWN_MSGS, 0)
    can_socket.bind((interface,))
    return can_socket


def rpm_frame(rpm: int) -> bytes:
    """Build a documented SCX RPM frame (ID 0x212, DLC 8)."""
    payload = b"\x00" * 6 + rpm.to_bytes(2, byteorder="big")
    return struct.pack(CAN_FRAME_FORMAT, SCX_RPM_ID, 8, payload)


def send_rpm(can_socket: socket.socket, rpm: int) -> None:
    can_socket.send(rpm_frame(rpm))


def decode_functions(payload: bytes) -> int:
    """Return the documented 30-bit SCX function-state bitmap."""
    return (
        payload[1]
        | (payload[2] << 8)
        | (payload[3] << 16)
        | ((payload[4] & 0x3F) << 24)
    )


def active_functions(bitmap: int) -> list[int]:
    return [number for number in range(1, SCX_FUNCTION_COUNT + 1)
            if bitmap & (1 << (number - 1))]


def format_functions(bitmap: int) -> str:
    functions = active_functions(bitmap)
    return ", ".join(f"F{number}" for number in functions) if functions else "none"


def print_state_change(previous: int | None, current: int) -> None:
    if previous is None:
        print(f"SCX 0x121 initial state: {format_functions(current)}")
        return

    pressed = current & ~previous
    released = previous & ~current
    if pressed or released:
        changes = []
        if pressed:
            changes.append("pressed " + format_functions(pressed))
        if released:
            changes.append("released " + format_functions(released))
        print("SCX 0x121: " + "; ".join(changes))


def receive_state(can_socket: socket.socket) -> int | None:
    raw_frame = can_socket.recv(struct.calcsize(CAN_FRAME_FORMAT))
    if len(raw_frame) != struct.calcsize(CAN_FRAME_FORMAT):
        print("Ignoring truncated CAN frame", file=sys.stderr)
        return None

    can_id, dlc, payload = struct.unpack(CAN_FRAME_FORMAT, raw_frame)
    if can_id != SCX_BUTTON_STATE_ID or dlc != 8:
        return None
    if payload[0] != 0 or payload[5:] != b"\x00\x00\x00":
        print("Warning: 0x121 reserved bytes are not zero", file=sys.stderr)
    if payload[4] & 0xC0:
        print("Warning: 0x121 byte4 reserved bits 6-7 are set", file=sys.stderr)
    return decode_functions(payload)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Receive SCX Hub button states and optionally drive its RPM LEDs."
    )
    parser.add_argument("--interface", default="can1", help="SocketCAN interface (default: can1)")
    parser.add_argument(
        "--configure",
        action="store_true",
        help="configure the interface for 500 kbit/s Classic CAN before testing (requires root)",
    )
    parser.add_argument(
        "--rpm",
        type=int,
        default=0,
        help="RPM sent to SCX Hub LED controller (default: 0; range: 0-65535)",
    )
    parser.add_argument(
        "--no-rpm",
        action="store_true",
        help="receive 0x121 frames only; do not transmit 0x212 RPM frames",
    )
    parser.add_argument(
        "--rpm-sweep",
        action="store_true",
        help="continuously sweep RPM from 800 through 6500 in 100 RPM steps",
    )
    parser.add_argument(
        "--period-ms",
        type=int,
        default=40,
        help="RPM transmit interval in milliseconds, from 20 through 50 (default: 40)",
    )
    parser.add_argument(
        "--heartbeat-timeout",
        type=float,
        default=3.0,
        help="warn when no valid 0x121 arrives for this many seconds (default: 3)",
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=0.0,
        help="test duration in seconds; 0 runs until Ctrl-C (default: 0)",
    )
    args = parser.parse_args()
    if not 0 <= args.rpm <= 65535:
        parser.error("--rpm must be between 0 and 65535")
    if not 20 <= args.period_ms <= 50:
        parser.error("--period-ms must be between 20 and 50")
    if args.heartbeat_timeout <= 0:
        parser.error("--heartbeat-timeout must be positive")
    if args.duration < 0:
        parser.error("--duration cannot be negative")
    if args.no_rpm and args.rpm_sweep:
        parser.error("--no-rpm and --rpm-sweep cannot be used together")
    return args


def main() -> int:
    args = parse_arguments()
    try:
        if args.configure:
            configure_interface(args.interface)
        can_socket = open_scx_socket(args.interface)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Unable to open {args.interface}: {error}", file=sys.stderr)
        return 1

    print(f"Listening on {args.interface} for SCX 0x121 function states.")
    if args.no_rpm:
        print("RPM transmission disabled.")
    elif args.rpm_sweep:
        print(
            f"Sweeping SCX 0x212 RPM from {SCX_RPM_SWEEP_MIN} through "
            f"{SCX_RPM_SWEEP_MAX} every {args.period_ms} ms."
        )
    else:
        print(f"Sending SCX 0x212 RPM={args.rpm} every {args.period_ms} ms.")

    started = time.monotonic()
    state: int | None = None
    # Start the timeout immediately so a Hub that never sends 0x121 is also
    # reported as disconnected after the configured heartbeat interval.
    last_heartbeat = started
    heartbeat_reported_missing = False
    next_rpm = started
    interval = args.period_ms / 1000.0
    rpm_transmit_failed = False
    current_rpm = SCX_RPM_SWEEP_MIN if args.rpm_sweep else args.rpm

    try:
        while True:
            now = time.monotonic()
            if args.duration and now - started >= args.duration:
                break

            if not args.no_rpm and now >= next_rpm:
                try:
                    send_rpm(can_socket, current_rpm)
                    if rpm_transmit_failed:
                        print("SCX RPM transmission restored.")
                    rpm_transmit_failed = False
                except OSError as error:
                    if not rpm_transmit_failed:
                        print(
                            f"SCX RPM transmission failed: {error}. "
                            "Check that the Hub is powered, connected, and acknowledging CAN frames.",
                            file=sys.stderr,
                        )
                    rpm_transmit_failed = True
                if args.rpm_sweep:
                    current_rpm += SCX_RPM_SWEEP_STEP
                    if current_rpm > SCX_RPM_SWEEP_MAX:
                        current_rpm = SCX_RPM_SWEEP_MIN
                next_rpm = now + interval

            if now - last_heartbeat >= args.heartbeat_timeout:
                if not heartbeat_reported_missing:
                    print(
                        f"SCX heartbeat missing for {args.heartbeat_timeout:g} seconds; "
                        "clearing stored function state.",
                        file=sys.stderr,
                    )
                    state = None
                    heartbeat_reported_missing = True

            timeout = 0.1
            if not args.no_rpm:
                timeout = max(0.0, min(timeout, next_rpm - time.monotonic()))
            readable, _, _ = select.select((can_socket,), (), (), timeout)
            if readable:
                received_state = receive_state(can_socket)
                if received_state is not None:
                    print_state_change(state, received_state)
                    state = received_state
                    last_heartbeat = time.monotonic()
                    heartbeat_reported_missing = False
    except KeyboardInterrupt:
        print("\nStopping SCX test.")
    finally:
        if not args.no_rpm:
            try:
                send_rpm(can_socket, 0)
                print("Sent final SCX 0x212 zero-RPM frame.")
            except OSError as error:
                print(f"Unable to send final zero-RPM frame: {error}", file=sys.stderr)
        can_socket.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

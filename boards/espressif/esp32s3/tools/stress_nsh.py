#!/usr/bin/env python3
"""NSH USB stress test for ESP32-S3 incremental bring-up.

Checks: boot (minboot or rcS), dataman, MTD, echo, 50x help/ps/cat rc, no panic.
Exit 0 on pass, 1 on open failure, 2 on functional failure.
"""

from __future__ import annotations

import argparse
import os
import serial
import sys
import time

PANIC_MARKERS = (
    "EXCCAUSE",
    "Backtrace",
    "Assertion failed",
    "panic",
    "Hard fault",
)


def read_available(ser: serial.Serial, limit: int = 65536) -> str:
    n = ser.in_waiting
    if not n:
        return ""
    return ser.read(min(n, limit)).decode("utf-8", errors="replace")


def boot_complete_from_log(buf: str, stage: int) -> bool:
    """True when captured serial log shows expected boot markers."""
    if "nsh>" not in buf:
        return False
    if "[minboot] ready" in buf:
        return True
    if stage >= 12:
        return (
            "[esp32s3] board defaults" in buf
            or "board defaults:" in buf.lower()
        )
    if stage >= 11:
        return "[minboot] ready" in buf
    return True


def append_and_check(buf: str, chunk: str, stage: int) -> tuple[str, bool, bool, bool]:
    """Append serial data; return (buf, panic, log_ok, prompt_ok)."""
    if not chunk:
        return buf, False, boot_complete_from_log(buf, stage), "nsh>" in buf

    buf = (buf + chunk)[-16384:]
    lower = buf.lower()
    panic = any(m.lower() in lower for m in PANIC_MARKERS)
    log_ok = boot_complete_from_log(buf, stage)
    prompt_ok = "nsh>" in buf
    return buf, panic, log_ok, prompt_ok


def wait_for_port_and_nsh(
    port: str,
    baud: int,
    timeout: float,
    stage: int,
) -> tuple[serial.Serial | None, str, bool, bool, float]:
    """Poll port enumeration (1 Hz), then probe NSH with CR (1 Hz).

    Returns (serial, boot_buf, log_boot_ok, prompt_ok, elapsed_s).
    """
    deadline = time.monotonic() + timeout
    started = time.monotonic()
    ser: serial.Serial | None = None
    buf = ""
    log_ok = False
    prompt_ok = False
    tick = 0

    while time.monotonic() < deadline:
        tick += 1
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break

        if ser is None:
            if os.path.exists(port):
                try:
                    ser = serial.Serial(port, baud, timeout=0.1)
                    ser.reset_input_buffer()
                    print(f"=== Serial enumerated: {port} (tick {tick}) ===")
                except OSError as exc:
                    print(f"=== Serial present but open failed (tick {tick}): {exc} ===")
            else:
                print(f"=== Waiting for {port} (tick {tick}) ===")
            time.sleep(min(1.0, remaining))
            continue

        buf, panic, log_ok, prompt_ok = append_and_check(buf, read_available(ser, 8192), stage)
        if panic:
            return ser, buf, False, False, time.monotonic() - started
        if log_ok or prompt_ok:
            return ser, buf, log_ok, prompt_ok, time.monotonic() - started

        ser.write(b"\r\n")
        time.sleep(min(1.0, deadline - time.monotonic()))

        buf, panic, log_ok, prompt_ok = append_and_check(buf, read_available(ser, 8192), stage)
        if panic:
            return ser, buf, False, False, time.monotonic() - started
        if log_ok or prompt_ok:
            return ser, buf, log_ok, prompt_ok, time.monotonic() - started

    elapsed = time.monotonic() - started
    return ser, buf, log_ok, prompt_ok, elapsed


def run_cmd(ser: serial.Serial, cmd: str, wait: float) -> str:
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode())
    time.sleep(wait)
    return read_available(ser)


def rc_cat_path(stage: int, boot: str) -> str:
    if stage >= 12 or "[minboot]" not in boot:
        return "/etc/init.d/rc.board_defaults"
    return "/etc/init.d/rc.board_minboot"


def main() -> int:
    parser = argparse.ArgumentParser(description="ESP32-S3 NSH stress test")
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--cycles", type=int, default=50)
    parser.add_argument(
        "--boot-wait",
        type=float,
        default=10.0,
        help="Max seconds to wait for port enumerate + nsh> (1 Hz poll/probe)",
    )
    parser.add_argument("--stage", type=int, default=12,
                        help="Boot/check profile (default: 12 = full rcS)")
    args = parser.parse_args()

    if args.stage >= 12:
        boot_timeout = max(args.boot_wait, 10.0)
    elif args.stage >= 11:
        boot_timeout = max(args.boot_wait, 8.0)
    else:
        boot_timeout = args.boot_wait

    ser, boot, log_boot_ok, prompt_ok, elapsed = wait_for_port_and_nsh(
        args.port, args.baud, boot_timeout, args.stage
    )

    if ser is None:
        print(f"OPEN_FAIL: {args.port} not available within {boot_timeout}s")
        return 1

    print(
        f"=== NSH ready ({elapsed:.1f}s / {boot_timeout}s, "
        f"log_boot_ok={log_boot_ok}, nsh_prompt={prompt_ok}) ==="
    )
    if not prompt_ok and not log_boot_ok:
        print("WARN: no nsh> prompt before boot-wait expired")

    print("=== BOOT (tail) ===")
    print(boot[-2000:])
    if any(m.lower() in boot.lower() for m in PANIC_MARKERS):
        print("FAIL: panic markers in boot output")
        ser.close()
        return 2

    checks: dict[str, bool] = {
        "echo": False,
        "mtd": False,
        "boot_complete": log_boot_ok or prompt_ok,
        "nsh_prompt": prompt_ok,
        "minboot": "[minboot]" in boot,
        "rcs_boot": (
            "[esp32s3] board defaults" in boot
            or "board defaults:" in boot.lower()
        ),
        "dataman": "dataman" in boot.lower(),
    }
    cat_rc = rc_cat_path(args.stage, boot)

    ser.write(b"w")
    time.sleep(0.3)
    echo = read_available(ser, 64)
    checks["echo"] = "w" in echo
    print("=== ECHO (sent w) ===", repr(echo))
    ser.write(b"\r\n")

    fs_out = run_cmd(ser, "ls /fs", 0.8)
    print("=== ls /fs ===")
    print(fs_out[:400])
    checks["mtd"] = "mtd_params" in fs_out

    ps_out = run_cmd(ser, "ps", 1.0)
    print("=== ps (once) ===")
    ps_limit = 4096 if args.stage >= 11 else 1200
    print(ps_out[:ps_limit])

    checks["dataman"] = checks["dataman"] or "dataman" in ps_out.lower()

    if args.stage >= 12 and not checks["rcs_boot"]:
        rc_probe = run_cmd(ser, "cat /etc/init.d/rc.board_defaults", 1.0)
        checks["rcs_boot"] = (
            "[esp32s3] board defaults" in rc_probe
            or "board defaults:" in rc_probe.lower()
        )

    if args.stage >= 1:
        checks["mavlink"] = "mavlink" in ps_out.lower()
        if not checks["mavlink"]:
            print("WARN: stage>=1 but mavlink not in ps output")

    if args.stage >= 11:
        checks["commander"] = "commander" in ps_out.lower()
        checks["wq_ins"] = "wq:ins" in ps_out.lower()
        checks["wq_rate"] = "wq:rate_ctrl" in ps_out.lower()
        checks["wq_nav"] = "wq:nav_and_controllers" in ps_out.lower()
        for name, ok in (
            ("commander", checks["commander"]),
            ("wq:INS0", checks["wq_ins"]),
            ("wq:rate_ctrl", checks["wq_rate"]),
            ("wq:nav_and_controllers", checks["wq_nav"]),
        ):
            if not ok:
                print(f"WARN: stage>=11 but {name} not in ps output")

    if args.stage >= 2 and "wq:lp_default" not in ps_out:
        print("WARN: stage>=2 but wq:lp_default not in ps output")

    print(f"=== stress cat target: {cat_rc} ===")
    for cycle in range(1, args.cycles + 1):
        for cmd, wait in (
            ("help", 0.3),
            ("ps", 0.5),
            (f"cat {cat_rc}", 0.8),
        ):
            out = run_cmd(ser, cmd, wait)
            lower = out.lower()
            if any(m.lower() in lower for m in PANIC_MARKERS):
                print(f"FAIL cycle {cycle} cmd={cmd!r}")
                print(out[-1200:])
                ser.close()
                return 2
        print(f"cycle {cycle}/{args.cycles} ok")

    print("CHECKS:", checks)
    required = {"echo", "mtd", "boot_complete", "nsh_prompt"}
    if args.stage >= 1:
        required.add("mavlink")
    if args.stage >= 11:
        required |= {"commander", "wq_ins", "wq_rate", "wq_nav", "dataman"}
    if args.stage >= 12:
        required.add("rcs_boot")

    missing = {k: v for k, v in checks.items() if k in required and not v}
    if missing:
        print("FAIL: missing checks", missing)
        ser.close()
        return 2

    stage_msg = f" stage={args.stage}" if args.stage >= 0 else ""
    print(f"ALL_OK{stage_msg}")
    ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Minimal Thessla Green AirPack 4 Modbus RTU slave for testing without the real unit.

Creates a pseudo-terminal and links it to the given path, e.g.:

    python3 airpack_simulator.py /tmp/ttyAIRPACK

then point the firmware's UART at that path (ESPHome `host` platform, `uart: port:`),
or a USB-RS485 adapter at it via `socat`.

It mimics the controller's documented behaviour that matters to clients:
  * unit address 10, at most 16 registers/bits per request,
  * reads touching an undocumented address fail with exception 0x02,
  * temporary mode is started only by the 3-register block writes at 4400/4403.
"""

import os
import pty
import select
import sys
import termios
import time
import tty

ADDRESS = 10
VERBOSE = bool(os.environ.get("SIM_VERBOSE"))
MAX_COUNT = 16

INPUT = {
    0: 4, 1: 85, 4: 2,  # firmware 4.85.2
    16: 93, 17: 203, 18: 221, 19: 92, 22: 218,  # TZ1 TN1 TP TZ2 TO (x0.1 °C); TN2/TZ3 absent
    24: 0x1A, 25: 0x2B, 26: 0x3C, 27: 0x4D, 28: 0x5E, 29: 0x6F,  # serial
    271: 1, 272: 15, 273: 15, 274: 63, 275: 63,
}
HOLDING = {
    0: 0x2610, 1: 0x2903, 2: 0x1754, 3: 0x0000,  # 2026-10-29 Thu 17:54
    256: 63, 257: 63,
    4192: 1, 4198: 0,
    4208: 1, 4209: 1, 4210: 15, 4211: 30, 4212: 42, 4213: 44,
    4224: 0,
    4304: 0, 4305: 0,
    4320: 0, 4321: 20, 4322: 44, 4323: 38, 4330: 0, 4331: 1,
    4387: 1,
    4400: 0, 4401: 0, 4402: 0, 4403: 0, 4404: 0, 4405: 0,
    4704: 0, 4711: 0,
    4384: 0,  # S alarm code
    4482: 25, 4483: 20,  # filter wear %
    4660: 0x3943, 4662: 0x3943,  # filter replacement dates (2028-10-03)
    8191: 4, 8192: 0, 8193: 0,
}
COILS = {5: 0, 9: 0, 10: 1, 11: 1, 12: 0, 13: 0, 14: 0, 15: 0}
DISCRETE = {i: 0 for i in (0, 1, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 18, 19, 21)}

# What a write may contain, per register (None = any value).
WRITABLE = {
    0: None, 1: None, 2: None, 3: None,
    4208: {0, 1}, 4209: {0, 1}, 4210: set(range(10, 101)), 4212: set(range(20, 91)),
    4224: {0, 2, 7, 10, 11}, 4304: {0, 1}, 4320: {0, 1},
    4321: set(range(10, 41)), 4322: set(range(30, 61)), 4323: set(range(30, 61)), 4331: {1, 2, 3},
    4387: {0, 1}, 4711: {0, 1, 2}, 8191: {0x11, 0x21},
}


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def frame(pdu: bytes) -> bytes:
    body = bytes([ADDRESS]) + pdu
    crc = crc16(body)
    return body + bytes([crc & 0xFF, crc >> 8])


def exception(fc: int, code: int) -> bytes:
    return frame(bytes([fc | 0x80, code]))


def log(msg: str) -> None:
    print(f"[sim {time.strftime('%H:%M:%S')}] {msg}", flush=True)


def handle(pdu: bytes) -> bytes:
    fc = pdu[0]
    if fc in (1, 2, 3, 4):
        start, count = int.from_bytes(pdu[1:3], "big"), int.from_bytes(pdu[3:5], "big")
        table = {1: COILS, 2: DISCRETE, 3: HOLDING, 4: INPUT}[fc]
        if not 1 <= count <= MAX_COUNT:
            log(f"FC{fc} {start}+{count}: too many -> exception 3")
            return exception(fc, 3)
        addresses = range(start, start + count)
        if fc == 3 and start < 4 and (start != 0 or count != 4):
            # Manufacturer table: the four clock registers must be read together.
            log(f"FC3 {start}+{count}: partial clock read -> exception 2")
            return exception(fc, 2)
        if any(a not in table for a in addresses):
            log(f"FC{fc} {start}+{count}: undocumented address -> exception 2")
            return exception(fc, 2)
        if fc in (1, 2):
            data = bytearray((count + 7) // 8)
            for i, a in enumerate(addresses):
                if table[a]:
                    data[i // 8] |= 1 << (i % 8)
            if VERBOSE:
                log(f"FC{fc} {start}+{count}")
            return frame(bytes([fc, len(data)]) + bytes(data))
        if VERBOSE:
            log(f"FC{fc} {start}+{count}")
        payload = b"".join(table[a].to_bytes(2, "big") for a in addresses)
        return frame(bytes([fc, len(payload)]) + payload)

    if fc == 6:
        address, value = int.from_bytes(pdu[1:3], "big"), int.from_bytes(pdu[3:5], "big")
        allowed = WRITABLE.get(address, "missing")
        if allowed == "missing" or (allowed is not None and value not in allowed):
            log(f"WRITE {address} = {value}: rejected -> exception 3")
            return exception(fc, 3)
        if address == 8191:
            # Filter replacement command: wear back to 0, register keeps reading the filter type.
            HOLDING[4482 if value == 0x11 else 4483] = 0
            log(f"FILTER REPLACED: {'supply' if value == 0x11 else 'extract'}")
            return frame(pdu[:5])
        HOLDING[address] = value
        log(f"WRITE {address} (0x{address:04X}) = {value}")
        return frame(pdu[:5])

    if fc == 16:
        start, count = int.from_bytes(pdu[1:3], "big"), int.from_bytes(pdu[3:5], "big")
        values = [int.from_bytes(pdu[6 + 2 * i : 8 + 2 * i], "big") for i in range(count)]
        if start in (4400, 4403) and count == 3 and values[0] == 2 and values[2] == 1:
            target = 4211 if start == 4400 else 4213
            HOLDING[4208] = 2
            HOLDING[target] = values[1]
            log(f"BLOCK {start}: temporary mode, register {target} = {values[1]}")
        elif start == 0 and count == 4:
            HOLDING.update({i: v for i, v in enumerate(values)})
            log("BLOCK 0: clock = " + " ".join(f"{v:04X}" for v in values))
        else:
            log(f"BLOCK {start}+{count} {values}: not a documented command -> exception 2")
            return exception(fc, 2)
        return frame(pdu[:5])

    log(f"FC{fc}: unsupported -> exception 1")
    return exception(fc, 1)


def expected_length(buf: bytes) -> int | None:
    if len(buf) < 2:
        return None
    fc = buf[1]
    if fc in (1, 2, 3, 4, 5, 6):
        return 8
    if fc == 16:
        return 9 + buf[6] if len(buf) >= 7 else None
    return 8


# --- unit dynamics -----------------------------------------------------------------------------
# The real controller recalculates the set intensity about a second after a change and the fans
# then ramp airflow over several seconds. Mimic that so clients can be tested for staleness.
NOMINAL_FLOW = 420  # m3/h at 100 %
AUTO_SCHEDULE_PERCENT = 40
_target_since = [0.0, None]  # time the target changed, target percent


def target_percent() -> int:
    mode = HOLDING[4208]
    if mode == 1:
        return HOLDING[4210]
    if mode == 2:
        return HOLDING[4211]
    return AUTO_SCHEDULE_PERCENT


def step_dynamics(now: float) -> None:
    target = target_percent()
    if HOLDING[4387] == 0:
        target = 0
    if target != _target_since[1]:
        _target_since[0], _target_since[1] = now, target
    if now - _target_since[0] >= 1.0:
        INPUT[272] = INPUT[273] = target
    flow_target = round(INPUT[272] * NOMINAL_FLOW / 100)
    for reg in (256, 257, 274, 275):
        table = HOLDING if reg < 258 else INPUT
        cur = table[reg]
        table[reg] = cur + max(-2, min(2, flow_target - cur))  # fans ramp ~40 m3/h per second


def main() -> None:
    link = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ttyAIRPACK"
    master, slave = pty.openpty()
    tty.setraw(master)
    attrs = termios.tcgetattr(slave)
    tty.setraw(slave)
    termios.tcsetattr(slave, termios.TCSANOW, attrs)
    if os.path.lexists(link):
        os.unlink(link)
    os.symlink(os.ttyname(slave), link)
    log(f"AirPack simulator on {link} -> {os.ttyname(slave)}, unit address {ADDRESS}")

    buf = b""
    while True:
        ready, _, _ = select.select([master], [], [], 0.05)
        step_dynamics(time.monotonic())
        if not ready:
            buf = b""  # inter-frame silence
            continue
        buf += os.read(master, 256)
        while True:
            need = expected_length(buf)
            if need is None or len(buf) < need:
                break
            request, buf = buf[:need], buf[need:]
            if crc16(request[:-2]) != int.from_bytes(request[-2:], "little"):
                log(f"bad CRC: {request.hex()}")
                buf = b""
                break
            if request[0] != ADDRESS:
                continue
            os.write(master, handle(request[1:-2]))


if __name__ == "__main__":
    main()

"""Does an accepted command push its own state, and does the push carry the new value?

Sends one command at a time and decodes every 0x95 that follows. A push carrying the old
value would look identical to no push at all from a host's side, so both are worth telling
apart here.
"""
import re
import struct
import time

import serial

CASES = [
    ("<N_int,42>",        "N_int_state should become 42"),
    ("<N_float,-1.25>",   "N_float_state should become -1.25"),
    ("<S_enabled,1>",     "S_enabled_state should become 1"),
    ("<L_wave,Triangle>", "L_wave_state should become Triangle"),
    ("<T_label,from-cmd>", "T_label_state should become from-cmd"),
    ("<N_int,500>",       "refused, so nothing should be pushed"),
]

NAME = {0x0: "bool", 0x1: "byte", 0x2: "short", 0x3: "ushort", 0x4: "int", 0x5: "uint",
        0x6: "long", 0x7: "ulong", 0x8: "float", 0x9: "double", 0xA: "string"}


def decode(payload):
    if len(payload) < 5:
        return None
    idx = payload[2] | (payload[3] << 8)
    dtype = payload[4]
    body = payload[5:]
    if dtype == 0xA:
        return idx, "string", body[1:1 + body[0]].decode("ascii", "replace")
    # Widths are the device's, not the host's: int is two bytes on AVR and four on a 32-bit
    # core, so the body length is what picks the format.
    signed = dtype in (0x2, 0x4, 0x6)
    fmt = {0x0: "<?", 0x1: "<B", 0x8: "<f", 0x9: "<d"}.get(dtype)
    if fmt is None:
        by_len = {1: "b", 2: "h", 4: "l", 8: "q"}.get(len(body))
        if by_len:
            fmt = "<" + (by_len if signed else by_len.upper())
    if not fmt or len(body) != struct.calcsize(fmt):
        return idx, NAME.get(dtype, "?"), f"<{len(body)} bytes>"
    return idx, NAME.get(dtype, "?"), struct.unpack(fmt, body)[0]


with serial.Serial("COM24", 115200, timeout=0.2) as s:
    s.setDTR(False); time.sleep(0.2); s.setDTR(True)
    time.sleep(3.5)
    s.reset_input_buffer()

    for line, why in CASES:
        s.write(line.encode()); s.flush()
        buf = b""
        end = time.time() + 1.2
        while time.time() < end:
            buf += s.read(4096)

        frames = re.findall(rb"<BLAECK:(.)(?::)(.{4})(?::)(.*?)/BLAECK>\r\n", buf, flags=re.S)
        pushes = [decode(p) for k, _m, p in frames if k[0] == 0x95]
        text = re.sub(rb"<BLAECK:.*?/BLAECK>\r\n", b"", buf, flags=re.S)
        cmd = [l for l in re.findall(rb"CMD [^\r\n]+", text)]

        print(f"\n{line}   ({why})")
        print(f"   handler: {cmd[0].decode() if cmd else 'did not run'}")
        if not pushes:
            print("   0x95 pushes: none")
        for p in pushes:
            print(f"   0x95 push: channel {p[0]}  {p[1]} = {p[2]!r}")

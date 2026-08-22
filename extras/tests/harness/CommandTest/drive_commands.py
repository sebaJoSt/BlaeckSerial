"""Send every command the harness declares, with a value it must take and one it must refuse.

A typed command is checked before its handler runs, so a refusal is an absence: the board
prints nothing. Each case says which it expects, and the mismatch is the finding.
"""
import re
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM24"

# (command line to send, expect_accepted, why)
CASES = [
    ("<N_int,50>",            True,  "in range"),
    ("<N_int,500>",           False, "above max"),
    ("<N_int,-1>",            False, "below min"),
    ("<N_int,abc>",           False, "not a number"),
    ("<N_int,>",              False, "empty value"),
    ("<N_float,2.5>",         True,  "in range, on the step"),
    ("<N_float,2.6>",         True,  "in range, off the step"),
    ("<N_float,99>",          False, "above max"),
    ("<N_level,200>",         True,  "in range"),
    ("<N_level,256>",         False, "above max"),
    ("<N_badrange,7>",        True,  "range was dropped at declaration"),
    ("<S_enabled,1>",         True,  "on"),
    ("<S_enabled,0>",         True,  "off"),
    ("<S_enabled,7>",         False, "a switch takes 0 or 1"),
    ("<S_enabled,ON>",        False, "text is not 0 or 1"),
    ("<S_flag,1>",            True,  "on"),
    ("<L_wave,Square>",       True,  "a declared option"),
    ("<L_wave,Zigzag>",       False, "not a declared option"),
    ("<L_wave,square>",       True,  "options match case-insensitively"),
    ("<L_range,10V>",         True,  "a declared option"),
    ("<B_ping>",              True,  "a button takes no value"),
    ("<B_ping,1>",            True,  "a value a button did not ask for"),
    ("<B_reboot>",            True,  "diagnostic button"),
    ("<T_label,hello>",       True,  "inside maxLength"),
    ("<T_label," + "x" * 31 + ">", True,  "exactly maxLength"),
    ("<T_label," + "x" * 32 + ">", False, "one over maxLength"),
    ("<T_secret,hunter2>",    True,  "inside maxLength"),
    ("<P_print,Hi,3>",        True,  "plain, two parameters"),
    ("<P_print,Hi>",          False, "plain, handler refuses one parameter"),
    ("<Nope,1>",              False, "no such command"),
]


def read_for(s, seconds):
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        buf += s.read(4096)
    return buf


def text_of(raw):
    """Strip Blaeck frames; the debug text shares the port with them."""
    t = re.sub(rb"<BLAECK:.*?/BLAECK>\r\n", b"", raw, flags=re.S)
    out = []
    for line in t.split(b"\n"):
        line = line.replace(b"\r", b"")
        printable = bytes(c for c in line if 32 <= c < 127)
        if len(printable) >= 3 and len(printable) >= len(line) * 0.8:
            out.append(printable.decode("ascii", "replace"))
    return out


with serial.Serial(PORT, 115200, timeout=0.2) as s:
    s.setDTR(False)
    time.sleep(0.2)
    s.setDTR(True)          # reset, so startup checks are captured
    boot = read_for(s, 4.0)
    print("---- startup ----")
    for line in text_of(boot):
        print(" ", line)

    print("\n---- commands ----")
    results = []
    for line, expect, why in CASES:
        s.reset_input_buffer()
        s.write(line.encode("ascii"))
        s.flush()
        raw = read_for(s, 0.9)
        lines = text_of(raw)
        got = any(l.startswith("CMD ") and "refused-by-handler" not in l for l in lines)
        ok = got == expect
        results.append((ok, line, expect, got, why, lines))
        mark = "ok  " if ok else "MISMATCH"
        print(f"  {mark} {line[:44]:46} expected={'accept' if expect else 'refuse':6} "
              f"got={'accept' if got else 'refuse':6}  ({why})")
        for l in lines:
            if l.startswith("CMD ") or "ignored" in l:
                print(f"        | {l}")

bad = [r for r in results if not r[0]]
print(f"\n{len(results) - len(bad)}/{len(results)} as expected, {len(bad)} mismatched")

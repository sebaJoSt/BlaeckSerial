"""Fire every SignalTimingTest command and check the rows a host actually stored -
TimescaleDB, over psycopg2 - not Home Assistant's state or discovery layer.

Every other harness here asks whether a value survives to Home Assistant looking right.
This one asks what got written to disk, which is a different question with a different
tool: HA's REST API only fires the commands (the same way a person clicking a button
would), and psycopg2 - lgbk's own database connection, made directly rather than through
lgbk - reads back what landed. A command can be fired correctly and still be a bug if the
row it produced is wrong, late, missing, or - the case this test exists for - never
arrives at all because a whole session silently stalled.

Eight things get checked, each against real rows rather than an assumption about the code:

    push        write() lands its own row immediately, not on the next periodic tick,
                and that row is partial - only Pushed is non-null, not a full snapshot
    explicit_ts write()'s per-call timestamp override lands exactly as given, independent
                of whatever TimestampMode happens to be active
    burst       five rapid write() calls back-to-back all survive, in order, none dropped
    mark_flush  update() alone reaches no row at all - the value only appears out-of-cycle
                once writeUpdatedData() is called, and that row is partial too
    micros      BLAECK_MICROS rows drift smoothly against wall-clock time (device and
                host oscillators disagree by a small constant rate) rather than jittering
                independently the way PC-mode arrival stamps do - the one signature that
                actually distinguishes "device clock, delta-tracked" from "host clock,
                stamped fresh every row"
    unix_calibrated  BLAECK_UNIX with a real callback lands near the fixed 2030 epoch the
                callback returns, and keeps advancing
    unix_no_callback the documented landmine: BLAECK_UNIX with no callback stamps every
                row at the Unix epoch exactly - and, the reason this case exists at all,
                logging must keep running afterwards rather than silently stalling
                (see BlaeckSerial.cpp's writeDataFrame commit message for the wire-format
                bug this used to trigger)
    force_update  FrozenForced (forceUpdate() on) and FrozenPlain (forceUpdate() off) log
                identically every periodic row - confirms the source has no dedup logic
                backing forceUpdate() at all, so a forced and an unforced signal holding
                the same frozen value cannot be told apart in the database

    HA_URL       Base URL, e.g. https://your-instance
    HA_TOKEN     A long-lived access token, OR
    HA_USERNAME  Username         - a token is exchanged the same way ha_registry.py does it
    HA_PASSWORD  Password

    TSDB_HOST    TimescaleDB host
    TSDB_PORT    TimescaleDB port (default 5432)
    TSDB_DB      Database name
    TSDB_USER    Database user
    TSDB_PASSWORD  Database password

  python drive_signal_timing.py [table]        table defaults to signal_timing_test

Requires a live lgbk session already logging this device to TimescaleDB and bridging to
the same Home Assistant instance - this script only fires commands and reads rows back,
it does not start logging itself.
"""
import datetime
import json
import os
import sys
import time
import urllib.error
import urllib.request

import psycopg2

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
try:
    import ha_registry
except ImportError:
    ha_registry = None

DEVICE = "signal_timing_test"
TABLE = sys.argv[1] if len(sys.argv) > 1 else "signal_timing_test"

UNIX_CALIBRATED_BASE = datetime.datetime(2030, 1, 1, tzinfo=datetime.timezone.utc)
EXPLICIT_TS = datetime.datetime(2000, 1, 1, tzinfo=datetime.timezone.utc)
EPOCH = datetime.datetime(1970, 1, 1, tzinfo=datetime.timezone.utc)


def _need_env(*names):
    missing = [n for n in names if not os.environ.get(n)]
    return missing


# ---- Home Assistant fire layer ------------------------------------------------------------
class HomeAssistant:
    def __init__(self, base_url, token):
        self.base_url = base_url
        self.token = token

    def _request(self, method, path, body=None):
        req = urllib.request.Request(
            self.base_url + path,
            data=json.dumps(body).encode() if body is not None else None,
            headers={"Authorization": f"Bearer {self.token}", "Content-Type": "application/json"},
            method=method,
        )
        with urllib.request.urlopen(req, timeout=15) as resp:
            raw = resp.read()
            return json.loads(raw) if raw else None

    def resolve(self, domain, object_id):
        """Home Assistant keeps a new entity_id (…_2, _3, …) per distinct device identity a
        topic has ever produced; old ones linger forever as "unavailable". A bare
        f"{domain}.{DEVICE}_{object_id}" is only ever the *first* topic this device name was
        ever logged under - almost never the live one. Search every matching entity_id
        instead and take the one that is not unavailable."""
        prefix = f"{domain}.{DEVICE}_{object_id}"
        try:
            states = self._request("GET", "/api/states")
        except urllib.error.HTTPError:
            return None
        candidates = [s for s in states
                      if s["entity_id"] == prefix or s["entity_id"].startswith(prefix + "_")]
        live = [s for s in candidates if s.get("state") != "unavailable"]
        chosen = live[-1] if live else (candidates[-1] if candidates else None)
        return chosen["entity_id"] if chosen else None

    def press(self, entity_id):
        self._request("POST", "/api/services/button/press", {"entity_id": entity_id})

    def select(self, entity_id, option):
        self._request("POST", "/api/services/select/select_option",
                       {"entity_id": entity_id, "option": option})


# ---- TimescaleDB read layer ----------------------------------------------------------------
COLUMNS = ["ID", "TimeStampUTC", "Uptime", "Periodic", "Pushed", "ExplicitTS", "Burst",
           "FrozenForced", "FrozenPlain", "Marked"]


class Db:
    def __init__(self):
        self.conn = psycopg2.connect(
            host=os.environ["TSDB_HOST"], port=os.environ.get("TSDB_PORT", 5432),
            dbname=os.environ["TSDB_DB"], user=os.environ["TSDB_USER"],
            password=os.environ["TSDB_PASSWORD"])

    def max_id(self):
        with self.conn.cursor() as cur:
            cur.execute(f'SELECT COALESCE(MAX("ID"), 0) FROM "{TABLE}"')
            return cur.fetchone()[0]

    def rows_after(self, last_id, limit=50):
        cols = ", ".join(f'"{c}"' for c in COLUMNS)
        with self.conn.cursor() as cur:
            cur.execute(f'SELECT {cols} FROM "{TABLE}" WHERE "ID" > %s ORDER BY "ID" ASC LIMIT %s',
                        (last_id, limit))
            return [dict(zip(COLUMNS, row)) for row in cur.fetchall()]

    def wait_for_new_rows(self, last_id, timeout=6.0, min_rows=1):
        deadline = time.time() + timeout
        while time.time() < deadline:
            rows = self.rows_after(last_id)
            if len(rows) >= min_rows:
                return rows
            time.sleep(0.2)
        return self.rows_after(last_id)


results = []


def check(name, ok, detail=""):
    results.append((name, ok, detail))
    mark = "ok  " if ok else "FAIL"
    print(f"  {mark} {name}" + (f"  ({detail})" if detail else ""))


def only_non_null(row, *expected_signals):
    """True if exactly the named signal columns are non-null among the value columns."""
    value_cols = [c for c in COLUMNS if c not in ("ID", "TimeStampUTC")]
    for col in value_cols:
        is_expected = col in expected_signals
        is_null = row[col] is None
        if is_expected and is_null:
            return False
        if not is_expected and not is_null:
            return False
    return True


def is_partial_write_row(row):
    """Uptime is set by loop() on every periodic tick and touched by no command handler -
    it is present in every full periodic row and absent from every out-of-cycle single-signal
    write() or writeUpdatedData() call. That makes it a reliable way to tell the two apart
    among a mix of new rows, rather than assuming a write's own row is the first new one -
    it usually is not, once MQTT round-trip latency is accounted for."""
    return row["Uptime"] is None


def wait_for_row(db, last_id, predicate, timeout=10.0):
    """Polls for new rows until one matches predicate, or the timeout elapses. Returns
    (matching_row_or_None, all_rows_seen)."""
    deadline = time.time() + timeout
    rows = []
    while time.time() < deadline:
        rows = db.rows_after(last_id, limit=50)
        match = next((r for r in rows if predicate(r)), None)
        if match is not None:
            return match, rows
        time.sleep(0.3)
    return None, rows


missing = _need_env("TSDB_HOST", "TSDB_DB", "TSDB_USER", "TSDB_PASSWORD")
if missing:
    print(f"cannot run: missing {', '.join(missing)} (TimescaleDB connection details)")
    raise SystemExit(1)

base_url = os.environ.get("HA_URL")
if not base_url:
    print("cannot run: set HA_URL (and HA_TOKEN, or HA_USERNAME + HA_PASSWORD) to fire commands")
    raise SystemExit(1)
token = ha_registry._resolve_token(base_url) if ha_registry else os.environ.get("HA_TOKEN")
if token is None:
    print("cannot run: could not resolve a token (install 'websocket-client', or set HA_TOKEN)")
    raise SystemExit(1)

ha = HomeAssistant(base_url, token)
db = Db()

mode_select = ha.resolve("select", "timestampmode")
push_button = ha.resolve("button", "fire_push")
ts_button = ha.resolve("button", "fire_explicit_ts")
burst_button = ha.resolve("button", "fire_burst")
mark_button = ha.resolve("button", "fire_mark")
flush_button = ha.resolve("button", "fire_flush")

if not all([mode_select, push_button, ts_button, burst_button, mark_button, flush_button]):
    print("cannot run: not every entity was found - is lgbk bridging this device right now?")
    raise SystemExit(1)

print(f"driving {DEVICE} against table {TABLE!r}\n")

# ---- setup: known-good starting mode ------------------------------------------------------
ha.select(mode_select, "PC")
time.sleep(1.5)

# ---- push: write() lands immediately, as a partial row -----------------------------------
last_id = db.max_id()
ha.press(push_button)
row, rows = wait_for_row(db, last_id, lambda r: is_partial_write_row(r) and r["Pushed"] is not None)
ok = row is not None and only_non_null(row, "Pushed")
check("push: write() lands its own row, partial (only Pushed)", ok,
      f"{row if row else f'no partial row among {len(rows)} new row(s)'}")

# ---- explicit_ts: per-call override lands exactly, as long as a mode emits a wire field ---
# PC mode is BLAECK_NO_TIMESTAMP: the 8-byte field is entirely absent from the wire in that
# mode (see writeDataFrame), so a per-call override has nowhere to land - Loggbok falls back
# to host arrival time regardless of what write() was passed. The override only makes sense
# once a timestamp field actually exists on the wire.
ha.select(mode_select, "UNIX_calibrated")
time.sleep(1.5)
last_id = db.max_id()
ha.press(ts_button)
row, rows = wait_for_row(db, last_id, lambda r: is_partial_write_row(r) and r["ExplicitTS"] is not None)
ok = row is not None and only_non_null(row, "ExplicitTS") and row["TimeStampUTC"] == EXPLICIT_TS
check("explicit_ts: write()'s explicit timestamp lands exactly at the hardcoded epoch", ok,
      f"{row['TimeStampUTC'] if row else f'no partial row among {len(rows)} new row(s)'}")

# ---- burst: five rapid write() calls, none dropped, in order ------------------------------
last_id = db.max_id()
ha.press(burst_button)
deadline = time.time() + 10.0
rows = []
while time.time() < deadline:
    rows = db.rows_after(last_id, limit=50)
    if sum(1 for r in rows if is_partial_write_row(r) and r["Burst"] is not None) >= 5:
        break
    time.sleep(0.3)
burst_rows = [r for r in rows if is_partial_write_row(r) and r["Burst"] is not None]
bursts = [r["Burst"] for r in burst_rows]
ok = (len(bursts) == 5 and bursts == sorted(bursts)
      and all(b2 - b1 == 1 for b1, b2 in zip(bursts, bursts[1:]))
      and all(only_non_null(r, "Burst") for r in burst_rows))
check("burst: five rapid writes survive, in order, none dropped", ok, f"Burst values: {bursts}")

# ---- mark_flush: update() alone writes nothing; writeUpdatedData() writes a partial row ----
# The wait before checking must comfortably exceed MQTT round-trip latency (HA -> broker ->
# lgbk -> serial -> device), not just the 1s periodic tick - firing flush too soon risks it
# reaching the device before mark's update() actually executed there, which would make
# writeUpdatedData() send nothing at all (nothing is dirty yet), a different failure than
# the one this check means to catch.
last_id = db.max_id()
ha.press(mark_button)
time.sleep(3.0)
rows_after_mark = db.rows_after(last_id)
immediate_partial = [r for r in rows_after_mark if is_partial_write_row(r)]
last_id = db.max_id()
ha.press(flush_button)
flushed, _ = wait_for_row(db, last_id, lambda r: is_partial_write_row(r) and r["Marked"] is not None)
ok = len(immediate_partial) == 0 and flushed is not None and only_non_null(flushed, "Marked")
check("mark_flush: update() writes nothing immediately, writeUpdatedData() writes a partial row",
      ok, f"immediate partial rows after mark: {len(immediate_partial)}, flush row: {flushed}")

# ---- micros: rows drift smoothly against wall-clock time, unlike PC's independent jitter ---
ha.select(mode_select, "MICROS")
time.sleep(1.5)  # let the resync settle before sampling
last_id = db.max_id()
time.sleep(8)
rows = [r for r in db.rows_after(last_id, limit=20) if not is_partial_write_row(r)]
deltas = [(b["TimeStampUTC"] - a["TimeStampUTC"]).total_seconds()
          for a, b in zip(rows, rows[1:])]
frac = [d - round(d) for d in deltas]  # sub-second component of each ~1s step
# A device-clock skew (MICROS mode) pushes every step's sub-second component the same
# direction; independent host-arrival jitter (PC mode) would not consistently agree on a
# sign. One noisy sample is tolerated.
signs = [1 if f > 0 else (-1 if f < 0 else 0) for f in frac if f != 0]
same_sign = max(signs.count(1), signs.count(-1)) if signs else 0
ok = len(rows) >= 6 and same_sign >= len(signs) - 1
check("micros: rows drift smoothly (device-clock skew), not independent jitter", ok,
      f"{len(rows)} rows, fractional deltas: {[round(f, 6) for f in frac]}")

# ---- unix_calibrated: lands near the fixed 2030 epoch and keeps advancing -----------------
ha.select(mode_select, "UNIX_calibrated")
time.sleep(1.5)
last_id = db.max_id()
rows = db.wait_for_new_rows(last_id, min_rows=2)
ok = (len(rows) >= 2
      and all(abs((r["TimeStampUTC"] - UNIX_CALIBRATED_BASE).total_seconds()) < 3600 for r in rows)
      and rows[1]["TimeStampUTC"] > rows[0]["TimeStampUTC"])
check("unix_calibrated: lands near the fixed 2030 epoch and advances", ok,
      f"{[r['TimeStampUTC'] for r in rows]}")

# ---- unix_no_callback: lands exactly at 1970, and logging keeps running afterwards --------
ha.select(mode_select, "UNIX_no_callback")
time.sleep(1.5)
last_id = db.max_id()
rows = db.wait_for_new_rows(last_id, min_rows=2)
lands_at_epoch = bool(rows) and all(r["TimeStampUTC"] == EPOCH for r in rows)
# The regression this guards: a stalled pipeline reports no growth for several seconds.
count_before = db.max_id()
time.sleep(5)
count_after = db.max_id()
keeps_logging = count_after > count_before
check("unix_no_callback: lands exactly at the Unix epoch", lands_at_epoch,
      f"{[r['TimeStampUTC'] for r in rows] if rows else 'no new row'}")
check("unix_no_callback: logging keeps running afterwards (no silent stall)", keeps_logging,
      f"{count_before} -> {count_after}")

ha.select(mode_select, "PC")  # leave the device in a known-good mode

# ---- force_update: FrozenForced and FrozenPlain log identically every row -----------------
last_id = db.max_id()
time.sleep(4)
rows = db.rows_after(last_id, limit=20)
periodic_rows = [r for r in rows if r["FrozenForced"] is not None and r["FrozenPlain"] is not None]
ok = (len(periodic_rows) >= 3
      and all(r["FrozenForced"] == r["FrozenPlain"] == 42.0 for r in periodic_rows))
check("force_update: FrozenForced and FrozenPlain log identically (forceUpdate has no "
      "dedup effect)", ok, f"{len(periodic_rows)} periodic rows checked")

# ---- summary --------------------------------------------------------------------------
failed = [name for name, ok, _ in results if not ok]
print(f"\n{len(results) - len(failed)} of {len(results)} checks passed"
      if failed else f"\nall {len(results)} checks passed")
if failed:
    raise SystemExit(1)

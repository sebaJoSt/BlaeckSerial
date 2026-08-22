"""Check that what EventMetadataTest declares survives all the way to Home Assistant, then
prove a fired occurrence is more than a declaration sitting still.

The counterpart to drive_signal_metadata.py and drive_command_metadata.py, for event
channels. Same three layers, plus one an event channel earns that a signal or a command
does not - it carries no value at rest, so "shown" only means anything once something has
actually been fired:

    declared    the sketch
    published   the discovery payload on the broker      <- checked here, always
    shown       the registry (entity_category, disabled_by, over ha_registry.py)
    fired       Home Assistant's event_type attribute, after this script presses the
                matching button itself - the layer none of the other harnesses have

A channel's event_types list is what a host derives icon-less device classes and closed
vocabularies from; button/doorbell/motion is the one closed set Home Assistant's own event
schema accepts, so a wrong value there fails discovery outright rather than a field being
merely absent.

    HA_URL       Base URL, e.g. https://your-instance
    HA_TOKEN     A long-lived access token, OR
    HA_USERNAME  Username         - a token is exchanged the same way ha_registry.py does it
    HA_PASSWORD  Password

  python drive_event_metadata.py [seconds]          watch the broker for a live bundle,
                                                     then fire every button and check HA
  python drive_event_metadata.py capture.json        read discovery out of a recording only
                                                     (skips the fire/verify layer - no
                                                     live bridge to press a button against)
"""
import json
import os
import sys
import time
import urllib.error
import urllib.request

import paho.mqtt.client as mqtt

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
try:
    import ha_registry
except ImportError:
    ha_registry = None

HOST = "mosquitto-ws-dut-monitoring-rptdiom.eu-de-9.icp.infineon.com"
ARG = sys.argv[1] if len(sys.argv) > 1 else None
CAPTURE = ARG if ARG and ARG.endswith(".json") else None
SECONDS = float(ARG) if ARG and not CAPTURE else 30
DEVICE = "event_metadata_test"

PRESENT = object()  # sentinel: key must be there, whatever it holds
ABSENT = None       # key must be absent

# channel name -> the discovery keys it must produce. Every channel lands on "event" - the
# platform column CommandMetadataTest and SignalMetadataTest need (several platforms each)
# does not apply here.
EXPECT = {
    "Bare":           {"event_types": ["first", "second"], "icon": ABSENT,
                        "device_class": ABSENT, "entity_category": ABSENT,
                        "enabled_by_default": ABSENT},
    "Icon":           {"event_types": ["ping", "pong"], "icon": "mdi:pulse"},
    "Diag":           {"event_types": ["check", "warn"], "entity_category": "diagnostic"},
    "Hidden":         {"event_types": ["trace", "dump"], "enabled_by_default": False},
    "Class_button":   {"event_types": ["press_start", "press_end"], "device_class": "button"},
    "Class_doorbell": {"event_types": ["ring"], "device_class": "doorbell"},
    "Class_motion":   {"event_types": ["detected", "cleared"], "device_class": "motion"},
    # More than two types, so the list is checked whole and in order, not just "has some".
    "Multi_type":     {"event_types": ["alpha", "beta", "gamma", "delta"]},
}

# channel name -> the one registry-only fact this table cannot already see in the discovery
# payload. entity_category and enabled_by_default are both published on the wire (see
# EXPECT above) - unlike CommandMetadataTest's registry table, this one only needs to
# confirm Home Assistant actually filed Hidden as disabled, which the payload's
# enabled_by_default: false key predicts but does not itself prove.
REGISTRY_EXPECT = {
    "Hidden": {"disabled_by": "integration"},
}

# (channel, event type to fire, the button command's own name) - one entry per fire command
# declared in the sketch, so every declared type gets pressed and checked, not just one per
# channel.
FIRE = [
    ("Bare", "first", "F_bare_first"),
    ("Bare", "second", "F_bare_second"),
    ("Icon", "ping", "F_icon_ping"),
    ("Icon", "pong", "F_icon_pong"),
    ("Diag", "check", "F_diag_check"),
    ("Diag", "warn", "F_diag_warn"),
    ("Hidden", "trace", "F_hidden_trace"),
    ("Hidden", "dump", "F_hidden_dump"),
    ("Class_button", "press_start", "F_button_start"),
    ("Class_button", "press_end", "F_button_end"),
    ("Class_doorbell", "ring", "F_doorbell_ring"),
    ("Class_motion", "detected", "F_motion_detected"),
    ("Class_motion", "cleared", "F_motion_cleared"),
    ("Multi_type", "alpha", "F_multi_alpha"),
    ("Multi_type", "beta", "F_multi_beta"),
    ("Multi_type", "gamma", "F_multi_gamma"),
    ("Multi_type", "delta", "F_multi_delta"),
]

# A disabled-by-default channel's button still exists (the button carries no such flag of
# its own), but its event entity does not - Home Assistant never creates it until someone
# enables it, exactly like Hidden_rawadc in SignalMetadataTest and Hidden in
# CommandMetadataTest. That is a pass, not a failure: firing it is exactly what
# EventTest's onFireDisabled already asks whether the wire still carries. Here the
# question is only "did Home Assistant create the entity", and the answer is correctly no.
DISABLED_CHANNELS = {"Hidden"}

bundle = {"value": None}


def on_connect(c, u, f, rc, p=None):
    c.subscribe("#", qos=0)


def on_message(c, u, m):
    if "config" not in m.topic or DEVICE not in m.topic:
        return
    payload = m.payload.decode("utf-8", "replace")
    if payload.startswith("{") and not m.retain and bundle["value"] is None:
        bundle["value"] = json.loads(payload)


if CAPTURE:
    for _t, topic, payload, retain in json.load(open(CAPTURE)):
        if "config" in topic and DEVICE in topic and payload.startswith("{") and not retain:
            bundle["value"] = json.loads(payload)
else:
    try:
        cl = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, transport="websockets")
    except (AttributeError, TypeError):
        cl = mqtt.Client(transport="websockets")
    cl.ws_set_options(path="/")
    cl.tls_set()
    cl.on_connect = on_connect
    cl.on_message = on_message
    cl.connect(HOST, 443, 60)
    cl.loop_start()

    end = time.time() + SECONDS
    while time.time() < end and bundle["value"] is None:
        time.sleep(0.5)
    cl.loop_stop()

if bundle["value"] is None:
    print(f"no live discovery bundle for {DEVICE} in {SECONDS:.0f}s - is a bridge running?")
    raise SystemExit(1)

cmps = bundle["value"].get("cmps", bundle["value"].get("components", {}))
by_name = {v.get("name"): v for v in cmps.values()}
print(f"{len(cmps)} components published\n")

problems = 0
for channel, keys in EXPECT.items():
    comp = by_name.get(channel)
    if comp is None:
        print(f"  MISSING   {channel}: nothing published")
        problems += 1
        continue
    if comp.get("platform") != "event":
        print(f"  PLATFORM  {channel}: {comp.get('platform')}, expected event")
        problems += 1
    for key, want in keys.items():
        got = comp.get(key, "(absent)")
        if want is PRESENT:
            if got == "(absent)":
                print(f"  MISSING   {channel}.{key} should be present, was absent")
                problems += 1
        elif want is ABSENT:
            if got != "(absent)":
                print(f"  EXTRA     {channel}.{key} = {got!r}, should not be published")
                problems += 1
        elif got != want:
            print(f"  WRONG     {channel}.{key} = {got!r}, declared {want!r}")
            problems += 1

published = set(by_name) - set(EXPECT)
if published:
    print(f"\n  published but not in the table: {sorted(published)}")

print(f"\n{len(EXPECT) - problems} of {len(EXPECT)} channels arrived as declared"
      if problems else f"\nall {len(EXPECT)} channels arrived as declared")

# ---- registry layer: whether Hidden is actually filed as disabled ------------------------------
registry_problems = 0
if ha_registry is None:
    print("\nregistry layer skipped: the 'websocket-client' package is not installed")
elif not os.environ.get("HA_URL"):
    print("\nregistry layer skipped: set HA_URL (and HA_TOKEN, or HA_USERNAME + HA_PASSWORD) "
          "to also check disabled_by")
else:
    base_url = os.environ["HA_URL"]
    token = ha_registry._resolve_token(base_url)
    registry = ha_registry.fetch_entity_registry(base_url, token)
    registry_by_uid = ha_registry.by_unique_id(registry)

    print(f"\nregistry: {len(registry_by_uid)} entries fetched from {base_url}")
    for channel, wanted in REGISTRY_EXPECT.items():
        comp = by_name.get(channel)
        if comp is None:
            continue  # already reported as MISSING above
        entry = registry_by_uid.get(comp.get("unique_id"))
        if entry is None:
            print(f"  MISSING   {channel}: no registry entry for unique_id {comp.get('unique_id')!r}")
            registry_problems += 1
            continue
        got = ha_registry.registry_facts(entry)
        for key, want in wanted.items():
            if got.get(key) != want:
                print(f"  WRONG     {channel}.{key} = {got.get(key)!r} (registry), declared {want!r}")
                registry_problems += 1

    print(f"{len(REGISTRY_EXPECT) - registry_problems} of {len(REGISTRY_EXPECT)} "
          f"registry-only facts matched"
          if registry_problems else f"all {len(REGISTRY_EXPECT)} registry-only facts matched")

# ---- fire layer: press every button, and check the event actually arrives correctly ------------
def run_fire_layer(base_url, token):
    """Presses every declared fire command and confirms Home Assistant's event_type
    attribute lands on the matching channel. Returns the number of mismatches."""

    def _request(method, path, body=None):
        req = urllib.request.Request(
            base_url + path,
            data=json.dumps(body).encode() if body is not None else None,
            headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
            method=method,
        )
        with urllib.request.urlopen(req, timeout=15) as resp:
            raw = resp.read()
            return json.loads(raw) if raw else None

    def resolve_entity(domain, object_id):
        entity_id = f"{domain}.{DEVICE}_{object_id}"
        try:
            entry = _request("GET", f"/api/states/{entity_id}")
            return entity_id if entry is not None else None
        except urllib.error.HTTPError:
            return None

    print(f"\nfire layer: pressing {len(FIRE)} buttons against {base_url}")
    fails = 0
    skipped_disabled = 0
    for channel, event_type, command in FIRE:
        button_id = resolve_entity("button", command.lower())
        event_id = resolve_entity("event", channel.lower())
        if button_id is None:
            print(f"  SKIP    {command}: no button entity found for it at all")
            fails += 1
            continue
        if event_id is None:
            if channel in DISABLED_CHANNELS:
                print(f"  DISABLED  {command}: {channel} is disabledByDefault(), so Home "
                      f"Assistant never created its event entity - as expected, not checked")
                skipped_disabled += 1
            else:
                print(f"  MISSING   {command}: no event entity for channel {channel!r}")
                fails += 1
            continue

        _request("POST", "/api/services/button/press", {"entity_id": button_id})

        deadline = time.time() + 8
        got_type = None
        while time.time() < deadline:
            entry = _request("GET", f"/api/states/{event_id}")
            got_type = entry.get("attributes", {}).get("event_type")
            if got_type == event_type:
                break
            time.sleep(0.4)

        if got_type == event_type:
            print(f"  FIRE OK   {command} -> {event_id}.event_type = {got_type!r}")
        else:
            print(f"  FIRE FAIL {command} -> {event_id}.event_type = {got_type!r}, "
                  f"expected {event_type!r}")
            fails += 1

    checked = len(FIRE) - skipped_disabled
    print(f"\n{checked - fails} of {checked} checkable fired events arrived as the declared "
          f"type ({skipped_disabled} on disabled-by-default channels correctly skipped)"
          if fails else f"\nall {checked} checkable fired events arrived as the declared type "
                         f"({skipped_disabled} on disabled-by-default channels correctly skipped)")
    return fails


fire_problems = 0
if CAPTURE:
    print("\nfire layer skipped: reading from a saved capture, no live bridge to press against")
elif not os.environ.get("HA_URL"):
    print("\nfire layer skipped: set HA_URL (and HA_TOKEN, or HA_USERNAME + HA_PASSWORD) "
          "to press every button and check its event")
else:
    _base_url = os.environ["HA_URL"]
    _token = ha_registry._resolve_token(_base_url) if ha_registry else os.environ.get("HA_TOKEN")
    if _token is None:
        print("\nfire layer skipped: could not resolve a token "
              "(install 'websocket-client', or set HA_TOKEN)")
    else:
        fire_problems = run_fire_layer(_base_url, _token)

if problems or registry_problems or fire_problems:
    raise SystemExit(1)
"""Check that what CommandMetadataTest declares survives into the discovery payload, and -
for the one modifier a state attribute can never carry - into the registry.

The counterpart to drive_signal_metadata.py, for commands instead of signals. Same three
layers matter:

    declared    the sketch
    published   the discovery payload on the broker  <- checked here, always
    shown       what Home Assistant made of it        <- checked here too, in two parts:
                  state attributes, over MCP (out of this script's reach - ask a client)
                  registry facts, over ha_registry.py (checked here if HA_URL is set)

entity_category never appears on an entity's state or its attributes for a command any
more than it does for a signal - it is a registry property, readable only over
ha_registry.py's WebSocket call. Set HA_URL (and either HA_TOKEN or HA_USERNAME +
HA_PASSWORD) to also check REGISTRY_EXPECT below; without it, this only reports that it
could not check it, rather than pretending it passed.

A few keys carry values this script cannot predict (a state_topic's exact string, a
value_template's exact Jinja) - those are checked only for presence or absence, marked
PRESENT/ABSENT in the table rather than a literal value.

  python drive_command_metadata.py [seconds]        watch the broker for a live bundle
  python drive_command_metadata.py capture.json     read one out of a recording
"""
import json
import os
import sys
import time

import paho.mqtt.client as mqtt

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
try:
    import ha_registry
except ImportError:
    ha_registry = None

HOST = "mosquitto-ws-dut-monitoring-rptdiom.eu-de-9.icp.infineon.com"
ARG = sys.argv[1] if len(sys.argv) > 1 else None
CAPTURE = ARG if ARG and ARG.endswith(".json") else None
SECONDS = float(ARG) if ARG and not CAPTURE else 30
DEVICE = "command_metadata_test"

PRESENT = object()  # sentinel: key must be there, whatever it holds
ABSENT = None       # key must be absent

# command name -> the discovery keys it must produce, and the platform it must land on.
EXPECT = {
    # ---- N: numbers -----------------------------------------------------------------------
    "N_bare":           ("number", {"min": 0, "max": 100, "step": 1,
                                     "unit_of_measurement": ABSENT, "mode": ABSENT,
                                     "device_class": ABSENT, "state_topic": ABSENT}),
    "N_unit":           ("number", {"min": -20, "max": 40, "step": 0.5, "unit_of_measurement": "V"}),
    "N_step_unset":     ("number", {"min": 0, "max": 10, "step": ABSENT}),
    "N_mode_box":       ("number", {"mode": "box"}),
    "N_mode_slider":    ("number", {"mode": "slider"}),
    "N_deviceclass":    ("number", {"device_class": "temperature"}),
    # Published as declared - Home Assistant's own floor (0.001) is not this bridge's rule to
    # enforce, so the step still arrives. Whether the resulting entity survives is a layer-3
    # question this script cannot answer for a signal either; see REGISTRY_EXPECT below.
    "N_tiny_step":      ("number", {"step": 0.0001}),
    "N_state_own":      ("number", {"state_topic": PRESENT}),
    "N_state_signal":   ("number", {"state_topic": PRESENT}),
    # ---- S: switches ------------------------------------------------------------------------
    "S_bare":           ("switch", {"payload_on": "1", "payload_off": "0", "device_class": ABSENT}),
    "S_deviceclass":    ("switch", {"device_class": "outlet"}),
    "S_state":          ("switch", {"state_topic": PRESENT}),
    # ---- L: selects, two different ways to carry state ---------------------------------------
    "L_bare":           ("select", {"options": ["Sine", "Square", "Triangle", "Sawtooth"],
                                     "state_topic": ABSENT, "value_template": ABSENT}),
    "L_state_text":     ("select", {"options": ["Idle", "Running", "Fault"],
                                     "state_topic": PRESENT, "value_template": ABSENT}),
    # withOwnState() on a select always resolves its stored index to the option name before
    # sending - the wire carries "10V", not "1" - so this is text state exactly like
    # L_state_text, and needs no value_template either. The variable behind it (byte
    # lStateIdx) is numeric only in the sketch's own RAM.
    "L_state_numeric":  ("select", {"options": ["1V", "10V", "100V"],
                                     "state_topic": PRESENT, "value_template": ABSENT}),
    # ---- B: buttons -------------------------------------------------------------------------
    "B_bare":           ("button", {"payload_press": ""}),
    "B_press_payload":  ("button", {"payload_press": "PING"}),
    "B_deviceclass":    ("button", {"device_class": "restart"}),
    # ---- T: text ----------------------------------------------------------------------------
    "T_bare":           ("text", {"max": 32, "mode": ABSENT}),
    "T_password":       ("text", {"max": 16, "mode": "password"}),
    # 300 exceeds Home Assistant's 255-character ceiling for a text entity's max - refused by
    # the firmware itself (withMaxLength() rejects anything over its own 255-byte buffer and
    # keeps the entry's default), so what reaches the host is max=255, not the 300 asked for.
    "T_maxlen_toobig":  ("text", {"max": 255}),
    # No withMaxLength() called at all - the firmware's command-table entries default to
    # meta_max=255 regardless, so an undeclared max looks identical on the wire to one
    # explicitly capped at the library's own ceiling.
    "T_no_maxlen":      ("text", {"max": 255}),
    # ---- how a host files it, isolated on their own controls ---------------------------------
    "Named":            ("switch", {"name": "A Friendly Command"}),
    "Iconed":           ("switch", {"icon": "mdi:tune"}),
    "Config_category":  ("switch", {"entity_category": "config"}),
    "Diag_category":    ("switch", {"entity_category": "diagnostic"}),
    "Hidden":           ("switch", {"enabled_by_default": False}),
}

# command name -> registry-only facts: properties Home Assistant keeps on the registry entry
# rather than in state attributes, so only ha_registry.py can read them. entity_category is
# the command-side match to SignalMetadataTest's Diag_uptime and Hidden_rawadc; N_tiny_step is
# checked here too, since a step below Home Assistant's 0.001 floor drops the number platform's
# schema validation before the entity is ever created, which reads back as "no registry entry
# at all" rather than a wrong field on one that exists - a different shape of failure than any
# signal-side case, confirmed empirically rather than assumed (N_tiny_step genuinely has no
# registry entry - HA drops it silently at layer 3, exactly as its own schema floor predicts).
REGISTRY_EXPECT = {
    "Config_category": {"entity_category": "config"},
    "Diag_category": {"entity_category": "diagnostic"},
    # Only true against a device Home Assistant has never seen this identity for.
    # config/entity_registry/remove (even config/device_registry/remove_config_entry on the
    # whole device) is not enough - re-adding an entity that way did not replay
    # enabled_by_default here, even though the wire payload was already byte-correct.
    # Changing --mqtt-topic (which is part of the device's own identifiers) is what actually
    # forces a from-scratch device and lets Home Assistant apply the decision, which is how
    # this was confirmed correct in the end.
    "Hidden": {"disabled_by": "integration"},
}

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
for command, (platform, keys) in EXPECT.items():
    wanted = keys.get("name", command)
    comp = by_name.get(wanted)
    if comp is None:
        print(f"  MISSING   {command}: nothing published under {wanted!r}")
        problems += 1
        continue
    if comp.get("platform") != platform:
        print(f"  PLATFORM  {command}: {comp.get('platform')}, expected {platform}")
        problems += 1
    for key, want in keys.items():
        if key == "name":
            continue
        got = comp.get(key, "(absent)")
        if want is PRESENT:
            if got == "(absent)":
                print(f"  MISSING   {command}.{key} should be present, was absent")
                problems += 1
        elif want is ABSENT:
            if got != "(absent)":
                print(f"  EXTRA     {command}.{key} = {got!r}, should not be published")
                problems += 1
        elif got != want:
            print(f"  WRONG     {command}.{key} = {got!r}, declared {want!r}")
            problems += 1

published = set(by_name) - {k.get("name", n) for n, (_p, k) in EXPECT.items()}
if published:
    print(f"\n  published but not in the table: {sorted(published)}")

print(f"\n{len(EXPECT) - problems} of {len(EXPECT)} commands arrived as declared"
      if problems else f"\nall {len(EXPECT)} commands arrived as declared")

# ---- registry layer: entity_category, disabled_by, and whether N_tiny_step exists at all ------
registry_problems = 0
if ha_registry is None:
    print("\nregistry layer skipped: the 'websocket-client' package is not installed")
elif not os.environ.get("HA_URL"):
    print("\nregistry layer skipped: set HA_URL (and HA_TOKEN, or HA_USERNAME + HA_PASSWORD) "
          "to also check entity_category and disabled_by")
else:
    base_url = os.environ["HA_URL"]
    token = ha_registry._resolve_token(base_url)
    registry = ha_registry.fetch_entity_registry(base_url, token)
    registry_by_uid = ha_registry.by_unique_id(registry)

    print(f"\nregistry: {len(registry_by_uid)} entries fetched from {base_url}")
    for command, wanted in REGISTRY_EXPECT.items():
        keys = EXPECT[command][1]
        comp_name = keys.get("name", command)
        comp = by_name.get(comp_name)
        if comp is None:
            continue  # already reported as MISSING above
        entry = registry_by_uid.get(comp.get("unique_id"))
        if entry is None:
            print(f"  MISSING   {command}: no registry entry for unique_id {comp.get('unique_id')!r}")
            registry_problems += 1
            continue
        got = ha_registry.registry_facts(entry)
        for key, want in wanted.items():
            if got.get(key) != want:
                print(f"  WRONG     {command}.{key} = {got.get(key)!r} (registry), declared {want!r}")
                registry_problems += 1

    # N_tiny_step is not in REGISTRY_EXPECT's per-field checks above because the question is
    # not what one field says - it is whether Home Assistant kept the entity at all once its
    # own number schema saw a step below the 0.001 floor. Reported separately either way, so a
    # run says what it found rather than assuming the entity survived just because a field
    # happened to match.
    tiny_comp = by_name.get("N_tiny_step")
    if tiny_comp is not None:
        tiny_entry = registry_by_uid.get(tiny_comp.get("unique_id"))
        state = "has a registry entry" if tiny_entry is not None else "has NO registry entry"
        print(f"  N_tiny_step (step=0.0001, below HA's 0.001 floor): {state}")

    print(f"{len(REGISTRY_EXPECT) - registry_problems} of {len(REGISTRY_EXPECT)} "
          f"registry-only facts matched"
          if registry_problems else f"all {len(REGISTRY_EXPECT)} registry-only facts matched")

if problems or registry_problems:
    raise SystemExit(1)

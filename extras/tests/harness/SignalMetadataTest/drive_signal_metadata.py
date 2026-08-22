"""Check that what SignalMetadataTest declares survives into the discovery payload,
and - for the three modifiers a state attribute can never carry - into the registry.

Three layers matter:

    declared    the sketch
    published   the discovery payload on the broker  <- checked here, always
    shown       what Home Assistant made of it        <- checked here too, in two parts:
                  state attributes, over MCP (out of this script's reach - ask a client)
                  registry facts, over ha_registry.py (checked here if HA_URL is set)

A host that cannot use a key drops it in silence, so the gap between declared and
published is the only thing that says so. Run a bridge against the board, point this at
the same broker, and it reports every key that did not arrive.

entity_category, suggested_display_precision and disabled_by never appear on an
entity's state or its attributes - Home Assistant keeps them on the registry entry
instead, which only ha_registry.py's WebSocket call can read. Set HA_URL (and either
HA_TOKEN or HA_USERNAME + HA_PASSWORD) to also check REGISTRY_EXPECT below; without
it, this only reports that it could not check them, rather than pretending they passed.

  python drive_signal_metadata.py [seconds]        watch the broker for a live bundle
  python drive_signal_metadata.py capture.json     read one out of a recording
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
DEVICE = "signal_metadata_test"

# signal name -> the discovery keys it must produce, and the platform it must land on.
# A key set to None must be ABSENT: an unset modifier that writes a key anyway is as much a
# bug as a set one that does not.
EXPECT = {
    "Bare":                 ("sensor", {"unit_of_measurement": None, "device_class": None,
                                        "state_class": None, "suggested_display_precision": None}),
    "Temp_C":               ("sensor", {"unit_of_measurement": "°C", "device_class": "temperature"}),
    "Pressure_hPa":         ("sensor", {"unit_of_measurement": "hPa", "device_class": "pressure"}),
    "Humidity_nounit":      ("sensor", {"device_class": "humidity", "unit_of_measurement": None}),
    "SC_measurement":       ("sensor", {"state_class": "measurement", "device_class": "power"}),
    "SC_total":             ("sensor", {"state_class": "total", "device_class": "energy"}),
    "SC_total_increasing":  ("sensor", {"state_class": "total_increasing"}),
    "SC_measurement_angle": ("sensor", {"state_class": "measurement_angle"}),
    "Prec_0":               ("sensor", {"suggested_display_precision": 0}),
    "Prec_3":               ("sensor", {"suggested_display_precision": 3}),
    "Icon_gauge":           ("sensor", {"icon": "mdi:gauge"}),
    "Named":                ("sensor", {"name": "A Friendly Name"}),
    "Chan1":                ("sensor", {}),
    "Chan2":                ("sensor", {}),
    "Chan3":                ("sensor", {}),
    "Diag_uptime":          ("sensor", {"entity_category": "diagnostic", "device_class": "duration"}),
    "Hidden_rawadc":        ("sensor", {"enabled_by_default": False}),
    "Forced":               ("sensor", {"force_update": True}),
    "Bool_bare":            ("binary_sensor", {"device_class": None}),
    "Bool_motion":          ("binary_sensor", {"device_class": "motion"}),
    "Bool_door":            ("binary_sensor", {"device_class": "door"}),
    "Text_plain":           ("sensor", {"options": None}),
    "Text_options":         ("sensor", {"options": ["Idle", "Running", "Fault"]}),
    "Text_named":           ("sensor", {"name": "Bench Label", "icon": "mdi:tag"}),
    "Enum_and_options":     ("sensor", {"device_class": "enum", "options": ["Idle", "Running", "Fault"]}),
    "Class_wins_over_options": ("sensor", {"device_class": "temperature", "options": None}),
    "Empty_options":        ("sensor", {"device_class": None, "options": None}),
}

# signal name -> registry-only facts (entity_category, suggested_display_precision,
# disabled_by): properties Home Assistant keeps on the entity's registry entry rather than in
# its state attributes, so no MCP tool here can see them - only ha_registry.py, one directory
# up, can. Checked separately below, and only if HA_URL is set in the environment: unlike the
# broker check above, a registry entry survives whether or not a bridge is currently running,
# so this can be run long after a harness session ended.
REGISTRY_EXPECT = {
    "Prec_0": {"suggested_display_precision": 0},
    "Prec_3": {"suggested_display_precision": 3},
    "Diag_uptime": {"entity_category": "diagnostic"},
    "Hidden_rawadc": {"disabled_by": "integration"},
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
    # A recording holds every publish; take the last live bundle, which is the one the
    # run ended with.
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
for signal, (platform, keys) in EXPECT.items():
    # A display name replaces the signal's name, so look the component up by whichever
    # of the two this signal is expected to carry.
    wanted = keys.get("name", signal)
    comp = by_name.get(wanted)
    if comp is None:
        print(f"  MISSING   {signal}: nothing published under {wanted!r}")
        problems += 1
        continue
    if comp.get("platform") != platform:
        print(f"  PLATFORM  {signal}: {comp.get('platform')}, expected {platform}")
        problems += 1
    for key, want in keys.items():
        if key == "name":
            continue
        got = comp.get(key, "(absent)")
        if want is None:
            if got != "(absent)":
                print(f"  EXTRA     {signal}.{key} = {got!r}, should not be published")
                problems += 1
        elif got != want:
            print(f"  WRONG     {signal}.{key} = {got!r}, declared {want!r}")
            problems += 1

published = set(by_name) - {k.get("name", n) for n, (_p, k) in EXPECT.items()}
if published:
    print(f"\n  published but not in the table: {sorted(published)}")

print(f"\n{len(EXPECT) - problems} of {len(EXPECT)} signals arrived as declared"
      if problems else f"\nall {len(EXPECT)} signals arrived as declared")

# ---- registry layer: entity_category, suggested_display_precision, disabled_by ----------------
# None of these three ever reach a state attribute, so nothing above could have checked them -
# only Home Assistant's own registry holds them, and only ha_registry.py's WebSocket call can
# read it. Skipped, rather than silently passed, when the environment holds no HA_URL: a
# harness that reported "0 problems" here without ever checking would be worse than one that
# says plainly it did not look.
registry_problems = 0
if ha_registry is None:
    print("\nregistry layer skipped: the 'websocket-client' package is not installed")
elif not os.environ.get("HA_URL"):
    print("\nregistry layer skipped: set HA_URL (and HA_TOKEN, or HA_USERNAME + HA_PASSWORD) "
          "to also check entity_category, suggested_display_precision and disabled_by")
else:
    base_url = os.environ["HA_URL"]
    token = ha_registry._resolve_token(base_url)
    registry = ha_registry.fetch_entity_registry(base_url, token)
    registry_by_uid = ha_registry.by_unique_id(registry)

    print(f"\nregistry: {len(registry_by_uid)} entries fetched from {base_url}")
    for signal, wanted in REGISTRY_EXPECT.items():
        keys = EXPECT[signal][1]
        comp_name = keys.get("name", signal)
        comp = by_name.get(comp_name)
        if comp is None:
            # Already reported as MISSING above; nothing new to say.
            continue
        entry = registry_by_uid.get(comp.get("unique_id"))
        if entry is None:
            print(f"  MISSING   {signal}: no registry entry for unique_id {comp.get('unique_id')!r}")
            registry_problems += 1
            continue
        got = ha_registry.registry_facts(entry)
        for key, want in wanted.items():
            if got.get(key) != want:
                print(f"  WRONG     {signal}.{key} = {got.get(key)!r} (registry), declared {want!r}")
                registry_problems += 1

    print(f"{len(REGISTRY_EXPECT) - registry_problems} of {len(REGISTRY_EXPECT)} "
          f"registry-only facts matched"
          if registry_problems else f"all {len(REGISTRY_EXPECT)} registry-only facts matched")

if problems or registry_problems:
    raise SystemExit(1)

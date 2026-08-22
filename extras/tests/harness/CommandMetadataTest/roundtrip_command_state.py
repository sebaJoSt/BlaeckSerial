"""Check the one thing drive_command_metadata.py cannot: that a value actually round-trips.

drive_command_metadata.py answers "does what CommandMetadataTest declares survive to Home
Assistant" - metadata only. It never writes anything, so a command whose withOwnState()
binding is declared but never wired up correctly on the device side would still pass every
check there: the discovery payload and the registry entry look identical whether or not the
handler actually calls writeCommandState().

This closes that gap for the three commands that can prove it end to end without a person
watching a dashboard:

    N_state_own       number, own state (a float the handler writes back)
    S_state           switch, own state (a bool the handler writes back)
    L_state_numeric   select, own state (an index the handler resolves to an option name)

For each: read the entity's current state over Home Assistant's REST API, call the service
a person would use from the dashboard (number.set_value, switch.turn_on, select.select_option)
with a value different from the current one, then poll the same REST endpoint until the state
changes or a timeout passes. A pass here means the whole loop is live - MCP/dashboard call ->
MQTT command topic -> the device's handler -> writeCommandState() -> the device's own state
topic -> the MQTT bridge -> Home Assistant's state - not just that the declaration looked
right sitting still.

Two kinds this harness cannot close this way: a button has no state to read back (Home
Assistant writes its own press timestamp locally, regardless of whether the device answers),
and no text command in this sketch declares a state binding at all - T_bare and its siblings
are declaration-only, by design, since CommandTest already covers what a text handler does
with what it receives.

Uses the REST API rather than the WebSocket one ha_registry.py uses, because a service call
and a state poll are exactly what /api/services and /api/states are for - no MCP tool reaches
either of these from a script, which is why this exists as one.

    HA_URL       Base URL, e.g. https://your-instance
    HA_TOKEN     A long-lived access token, OR
    HA_USERNAME  Username         - a token is exchanged the same way ha_registry.py does it
    HA_PASSWORD  Password

    python roundtrip_command_state.py [entity_filter]   default filter: command_metadata_test
"""
import json
import os
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from ha_registry import _resolve_token  # noqa: E402


def _request(base_url, token, method, path, body=None):
    req = urllib.request.Request(
        base_url + path,
        data=json.dumps(body).encode() if body is not None else None,
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        method=method,
    )
    with urllib.request.urlopen(req, timeout=15) as resp:
        raw = resp.read()
        return json.loads(raw) if raw else None


def fetch_states(base_url, token):
    return _request(base_url, token, "GET", "/api/states")


def call_service(base_url, token, domain, service, entity_id, extra):
    body = {"entity_id": entity_id, **extra}
    _request(base_url, token, "POST", f"/api/services/{domain}/{service}", body)


def poll_state(base_url, token, entity_id, want, timeout_s=8):
    """Polls until the entity's state matches `want` (a predicate) or timeout_s passes.
    Returns the last-seen state string either way, so a caller can report what it was."""
    deadline = time.time() + timeout_s
    last = None
    while time.time() < deadline:
        entry = _request(base_url, token, "GET", f"/api/states/{entity_id}")
        last = entry.get("state")
        if want(last):
            return last
        time.sleep(0.5)
    return last


def resolve_entity(states, domain, name_fragment, device_filter):
    """Picks the freshest match: several devices sharing a name (a rediscovered one under a
    different --mqtt-topic, exactly what this repo's own verification run left behind) show
    up as entity_id_2, _3 and so on - the one not stuck on "unavailable" is the live one."""
    candidates = [
        s for s in states
        if s["entity_id"].startswith(domain + ".")
        and device_filter in s["entity_id"]
        and name_fragment in s["entity_id"]
    ]
    if not candidates:
        return None
    live = [c for c in candidates if c["state"] != "unavailable"]
    return (live or candidates)[-1]["entity_id"]


# (domain, name fragment in the entity_id, service, service data, a value to set, a
#  predicate the resulting state should satisfy)
ROUNDTRIP = [
    ("number", "n_state_own", "set_value", lambda v: {"value": v}, 3.7,
     lambda s: s == "3.7"),
    ("switch", "s_state", "turn_on", lambda v: {}, None,
     lambda s: s == "on"),
    ("select", "l_state_numeric", "select_option", lambda v: {"option": v}, "100V",
     lambda s: s == "100V"),
]


def main():
    device_filter = sys.argv[1] if len(sys.argv) > 1 else "command_metadata_test"

    base_url = os.environ.get("HA_URL")
    if not base_url:
        raise SystemExit("set HA_URL in the environment")
    token = _resolve_token(base_url)

    states = fetch_states(base_url, token)

    failures = 0
    for domain, fragment, service, make_data, value, want in ROUNDTRIP:
        entity_id = resolve_entity(states, domain, fragment, device_filter)
        if entity_id is None:
            print(f"  SKIP    {domain}.*{fragment}* - no matching entity found")
            failures += 1
            continue

        before = _request(base_url, token, "GET", f"/api/states/{entity_id}")["state"]
        call_service(base_url, token, domain, service, entity_id, make_data(value))
        after = poll_state(base_url, token, entity_id, want)

        if want(after):
            print(f"  ROUNDTRIP OK   {entity_id}: {before!r} -> {after!r}")
        else:
            print(f"  ROUNDTRIP FAIL {entity_id}: {before!r} -> {after!r} (did not reach the expected value)")
            failures += 1

    total = len(ROUNDTRIP)
    print(f"\n{total - failures} of {total} commands round-tripped through the device")
    print("(not covered: buttons have no state to read back; no text command here "
          "declares a state binding)")

    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()

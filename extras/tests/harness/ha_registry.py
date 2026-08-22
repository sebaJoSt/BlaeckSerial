"""Read Home Assistant's entity registry - the facts no MCP tool here can see.

`list_entities` and `get_state` (over MCP) only ever return an entity's *state*: its
current value and its state attributes. Three signal modifiers never show up there,
because Home Assistant stores them on the entity's *registry* entry instead, which the
Assist API - what the MCP integration is built on - does not expose at all:

    withDisplayPrecision()  -> options.sensor.suggested_display_precision
    .diagnostic()           -> entity_category
    .disabledByDefault()    -> disabled_by (the string "integration", not a bool)

A harness that only checked state attributes would report those three modifiers as
"can't tell" forever, and a regression in any of them would pass silently. This talks
to Home Assistant's WebSocket API directly instead - `config/entity_registry/list` -
which is the same call the frontend's Settings > Entities page makes, run here without
opening a browser.

The registry is not live state: an entry set from a discovery payload stays put whether
or not a host is currently running, which is why this can be checked hours after a
harness run - unlike the "shown" layer in drive_signal_metadata.py, which needs a live
bridge to answer at all.

Credentials are never read from a file this script owns; it only reads what the
environment hands it:

    HA_URL       Base URL, e.g. https://your-instance
    HA_TOKEN     A long-lived access token (Settings > People > <user> > create one),
                 OR
    HA_USERNAME  Username, e
    HA_PASSWORD  Password  - a token is exchanged and never written to disk

    python ha_registry.py signal_metadata_test          # print every matching entry
    python ha_registry.py signal_metadata_test --json    # machine-readable, for a driver
"""
import json
import os
import sys
import urllib.parse

import websocket


def _login_flow_token(base_url, username, password):
    """Exchanges a username/password for a short-lived bearer token, the same way the
    frontend's login page does. Good for one script run; nothing is persisted."""
    import urllib.request

    def post(path, body):
        req = urllib.request.Request(
            base_url + path,
            data=json.dumps(body).encode(),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=15) as resp:
            return json.loads(resp.read())

    flow = post("/auth/login_flow", {
        "client_id": base_url, "handler": ["homeassistant", None], "redirect_uri": base_url + "/"
    })
    result = post(f"/auth/login_flow/{flow['flow_id']}", {
        "client_id": base_url, "username": username, "password": password
    })
    if result.get("type") != "create_entry":
        raise RuntimeError(f"login failed: {result}")

    req = urllib.request.Request(
        base_url + "/auth/token",
        data=urllib.parse.urlencode({
            "grant_type": "authorization_code",
            "code": result["result"],
            "client_id": base_url,
        }).encode(),
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=15) as resp:
        return json.loads(resp.read())["access_token"]


def fetch_entity_registry(base_url, token):
    """Returns every entity registry entry Home Assistant holds, keyed by entity_id."""
    ws_url = base_url.replace("https://", "wss://").replace("http://", "ws://") + "/api/websocket"
    ws = websocket.create_connection(ws_url, timeout=15)
    try:
        msg = json.loads(ws.recv())
        if msg["type"] != "auth_required":
            raise RuntimeError(f"unexpected greeting: {msg}")
        ws.send(json.dumps({"type": "auth", "access_token": token}))
        msg = json.loads(ws.recv())
        if msg["type"] != "auth_ok":
            raise RuntimeError(f"auth failed: {msg}")

        ws.send(json.dumps({"id": 1, "type": "config/entity_registry/list"}))
        resp = json.loads(ws.recv())
        if not resp.get("success"):
            raise RuntimeError(f"registry list failed: {resp}")
        return {e["entity_id"]: e for e in resp["result"]}
    finally:
        ws.close()


def by_unique_id(registry):
    """Same registry, indexed by unique_id instead of entity_id.

    entity_id is only assigned once, on an entity's first discovery, and stays whatever it
    was even if a later firmware renames the signal or its display name - exactly the
    "relabel without moving anything" the library promises. unique_id is what the discovery
    payload's own `unique_id`/`object_id` field carries, so it is the key a driver that only
    has the payload to go on can actually rely on.
    """
    return {e["unique_id"]: e for e in registry.values() if e.get("unique_id")}


def registry_facts(entry):
    """The three facts state attributes cannot carry, pulled out of one registry entry."""
    return {
        "entity_category": entry.get("entity_category"),
        "suggested_display_precision": (entry.get("options") or {}).get("sensor", {}).get("suggested_display_precision"),
        "disabled_by": entry.get("disabled_by"),
    }


def _resolve_token(base_url):
    token = os.environ.get("HA_TOKEN")
    if token:
        return token
    username = os.environ.get("HA_USERNAME")
    password = os.environ.get("HA_PASSWORD")
    if username and password:
        return _login_flow_token(base_url, username, password)
    raise RuntimeError("set HA_TOKEN, or HA_USERNAME + HA_PASSWORD, in the environment")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        raise SystemExit(1)
    filter_str = sys.argv[1]
    as_json = "--json" in sys.argv[2:]

    base_url = os.environ.get("HA_URL")
    if not base_url:
        raise SystemExit("set HA_URL in the environment")

    token = _resolve_token(base_url)
    registry = fetch_entity_registry(base_url, token)
    matches = {eid: e for eid, e in registry.items() if filter_str in eid}

    if as_json:
        print(json.dumps({eid: registry_facts(e) for eid, e in matches.items()}, indent=2))
        return

    for eid in sorted(matches):
        print(eid, registry_facts(matches[eid]))


if __name__ == "__main__":
    main()

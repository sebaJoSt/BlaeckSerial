# Waveform Generator — Node-RED dashboard

A Node-RED **Dashboard 2.0** dashboard for the BlaeckSerial
**WaveformGenerator** example, driven over MQTT by Loggbok.

It provides:

- A live line chart for `Output`.
- Text readouts for the reported frequency, amplitude, offset, waveform and enabled state.
- Sliders/dropdowns/buttons that publish `SET_*` commands back to the device.

| File | Purpose |
| --- | --- |
| `WaveformGenerator.flows.json` | Node-RED flow to import. |

## How it fits together

```text
Arduino (WaveformGenerator.ino)
        │  serial (BlaeckSerial)
        ▼
   Loggbok  ──►  MQTT broker  ◄──  Node-RED Dashboard 2.0
   --mqtt        loggbok/wave/*
```

Loggbok publishes each signal to `<state topic>/<Signal>` and forwards commands
received on `<command topic>/_cmd/<COMMAND>` to the device. Both topic roots are
configurable at runtime from the dashboard's **Topic routing** group (default
`loggbok/wave`), so the Loggbok `--table` no longer has to be `wave`.

## Topic routing

The dashboard's **Topic routing** group sets where telemetry is read from and
where commands are published — the same style as the Home Assistant example:

| Control | Purpose |
| --- | --- |
| **State topic** | Root Loggbok publishes telemetry under (`<root>/<Signal>`). Defaults to `loggbok/wave`. |
| **Command topic mode** | Dropdown: *Same as state topic* (default) or *Custom*. |
| **Command topic** | Root used for commands when the mode is *Custom*. |
| **Effective command topic** | Read-only readout of the root commands are published to. |

Internally a single `mqtt in` node subscribes to `#` and routes messages whose
topic matches `<state topic>/<Signal>` to the widgets (mirroring the HA
automations that trigger on `#`). Commands are published to
`<effective command topic>/_cmd/<COMMAND>`. Changing either field takes effect
immediately — no redeploy.

## Prerequisites

- Node-RED running on `http://127.0.0.1:1880`.
- The Dashboard 2.0 package
  [`@flowfuse/node-red-dashboard`](https://dashboard.flowfuse.com/) installed.
- An MQTT broker reachable at `127.0.0.1:1884`, or edit the imported MQTT broker
  config node to match your broker.

This flow uses Dashboard 2.0 nodes such as `ui-base`, `ui-page`, `ui-chart` and
`ui-slider`. It is not for the legacy `node-red-dashboard` 1.x package.

## Setup

### 1. Upload the sketch

Open `../WaveformGenerator.ino`, select your board, and upload. The sketch runs
at **115200 baud** and registers the waveform signals and commands.

### 2. Run Loggbok with the MQTT bridge

The MQTT `--table` sets the topic root; it must match the **State topic** field
on the dashboard (default `loggbok/wave`). Either run Loggbok with
`--table wave`, or set the dashboard's State topic to whatever your table is.
With the local Mosquitto container from the test setup, use `127.0.0.1:1884`:

```bash
lgbk log --port COM24 --table wave --signals * --mqtt \
         --mqtt-endpoint mqtt://127.0.0.1:1884
```

Replace `COM24` with your board's serial port.
Dashboard commands are routed by MQTT topic, so no command allow-list is needed.
The controls publish `SET_FREQ`, `SET_AMP`, `SET_OFFSET`, `SET_WAVE` and
`SET_ENABLE` to the `_cmd` topics shown below.
Use `loggbok/_all/_cmd/<COMMAND>` instead of `loggbok/wave/_cmd/<COMMAND>` to
broadcast a command to all Loggbok MQTT bridges.

### 3. Import the flow

In Node-RED: **Menu → Import**, paste the contents of
[`WaveformGenerator.flows.json`](./WaveformGenerator.flows.json), or import the
file directly.

After importing, check the MQTT broker config node:

```text
host: 127.0.0.1
port: 1884
```

Then click **Deploy**.

### 4. Open the dashboard

```text
http://127.0.0.1:1880/dashboard
```

The `/dashboard` path is configured by the Dashboard 2.0 `ui-base` node in the
flow.

## Topic map

Topics below assume the default routing (`loggbok/wave`); replace the root with
your configured **State topic** / **Command topic**.

| Dashboard value | Direction | Topic |
| --- | --- | --- |
| `Output` | read | `loggbok/wave/Output` |
| `Frequency` | read / write | `loggbok/wave/Frequency` · `.../_cmd/SET_FREQ` |
| `Amplitude` | read / write | `loggbok/wave/Amplitude` · `.../_cmd/SET_AMP` |
| `Offset` | read / write | `loggbok/wave/Offset` · `.../_cmd/SET_OFFSET` |
| `Waveform` | read / write | `loggbok/wave/Waveform` · `.../_cmd/SET_WAVE` |
| `Enabled` | read / write | `loggbok/wave/Enabled` · `.../_cmd/SET_ENABLE` |

Waveform values map as `0 = Sine`, `1 = Square`, `2 = Triangle`,
`3 = Sawtooth`.

## Notes

- **State reflection:** the dashboard sliders use `passthru: false`. Incoming
  reported values move the widgets silently; only user interaction publishes a
  command. This keeps display state and command output separate and avoids a
  feedback loop.
- **Retained MQTT values:** on load, the widgets snap to the board's last
  reported state if Loggbok published retained values.

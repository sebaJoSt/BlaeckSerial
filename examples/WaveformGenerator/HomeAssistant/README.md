# Waveform Generator — Home Assistant dashboard

A fancy, modern Home Assistant dashboard for the BlaeckSerial **WaveformGenerator**
example, driven entirely over MQTT by [Loggbok](https://github.com/sebaJoSt/Loggbok).

It uses only **built-in** Home Assistant cards (no HACS, no custom components):

- A **gauge** with colored segments for the live `Output`.
- **Tile** cards with interactive *features* for the controls — a toggle for the
  output, a dropdown for the waveform shape, and sliders for frequency, amplitude
  and offset.

All six entities are grouped under a single auto-generated **Waveform Generator**
device.

| File | Purpose |
| --- | --- |
| `mqtt-entities.yaml` | MQTT entity definitions — paste into `configuration.yaml`. |
| `dashboard.yaml` | The Sections-view dashboard (Lovelace YAML). |

> **Note:** these files target Home Assistant **2024.4+** because they use the
> Sections dashboard view and tile-card *features*. They have been tested with
> the local WSL/Home Assistant setup used by this example. Adjust slider
> ranges/icons to taste.

---

## Prerequisites

- `../WaveformGenerator.ino` uploaded to the board.
- **Loggbok** running the serial-to-MQTT bridge.
- An MQTT broker reachable at `127.0.0.1:1884`, or equivalent broker settings
  adjusted in Loggbok and Home Assistant.
- Home Assistant **2024.4+**.
- The Home Assistant **MQTT integration** configured for the same broker.

---

## How it fits together

```
Arduino (WaveformGenerator.ino)
        │  serial (BlaeckSerial)
        ▼
   Loggbok  ──►  MQTT broker  ◄──  Home Assistant
   --mqtt        loggbok/wave/*        (MQTT integration)
```

Loggbok publishes each signal to `loggbok/wave/<Signal>` (retained) and forwards
commands received on `loggbok/wave/_cmd/<COMMAND>` to the device. Home Assistant
subscribes/publishes to those same topics.

### Topic map

| HA entity | Direction | Topic |
| --- | --- | --- |
| `sensor.waveform_generator_output` | read | `loggbok/wave/Output` |
| `number.waveform_generator_frequency` | read / write | `loggbok/wave/Frequency` · `…/_cmd/SET_FREQ` |
| `number.waveform_generator_amplitude` | read / write | `loggbok/wave/Amplitude` · `…/_cmd/SET_AMP` |
| `number.waveform_generator_offset` | read / write | `loggbok/wave/Offset` · `…/_cmd/SET_OFFSET` |
| `select.waveform_generator_waveform` | read / write | `loggbok/wave/Waveform` · `…/_cmd/SET_WAVE` |
| `switch.waveform_generator_enabled` | read / write | `loggbok/wave/Enabled` · `…/_cmd/SET_ENABLE` |

The waveform shape maps `0→Sine`, `1→Square`, `2→Triangle`, `3→Sawtooth`
(handled by the `value_template` / `command_template` in the select entity).

---

## Setup

### 1. Upload the sketch

Open `../WaveformGenerator.ino`, select your board, and upload. The sketch runs
at **115200 baud** and registers the waveform signals and commands.

### 2. Run Loggbok with the MQTT bridge

The MQTT `--table` **must** be `wave` so the topics match this dashboard.
With the local Mosquitto container from the test setup, use `127.0.0.1:1884`:

```bash
lgbk log --port COM24 --table wave --signals * --mqtt \
         --mqtt-endpoint mqtt://127.0.0.1:1884
```

(Use the Loggbok desktop app's MQTT settings for the same effect. If you use a
different broker, replace the host/port accordingly.)
Dashboard commands are routed by MQTT topic, so no command allow-list is needed.
The controls publish `SET_FREQ`, `SET_AMP`, `SET_OFFSET`, `SET_WAVE` and
`SET_ENABLE` to the `_cmd` topics listed above.
Use `loggbok/_all/_cmd/<COMMAND>` instead of `loggbok/wave/_cmd/<COMMAND>` to
broadcast a command to all Loggbok MQTT bridges.

> **Tip:** disable DTR in Loggbok's serial settings so the Arduino isn't reset
> each time logging (re)connects — that way the waveform keeps its current
> shape/parameters across restarts.

### 3. Add the MQTT integration in Home Assistant

If you haven't already: **Settings → Devices & services → Add integration →
MQTT**, and point it at the **same broker** Loggbok publishes to.

### 4. Add the entities

Copy the contents of [`mqtt-entities.yaml`](./mqtt-entities.yaml) into your
`configuration.yaml` (merge into an existing `mqtt:` block if you have one), then
**Developer Tools → YAML → Restart** (or restart Home Assistant). You should see a
new **Waveform Generator** device with six entities.

### 5. Add the dashboard

**Settings → Dashboards → + Add dashboard → New dashboard from scratch.** Open it,
click the pencil (Edit), then the three-dots menu → **Edit in YAML**, and paste
[`dashboard.yaml`](./dashboard.yaml).

---

## Going further (optional, needs HACS)

For an oscilloscope-style live trace, install **ApexCharts Card** from HACS and
point a `series:` at `sensor.waveform_generator_output` with a short `graph_span`
(e.g. `10s`) and `update_interval: 1s`. The smoothness of any chart is limited
by Loggbok's log interval — use a fast interval for a more detailed trace.

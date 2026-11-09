# esphome-aquarea-cascade-rs485

ESPHome firmware for an ESP32 + RS485 transceiver that **writes operating commands to Panasonic Aquarea HP1** (CZ-NSMB Modbus RTU) while a CMH cascade controller remains Modbus master on the same bus.

Status / temperatures / energy are **not** read by this device. Home Assistant should keep using **Modbus TCP on the CMH** for monitoring.

## Hardware

| Signal | ESP32 |
|--------|-------|
| UART2 TX | GPIO17 |
| UART2 RX | GPIO16 |
| Bus | RS485 A/B @ **9600 8N1** (auto-DE transceiver) |

Slave target: **Modbus id 1 (HP1)** only. HP2+ stay under CMH control.

## Behaviour

1. RX sniff of CMH FC03 traffic (always).
2. On button press: wait for bus idle ≥ 80 ms, send FC06 writes, return to sniff.
3. Confirm target state from **CMH poll responses** (HR0 / HR4 / HR30) — no active master reads while CMH is up.
4. Rewrite every ~2 s until match is stable (2 polls) or **timeout 60 s**.
5. `text_sensor` **HP1 Command Result**: `idle` / `running:…` / `ok:…:Nms` / `timeout`.

### Commands (HP1)

| Button | HR4 mode | HR30 tank | HR0 |
|--------|----------|-----------|-----|
| Off | — | — | 0 |
| Heat | 1 | **0 (ECS off)** | 1 |
| Cool | 5 | **0 (ECS off)** | 1 |
| ECS | 3 (Tank) | 1 | 1 |
| Heat+ECS | 2 | 1 | 1 |
| Cool+ECS | 4 | 1 | 1 |

Write order (non-off): **tank → mode → ON** (Heat/Cool alone clear ECS before applying space mode).

## Home Assistant

1. Flash this project (`esphome run aquarea-hp1-writer.yaml`).
2. Add the ESPHome device (API).
3. Use the six buttons in automations / scripts.
4. Optionally wait until `HP1 Command Result` starts with `ok:` (up to 60 s).
5. Keep CMH Modbus TCP entities for live mode / temps / tank feedback.

## Config / flash

```bash
cp secrets.yaml.example secrets.yaml
# edit secrets; generate api key: openssl rand -base64 32
python -m esphome compile aquarea-hp1-writer.yaml
```

**First flash** (migrating from the PlatformIO lab firmware): use **USB serial** — ArduinoOTA on the lab image does not speak ESPHome OTA (port 3232). After the first ESPHome flash, subsequent updates can use ESPHome OTA.

```bash
python -m esphome upload aquarea-hp1-writer.yaml --device COMx
# or: python -m esphome run aquarea-hp1-writer.yaml
```

External component path: `components/aquarea_cascade` (YAML key `aquarea_cascade`).

## License

MIT

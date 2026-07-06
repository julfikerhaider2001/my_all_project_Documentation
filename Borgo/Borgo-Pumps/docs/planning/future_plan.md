# Borgo Pumps Future Plan

## Optional Cloud Services

A future cloud service may provide encrypted account backup, multi-user farms, cross-phone recovery, shared groups, remote audit history, and administration. It is not part of the direct-SMS MVP and must not become necessary for local pump control.

## Additional Device Transports

Future hardware may support MQTT over cellular data, Wi-Fi, LoRa gateways, or BLE commissioning. Transport-specific code should sit behind a common command lifecycle while preserving per-device authentication and acknowledgements.

## Telemetry

PostgreSQL remains suitable for account and relational product data. Add a time-series system only when real sensor volume, retention, and query patterns justify it. Candidate options include TimescaleDB and Apache IoTDB.

## Product Expansion

- Moisture, pressure, flow, voltage, and current sensors.
- Dry-run and overload protection.
- Owner, manager, worker, and installer roles.
- Weather-aware or sensor-driven automation.
- Firmware update tracking and fleet health.
- Encrypted recovery and device transfer workflows.

Any cloud backend, new transport, or automation engine requires a separate OpenSpec change before implementation.

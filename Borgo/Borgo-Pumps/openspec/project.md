# Project: Agricultural GSM IoT App

## Purpose

Build an agricultural IoT platform that lets farmers remotely register, monitor, and control GSM-based field devices such as pump controllers, motor starters, valves, lights, fans, and future sensor-enabled equipment.

The initial hardware target is a SIM800L-based device with ON/OFF control. The first software milestone should focus on trustworthy device registration, secure SMS command handling, group control foundations, and clear device state tracking.

## Current Scope

- GSM-only device support.
- Device registration by device ID, claim code, and SIM number.
- Direct Android SMS command delivery from the owner's phone SIM.
- Signed SMS command verification on the device.
- Signed device acknowledgements.
- Individual device control.
- Multiple-device or group control.
- Device states and command history.

## Out of Scope for Initial MVP

- Wi-Fi, LoRa, BLE, or MQTT-first devices.
- Full encrypted SMS payloads.
- Sensor automation.
- AI crop advisory.
- Camera monitoring.
- Payment or billing logic.

These may be added later through OpenSpec changes.

## Key Architecture

Current MVP command path:

```text
Borgo Pumps Android app -> phone SIM SMS -> GSM Device
GSM Device -> ACK SMS -> Borgo Pumps Android app
```

The Android app is the primary control surface. It handles Firebase phone login, QR pairing, command signing, SMS delivery through the user's phone SIM, incoming ACK verification, and local command/activity state. The MVP has no application backend.

## Domain Concepts

- `User`: farmer, farm owner, worker, or installer using the app.
- `Farm`: top-level grouping for agricultural locations.
- `Zone`: area within a farm, such as a field, greenhouse, or pump zone.
- `Device`: GSM controller installed in the field.
- `Device ID`: public hardware identifier.
- `Claim Code`: one-time registration code proving physical access to a device.
- `Command Key`: private key derived during physical pairing and used for command authentication.
- `SIM Number`: phone number for SMS delivery to the device.
- `Command`: requested action such as ON, OFF, STATUS, or VERIFY.
- `Counter`: monotonic per-device command number used for replay protection.
- `Acknowledgement`: signed response from the device.

## Device States

- `Unclaimed`: provisioned device exists but is not owned by a user.
- `Pending Verification`: user submitted registration details, but SMS verification has not completed.
- `Active`: device is registered and verified.
- `Offline`: device has not responded recently or failed command verification.
- `Suspended`: device is administratively blocked.

## Technical Constraints

- Devices may operate in low-connectivity rural areas.
- SMS delivery can be delayed, duplicated, or fail silently.
- Device clocks may be unreliable, so command counters are preferred over timestamp-only replay protection.
- SIM cards may be replaced by farmers or installers.
- Device memory and firmware capacity may be limited.

## Security Principles

- A SIM number is a routing address, not an identity.
- Plain SMS commands must never control hardware state.
- Every state-changing command must be signed.
- Device acknowledgements must be authenticated before the Android app trusts them.
- Claim codes must not remain reusable after successful registration.
- Secret keys must be unique per device and hidden from users.

## Spec Workflow

Canonical requirements live under `openspec/specs/`.

Use `openspec/changes/` for substantial new work after the baseline is established. A change should include:

- `proposal.md`
- `tasks.md`
- spec deltas under `specs/<capability>/spec.md`

Validate with:

```powershell
& 'C:\Users\Shoummo\AppData\Roaming\npm\openspec.cmd' validate --all --strict --no-interactive
```

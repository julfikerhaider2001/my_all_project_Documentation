# AGENTS.md

## Project Overview

This repository contains the Borgo Pumps product: native Android software, Arduino Nano/SIM800L firmware, PCB work, documentation, and formal specifications.

The product should prioritize reliability in rural environments, simple farmer-facing workflows, and secure SMS command handling.

## Core Product Direction

- Support GSM-only devices first.
- Treat device registration, ownership, and command authentication as core product flows.
- The current MVP direction is a single native Android app that sends SMS directly from the owner's phone SIM:

```text
Borgo Pumps Android app -> phone SIM SMS -> GSM Device
GSM Device -> ACK SMS -> Borgo Pumps Android app
```

- Do not rely on SIM number as device identity.
- Do not allow plain unauthenticated SMS commands.
- Prefer signed SMS commands for MVP security; add full encryption later if needed.

## Documentation Conventions

- Follow the repository and collaboration rules in `COLLABORATION.md`.
- Keep product decisions and supporting plans under `docs/`.
- Keep formal behavior requirements in `openspec/specs/`.
- Use OpenSpec changes under `openspec/changes/` before making major product or architecture changes.
- Update `openspec/project.md` when project direction, architecture, or constraints change.

## OpenSpec Workflow

Before implementing a substantial new feature:

1. Create a change folder under `openspec/changes/<change-id>/`.
2. Add `proposal.md`, `tasks.md`, and spec deltas under `specs/<capability>/spec.md`.
3. Validate with:

```powershell
& 'C:\Users\Shoummo\AppData\Roaming\npm\openspec.cmd' validate --all --strict --no-interactive
```

4. Implement only after the intended behavior is clear.
5. Update the canonical spec under `openspec/specs/` when the change is accepted or archived.

For this early repo, direct edits to baseline specs are acceptable while the product foundation is still being established.

## Engineering Guidelines

- Keep designs simple enough for low-connectivity agricultural environments.
- Prefer explicit states such as `Unclaimed`, `Pending Verification`, `Active`, `Offline`, and `Suspended`.
- Store secrets securely; never expose device secret keys in app UI, QR codes, logs, or public docs.
- Every command that changes physical device state must be authenticated and logged.
- Design group commands as multiple auditable per-device commands, not as an invisible bulk action.
- Build for SIM replacement, failed SMS delivery, retries, and delayed acknowledgements.
- Keep the Android MVP independent of an application backend. Any future cloud service requires an OpenSpec change.
- Keep hardware-specific firmware differences under `firmware/boards/<revision>/`.
- Update `firmware/COMPATIBILITY.md` in the same change as any compatibility-affecting firmware or hardware edit.

## Security Rules

- Devices must reject malformed, unsigned, replayed, or incorrectly signed SMS commands.
- Signed commands must include at minimum device ID, command, monotonic counter, and signature.
- Device replies must be authenticated before the Android app updates command status.
- Sender-number filtering may be used as an extra layer, but it must not be the primary security control.
- Claim codes should be single-use or disabled after successful registration.

## Verification

When OpenSpec files change, run:

```powershell
& 'C:\Users\Shoummo\AppData\Roaming\npm\openspec.cmd' validate --all --strict --no-interactive
```

Android:

```powershell
cd software/android
$env:JAVA_HOME='C:\Program Files\Android\Android Studio\jbr'
.\gradlew.bat testDebugUnitTest assembleDebug
```

Arduino Nano:

```powershell
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328 firmware/nano
```

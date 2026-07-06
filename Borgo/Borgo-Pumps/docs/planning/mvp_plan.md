# Borgo Pumps MVP Plan

## Product Scope

- Firebase phone-number login.
- Add multiple GSM devices by secure QR pairing.
- Enter and edit each device SIM number in the Android app.
- Send authenticated ON/OFF commands through the owner's phone SIM.
- Verify signed device acknowledgements.
- Device list, clear status, activity history, groups, and simple scheduling.

## Architecture

```text
Android app -> Android SmsManager -> SIM800L device
SIM800L device -> SMS acknowledgement -> Android BroadcastReceiver
```

The MVP has no application backend. Device records and command keys are local to Android. Firebase is used only for authentication at this stage.

## Registration

1. Program a unique device ID and random claim credential into one controller.
2. Generate the matching installer QR.
3. Scan the QR and manually enter the device SIM number.
4. Open the device's physical pairing window.
5. Exchange authenticated `BF1 P1` and `BF1 P2` SMS messages.
6. Derive and store a new command key; discard the claim credential from active app state.

## Delivery Order

1. Stabilize secure single-device pairing and control.
2. Persist delayed acknowledgements when the UI is not running.
3. Complete editable groups with auditable per-device commands.
4. Implement reliable Android background schedules.
5. Add activity persistence and export.
6. Prepare Play internal testing and SMS-permission review material.

## MVP Constraints

- A real Android phone and active SIM are required.
- SMS can be delayed, duplicated, or unavailable.
- Every state-changing command must be authenticated and replay-protected.
- SIM800L requires a dedicated power supply capable of handling burst current.
- The app must remain useful without mobile data after login.

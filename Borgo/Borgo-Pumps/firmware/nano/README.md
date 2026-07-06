# Borgo Pumps Nano + SIM800L Firmware

This reference sketch securely pairs one Arduino Nano pump controller with the Borgo Pumps Android app and accepts authenticated `ON`, `OFF`, and `STATUS` SMS commands.

## Required library

Install **Crypto by Rhys Weatherley** from the Arduino Library Manager. The sketch uses its `SHA256` HMAC implementation.

## Wiring

| Nano | Connection |
| --- | --- |
| D10 | SIM800L TX |
| D11 | SIM800L RX through a suitable voltage divider/level shifter |
| D7 | Relay control input |
| D4 | Pair button to GND (`INPUT_PULLUP`) |
| D13 | Pair/status LED |
| GND | Common ground with SIM800L and relay supply |

Do not power the SIM800L from the Nano 5 V pin. Use a stable SIM800L-compatible supply capable of roughly 2 A burst current and connect grounds together.

## Provision one device

1. Generate a unique ID such as `BF000123`.
2. Generate a random 16-byte claim code, represented as 32 uppercase hexadecimal characters.
3. Copy `provisioning.example.h` to `provisioning.h` and set `DEVICE_ID` and `CLAIM_CODE_HEX` there.
4. Enter the same ID and claim code in `software/tools/qr-generator`.
5. Flash the Nano and attach the QR label to the inside of the enclosure or another installer-controlled location.

Never reuse a claim code across devices. The QR contains a bootstrap claim credential, not the permanent command key.

`provisioning.h` is intentionally ignored by git. Hardware pin differences belong in `firmware/boards/<revision>/config.h`, not in per-revision branches or duplicated sketches.

## Pairing

1. Scan the device QR in Borgo Pumps.
2. Enter and save the SIM number installed in the SIM800L.
3. Hold the physical pair button for at least 2 seconds and release it. D13 flashes for the three-minute pairing window.
4. Tap **Pair device** in the Android app.
5. Wait for the authenticated reply. D13 becomes steadily lit when pairing completes.

The claim code is never sent in the SMS. The app and controller derive a new 256-bit command key from two nonces and then use that key for all commands.

## Factory reset

Power off the controller. Hold the pair button, power it on, and keep holding for at least 8 seconds. This clears the derived key and replay counter. Re-pairing still requires the original QR and a new physical pairing window.

## SMS protocol

```text
BF1 P1  <deviceId> <appNonce> <proof>
BF1 P2  <deviceId> <appNonce> <deviceNonce> OK <proof>
BF1 CMD <deviceId> <ON|OFF|STATUS> <counter> <proof>
BF1 ACK <deviceId> <command> <counter> OK <proof>
```

Proofs are the first 8 bytes of HMAC-SHA256, encoded as 16 uppercase hexadecimal characters. Commands with invalid tags, wrong sender numbers, unsupported actions, or old counters are ignored.

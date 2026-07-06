# Secure GSM Device Registration

## Identity

- Device ID: public, unique controller identifier.
- SIM number: editable SMS routing address, never device identity.
- Claim code: random 128-bit bootstrap credential printed in the installer QR.
- Command key: 256-bit key derived during pairing and never placed in the QR or SMS.

## QR Payload

```json
{
  "type": "borgo-farm-device",
  "version": 1,
  "name": "Main Pump",
  "model": "nano-pump-v1",
  "deviceId": "BF000123",
  "claimCode": "32_HEXADECIMAL_CHARACTERS"
}
```

## Pairing

1. The installer scans the QR and enters the SIM number.
2. A physical button opens a three-minute pairing window.
3. The app sends `BF1 P1 <deviceId> <appNonce> <proof>`.
4. Firmware verifies the proof without receiving the claim code itself.
5. Both sides derive a command key from the claim code and two nonces.
6. Firmware sends `BF1 P2 <deviceId> <appNonce> <deviceNonce> OK <proof>`.
7. Android verifies the sender, nonce, and proof before enabling controls.

## Normal Commands

```text
BF1 CMD <deviceId> <ON|OFF|STATUS> <counter> <proof>
BF1 ACK <deviceId> <command> <counter> <status> <proof>
```

Firmware rejects malformed commands, invalid proofs, unsupported actions, and counters that are not greater than the last accepted value. Sender-number filtering is an additional check, not the primary security boundary.

## Reset and Transfer

Re-pairing requires the physical factory-reset gesture and the original installer QR. Reset erases the derived command key and replay counter. A future cloud ownership registry may add transfer approval but must preserve physical recovery.

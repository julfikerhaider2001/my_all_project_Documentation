# Borgo Pumps QR Generator

Open `index.html` in a browser.

The QR payload format is:

```json
{
  "type": "borgo-farm-device",
  "version": 1,
  "name": "Main Pump",
  "model": "nano-pump-v1",
  "deviceId": "BF000123",
  "claimCode": "8B7C2E95A1834F60D4B9C1276A5E03F1"
}
```

The claim code must be a random 16-byte value represented by 32 hexadecimal characters. Program the same device ID and claim code into exactly one controller. It is a bootstrap credential used only to derive a separate command key during physically authorized pairing.

The QR does not contain the device SIM number or permanent command key. Keep installer QR labels controlled and never reuse a claim code between devices.

The device SIM number is intentionally not included. Enter and edit the SIM number inside the Android app after scanning.

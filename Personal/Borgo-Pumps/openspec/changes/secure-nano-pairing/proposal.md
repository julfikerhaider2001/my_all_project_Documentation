# Change: Secure Arduino Nano Device Pairing

## Why

The current field-test flow places the permanent SMS command secret in the device QR code. Anyone who copies that QR can generate valid pump commands. Borgo Pumps needs a registration flow that proves physical access, never transmits the printed claim credential by SMS, and derives a separate command key after pairing.

## What Changes

- Provision each Nano controller with a public device ID and a random 128-bit claim credential.
- Put the device ID, model, display name, and one-time claim credential in the QR; never put the permanent command key or SIM number in it.
- Require a physical button press to open a short pairing window.
- Authenticate a two-message SMS nonce exchange with HMAC-SHA256.
- Derive a unique 256-bit command key after pairing and disable remote re-pairing.
- Authenticate commands and acknowledgements with a monotonic counter and truncated HMAC tag.
- Store Android secrets behind an Android Keystore AES-GCM key.

## Impact

- Adds an Arduino Nano/SIM800L reference firmware sketch.
- Changes the QR payload from `secretKey` to `claimCode`.
- Changes the Android device model, persistence, pairing UI, SMS parser, and command protocol.
- Existing field-test registrations containing `secretKey` must be cleared and scanned again.

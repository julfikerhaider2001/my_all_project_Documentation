# Firmware Compatibility

This file is the source of truth for supported Borgo Pumps hardware revisions.

| Firmware | Hardware revision | Controller | Modem | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| `0.1.x` | Rev A | Arduino Nano ATmega328P | SIM800L | Supported | Direct SMS, secure QR pairing, one relay output |

## Revision Policy

- Hardware-specific pins and electrical behavior live in `boards/<revision>/config.h`.
- Shared protocol and control logic remain in `firmware.ino`.
- Any fabrication-affecting hardware change requires a new revision letter and a compatibility-table update.
- Retired deployed revisions receive critical fixes only from `support/<revision>-legacy` when such a branch is approved.

# Hardware Documentation

## Rev A Reference

- Controller: Arduino Nano ATmega328P
- Modem: SIM800L
- Output: single relay-controlled pump
- Registration input: physical pair button
- Status output: onboard LED

The current wiring table and power warnings are maintained in [the Nano firmware README](../../firmware/nano/README.md). Firmware support is tracked in [COMPATIBILITY.md](../../firmware/COMPATIBILITY.md).

## Preliminary BOM

| Item | Quantity | Notes |
| --- | ---: | --- |
| Arduino Nano ATmega328P | 1 | Rev A controller |
| SIM800L module | 1 | Requires a stable supply with approximately 2 A burst capability |
| Relay module | 1 | Confirm active level against `boards/rev-a/config.h` |
| Momentary push button | 1 | Pairing and factory-reset input |
| Logic-level divider or shifter | 1 | Nano TX to SIM800L RX |
| External power supply | 1 | Do not power SIM800L from the Nano 5 V pin |

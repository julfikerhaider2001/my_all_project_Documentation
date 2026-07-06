# GSM Device Registration Specification

## Purpose

Define secure, backend-independent registration and authenticated SMS control for Borgo Pumps GSM devices.

## Requirements

### Requirement: Device provisioning

Each manufactured device SHALL have a unique public device ID and random 128-bit claim credential. A permanent command key SHALL NOT be printed, encoded in the QR, or transmitted during pairing.

#### Scenario: Installer QR is generated

- **GIVEN** a controller has a device ID and claim credential
- **WHEN** its installer QR is generated
- **THEN** the QR SHALL include protocol version, device ID, model, display name, and claim credential
- **AND** it SHALL NOT include a SIM number or permanent command key

### Requirement: Physical device claiming

The app SHALL bind a controller through a physically gated authenticated SMS nonce exchange.

#### Scenario: User starts pairing

- **GIVEN** the user scanned the matching QR and entered the device SIM number
- **AND** the physical pairing button opened a temporary pairing window
- **WHEN** the app sends `BF1 P1 <deviceId> <appNonce> <proof>`
- **THEN** the proof SHALL authenticate `P1|<deviceId>|<appNonce>` using the claim credential
- **AND** the claim credential itself SHALL NOT appear in the SMS

#### Scenario: Device completes pairing

- **GIVEN** the device is unclaimed, inside its pairing window, and received a valid `P1`
- **WHEN** it generates a device nonce
- **THEN** both sides SHALL derive a unique 256-bit command key
- **AND** the device SHALL reply with an authenticated `BF1 P2` message
- **AND** the app SHALL become active only after verifying sender, nonces, and proof

#### Scenario: Remote pairing lacks physical presence

- **GIVEN** the physical pairing window is closed
- **WHEN** any new `P1` message arrives
- **THEN** firmware SHALL reject it without replacing the command key

### Requirement: Authenticated commands

Firmware SHALL execute state-changing commands only when their HMAC proof and monotonic counter are valid.

#### Scenario: Valid ON command arrives

- **GIVEN** a paired device last accepted counter `41`
- **WHEN** it receives `BF1 CMD <deviceId> ON 42 <proof>` with the correct proof
- **THEN** it SHALL persist counter `42`
- **AND** it SHALL switch the configured output ON

#### Scenario: Forged or replayed command arrives

- **GIVEN** an attacker knows the device SIM number
- **WHEN** it sends plain text, an invalid proof, or a counter not greater than the saved counter
- **THEN** firmware SHALL reject the command
- **AND** it SHALL NOT change physical output state

### Requirement: Authenticated acknowledgements

The app SHALL update machine state only after validating an acknowledgement with the derived command key.

#### Scenario: Valid acknowledgement arrives

- **GIVEN** the app sent command counter `42`
- **WHEN** it receives `BF1 ACK <deviceId> ON 42 OK <proof>` from the configured SIM number
- **AND** the proof is valid
- **THEN** it SHALL mark the pump as running

#### Scenario: Invalid acknowledgement arrives

- **GIVEN** an acknowledgement has an invalid sender, device ID, counter, or proof
- **WHEN** the app validates it
- **THEN** it SHALL ignore the message for state changes
- **AND** it SHALL record a rejected acknowledgement event

### Requirement: Secret storage

The app and firmware SHALL keep derived command keys out of UI, logs, QR payloads, and unencrypted preferences.

#### Scenario: Android persists a device secret

- **GIVEN** a claim credential or command key must survive restart
- **WHEN** Android saves the device
- **THEN** it SHALL encrypt the value using a key held by Android Keystore
- **AND** ordinary preferences SHALL contain ciphertext only

#### Scenario: Physical factory reset occurs

- **GIVEN** a controller must be transferred or recovered
- **WHEN** the documented physical reset gesture completes
- **THEN** firmware SHALL erase its derived command key and replay counter
- **AND** subsequent pairing SHALL still require the original claim QR and physical pairing window

### Requirement: SIM replacement

The app SHALL allow the owner to edit a paired device's destination SIM number without treating that number as device identity.

#### Scenario: SIM number changes

- **GIVEN** a securely paired device
- **WHEN** its owner saves a new valid E.164 SIM number
- **THEN** future authenticated commands SHALL route to the new number
- **AND** the device ID and command key SHALL remain unchanged

### Requirement: Group command auditability

Group control SHALL execute as one independently authenticated command per target device.

#### Scenario: User controls a group

- **GIVEN** a group contains multiple active devices
- **WHEN** the user starts or stops the group
- **THEN** the app SHALL create one command and counter update per eligible device
- **AND** it SHALL show success, failure, pending, or timeout per device

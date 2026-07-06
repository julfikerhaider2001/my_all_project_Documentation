## MODIFIED Requirements

### Requirement: Device provisioning

Each manufactured device SHALL have a unique public device ID and a random 128-bit claim credential before registration. A permanent command key SHALL NOT be printed, encoded in the QR, or transmitted during pairing.

#### Scenario: User-visible QR excludes permanent secrets

- **GIVEN** a device is prepared for installation
- **WHEN** its QR code is generated
- **THEN** the QR SHALL include protocol version, device ID, model, display name, and claim credential
- **AND** it SHALL NOT include the SIM number or a permanent command key

#### Scenario: Firmware is provisioned

- **GIVEN** a new Nano controller is programmed
- **WHEN** manufacturing provisioning is complete
- **THEN** firmware SHALL contain the matching device ID and claim credential
- **AND** the device SHALL begin in the `Unclaimed` state

### Requirement: Device claiming

The app SHALL bind a device through a physically gated, authenticated SMS nonce exchange.

#### Scenario: User starts pairing

- **GIVEN** the user scanned a valid QR and entered the device SIM number
- **AND** the device physical pairing button opened a temporary pairing window
- **WHEN** the app sends `BF1 P1 <deviceId> <appNonce> <proof>`
- **THEN** the proof SHALL be a truncated HMAC-SHA256 over `P1|<deviceId>|<appNonce>` using the claim credential
- **AND** the claim credential itself SHALL NOT appear in the SMS

#### Scenario: Device completes pairing

- **GIVEN** the device is unclaimed and inside its physical pairing window
- **AND** the `P1` proof is valid
- **WHEN** the device generates a nonce
- **THEN** both sides SHALL derive the command key from `KEY|<deviceId>|<appNonce>|<deviceNonce>` using HMAC-SHA256 keyed by the claim credential
- **AND** the device SHALL reply with `BF1 P2 <deviceId> <appNonce> <deviceNonce> OK <proof>`
- **AND** the `P2` proof SHALL be computed using the derived command key
- **AND** the device SHALL reject further remote pairing while paired

#### Scenario: Pairing is attempted without physical presence

- **GIVEN** the physical pairing window is closed
- **WHEN** the device receives a syntactically valid `P1` message
- **THEN** it SHALL reject the request
- **AND** it SHALL NOT derive or replace the command key

### Requirement: Authenticated SMS commands

The device SHALL execute commands only when a `BF1 CMD` message has a valid HMAC tag under the derived command key.

#### Scenario: Device receives a valid command

- **GIVEN** a paired device last accepted counter `41`
- **WHEN** it receives `BF1 CMD <deviceId> ON 42 <proof>`
- **AND** the proof matches `CMD|<deviceId>|ON|42`
- **THEN** it SHALL switch the output ON
- **AND** it SHALL persist counter `42`

#### Scenario: Device receives an unauthenticated command

- **GIVEN** an attacker knows the device SIM number
- **WHEN** the attacker sends plain `ON`, plain `OFF`, an invalid proof, or a replayed counter
- **THEN** the device SHALL reject the message
- **AND** it SHALL not change the physical output

### Requirement: Signed acknowledgements

The app SHALL update device state only after verifying a command acknowledgement using the derived command key.

#### Scenario: App receives a valid acknowledgement

- **GIVEN** the app sent counter `42`
- **WHEN** it receives `BF1 ACK <deviceId> ON 42 OK <proof>` from the configured SIM number
- **AND** the proof matches `ACK|<deviceId>|ON|42|OK`
- **THEN** the app SHALL mark the pump as running

#### Scenario: App receives a forged acknowledgement

- **GIVEN** an acknowledgement has an invalid proof, wrong sender, wrong device ID, or unexpected counter
- **WHEN** the app validates it
- **THEN** the app SHALL ignore it for state changes
- **AND** it SHALL record a rejected acknowledgement event

## ADDED Requirements

### Requirement: Secret storage

The app and firmware SHALL protect derived command keys from ordinary UI, logs, QR payloads, and unencrypted application preferences.

#### Scenario: Android persists registration

- **GIVEN** a claim credential or command key must survive an app restart
- **WHEN** the app saves the device
- **THEN** it SHALL encrypt the secret with an AES-GCM key held by Android Keystore
- **AND** ordinary preferences SHALL contain ciphertext only

#### Scenario: Device is factory reset

- **GIVEN** a paired device must be transferred to a new owner
- **WHEN** an installer performs the documented physical factory-reset gesture
- **THEN** firmware SHALL erase the derived command key and replay counter
- **AND** pairing SHALL still require the physical pairing window and original claim QR

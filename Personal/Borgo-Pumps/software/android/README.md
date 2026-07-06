# Borgo Pumps Native Android

Native Android MVP for Borgo Pumps.

This app is the real Android direction after choosing the one-app model:

```text
Borgo Pumps Android app
  -> Firebase Phone Auth
  -> Direct SMS command sending from the user's phone SIM
  -> SIM800L receives signed command
  -> SIM800L replies with signed ACK
  -> App receives ACK SMS
```

## Stack

- Kotlin
- Jetpack Compose
- Firebase Auth
- Firebase Auth
- Android `SmsManager`
- SMS `BroadcastReceiver`

## Package

```text
xyz.borgo.farm
```

The app module already contains:

```text
app/google-services.json
```

## Build

Open this folder in Android Studio from the repository root:

```text
software/android
```

Then:

```text
Sync Gradle
Run on a real Android device
```

SMS sending requires a physical Android phone with:

- SIM card
- SMS balance/package
- Network signal
- SMS permissions granted

The emulator cannot test real SMS sending to SIM800L.

## Firebase Setup

In Firebase Console:

1. Enable Phone Authentication.
2. Add test phone numbers while developing.
3. Confirm Android app package is `xyz.borgo.farm`.
4. Add debug/release SHA-1 and SHA-256 fingerprints.

## SMS Permissions

The app requests:

```text
SEND_SMS
RECEIVE_SMS
READ_SMS
POST_NOTIFICATIONS
RECEIVE_BOOT_COMPLETED
```

Google Play may review or restrict SMS permissions. For first field tests, use a direct APK/internal testing build.

## Command Format

Pairing request and authenticated device reply:

```text
BF1 P1 <deviceId> <appNonce> <proof>
BF1 P2 <deviceId> <appNonce> <deviceNonce> OK <proof>
```

The QR supplies a random 128-bit claim credential. The credential itself is never sent by SMS. Both sides derive a separate 256-bit command key after the physical device button opens the pairing window.

Outgoing command and incoming acknowledgement:

```text
BF1 CMD <deviceId> <ON|OFF|STATUS> <counter> <proof>
BF1 ACK <deviceId> <command> <counter> OK <proof>
```

Proofs are the first 8 bytes of HMAC-SHA256, encoded as 16 uppercase hexadecimal characters. The app rejects acknowledgements with an invalid sender, counter, or proof.

## Registration

1. Program a unique device ID and random claim code into the matching Nano firmware.
2. Generate and scan its QR with `software/tools/qr-generator`.
3. Enter and save the SIM number installed in the controller.
4. Hold the controller pair button for 2 seconds and release it.
5. Tap **Pair device** within 3 minutes.
6. Wait for the verified device reply before ON/OFF controls become available.

Claim credentials and derived keys are encrypted before being stored in app preferences using an AES-GCM key held by Android Keystore.

Old development registrations containing a QR `secretKey` are intentionally ignored. Clear app data or scan a new-format QR after updating.

## Build From PowerShell

```powershell
$env:JAVA_HOME='C:\Program Files\Android\Android Studio\jbr'
$env:Path="$env:JAVA_HOME\bin;$env:Path"
.\gradlew.bat testDebugUnitTest assembleDebug
```

## Current MVP Status

Implemented:

- Firebase Phone Auth UI flow.
- Dashboard UI.
- Pump control UI.
- Device registration UI.
- Groups and schedule UI.
- QR claim-code registration and physical pairing states.
- Android Keystore-backed secret encryption.
- Signed SMS command creation.
- Direct SMS sending through `SmsManager`.
- SMS sent/delivered receiver.
- Incoming `BF1 P2` and `BF1 ACK` parsers.
- Pairing and acknowledgement proof verification.
- Foreground UI updates for sent, delivered, and ACK events.

Next:

- Persist delayed incoming SMS events when the app UI is not running.
- Add foreground service for scheduled commands.

# Borgo Pumps

Borgo Pumps is a native Android control system for GSM agricultural pumps. The Rev A device uses an Arduino Nano and SIM800L to switch one pump through authenticated SMS commands.

## Repository Structure

```text
firmware/                    Shared Nano/SIM800L firmware and board configs
software/android/            Native Kotlin and Jetpack Compose application
software/tools/qr-generator/ Offline manufacturing and provisioning utility
pcb/                         PCB design sources by hardware revision
docs/                        Plans, hardware notes, and design references
openspec/                    OpenSpec requirements and accepted changes
```

`openspec/` remains at repository root because the OpenSpec CLI discovers it there. Root files such as `AGENTS.md`, `PRODUCT.md`, and `COLLABORATION.md` define repository-wide behavior.

## MVP Architecture

```text
Borgo Pumps Android app -> owner's phone SIM -> authenticated SMS -> SIM800L device
SIM800L device -> authenticated acknowledgement SMS -> Borgo Pumps Android app
```

- Firebase Phone Authentication handles user login.
- The Android phone sends SMS directly; there is no deployed application backend.
- QR scanning and a physical pairing button establish a per-device command key.
- Device SIM numbers are routing addresses, not identities.
- ON/OFF commands and acknowledgements use HMAC authentication and replay counters.

## Build

Android:

```powershell
cd software/android
$env:JAVA_HOME='C:\Program Files\Android\Android Studio\jbr'
.\gradlew.bat testDebugUnitTest assembleDebug
```

Firmware:

```powershell
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328 firmware/nano
```

OpenSpec:

```powershell
& 'C:\Users\Shoummo\AppData\Roaming\npm\openspec.cmd' validate --all --strict --no-interactive
```

See [COLLABORATION.md](COLLABORATION.md) for branch, PR, versioning, and release rules. Firmware and hardware support are tracked in [COMPATIBILITY.md](firmware/COMPATIBILITY.md).

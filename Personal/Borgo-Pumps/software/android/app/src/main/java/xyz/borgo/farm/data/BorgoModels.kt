package xyz.borgo.farm.data

enum class PumpStatus {
    RUNNING,
    STOPPED,
    OFFLINE,
    PENDING
}

enum class PairingStatus {
    UNPAIRED,
    PAIRING,
    ACTIVE,
    FAILED
}

data class PumpDevice(
    val id: String,
    val name: String,
    val simNumber: String,
    val zone: String,
    val claimCode: String,
    val commandKey: String,
    val pairingNonce: String,
    val pairingStatus: PairingStatus,
    val counter: Int,
    val status: PumpStatus,
    val pendingCommand: String? = null,
    val pressureBar: String = "4.2",
    val flowRate: String = "12"
)

data class ActivityLog(
    val title: String,
    val detail: String
)

data class DeviceGroup(
    val name: String,
    val detail: String,
    val state: String
)

data class FarmSchedule(
    val target: String,
    val detail: String,
    val enabled: Boolean
)

data class SignedCommand(
    val deviceId: String,
    val command: String,
    val counter: Int,
    val signature: String
) {
    val smsBody: String = "BF1 CMD $deviceId $command $counter $signature"
}

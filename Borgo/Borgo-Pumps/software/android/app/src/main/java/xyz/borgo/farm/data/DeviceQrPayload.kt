package xyz.borgo.farm.data

import org.json.JSONObject

data class DeviceQrPayload(
    val name: String,
    val model: String,
    val deviceId: String,
    val claimCode: String
) {
    fun toPumpDevice(): PumpDevice {
        return PumpDevice(
            id = deviceId,
            name = name.ifBlank { "Main Pump" },
            simNumber = "",
            zone = model,
            claimCode = claimCode,
            commandKey = "",
            pairingNonce = "",
            pairingStatus = PairingStatus.UNPAIRED,
            counter = 0,
            status = PumpStatus.OFFLINE
        )
    }
}

object DeviceQrParser {
    fun parse(rawValue: String): DeviceQrPayload? {
        return runCatching {
            val json = JSONObject(rawValue)
            val type = json.optString("type")
            val version = json.optInt("version", 0)
            if (type != "borgo-farm-device" || version != 1) return null
            val deviceId = json.optString("deviceId").trim()
            val claimCode = json.optString("claimCode").trim().uppercase()
            if (!deviceId.matches(Regex("[A-Za-z0-9_-]{4,16}"))) return null
            if (!claimCode.matches(Regex("[A-F0-9]{32}"))) return null
            DeviceQrPayload(
                name = json.optString("name", "Main Pump").trim(),
                model = json.optString("model", "nano-pump-v1").trim(),
                deviceId = deviceId,
                claimCode = claimCode
            )
        }.getOrNull()
    }
}

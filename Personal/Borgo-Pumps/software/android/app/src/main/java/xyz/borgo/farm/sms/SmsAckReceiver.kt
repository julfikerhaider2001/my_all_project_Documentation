package xyz.borgo.farm.sms

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.provider.Telephony
import android.util.Log

class SmsAckReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Telephony.Sms.Intents.SMS_RECEIVED_ACTION) return
        val messages = Telephony.Sms.Intents.getMessagesFromIntent(intent)
        val fullBody = messages.joinToString(separator = "") { it.messageBody.orEmpty() }
        val sender = messages.firstOrNull()?.originatingAddress.orEmpty()
        PairingReplyParser.parse(fullBody)?.let {
            Log.i("BorgoSmsAck", "pairing reply received for ${it.deviceId}")
            SmsEventBus.publish(SmsEvent.Pairing(it, sender))
            return
        }
        DeviceAckParser.parse(fullBody)?.let {
            Log.i("BorgoSmsAck", "command acknowledgement received for ${it.deviceId}")
            SmsEventBus.publish(SmsEvent.Ack(it, sender))
        }
    }
}

data class DeviceAck(
    val deviceId: String,
    val command: String,
    val counter: Int,
    val status: String,
    val signature: String
)

object DeviceAckParser {
    fun parse(body: String): DeviceAck? {
        val parts = body.trim().split(Regex("\\s+"))
        if (parts.size != 7 || parts[0] != "BF1" || parts[1] != "ACK") return null
        val counter = parts[4].toIntOrNull() ?: return null
        return DeviceAck(
            deviceId = parts[2],
            command = parts[3],
            counter = counter,
            status = parts[5],
            signature = parts[6]
        )
    }
}

data class PairingReply(
    val deviceId: String,
    val appNonce: String,
    val deviceNonce: String,
    val status: String,
    val signature: String
)

object PairingReplyParser {
    fun parse(body: String): PairingReply? {
        val parts = body.trim().split(Regex("\\s+"))
        if (parts.size != 7 || parts[0] != "BF1" || parts[1] != "P2") return null
        if (!parts[3].matches(Regex("[A-Fa-f0-9]{16}"))) return null
        if (!parts[4].matches(Regex("[A-Fa-f0-9]{16}"))) return null
        if (!parts[6].matches(Regex("[A-Fa-f0-9]{16}"))) return null
        return PairingReply(parts[2], parts[3].uppercase(), parts[4].uppercase(), parts[5], parts[6].uppercase())
    }
}

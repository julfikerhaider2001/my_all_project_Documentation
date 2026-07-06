package xyz.borgo.farm.sms

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build
import android.telephony.SmsManager
import xyz.borgo.farm.data.PumpDevice

object SmsCommandDispatcher {
    const val ACTION_SMS_SENT = "xyz.borgo.farm.SMS_SENT"
    const val ACTION_SMS_DELIVERED = "xyz.borgo.farm.SMS_DELIVERED"
    const val EXTRA_DEVICE_ID = "device_id"
    const val EXTRA_COMMAND = "command"

    fun sendPumpCommand(context: Context, pump: PumpDevice, command: String): String {
        require(pump.commandKey.isNotBlank()) { "Device is not securely paired" }
        val signed = CommandSigner.sign(
            deviceId = pump.id,
            command = command,
            counter = pump.counter + 1,
            commandKey = pump.commandKey
        )
        sendText(context, pump, command, signed.smsBody)
        return signed.smsBody
    }

    fun sendPairingRequest(context: Context, pump: PumpDevice): PairingRequest {
        val request = PairingCrypto.createRequest(pump)
        sendText(context, pump, "PAIR", request.smsBody)
        return request
    }

    private fun sendText(context: Context, pump: PumpDevice, command: String, body: String) {
        val sentIntent = Intent(context, SmsStatusReceiver::class.java).apply {
            action = ACTION_SMS_SENT
            putExtra(EXTRA_DEVICE_ID, pump.id)
            putExtra(EXTRA_COMMAND, command)
        }
        val deliveredIntent = Intent(context, SmsStatusReceiver::class.java).apply {
            action = ACTION_SMS_DELIVERED
            putExtra(EXTRA_DEVICE_ID, pump.id)
            putExtra(EXTRA_COMMAND, command)
        }
        val flags = PendingIntent.FLAG_UPDATE_CURRENT or
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) PendingIntent.FLAG_IMMUTABLE else 0
        val sentPendingIntent = PendingIntent.getBroadcast(
            context,
            "${pump.id}-$command-sent".hashCode(),
            sentIntent,
            flags
        )
        val deliveredPendingIntent = PendingIntent.getBroadcast(
            context,
            "${pump.id}-$command-delivered".hashCode(),
            deliveredIntent,
            flags
        )

        SmsManager.getDefault().sendTextMessage(
            pump.simNumber,
            null,
            body,
            sentPendingIntent,
            deliveredPendingIntent
        )
    }
}

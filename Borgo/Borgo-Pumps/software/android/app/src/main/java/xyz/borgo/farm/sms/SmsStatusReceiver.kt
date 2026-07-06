package xyz.borgo.farm.sms

import android.app.Activity
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.telephony.SmsManager
import android.util.Log

class SmsStatusReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val deviceId = intent.getStringExtra(SmsCommandDispatcher.EXTRA_DEVICE_ID).orEmpty()
        val command = intent.getStringExtra(SmsCommandDispatcher.EXTRA_COMMAND).orEmpty()
        val status = when (intent.action) {
            SmsCommandDispatcher.ACTION_SMS_SENT -> sentStatus(resultCode)
            SmsCommandDispatcher.ACTION_SMS_DELIVERED -> "delivered_to_network"
            else -> "unknown"
        }
        Log.i("BorgoSmsStatus", "device=$deviceId command=$command status=$status")
        when (intent.action) {
            SmsCommandDispatcher.ACTION_SMS_SENT -> SmsEventBus.publish(
                SmsEvent.Sent(
                    deviceId = deviceId,
                    command = command,
                    status = status
                )
            )
            SmsCommandDispatcher.ACTION_SMS_DELIVERED -> SmsEventBus.publish(
                SmsEvent.Delivered(
                    deviceId = deviceId,
                    command = command
                )
            )
        }
    }

    private fun sentStatus(resultCode: Int): String {
        return when (resultCode) {
            Activity.RESULT_OK -> "sms_sent"
            SmsManager.RESULT_ERROR_GENERIC_FAILURE -> "generic_failure"
            SmsManager.RESULT_ERROR_NO_SERVICE -> "no_service"
            SmsManager.RESULT_ERROR_NULL_PDU -> "null_pdu"
            SmsManager.RESULT_ERROR_RADIO_OFF -> "radio_off"
            else -> "send_failed_$resultCode"
        }
    }
}

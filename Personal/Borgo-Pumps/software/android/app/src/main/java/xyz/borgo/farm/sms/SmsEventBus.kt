package xyz.borgo.farm.sms

import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow

sealed class SmsEvent {
    data class Sent(
        val deviceId: String,
        val command: String,
        val status: String
    ) : SmsEvent()

    data class Delivered(
        val deviceId: String,
        val command: String
    ) : SmsEvent()

    data class Ack(
        val ack: DeviceAck,
        val sender: String
    ) : SmsEvent()

    data class Pairing(
        val reply: PairingReply,
        val sender: String
    ) : SmsEvent()
}

object SmsEventBus {
    private val eventsInternal = MutableSharedFlow<SmsEvent>(extraBufferCapacity = 32)
    val events = eventsInternal.asSharedFlow()

    fun publish(event: SmsEvent) {
        eventsInternal.tryEmit(event)
    }
}

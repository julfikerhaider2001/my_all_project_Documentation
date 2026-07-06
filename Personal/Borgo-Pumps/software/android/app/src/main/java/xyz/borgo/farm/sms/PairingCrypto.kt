package xyz.borgo.farm.sms

import java.security.MessageDigest
import java.security.SecureRandom
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec
import xyz.borgo.farm.data.PumpDevice

data class PairingRequest(
    val appNonce: String,
    val smsBody: String
)

object PairingCrypto {
    private val secureRandom = SecureRandom()

    fun createRequest(device: PumpDevice): PairingRequest {
        require(device.claimCode.matches(Regex("[A-F0-9]{32}"))) { "Missing or invalid claim credential" }
        val appNonce = device.pairingNonce.ifBlank { randomHex(8) }
        val tag = proof(device.claimCode, "P1|${device.id}|$appNonce")
        return PairingRequest(appNonce, "BF1 P1 ${device.id} $appNonce $tag")
    }

    fun deriveCommandKey(claimCode: String, deviceId: String, appNonce: String, deviceNonce: String): String {
        return hmac(hexToBytes(claimCode), "KEY|$deviceId|$appNonce|$deviceNonce", 32).toHex()
    }

    fun verifyPairingReply(reply: PairingReply, claimCode: String): String? {
        if (reply.status != "OK") return null
        val commandKey = deriveCommandKey(claimCode, reply.deviceId, reply.appNonce, reply.deviceNonce)
        val expected = proof(commandKey, "P2|${reply.deviceId}|${reply.appNonce}|${reply.deviceNonce}|${reply.status}")
        return commandKey.takeIf { constantTimeEquals(expected, reply.signature) }
    }

    fun verifyCommandAck(ack: DeviceAck, commandKey: String): Boolean {
        if (commandKey.isBlank()) return false
        val expected = proof(commandKey, "ACK|${ack.deviceId}|${ack.command}|${ack.counter}|${ack.status}")
        return constantTimeEquals(expected, ack.signature)
    }

    fun proof(keyHex: String, message: String): String = hmac(hexToBytes(keyHex), message, 8).toHex()

    private fun randomHex(byteCount: Int): String = ByteArray(byteCount).also(secureRandom::nextBytes).toHex()

    private fun hmac(key: ByteArray, message: String, outputBytes: Int): ByteArray {
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(key, "HmacSHA256"))
        return mac.doFinal(message.toByteArray(Charsets.US_ASCII)).copyOf(outputBytes)
    }

    private fun hexToBytes(value: String): ByteArray {
        require(value.length % 2 == 0 && value.matches(Regex("[A-Fa-f0-9]+"))) { "Invalid hexadecimal key" }
        return ByteArray(value.length / 2) { index ->
            value.substring(index * 2, index * 2 + 2).toInt(16).toByte()
        }
    }

    private fun ByteArray.toHex(): String = joinToString("") { "%02X".format(it.toInt() and 0xFF) }

    private fun constantTimeEquals(left: String, right: String): Boolean {
        return MessageDigest.isEqual(
            left.uppercase().toByteArray(Charsets.US_ASCII),
            right.uppercase().toByteArray(Charsets.US_ASCII)
        )
    }
}

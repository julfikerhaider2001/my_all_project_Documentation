package xyz.borgo.farm.sms

import xyz.borgo.farm.data.SignedCommand

object CommandSigner {
    fun sign(deviceId: String, command: String, counter: Int, commandKey: String): SignedCommand {
        val signature = PairingCrypto.proof(
            keyHex = commandKey,
            message = "CMD|$deviceId|$command|$counter"
        )
        return SignedCommand(deviceId, command, counter, signature)
    }
}

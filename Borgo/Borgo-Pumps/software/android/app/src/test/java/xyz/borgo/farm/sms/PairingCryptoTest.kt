package xyz.borgo.farm.sms

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class PairingCryptoTest {
    private val claimCode = "8B7C2E95A1834F60D4B9C1276A5E03F1"
    private val appNonce = "0011223344556677"
    private val deviceNonce = "8899AABBCCDDEEFF"
    private val commandKey = "827698A08011E88794E904F12BA0A9CB098AB26495A66F2044C12552A32FD618"

    @Test
    fun derivesNanoCompatibleCommandKey() {
        assertEquals(
            commandKey,
            PairingCrypto.deriveCommandKey(claimCode, "BF000123", appNonce, deviceNonce)
        )
    }

    @Test
    fun verifiesPairingReplyVector() {
        val reply = PairingReply(
            deviceId = "BF000123",
            appNonce = appNonce,
            deviceNonce = deviceNonce,
            status = "OK",
            signature = "9DE50B1ACD9CFDAD"
        )
        assertEquals(commandKey, PairingCrypto.verifyPairingReply(reply, claimCode))
        assertNotNull(PairingReplyParser.parse("BF1 P2 BF000123 $appNonce $deviceNonce OK 9DE50B1ACD9CFDAD"))
    }

    @Test
    fun verifiesCommandAndAckVectors() {
        assertEquals("9B245248C0160CB0", PairingCrypto.proof(commandKey, "CMD|BF000123|ON|1"))
        val ack = DeviceAck("BF000123", "ON", 1, "OK", "2D7C1A602066AC80")
        assertTrue(PairingCrypto.verifyCommandAck(ack, commandKey))
        assertFalse(PairingCrypto.verifyCommandAck(ack.copy(signature = "0000000000000000"), commandKey))
    }
}

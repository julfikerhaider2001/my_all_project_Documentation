package xyz.borgo.farm.data

object DemoRepository {
    val mainPump = PumpDevice(
        id = "AGRI-000123",
        name = "Main Pump",
        simNumber = "+8801738833277",
        zone = "Field test device",
        claimCode = "",
        commandKey = "",
        pairingNonce = "",
        pairingStatus = PairingStatus.UNPAIRED,
        counter = 1042,
        status = PumpStatus.OFFLINE
    )

    val initialActivity = listOf(
        ActivityLog("Ready", "Main Pump is configured to send SMS to +8801738833277")
    )
}

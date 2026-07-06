package xyz.borgo.farm

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.PowerSettingsNew
import androidx.compose.material.icons.outlined.QrCodeScanner
import androidx.compose.material.icons.outlined.Schedule
import androidx.compose.material.icons.outlined.Sms
import androidx.compose.material.icons.outlined.WaterDrop
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import com.google.firebase.auth.FirebaseAuth
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch
import org.json.JSONArray
import org.json.JSONObject
import xyz.borgo.farm.data.ActivityLog
import xyz.borgo.farm.data.DeviceQrParser
import xyz.borgo.farm.data.FarmSchedule
import xyz.borgo.farm.data.FirebasePhoneAuth
import xyz.borgo.farm.data.PairingStatus
import xyz.borgo.farm.data.PumpDevice
import xyz.borgo.farm.data.PumpStatus
import xyz.borgo.farm.data.SecureSecretStore
import xyz.borgo.farm.qr.QrScannerView
import xyz.borgo.farm.sms.PairingCrypto
import xyz.borgo.farm.sms.SmsCommandDispatcher
import xyz.borgo.farm.sms.SmsEvent
import xyz.borgo.farm.sms.SmsEventBus

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent { BorgoFarmApp() }
    }
}

private enum class AppTab {
    Devices,
    Groups,
    Schedule
}

private val Bg = Color(0xFF050607)
private val SurfaceA = Color(0xFF17181B)
private val SurfaceB = Color(0xFF202126)
private val Border = Color(0xFF2A2D35)
private val TextPrimary = Color(0xFFF3F5FB)
private val TextMuted = Color(0xFFA9ADBA)
private val BorgoBlue = Color(0xFFA9C5FF)
private val BorgoBlueStrong = Color(0xFF7DA6FF)
private val Success = Color(0xFF41D17D)
private val Amber = Color(0xFFF5B84B)
private val Danger = Color(0xFFFF7878)
private const val DevicePrefs = "borgo_device_registration"
private const val KeyDevices = "devices"

@Composable
fun BorgoFarmApp() {
    MaterialTheme {
        var isLoggedIn by remember { mutableStateOf(FirebaseAuth.getInstance().currentUser != null) }
        Surface(color = Bg, modifier = Modifier.fillMaxSize()) {
            if (isLoggedIn) MainShell() else PhoneAuthScreen(onContinue = { isLoggedIn = true })
        }
    }
}

@Composable
private fun PhoneAuthScreen(onContinue: () -> Unit) {
    val context = LocalContext.current
    val phoneAuth = remember { FirebasePhoneAuth() }
    var phone by remember { mutableStateOf("+8801700000811") }
    var code by remember { mutableStateOf("") }
    var verificationId by remember { mutableStateOf<String?>(null) }
    var message by remember { mutableStateOf("Use Firebase OTP, or continue in Field Test Mode for hardware testing.") }
    var isLoading by remember { mutableStateOf(false) }

    fun sendCode() {
        val activity = context as? Activity
        if (!phone.startsWith("+") || phone.length < 8) {
            message = "Enter the phone number in international format."
            return
        }
        if (activity == null) {
            message = "Phone auth requires an Android activity."
            return
        }
        isLoading = true
        message = "Requesting Firebase OTP..."
        phoneAuth.requestCode(
            activity = activity,
            phoneNumber = phone,
            onCodeSent = {
                verificationId = it
                isLoading = false
                message = "Code accepted by Firebase. Enter OTP and tap Verify."
            },
            onVerified = {
                isLoading = false
                onContinue()
            },
            onError = {
                isLoading = false
                message = it
            }
        )
    }

    fun verifyCode() {
        val id = verificationId
        if (id == null) {
            message = "First tap Send OTP and wait for Firebase to accept the request."
            return
        }
        if (code.length < 6) {
            message = "Enter the 6-digit OTP."
            return
        }
        isLoading = true
        phoneAuth.verifyCode(
            verificationId = id,
            code = code,
            onVerified = {
                isLoading = false
                onContinue()
            },
            onError = {
                isLoading = false
                message = it
            }
        )
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFFF8FAFF)),
        contentAlignment = Alignment.Center
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(20.dp)
        ) {
            BrandLoginHeader()
            Card(
                colors = CardDefaults.cardColors(containerColor = Color(0xFF4A4B51)),
                shape = RoundedCornerShape(8.dp),
                modifier = Modifier.fillMaxWidth()
            ) {
                Column(Modifier.padding(22.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
                    Text("Borgo Pumps", color = TextPrimary, fontSize = 22.sp, fontWeight = FontWeight.Bold)
                    Text(message, color = Color(0xFFD9DCE5), fontSize = 12.sp, lineHeight = 17.sp)
                    DarkField("Phone number", phone, { phone = it }, KeyboardType.Phone)
                    DarkField("Verification code", code, { code = it }, KeyboardType.Number)
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp), modifier = Modifier.fillMaxWidth()) {
                        BorgoButton("Send OTP", { if (!isLoading) sendCode() }, secondary = verificationId != null, modifier = Modifier.weight(1f))
                        BorgoButton("Verify", { if (!isLoading) verifyCode() }, modifier = Modifier.weight(1f))
                    }
                    BorgoButton("Continue in Field Test Mode", onContinue, secondary = true, modifier = Modifier.fillMaxWidth())
                }
            }
        }
    }
}

@Composable
private fun MainShell() {
    val context = LocalContext.current
    val snackbarHostState = remember { SnackbarHostState() }
    val scope = rememberCoroutineScope()
    val devices = remember { mutableStateListOf<PumpDevice>().apply { addAll(loadDevices(context)) } }
    val activity = remember { mutableStateListOf<ActivityLog>() }
    val schedules = remember { mutableStateListOf<FarmSchedule>() }
    var selectedTab by remember { mutableStateOf(AppTab.Devices) }
    var isScanningQr by remember { mutableStateOf(false) }
    var hasSmsPermission by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(context, Manifest.permission.SEND_SMS) == PackageManager.PERMISSION_GRANTED &&
                ContextCompat.checkSelfPermission(context, Manifest.permission.RECEIVE_SMS) == PackageManager.PERMISSION_GRANTED
        )
    }
    val smsPermissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { results ->
        hasSmsPermission = results[Manifest.permission.SEND_SMS] == true &&
            results[Manifest.permission.RECEIVE_SMS] == true
    }
    val cameraPermissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { isGranted ->
        if (isGranted) isScanningQr = true else scope.launch { snackbarHostState.showSnackbar("Camera permission is required") }
    }

    fun requestSmsPermission() {
        smsPermissionLauncher.launch(
            arrayOf(
                Manifest.permission.SEND_SMS,
                Manifest.permission.RECEIVE_SMS,
                Manifest.permission.READ_SMS
            )
        )
    }

    fun requestQrScan() {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            isScanningQr = true
        } else {
            cameraPermissionLauncher.launch(Manifest.permission.CAMERA)
        }
    }

    fun saveDeviceList() {
        saveDevices(context, devices)
    }

    fun addDevice(device: PumpDevice): Boolean {
        val index = devices.indexOfFirst { it.id == device.id }
        return if (index >= 0) {
            activity.add(0, ActivityLog("Duplicate QR blocked", "${device.id} is already registered"))
            scope.launch { snackbarHostState.showSnackbar("Device already exists: ${device.id}") }
            false
        } else {
            devices.add(device)
            saveDeviceList()
            activity.add(0, ActivityLog("Device scanned", "${device.name} is ready for its SIM number and physical pairing."))
            true
        }
    }

    fun pairDevice(device: PumpDevice) {
        if (!device.simNumber.startsWith("+") || device.simNumber.length < 8) {
            scope.launch { snackbarHostState.showSnackbar("Save the device SIM number before pairing") }
            return
        }
        if (device.claimCode.isBlank()) {
            scope.launch { snackbarHostState.showSnackbar("Claim credential is missing. Scan the device QR again.") }
            return
        }
        if (!hasSmsPermission) {
            requestSmsPermission()
            scope.launch { snackbarHostState.showSnackbar("SMS permission is required") }
            return
        }
        val request = runCatching { SmsCommandDispatcher.sendPairingRequest(context, device) }
            .getOrElse {
                scope.launch { snackbarHostState.showSnackbar("Could not create pairing request") }
                return
            }
        val index = devices.indexOfFirst { it.id == device.id }
        if (index >= 0) {
            devices[index] = device.copy(
                pairingNonce = request.appNonce,
                pairingStatus = PairingStatus.PAIRING,
                status = PumpStatus.PENDING,
                pendingCommand = "PAIR"
            )
            saveDeviceList()
        }
        activity.add(0, ActivityLog("Pairing request sent", "Waiting for an authenticated reply from ${device.name}."))
        scope.launch { snackbarHostState.showSnackbar("Pairing request sent. Keep the app open while the device replies.") }
    }

    fun commandDevice(device: PumpDevice, command: String) {
        if (device.pairingStatus != PairingStatus.ACTIVE || device.commandKey.isBlank()) {
            scope.launch { snackbarHostState.showSnackbar("Pair this device before controlling it") }
            return
        }
        if (!device.simNumber.startsWith("+") || device.simNumber.length < 8) {
            scope.launch { snackbarHostState.showSnackbar("Add a valid SIM number before sending SMS") }
            return
        }
        if (!hasSmsPermission) {
            requestSmsPermission()
            scope.launch { snackbarHostState.showSnackbar("SMS permission is required") }
            return
        }
        val smsBody = SmsCommandDispatcher.sendPumpCommand(context, device, command)
        val index = devices.indexOfFirst { it.id == device.id }
        if (index >= 0) {
            devices[index] = device.copy(
                status = PumpStatus.PENDING,
                counter = device.counter + 1,
                pendingCommand = command
            )
            saveDeviceList()
        }
        activity.add(0, ActivityLog("$command queued", "To ${device.simNumber}: $smsBody"))
        scope.launch { snackbarHostState.showSnackbar("$command queued to ${device.name}") }
    }

    LaunchedEffect(Unit) {
        if (!hasSmsPermission) requestSmsPermission()
    }

    LaunchedEffect(Unit) {
        SmsEventBus.events.collect { event ->
            when (event) {
                is SmsEvent.Sent -> {
                    activity.add(0, ActivityLog("SMS status", "${event.status.replace('_', ' ')} for ${event.command}"))
                    if (event.command == "PAIR" && event.status != "sms_sent") {
                        val index = devices.indexOfFirst { it.id == event.deviceId }
                        if (index >= 0) {
                            devices[index] = devices[index].copy(pairingStatus = PairingStatus.FAILED, status = PumpStatus.OFFLINE)
                            saveDeviceList()
                        }
                    }
                }
                is SmsEvent.Delivered -> activity.add(0, ActivityLog("Carrier report", "Delivery report received for ${event.command}"))
                is SmsEvent.Ack -> {
                    val index = devices.indexOfFirst { it.id == event.ack.deviceId }
                    if (index >= 0) {
                        val current = devices[index]
                        val valid = samePhone(current.simNumber, event.sender) &&
                            event.ack.counter == current.counter &&
                            PairingCrypto.verifyCommandAck(event.ack, current.commandKey)
                        if (valid) {
                            devices[index] = current.copy(
                                status = ackStatus(event.ack.command, event.ack.status),
                                pendingCommand = null
                            )
                            saveDeviceList()
                            activity.add(0, ActivityLog("Device confirmed", "${event.ack.status} for ${event.ack.command} from ${current.name}."))
                        } else {
                            activity.add(0, ActivityLog("Rejected acknowledgement", "Invalid sender, counter, or proof for ${current.name}."))
                        }
                    }
                }
                is SmsEvent.Pairing -> {
                    val index = devices.indexOfFirst { it.id == event.reply.deviceId }
                    if (index >= 0) {
                        val current = devices[index]
                        val validContext = current.pairingStatus == PairingStatus.PAIRING &&
                            current.pairingNonce == event.reply.appNonce &&
                            samePhone(current.simNumber, event.sender)
                        val commandKey = if (validContext) {
                            runCatching { PairingCrypto.verifyPairingReply(event.reply, current.claimCode) }.getOrNull()
                        } else null
                        if (commandKey != null) {
                            devices[index] = current.copy(
                                claimCode = "",
                                commandKey = commandKey,
                                pairingNonce = "",
                                pairingStatus = PairingStatus.ACTIVE,
                                status = PumpStatus.STOPPED,
                                pendingCommand = null,
                                counter = 0
                            )
                            saveDeviceList()
                            activity.add(0, ActivityLog("Device paired", "${current.name} returned a valid cryptographic proof."))
                            scope.launch { snackbarHostState.showSnackbar("${current.name} paired securely") }
                        } else {
                            devices[index] = current.copy(pairingStatus = PairingStatus.FAILED, status = PumpStatus.OFFLINE, pendingCommand = null)
                            saveDeviceList()
                            activity.add(0, ActivityLog("Pairing rejected", "The sender, nonce, or proof was invalid."))
                            scope.launch { snackbarHostState.showSnackbar("Pairing reply could not be verified") }
                        }
                    }
                }
            }
        }
    }

    if (isScanningQr) {
        QrScannerView(
            onQrDetected = { rawValue ->
                val payload = DeviceQrParser.parse(rawValue)
                if (payload == null) {
                    scope.launch { snackbarHostState.showSnackbar("Invalid Borgo Pumps QR") }
                    return@QrScannerView
                }
                val added = addDevice(payload.toPumpDevice())
                isScanningQr = false
                if (added) scope.launch { snackbarHostState.showSnackbar("Device scanned. Add its SIM number next.") }
            },
            onCancel = { isScanningQr = false }
        )
        return
    }

    Scaffold(snackbarHost = { SnackbarHost(snackbarHostState) }, containerColor = Bg) { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .fillMaxSize()
                .background(Bg)
                .verticalScroll(rememberScrollState())
                .padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            TopHeader()
            TabBar(selectedTab = selectedTab, onSelected = { selectedTab = it })
            when (selectedTab) {
                AppTab.Devices -> DevicesTab(
                    devices = devices,
                    activity = activity,
                    hasSmsPermission = hasSmsPermission,
                    onScanQr = ::requestQrScan,
                    onRequestSmsPermission = ::requestSmsPermission,
                    onSaveNumber = { device, simNumber ->
                        val index = devices.indexOfFirst { it.id == device.id }
                        if (index >= 0) {
                            devices[index] = device.copy(simNumber = simNumber.trim())
                            saveDeviceList()
                            activity.add(0, ActivityLog("SIM updated", "${device.name} now targets ${simNumber.trim()}"))
                            scope.launch { snackbarHostState.showSnackbar("SIM saved") }
                        }
                    },
                    onPair = ::pairDevice,
                    onCommand = ::commandDevice
                )
                AppTab.Groups -> GroupsTab(devices = devices, onCommand = ::commandDevice)
                AppTab.Schedule -> ScheduleTab(devices = devices, schedules = schedules)
            }
        }
    }
}

@Composable
private fun TopHeader() {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(52.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Icon(Icons.Outlined.WaterDrop, contentDescription = null, tint = BorgoBlue, modifier = Modifier.size(20.dp))
        Column {
            Text("Borgo Pumps", color = TextPrimary, fontSize = 18.sp, fontWeight = FontWeight.Bold)
            Text("GSM pump control", color = TextMuted, fontSize = 12.sp)
        }
    }
}

@Composable
private fun TabBar(selectedTab: AppTab, onSelected: (AppTab) -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .background(Color(0xFF101114))
            .padding(4.dp),
        horizontalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        TabButton(AppTab.Devices, "Devices", Icons.Outlined.PowerSettingsNew, selectedTab, onSelected, Modifier.weight(1f))
        TabButton(AppTab.Groups, "Groups", Icons.Outlined.Sms, selectedTab, onSelected, Modifier.weight(1f))
        TabButton(AppTab.Schedule, "Schedule", Icons.Outlined.Schedule, selectedTab, onSelected, Modifier.weight(1f))
    }
}

@Composable
private fun TabButton(
    tab: AppTab,
    label: String,
    icon: ImageVector,
    selectedTab: AppTab,
    onSelected: (AppTab) -> Unit,
    modifier: Modifier = Modifier
) {
    Button(
        onClick = { onSelected(tab) },
        modifier = modifier.height(42.dp),
        shape = RoundedCornerShape(6.dp),
        colors = ButtonDefaults.buttonColors(
            containerColor = if (selectedTab == tab) BorgoBlue else Color.Transparent,
            contentColor = if (selectedTab == tab) Color(0xFF15213A) else TextMuted
        )
    ) {
        Icon(icon, contentDescription = null, modifier = Modifier.size(15.dp))
        Spacer(Modifier.size(6.dp))
        Text(label, fontSize = 11.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun DevicesTab(
    devices: List<PumpDevice>,
    activity: List<ActivityLog>,
    hasSmsPermission: Boolean,
    onScanQr: () -> Unit,
    onRequestSmsPermission: () -> Unit,
    onSaveNumber: (PumpDevice, String) -> Unit,
    onPair: (PumpDevice) -> Unit,
    onCommand: (PumpDevice, String) -> Unit
) {
    BorgoCard {
        Text("Devices", color = TextPrimary, fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Text(
            if (devices.isEmpty()) "No devices yet. Add your first SIM800L pump by scanning its Borgo QR code."
            else "${devices.size} registered device${if (devices.size == 1) "" else "s"}. Add or edit each SIM number in the device card.",
            color = TextMuted,
            fontSize = 12.sp,
            lineHeight = 17.sp
        )
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp), modifier = Modifier.fillMaxWidth()) {
            BorgoButton("Scan QR", onScanQr, modifier = Modifier.weight(1f), icon = Icons.Outlined.QrCodeScanner)
            if (!hasSmsPermission) {
                BorgoButton("Allow SMS", onRequestSmsPermission, secondary = true, modifier = Modifier.weight(1f), icon = Icons.Outlined.Sms)
            }
        }
    }

    devices.forEach { device ->
        DeviceCard(device = device, onSaveNumber = onSaveNumber, onPair = onPair, onCommand = onCommand)
    }

    ActivityPanel(activity)
}

@Composable
private fun DeviceCard(
    device: PumpDevice,
    onSaveNumber: (PumpDevice, String) -> Unit,
    onPair: (PumpDevice) -> Unit,
    onCommand: (PumpDevice, String) -> Unit
) {
    var simNumber by remember(device.id, device.simNumber) { mutableStateOf(device.simNumber) }
    val statusText = when (device.pairingStatus) {
        PairingStatus.UNPAIRED -> if (device.simNumber.isBlank()) "Needs SIM" else "Ready to pair"
        PairingStatus.PAIRING -> "Pairing"
        PairingStatus.FAILED -> "Pairing failed"
        PairingStatus.ACTIVE -> when (device.status) {
        PumpStatus.RUNNING -> "Running"
        PumpStatus.STOPPED -> "Stopped"
        PumpStatus.OFFLINE -> "Offline"
        PumpStatus.PENDING -> "Pending ${device.pendingCommand ?: "command"}"
        }
    }
    val statusColor = when (device.pairingStatus) {
        PairingStatus.UNPAIRED -> TextMuted
        PairingStatus.PAIRING -> Amber
        PairingStatus.FAILED -> Danger
        PairingStatus.ACTIVE -> when (device.status) {
            PumpStatus.RUNNING -> Success
            PumpStatus.STOPPED -> TextMuted
            PumpStatus.OFFLINE -> Danger
            PumpStatus.PENDING -> Amber
        }
    }

    BorgoCard {
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.Top) {
            Column(Modifier.weight(1f)) {
                Text(device.name, color = TextPrimary, fontSize = 22.sp, fontWeight = FontWeight.Bold)
                Text(device.id, color = TextMuted, fontSize = 12.sp)
            }
            StatusPill(statusText, statusColor)
        }
        DetailRow("Target SIM", device.simNumber.ifBlank { "Not set" })
        DetailRow("Hardware", device.zone)
        if (device.pairingStatus == PairingStatus.ACTIVE) DetailRow("Next counter", (device.counter + 1).toString())
        DarkField("Editable SIM number", simNumber, { simNumber = it }, KeyboardType.Phone)
        BorgoButton(
            "Save SIM Number",
            { onSaveNumber(device, simNumber) },
            secondary = true,
            modifier = Modifier.fillMaxWidth(),
            icon = Icons.Outlined.Sms
        )
        if (device.pairingStatus != PairingStatus.ACTIVE) {
            Text(
                "Hold the controller's pair button for 2 seconds, then tap Pair device within 3 minutes.",
                color = TextMuted,
                fontSize = 12.sp,
                lineHeight = 17.sp
            )
            BorgoButton(
                if (device.pairingStatus == PairingStatus.PAIRING) "Resend pairing SMS" else "Pair device",
                { onPair(device) },
                modifier = Modifier.fillMaxWidth(),
                enabled = device.simNumber.isNotBlank()
            )
        } else {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                BorgoButton("OFF", { onCommand(device, "OFF") }, secondary = true, modifier = Modifier.weight(1f))
                BorgoButton("ON", { onCommand(device, "ON") }, modifier = Modifier.weight(1f))
            }
        }
    }
}

@Composable
private fun GroupsTab(devices: List<PumpDevice>, onCommand: (PumpDevice, String) -> Unit) {
    BorgoCard {
        Text("Groups", color = TextPrimary, fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Text(
            "For MVP testing, the default group contains all scanned devices. Later this becomes editable device selection.",
            color = TextMuted,
            fontSize = 12.sp,
            lineHeight = 17.sp
        )
        DetailRow("Default group", "All Pumps")
        DetailRow("Devices", devices.size.toString())
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp), modifier = Modifier.fillMaxWidth()) {
            BorgoButton("Stop Group", { devices.forEach { onCommand(it, "OFF") } }, secondary = true, modifier = Modifier.weight(1f))
            BorgoButton("Start Group", { devices.forEach { onCommand(it, "ON") } }, modifier = Modifier.weight(1f))
        }
    }
    devices.forEach {
        BorgoCard {
            Text(it.name, color = TextPrimary, fontSize = 15.sp, fontWeight = FontWeight.Bold)
            Text(it.simNumber.ifBlank { "SIM number not set" }, color = TextMuted, fontSize = 12.sp)
        }
    }
}

@Composable
private fun ScheduleTab(devices: List<PumpDevice>, schedules: MutableList<FarmSchedule>) {
    var command by remember { mutableStateOf("ON") }
    var time by remember { mutableStateOf("06:00") }

    BorgoCard {
        Text("Scheduling Plan", color = TextPrimary, fontSize = 20.sp, fontWeight = FontWeight.Bold)
        Text(
            "Create local schedule plans for scanned devices. Background execution will be wired after the field-control loop is stable.",
            color = TextMuted,
            fontSize = 12.sp,
            lineHeight = 17.sp
        )
        DetailRow("Target", if (devices.isEmpty()) "No devices" else "All Pumps")
        DarkField("Time", time, { time = it })
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp), modifier = Modifier.fillMaxWidth()) {
            BorgoButton("Command OFF", { command = "OFF" }, secondary = command != "OFF", modifier = Modifier.weight(1f))
            BorgoButton("Command ON", { command = "ON" }, secondary = command != "ON", modifier = Modifier.weight(1f))
        }
        BorgoButton(
            "Save Schedule",
            {
                schedules.add(0, FarmSchedule("All Pumps", "Every day at $time - $command", true))
            },
            modifier = Modifier.fillMaxWidth()
        )
    }

    schedules.forEach {
        BorgoCard {
            Text(it.target, color = TextPrimary, fontSize = 15.sp, fontWeight = FontWeight.Bold)
            Text(it.detail, color = TextMuted, fontSize = 12.sp)
            StatusPill(if (it.enabled) "Planned" else "Off", BorgoBlue)
        }
    }
}

@Composable
private fun ActivityPanel(activity: List<ActivityLog>) {
    BorgoCard {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Outlined.Sms, contentDescription = null, tint = BorgoBlue, modifier = Modifier.size(18.dp))
            Text("SMS Activity", color = TextPrimary, fontSize = 16.sp, fontWeight = FontWeight.Bold)
        }
        if (activity.isEmpty()) {
            Text("No SMS commands yet.", color = TextMuted, fontSize = 13.sp)
        } else {
            activity.take(8).forEach {
                Column(verticalArrangement = Arrangement.spacedBy(3.dp)) {
                    Text(it.title, color = TextPrimary, fontSize = 13.sp, fontWeight = FontWeight.Bold)
                    Text(it.detail, color = TextMuted, fontSize = 12.sp, maxLines = 3, overflow = TextOverflow.Ellipsis)
                }
            }
        }
    }
}

@Composable
private fun DetailRow(label: String, value: String) {
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, color = TextMuted, fontSize = 12.sp)
        Text(value, color = TextPrimary, fontSize = 12.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun BrandLoginHeader() {
    Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Box(
            modifier = Modifier
                .size(54.dp)
                .clip(RoundedCornerShape(12.dp))
                .background(Color(0xFFE8EEFF)),
            contentAlignment = Alignment.Center
        ) {
            Icon(Icons.Outlined.WaterDrop, contentDescription = null, tint = BorgoBlueStrong, modifier = Modifier.size(30.dp))
        }
        Text("Borgo Pumps", color = BorgoBlueStrong, fontSize = 14.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun BorgoCard(
    modifier: Modifier = Modifier,
    horizontalAlignment: Alignment.Horizontal = Alignment.Start,
    content: @Composable ColumnScope.() -> Unit
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .border(1.dp, Border, RoundedCornerShape(8.dp))
            .background(SurfaceA)
            .padding(14.dp),
        horizontalAlignment = horizontalAlignment,
        verticalArrangement = Arrangement.spacedBy(12.dp),
        content = content
    )
}

@Composable
private fun DarkField(
    label: String,
    value: String,
    onValueChange: (String) -> Unit,
    keyboardType: KeyboardType = KeyboardType.Text
) {
    Column(verticalArrangement = Arrangement.spacedBy(7.dp)) {
        Text(label.uppercase(), color = TextMuted, fontSize = 10.sp, fontWeight = FontWeight.ExtraBold)
        OutlinedTextField(
            value = value,
            onValueChange = onValueChange,
            keyboardOptions = KeyboardOptions(keyboardType = keyboardType),
            singleLine = true,
            shape = RoundedCornerShape(8.dp),
            modifier = Modifier.fillMaxWidth()
        )
    }
}

@Composable
private fun BorgoButton(
    text: String,
    onClick: () -> Unit,
    secondary: Boolean = false,
    modifier: Modifier = Modifier,
    icon: ImageVector = Icons.Outlined.PowerSettingsNew,
    enabled: Boolean = true
) {
    Button(
        onClick = onClick,
        enabled = enabled,
        modifier = modifier.height(44.dp),
        shape = RoundedCornerShape(7.dp),
        colors = ButtonDefaults.buttonColors(
            containerColor = if (secondary) SurfaceB else BorgoBlue,
            contentColor = if (secondary) BorgoBlue else Color(0xFF1B2540)
        )
    ) {
        Icon(icon, contentDescription = null, modifier = Modifier.size(16.dp))
        Spacer(Modifier.size(8.dp))
        Text(text, fontSize = 12.sp, fontWeight = FontWeight.ExtraBold)
    }
}

@Composable
private fun StatusPill(text: String, color: Color) {
    Box(
        modifier = Modifier
            .clip(CircleShape)
            .background(color.copy(alpha = 0.14f))
            .padding(horizontal = 9.dp, vertical = 5.dp)
    ) {
        Text(text, color = color, fontSize = 10.sp, fontWeight = FontWeight.Bold)
    }
}

private fun ackStatus(command: String, status: String): PumpStatus {
    if (!status.equals("OK", ignoreCase = true)) return PumpStatus.STOPPED
    return when {
        command.equals("ON", ignoreCase = true) -> PumpStatus.RUNNING
        command.equals("OFF", ignoreCase = true) -> PumpStatus.STOPPED
        else -> PumpStatus.STOPPED
    }
}

private fun loadDevices(context: Context): List<PumpDevice> {
    val raw = context.getSharedPreferences(DevicePrefs, Context.MODE_PRIVATE).getString(KeyDevices, null) ?: return emptyList()
    return runCatching {
        val array = JSONArray(raw)
        val secrets = SecureSecretStore()
        List(array.length()) { index ->
            val item = array.getJSONObject(index)
            val claimCode = secrets.decrypt(item.optString("claimCodeEncrypted", ""))
            val commandKey = secrets.decrypt(item.optString("commandKeyEncrypted", ""))
            PumpDevice(
                id = item.getString("id"),
                name = item.getString("name"),
                simNumber = item.optString("simNumber", ""),
                zone = item.optString("zone", "nano-pump-v1"),
                claimCode = claimCode,
                commandKey = commandKey,
                pairingNonce = item.optString("pairingNonce", ""),
                pairingStatus = runCatching {
                    PairingStatus.valueOf(item.optString("pairingStatus", PairingStatus.UNPAIRED.name))
                }.getOrDefault(PairingStatus.UNPAIRED),
                counter = item.optInt("counter", 0),
                status = if (commandKey.isNotBlank()) PumpStatus.STOPPED else PumpStatus.OFFLINE
            )
        }.filter { it.claimCode.isNotBlank() || it.commandKey.isNotBlank() }
    }.getOrDefault(emptyList())
}

private fun saveDevices(context: Context, devices: List<PumpDevice>) {
    val array = JSONArray()
    val secrets = SecureSecretStore()
    devices.forEach {
        array.put(
            JSONObject()
                .put("id", it.id)
                .put("name", it.name)
                .put("simNumber", it.simNumber)
                .put("zone", it.zone)
                .put("claimCodeEncrypted", secrets.encrypt(it.claimCode))
                .put("commandKeyEncrypted", secrets.encrypt(it.commandKey))
                .put("pairingNonce", it.pairingNonce)
                .put("pairingStatus", it.pairingStatus.name)
                .put("counter", it.counter)
        )
    }
    context.getSharedPreferences(DevicePrefs, Context.MODE_PRIVATE)
        .edit()
        .putString(KeyDevices, array.toString())
        .apply()
}

private fun samePhone(left: String, right: String): Boolean {
    val leftDigits = left.filter(Char::isDigit)
    val rightDigits = right.filter(Char::isDigit)
    if (leftDigits.length < 10 || rightDigits.length < 10) return leftDigits == rightDigits
    return leftDigits.takeLast(10) == rightDigits.takeLast(10)
}

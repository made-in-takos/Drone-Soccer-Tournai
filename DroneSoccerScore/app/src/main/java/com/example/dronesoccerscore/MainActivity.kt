package com.example.drone_soccer_score

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.util.Log
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.app.ActivityCompat
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.*
import java.io.IOException
import java.io.InputStream
import java.util.*

private enum class TimerMode { COUNTDOWN, CHRONOMETER }

private val DURATION_OPTIONS = listOf(3, 5, 10)

private const val ESP32_BT_NAME = "Goal-Rouge"

@Suppress("DEPRECATION")
private fun vibrateTimerEnd(context: Context) {
	val vibrator = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
		val manager = context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as VibratorManager
		manager.defaultVibrator
	} else {
		context.getSystemService(Context.VIBRATOR_SERVICE) as Vibrator
	}
	if (!vibrator.hasVibrator()) return
	val pattern = longArrayOf(0, 400, 150, 400, 150, 600)
	if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
		vibrator.vibrate(VibrationEffect.createWaveform(pattern, -1))
	} else {
		vibrator.vibrate(pattern, -1)
	}
}

class MainActivity : ComponentActivity() {
	private val viewModel: MainViewModel by viewModels()
	private var bluetoothAdapter: BluetoothAdapter? = null
	private var bluetoothSocket: BluetoothSocket? = null
	private var connectedDevice: BluetoothDevice? = null
	private var isConnected = false
	private var bluetoothListenerJob: Job? = null

	// UUID SPP (Serial Port Profile) — standard Bluetooth Classic, ESP32 inclus
	private val uuid = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")

	// Tampon d'entrée pour assembler les lignes
	private val incomingBuffer = StringBuilder()
	
	// Permissions
	private val requestPermissionLauncher = registerForActivityResult(
		ActivityResultContracts.RequestMultiplePermissions()
	) { permissions ->
		val allGranted = permissions.values.all { it }
		if (!allGranted) {
			Toast.makeText(this, "Permissions Bluetooth refusées", Toast.LENGTH_SHORT).show()
		}
	}

	private fun connectToBluetoothByAddress(deviceAddress: String) {
		if (bluetoothAdapter?.isEnabled != true) {
			Toast.makeText(this, "Activez le Bluetooth", Toast.LENGTH_SHORT).show()
			return
		}
		if (ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
			Toast.makeText(this, "Autorisation Bluetooth requise", Toast.LENGTH_SHORT).show()
			return
		}
		val device = bluetoothAdapter?.bondedDevices?.firstOrNull { it.address == deviceAddress }
		if (device == null) {
			Toast.makeText(this, "Appareil introuvable", Toast.LENGTH_SHORT).show()
			return
		}
		viewModel.onConnecting()
		lifecycleScope.launch(Dispatchers.IO) {
			try {
				bluetoothAdapter?.cancelDiscovery()
				var socket: BluetoothSocket? = null
				try {
					socket = device.createRfcommSocketToServiceRecord(uuid)
					socket.connect()
				} catch (e: IOException) {
					socket = device.createInsecureRfcommSocketToServiceRecord(uuid)
					socket.connect()
				}
				bluetoothSocket = socket
				connectedDevice = device
				isConnected = true
				withContext(Dispatchers.Main) {
					viewModel.onConnected(deviceAddress)
					Toast.makeText(this@MainActivity, "Connecté à ${device.name}", Toast.LENGTH_SHORT).show()
				}
				startBluetoothListener()
			} catch (e: IOException) {
				Log.e("Bluetooth", "Erreur de connexion (sélection): ${e.message}")
				withContext(Dispatchers.Main) {
					viewModel.onConnectionFailed()
					Toast.makeText(this@MainActivity, "Échec de connexion", Toast.LENGTH_SHORT).show()
				}
			}
		}
	}

	private fun getPairedDevicesSafe(): List<Pair<String, String>> {
		if (ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
			return emptyList()
		}
		val devices = bluetoothAdapter?.bondedDevices ?: emptySet()
		return devices
			.map { (it.name ?: "Appareil inconnu") to it.address }
			.sortedByDescending { (name, _) -> name.contains(ESP32_BT_NAME, ignoreCase = true) }
	}
	
	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)
		bluetoothAdapter = BluetoothAdapter.getDefaultAdapter()
		setContent {
			DroneSoccerScoreScreen(
				viewModel = viewModel,
				onConnectClick = { requestBluetoothPermissions() },
				onDisconnectClick = { disconnectBluetooth() },
				pairedDevices = getPairedDevicesSafe(),
				onDeviceSelected = { addr -> connectToBluetoothByAddress(addr) }
			)
		}
		if (viewModel.isConnected && bluetoothSocket == null) {
			viewModel.connectedDeviceAddress?.let { connectToBluetoothByAddress(it) }
		}
	}
	
	private fun requestBluetoothPermissions() {
		val permissions = arrayOf(
			Manifest.permission.BLUETOOTH_CONNECT,
			Manifest.permission.BLUETOOTH_SCAN,
			Manifest.permission.ACCESS_FINE_LOCATION
		)
		requestPermissionLauncher.launch(permissions)
	}

	private fun disconnectBluetooth() {
		bluetoothListenerJob?.cancel()
		bluetoothListenerJob = null
		try {
			if (ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED) {
				bluetoothSocket?.close()
			}
		} catch (e: IOException) {
			Log.e("Bluetooth", "Erreur de déconnexion: ${e.message}")
		}
		bluetoothSocket = null
		connectedDevice = null
		isConnected = false
		viewModel.onDisconnected()
		Log.d("Bluetooth", "Déconnecté")
	}

	private fun startBluetoothListener() {
		bluetoothListenerJob?.cancel()
		bluetoothListenerJob = lifecycleScope.launch(Dispatchers.IO) {
			val inputStream: InputStream? = bluetoothSocket?.inputStream
			val buffer = ByteArray(1024)
			while (isConnected) {
				try {
					val bytes = inputStream?.read(buffer)
					if (bytes != null && bytes > 0) {
						val chunk = String(buffer, 0, bytes)
						appendAndProcessBuffer(chunk)
					}
				} catch (e: IOException) {
					if (isConnected) {
						Log.e("Bluetooth", "Erreur de lecture: ${e.message}")
					}
					break
				}
			}
		}
	}
	
	private fun appendAndProcessBuffer(chunk: String) {
		incomingBuffer.append(chunk)
		// Normaliser CRLF -> LF
		var idx = incomingBuffer.indexOf("\n")
		while (idx >= 0) {
			val line = incomingBuffer.substring(0, idx).trim('\r', '\n', ' ')
			if (line.isNotEmpty()) {
				parseScoreLine(line)
			}
			incomingBuffer.delete(0, idx + 1)
			idx = incomingBuffer.indexOf("\n")
		}
	}
	
	private fun parseScoreLine(line: String) {
		try {
			// Format "RED:x;BLACK:y" ou "RED:x;BLUE:y"
			if (line.contains("RED:") && (line.contains("BLACK:") || line.contains("BLUE:"))) {
				val redMatch = Regex("RED:(\\d+)").find(line)
				val otherMatch = Regex("(?:BLACK|BLUE):(\\d+)").find(line)
				val redScore = redMatch?.groupValues?.get(1)?.toIntOrNull()
				val otherScore = otherMatch?.groupValues?.get(1)?.toIntOrNull()
				if (redScore != null && otherScore != null) {
					runOnUiThread { viewModel.updateScores(redScore, otherScore) }
					return
				}
			}
			// Cas 2: logs français "Score Rouge: x" ou "Score Noir: x"
			if (line.startsWith("Score Rouge")) {
				val num = Regex("(\\d+)").find(line)?.groupValues?.get(1)?.toIntOrNull()
				if (num != null) runOnUiThread { viewModel.updateScores(num, viewModel.blackScore) }
				return
			}
			if (line.startsWith("Score Noir")) {
				val num = Regex("(\\d+)").find(line)?.groupValues?.get(1)?.toIntOrNull()
				if (num != null) runOnUiThread { viewModel.updateScores(viewModel.redScore, num) }
				return
			}
			// Sinon: ignorer la ligne
		} catch (e: Exception) {
			Log.e("Bluetooth", "Erreur de parsing ligne '$line': ${e.message}")
		}
	}

	override fun onDestroy() {
		super.onDestroy()
		if (!isChangingConfigurations) {
			disconnectBluetooth()
		}
	}
}

@Composable
fun DroneSoccerScoreScreen(
	viewModel: MainViewModel,
	onConnectClick: () -> Unit,
	onDisconnectClick: () -> Unit,
	pairedDevices: List<Pair<String, String>>,
	onDeviceSelected: (String) -> Unit
) {
	val context = LocalContext.current
	val scoreRed = viewModel.redScore
	val scoreBlack = viewModel.blackScore
	val isConnected = viewModel.isConnected
	val isConnecting = viewModel.isConnecting
	val connectFailed = viewModel.connectFailed
	var localScoreRed by remember { mutableIntStateOf(scoreRed) }
	var localScoreBlack by remember { mutableIntStateOf(scoreBlack) }
	LaunchedEffect(scoreRed, scoreBlack) {
		localScoreRed = scoreRed
		localScoreBlack = scoreBlack
	}

	val blueScheme = lightColorScheme(
		primary = Color(0xFF1565C0),
		onPrimary = Color.White,
		primaryContainer = Color(0xFF90CAF9),
		secondary = Color(0xFF0D47A1),
		tertiary = Color(0xFF0288D1),
		background = Color(0xFFE3F2FD),
		surface = Color(0xFFE3F2FD),
		onSurface = Color(0xFF0D47A1)
	)

	MaterialTheme(colorScheme = blueScheme) {
		Column(
			modifier = Modifier
				.fillMaxSize()
				.verticalScroll(rememberScrollState())
				.padding(16.dp),
			verticalArrangement = Arrangement.Center,
			horizontalAlignment = Alignment.CenterHorizontally
		) {
			Text("Drone Soccer Score", fontSize = 24.sp, color = MaterialTheme.colorScheme.onSurface)
			Spacer(Modifier.height(16.dp))
			Row(
				horizontalArrangement = Arrangement.spacedBy(8.dp),
				verticalAlignment = Alignment.CenterVertically
			) {
				Text(
					text = if (isConnected) "🔗 Connecté" else "❌ Déconnecté",
					color = if (isConnected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error
				)
			}
			Spacer(Modifier.height(8.dp))
			var showPicker by remember { mutableStateOf(false) }
			Row(
				horizontalArrangement = Arrangement.spacedBy(8.dp),
				verticalAlignment = Alignment.CenterVertically
			) {
				Button(
					onClick = { showPicker = true; onConnectClick() },
					enabled = !isConnected && !isConnecting
				) { Text("Connecter") }
				if (isConnecting) { CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp) }
				if (connectFailed && !isConnected && !isConnecting) { Text("✖", color = MaterialTheme.colorScheme.error) }
				Button(onClick = onDisconnectClick, enabled = isConnected) { Text("Déconnecter") }
			}

			if (showPicker && !isConnected) {
				AlertDialog(
					onDismissRequest = { showPicker = false },
					confirmButton = { TextButton(onClick = { showPicker = false }) { Text("Fermer") } },
					title = { Text("Appareils appairés") },
					text = {
						Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
							if (pairedDevices.isEmpty()) {
								Text("Aucun appareil appairé. Appairez l'ESP32 « $ESP32_BT_NAME » dans les paramètres Bluetooth du téléphone.")
							} else {
								pairedDevices.forEach { (name, address) ->
									ElevatedButton(onClick = { onDeviceSelected(address); showPicker = false }) {
										Column {
											Text(if (name.contains(ESP32_BT_NAME, ignoreCase = true)) "$name (ESP32)" else name)
											Text(address, fontSize = 12.sp)
										}
									}
								}
							}
						}
					}
					)
				}

			Spacer(Modifier.height(16.dp))
			MatchTimer(
				onTimerAlert = { minutes, isCountdown ->
					vibrateTimerEnd(context)
					val message = if (isCountdown) {
						"$minutes minutes écoulées !"
					} else {
						"Objectif de $minutes minutes atteint !"
					}
					Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
				}
			)

			Spacer(Modifier.height(20.dp))
			Row(
				horizontalArrangement = Arrangement.SpaceEvenly,
				modifier = Modifier.fillMaxWidth()
			) {
				ScoreCard(
					teamName = "Équipe Rouge",
					score = localScoreRed,
					color = Color(0xFFD32F2F),
					onAdd = { localScoreRed++; viewModel.updateScores(localScoreRed, localScoreBlack) },
					onSubtract = { if (localScoreRed > 0) { localScoreRed--; viewModel.updateScores(localScoreRed, localScoreBlack) } }
				)
				ScoreCard(
					teamName = "Équipe Bleue",
					score = localScoreBlack,
					color = Color(0xFF263238),
					onAdd = { localScoreBlack++; viewModel.updateScores(localScoreRed, localScoreBlack) },
					onSubtract = { if (localScoreBlack > 0) { localScoreBlack--; viewModel.updateScores(localScoreRed, localScoreBlack) } }
				)
			}

			Spacer(Modifier.height(16.dp))
			Button(
				onClick = {
					localScoreRed = 0
					localScoreBlack = 0
					viewModel.updateScores(0, 0)
				},
				colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.primary)
			) { Text("Remise à zéro") }

			Spacer(Modifier.height(20.dp))
			Text("Appuyez sur les boutons de la télécommande ou utilisez +/-", fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurface)
		}
	}
}

@Composable
fun MatchTimer(onTimerAlert: (durationMinutes: Int, isCountdown: Boolean) -> Unit) {
	var mode by remember { mutableStateOf(TimerMode.COUNTDOWN) }
	var durationMinutes by remember { mutableIntStateOf(3) }
	val targetSeconds = durationMinutes * 60

	var remainingSeconds by remember { mutableIntStateOf(targetSeconds) }
	var elapsedSeconds by remember { mutableIntStateOf(0) }
	var isRunning by remember { mutableStateOf(false) }
	var timerFinished by remember { mutableStateOf(false) }
	var alertTriggered by remember { mutableStateOf(false) }

	fun resetTimer() {
		isRunning = false
		remainingSeconds = targetSeconds
		elapsedSeconds = 0
		timerFinished = false
		alertTriggered = false
	}

	fun applyDuration(minutes: Int) {
		durationMinutes = minutes
		remainingSeconds = minutes * 60
		elapsedSeconds = 0
		timerFinished = false
		alertTriggered = false
	}

	fun applyMode(newMode: TimerMode) {
		mode = newMode
		remainingSeconds = targetSeconds
		elapsedSeconds = 0
		timerFinished = false
		alertTriggered = false
	}

	LaunchedEffect(isRunning, mode) {
		if (!isRunning) return@LaunchedEffect
		while (true) {
			delay(1000)
			when (mode) {
				TimerMode.COUNTDOWN -> {
					if (remainingSeconds > 0) {
						remainingSeconds--
					}
					if (remainingSeconds <= 0) {
						timerFinished = true
						isRunning = false
						onTimerAlert(durationMinutes, true)
						break
					}
				}
				TimerMode.CHRONOMETER -> {
					elapsedSeconds++
					if (elapsedSeconds >= targetSeconds && !alertTriggered) {
						alertTriggered = true
						onTimerAlert(durationMinutes, false)
					}
				}
			}
		}
	}

	val displaySeconds = when (mode) {
		TimerMode.COUNTDOWN -> remainingSeconds
		TimerMode.CHRONOMETER -> elapsedSeconds
	}
	val displayTime = "%02d:%02d".format(displaySeconds / 60, displaySeconds % 60)

	val isPaused = !isRunning && !timerFinished && when (mode) {
		TimerMode.COUNTDOWN -> remainingSeconds < targetSeconds
		TimerMode.CHRONOMETER -> elapsedSeconds > 0
	}

	val timerColor = when {
		isPaused -> Color(0xFFE65100)
		mode == TimerMode.COUNTDOWN -> when {
			timerFinished -> MaterialTheme.colorScheme.error
			remainingSeconds <= 30 && isRunning -> Color(0xFFE65100)
			else -> MaterialTheme.colorScheme.onSurface
		}
		else -> when {
			alertTriggered -> MaterialTheme.colorScheme.error
			elapsedSeconds >= targetSeconds - 30 && elapsedSeconds < targetSeconds && isRunning -> Color(0xFFE65100)
			else -> MaterialTheme.colorScheme.onSurface
		}
	}

	val canStart = when (mode) {
		TimerMode.COUNTDOWN -> !timerFinished && remainingSeconds > 0
		TimerMode.CHRONOMETER -> true
	}

	val statusText = when {
		isPaused -> "En pause"
		mode == TimerMode.COUNTDOWN && timerFinished -> "Temps écoulé !"
		mode == TimerMode.CHRONOMETER && alertTriggered -> "Objectif atteint !"
		else -> null
	}

	val statusColor = when {
		isPaused -> Color(0xFFE65100)
		else -> MaterialTheme.colorScheme.error
	}

	Card(
		modifier = Modifier.fillMaxWidth(),
		colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer)
	) {
		Column(
			modifier = Modifier.fillMaxWidth().padding(16.dp),
			horizontalAlignment = Alignment.CenterHorizontally,
			verticalArrangement = Arrangement.spacedBy(8.dp)
		) {
			Text(
				text = if (mode == TimerMode.COUNTDOWN) "Compte à rebours" else "Chronomètre",
				fontSize = 16.sp,
				fontWeight = FontWeight.Medium
			)

			Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
				FilterChip(
					selected = mode == TimerMode.COUNTDOWN,
					onClick = { applyMode(TimerMode.COUNTDOWN) },
					label = { Text("Compte à rebours", fontSize = 12.sp) },
					enabled = !isRunning && !isPaused
				)
				FilterChip(
					selected = mode == TimerMode.CHRONOMETER,
					onClick = { applyMode(TimerMode.CHRONOMETER) },
					label = { Text("Chronomètre", fontSize = 12.sp) },
					enabled = !isRunning && !isPaused
				)
			}

			Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
				DURATION_OPTIONS.forEach { minutes ->
					FilterChip(
						selected = durationMinutes == minutes,
						onClick = { applyDuration(minutes) },
						label = { Text("${minutes} min") },
						enabled = !isRunning && !isPaused
					)
				}
			}

			Text(
				text = displayTime,
				fontSize = 48.sp,
				fontWeight = FontWeight.Bold,
				color = timerColor
			)

			statusText?.let { text ->
				Text(text, color = statusColor, fontSize = 14.sp, fontWeight = FontWeight.Medium)
			}

			Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
				Button(
					onClick = {
						if (mode == TimerMode.COUNTDOWN) timerFinished = false
						isRunning = true
					},
					enabled = !isRunning && canStart
				) {
					Text("Démarrer")
				}
				OutlinedButton(
					onClick = { isRunning = false },
					enabled = isRunning
				) {
					Text("Pause")
				}
				OutlinedButton(onClick = { resetTimer() }) {
					Text("Réinitialiser")
				}
			}
		}
	}
}

@Composable
fun ScoreCard(
	teamName: String,
	score: Int,
	color: Color,
	onAdd: () -> Unit,
	onSubtract: () -> Unit
) {
	Card(
		modifier = Modifier.size(180.dp),
		colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer)
	) {
		Column(
			horizontalAlignment = Alignment.CenterHorizontally,
			verticalArrangement = Arrangement.Center,
			modifier = Modifier.fillMaxSize().padding(8.dp)
		) {
			Text(teamName, fontSize = 16.sp, color = color)
			Text("$score", fontSize = 36.sp, color = color)
			Spacer(Modifier.height(8.dp))
			Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
				Button(onClick = onSubtract, colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF90CAF9))) { Text("-1") }
				Button(onClick = onAdd, colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1976D2), contentColor = Color.White)) { Text("+1") }
			}
		}
	}
}


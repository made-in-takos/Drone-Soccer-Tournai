package com.example.drone_soccer_score

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel

class MainViewModel : ViewModel() {
	var redScore by mutableIntStateOf(0)
	var blackScore by mutableIntStateOf(0)
	var isConnected by mutableStateOf(false)
	var isConnecting by mutableStateOf(false)
	var connectFailed by mutableStateOf(false)
	var connectedDeviceAddress by mutableStateOf<String?>(null)

	fun updateScores(red: Int, black: Int) {
		redScore = red
		blackScore = black
	}

	fun onConnected(deviceAddress: String) {
		connectedDeviceAddress = deviceAddress
		isConnected = true
		isConnecting = false
		connectFailed = false
	}

	fun onConnectionFailed() {
		isConnected = false
		isConnecting = false
		connectFailed = true
	}

	fun onConnecting() {
		isConnecting = true
		connectFailed = false
	}

	fun onDisconnected() {
		isConnected = false
		isConnecting = false
		connectFailed = false
		connectedDeviceAddress = null
	}
}

package com.ionclaw.app.server

import android.app.Application
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.ionclaw.app.config.AppConfig
import com.ionclaw.app.network.NetworkInterfaces
import com.ionclaw.lib.IonClawPlatform
import com.ionclaw.lib.IonClawRuntime
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

// owns the native server lifecycle and exposes its state to compose
class ServerViewModel(application: Application) : AndroidViewModel(application) {
    val config = AppConfig(application)

    var isRunning by mutableStateOf(false)
        private set
    var isBusy by mutableStateOf(false)
        private set
    var port by mutableStateOf(0)
        private set
    var addresses by mutableStateOf<List<String>>(emptyList())
        private set
    var lastError by mutableStateOf<String?>(null)
        private set

    init {
        IonClawPlatform.register(application)
    }

    fun updateConfig(host: String, port: Int) {
        config.update(host, port)
    }

    fun start() {
        if (isBusy || isRunning) return

        if (config.port !in 1..65535) {
            lastError = "Invalid port ${config.port}: must be between 1 and 65535"
            return
        }

        isBusy = true
        lastError = null

        viewModelScope.launch {
            try {
                port = withContext(Dispatchers.IO) { boot(config.host, config.port) }
                isRunning = true
                addresses = NetworkInterfaces.localIPv4Addresses()
            } catch (error: Exception) {
                lastError = error.message
            }

            isBusy = false
        }
    }

    // creates the project skeleton on demand without starting the server
    fun initializeProject() {
        if (isBusy || isRunning) return

        isBusy = true
        lastError = null

        viewModelScope.launch {
            try {
                withContext(Dispatchers.IO) { IonClawRuntime.initializeProject(projectPath()) }
            } catch (error: Exception) {
                lastError = error.message
            }

            isBusy = false
        }
    }

    fun stop() {
        if (isBusy || !isRunning) return

        isBusy = true

        viewModelScope.launch {
            try {
                withContext(Dispatchers.IO) { IonClawRuntime.stopServer() }

                isRunning = false
                port = 0
                addresses = emptyList()
            } catch (error: Exception) {
                lastError = error.message
            }

            isBusy = false
        }
    }

    // initializes the project skeleton and boots the server off the main thread, returning the bound port
    private fun boot(host: String, port: Int): Int {
        val projectPath = projectPath()

        IonClawRuntime.initializeProject(projectPath)

        return IonClawRuntime.startServer(projectPath, host, port)
    }

    // writable project location inside the app sandbox, created on demand by the native init call
    private fun projectPath(): String {
        return File(getApplication<Application>().filesDir, "ionclaw/project").absolutePath
    }
}

package com.ionclaw.app.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material.icons.outlined.Dns
import androidx.compose.material.icons.outlined.FolderOpen
import androidx.compose.material.icons.outlined.Language
import androidx.compose.material.icons.outlined.Wifi
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CenterAlignedTopAppBar
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedCard
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ionclaw.app.R
import com.ionclaw.app.server.ServerViewModel
import com.ionclaw.app.ui.theme.BrandPrimary
import com.ionclaw.app.ui.theme.BrandSuccess
import com.ionclaw.app.ui.theme.BrandDanger
import com.ionclaw.app.ui.theme.CardBorder
import com.ionclaw.app.ui.theme.CardSurface
import com.ionclaw.app.ui.theme.HeaderBackground
import com.ionclaw.app.ui.theme.ScreenBackground
import kotlinx.coroutines.delay

private val ButtonShape = RoundedCornerShape(12.dp)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ServerScreen(viewModel: ServerViewModel, onOpenPanel: () -> Unit, modifier: Modifier = Modifier) {
    Scaffold(
        modifier = modifier,
        containerColor = ScreenBackground,
        topBar = {
            CenterAlignedTopAppBar(
                title = {
                    Image(
                        painter = painterResource(R.drawable.logo_dark),
                        contentDescription = "IonClaw",
                        modifier = Modifier.height(36.dp)
                    )
                },
                colors = TopAppBarDefaults.centerAlignedTopAppBarColors(containerColor = HeaderBackground)
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 24.dp, vertical = 40.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(28.dp)
        ) {
            ServerStatusView(isRunning = viewModel.isRunning)

            ServerCard(viewModel)

            ActionButtons(viewModel, onOpenPanel)

            if (viewModel.isRunning && viewModel.addresses.isNotEmpty()) {
                NetworkCard(addresses = viewModel.addresses, port = viewModel.port)
            }

            viewModel.lastError?.let { error ->
                Text(
                    text = error,
                    style = MaterialTheme.typography.bodySmall,
                    color = BrandDanger
                )
            }
        }
    }
}

@Composable
private fun ServerCard(viewModel: ServerViewModel) {
    val config = viewModel.config
    val fieldsEnabled = !viewModel.isRunning && !viewModel.isBusy

    SectionCard(title = "Server", icon = { Icon(Icons.Outlined.Dns, null, tint = BrandPrimary, modifier = Modifier.size(18.dp)) }, iconTint = BrandPrimary) {
        Row {
            OutlinedTextField(
                value = config.host,
                onValueChange = { viewModel.updateConfig(it, config.port) },
                label = { Text("Host") },
                singleLine = true,
                enabled = fieldsEnabled,
                modifier = Modifier.weight(2f)
            )

            Spacer(Modifier.width(12.dp))

            OutlinedTextField(
                value = config.port.toString(),
                onValueChange = { input ->
                    viewModel.updateConfig(config.host, input.filter(Char::isDigit).take(5).toIntOrNull() ?: 0)
                },
                label = { Text("Port") },
                singleLine = true,
                enabled = fieldsEnabled,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                modifier = Modifier.weight(1f)
            )
        }
    }
}

@Composable
private fun ActionButtons(viewModel: ServerViewModel, onOpenPanel: () -> Unit) {
    if (viewModel.isBusy) {
        CircularProgressIndicator(
            color = BrandPrimary,
            strokeWidth = 2.5.dp,
            modifier = Modifier.size(32.dp)
        )
        return
    }

    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        if (viewModel.isRunning) {
            OutlinedAction("Stop Server", Icons.Filled.Stop, BrandDanger, viewModel::stop)
            OutlinedAction("Open Panel", Icons.Outlined.Language, BrandPrimary, onOpenPanel)
        } else {
            Button(
                onClick = viewModel::start,
                shape = ButtonShape,
                colors = ButtonDefaults.buttonColors(containerColor = BrandPrimary),
                modifier = Modifier.width(240.dp).height(52.dp)
            ) {
                Icon(Icons.Filled.PlayArrow, null)
                Text("Start Server", fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(start = 8.dp))
            }

            OutlinedAction("Initialize Project", Icons.Outlined.FolderOpen, BrandPrimary, viewModel::initializeProject)
        }
    }
}

@Composable
private fun OutlinedAction(label: String, icon: androidx.compose.ui.graphics.vector.ImageVector, color: Color, onClick: () -> Unit) {
    OutlinedButton(
        onClick = onClick,
        shape = ButtonShape,
        border = BorderStroke(1.5.dp, color),
        colors = ButtonDefaults.outlinedButtonColors(contentColor = color),
        modifier = Modifier.width(240.dp).height(52.dp)
    ) {
        Icon(icon, null)
        Text(label, fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(start = 8.dp))
    }
}

@Composable
private fun NetworkCard(addresses: List<String>, port: Int) {
    val context = LocalContext.current
    var copiedAddress by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(copiedAddress) {
        if (copiedAddress != null) {
            delay(2000)
            copiedAddress = null
        }
    }

    SectionCard(title = "Network", icon = { Icon(Icons.Outlined.Wifi, null, tint = BrandSuccess, modifier = Modifier.size(18.dp)) }, iconTint = BrandSuccess) {
        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
            addresses.forEach { address ->
                val url = "http://$address:$port"
                val copied = copiedAddress == address

                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable {
                            copyToClipboard(context, url)
                            copiedAddress = address
                        }
                        .padding(vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = url,
                        style = MaterialTheme.typography.bodyMedium,
                        color = Color(0xFF616161),
                        modifier = Modifier.weight(1f)
                    )

                    Icon(
                        imageVector = if (copied) Icons.Filled.Check else Icons.Filled.ContentCopy,
                        contentDescription = "Copy",
                        tint = if (copied) BrandSuccess else BrandPrimary,
                        modifier = Modifier.size(18.dp)
                    )
                }
            }
        }
    }
}

@Composable
private fun SectionCard(title: String, icon: @Composable () -> Unit, iconTint: Color, content: @Composable () -> Unit) {
    OutlinedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.outlinedCardColors(containerColor = CardSurface),
        border = BorderStroke(1.dp, CardBorder)
    ) {
        Column(modifier = Modifier.padding(20.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    modifier = Modifier
                        .size(32.dp)
                        .clip(RoundedCornerShape(8.dp))
                        .background(iconTint.copy(alpha = 0.1f)),
                    contentAlignment = Alignment.Center
                ) {
                    icon()
                }

                Text(
                    text = title,
                    fontSize = 16.sp,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.onSurface,
                    modifier = Modifier.padding(start = 12.dp)
                )
            }

            Box(modifier = Modifier.padding(top = 20.dp)) {
                content()
            }
        }
    }
}

private fun copyToClipboard(context: Context, value: String) {
    val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
    clipboard.setPrimaryClip(ClipData.newPlainText("IonClaw", value))
}

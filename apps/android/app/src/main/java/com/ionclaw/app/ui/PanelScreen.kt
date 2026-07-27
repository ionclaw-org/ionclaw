package com.ionclaw.app.ui

import android.annotation.SuppressLint
import android.view.ViewGroup
import android.webkit.PermissionRequest
import android.webkit.WebChromeClient
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.core.view.doOnLayout
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.outlined.Home
import androidx.compose.material3.CenterAlignedTopAppBar
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import com.ionclaw.app.R
import com.ionclaw.app.ui.theme.HeaderBackground

// embedded web panel served by the local server
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PanelScreen(url: String, onBack: () -> Unit) {
    var isLoading by remember { mutableStateOf(true) }
    var webView by remember { mutableStateOf<WebView?>(null) }

    BackHandler(onBack = onBack)

    Scaffold(
        topBar = {
            CenterAlignedTopAppBar(
                title = {
                    Image(
                        painter = painterResource(R.drawable.logo_dark),
                        contentDescription = "IonClaw",
                        modifier = Modifier.height(36.dp)
                    )
                },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back", tint = Color.White)
                    }
                },
                actions = {
                    IconButton(onClick = { webView?.loadUrl(url) }) {
                        Icon(Icons.Outlined.Home, contentDescription = "Home", tint = Color.White)
                    }
                },
                colors = TopAppBarDefaults.centerAlignedTopAppBarColors(containerColor = HeaderBackground)
            )
        }
    ) { padding ->
        Box(modifier = Modifier.fillMaxSize().padding(padding)) {
            WebPanel(url = url, onCreated = { webView = it }, onFinished = { isLoading = false })

            if (isLoading) {
                CircularProgressIndicator(modifier = Modifier.align(Alignment.Center))
            }
        }
    }
}

@SuppressLint("SetJavaScriptEnabled")
@Composable
private fun WebPanel(url: String, onCreated: (WebView) -> Unit, onFinished: () -> Unit) {
    AndroidView(
        modifier = Modifier.fillMaxSize(),
        factory = { context ->
            WebView(context).apply {
                layoutParams = ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT
                )

                settings.javaScriptEnabled = true
                settings.domStorageEnabled = true
                settings.databaseEnabled = true
                settings.mediaPlaybackRequiresUserGesture = false
                settings.useWideViewPort = true
                settings.loadWithOverviewMode = true

                webViewClient = object : WebViewClient() {
                    override fun onPageFinished(view: WebView, url: String) = onFinished()
                }

                // the panel may request the microphone for voice features served by the local server
                webChromeClient = object : WebChromeClient() {
                    override fun onPermissionRequest(request: PermissionRequest) {
                        val audio = request.resources.filter { it == PermissionRequest.RESOURCE_AUDIO_CAPTURE }
                        if (audio.isNotEmpty()) request.grant(audio.toTypedArray()) else request.deny()
                    }
                }

                onCreated(this)

                // load only after the view has a real size, otherwise the page resolves its viewport
                // (vh/dvh) as zero and the whole app collapses to a blank height
                doOnLayout { loadUrl(url) }
            }
        },
        // release native webview resources when the panel leaves composition
        onRelease = { it.destroy() }
    )
}

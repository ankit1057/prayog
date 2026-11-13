package com.ankit1057.prayog.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// Light theme per org policy (CLAUDE.md). A dark palette is defined but only
// used when the system is in dark mode — the light palette is the canonical one.
private val LightColors = lightColorScheme(
    primary = Color(0xFF2E5EAA),
    onPrimary = Color.White,
    secondary = Color(0xFF5B8FB9),
    background = Color(0xFFF7F7F7),
    surface = Color.White,
    onSurface = Color(0xFF111111),
)

private val DarkColors = darkColorScheme(
    primary = Color(0xFF7FA7D4),
    onPrimary = Color.Black,
    secondary = Color(0xFFBFD7EA),
    background = Color(0xFF121212),
    surface = Color(0xFF1C1C1C),
    onSurface = Color(0xFFEEEEEE),
)

@Composable
fun PrayogTheme(content: @Composable () -> Unit) {
    val colors = if (isSystemInDarkTheme()) DarkColors else LightColors
    MaterialTheme(colorScheme = colors, content = content)
}

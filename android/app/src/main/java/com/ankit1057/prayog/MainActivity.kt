package com.ankit1057.prayog

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import com.ankit1057.prayog.ui.PrayogApp
import com.ankit1057.prayog.ui.PrayogTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            PrayogTheme {
                PrayogApp()
            }
        }
    }
}

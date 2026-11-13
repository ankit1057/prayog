package com.ankit1057.prayog.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.ankit1057.prayog.Report
import com.ankit1057.prayog.ReportStore
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PrayogApp() {
    var reports by remember { mutableStateOf(emptyList<Report>()) }
    var selected by remember { mutableStateOf<Report?>(null) }
    var loading by remember { mutableStateOf(true) }

    LaunchedEffect(Unit) {
        reports = withContext(Dispatchers.IO) { ReportStore.loadAll() }
        loading = false
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(if (selected == null) "prayog — experiments" else selected!!.experiment) },
                navigationIcon = {
                    if (selected != null) {
                        TextButton(onClick = { selected = null }) { Text("Back") }
                    }
                }
            )
        }
    ) { inner ->
        Box(Modifier.padding(inner).fillMaxSize()) {
            when {
                loading -> CircularProgressIndicator(Modifier.align(Alignment.Center))
                selected != null -> ReportDetail(selected!!)
                reports.isEmpty() -> EmptyState()
                else -> ReportList(reports) { selected = it }
            }
        }
    }
}

@Composable
private fun EmptyState() {
    Column(
        Modifier.fillMaxSize().padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Text("No reports found", style = MaterialTheme.typography.titleMedium)
        Spacer(Modifier.height(8.dp))
        Text(
            "Run an experiment from Termux:\n\n    PRAYOG_HOME=${ReportStore.DEFAULT_PATH.removeSuffix("/reports")} \\\n      prayog run experiments/001-sipllm-coldstart.toml",
            style = MaterialTheme.typography.bodySmall,
            fontFamily = FontFamily.Monospace,
        )
    }
}

@Composable
private fun ReportList(reports: List<Report>, onPick: (Report) -> Unit) {
    LazyColumn(Modifier.fillMaxSize().padding(horizontal = 12.dp)) {
        items(reports, key = { it.path }) { r ->
            Card(
                modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp),
                onClick = { onPick(r) }
            ) {
                Column(Modifier.padding(12.dp)) {
                    Text(r.experiment, style = MaterialTheme.typography.titleMedium)
                    Text(r.startedAt, style = MaterialTheme.typography.bodySmall)
                    Spacer(Modifier.height(4.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                        Chip("wall ${"%.3f".format(r.wallSec)}s")
                        Chip("rss ${"%.1f".format(r.maxRssMb)}MB")
                        Chip("exit ${r.exitCode}", ok = r.exitCode == 0)
                    }
                }
            }
        }
    }
}

@Composable
private fun Chip(label: String, ok: Boolean = true) {
    AssistChip(
        onClick = {},
        label = { Text(label, fontFamily = FontFamily.Monospace) },
        colors = AssistChipDefaults.assistChipColors(
            labelColor = if (ok) MaterialTheme.colorScheme.onSurface
                         else MaterialTheme.colorScheme.error
        ),
    )
}

@Composable
private fun ReportDetail(r: Report) {
    Column(Modifier.fillMaxSize().padding(16.dp)) {
        Text(r.experiment, style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        Text(r.startedAt, style = MaterialTheme.typography.bodySmall)
        Spacer(Modifier.height(12.dp))
        Metric("wall", "%.3f s".format(r.wallSec))
        Metric("maxrss", "%.1f MB".format(r.maxRssMb))
        Metric("exit", r.exitCode.toString())
        if (r.signal != 0) Metric("signal", r.signal.toString())
        Spacer(Modifier.height(16.dp))
        Text("cmd", style = MaterialTheme.typography.labelLarge)
        Text(r.cmd, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
        if (r.argv.size > 1) {
            Spacer(Modifier.height(8.dp))
            Text("argv[1..]", style = MaterialTheme.typography.labelLarge)
            Text(r.argv.drop(1).joinToString(" "), fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
        }
        if (r.stdoutSample.isNotEmpty()) {
            Spacer(Modifier.height(12.dp))
            Text("stdout sample", style = MaterialTheme.typography.labelLarge)
            Text(r.stdoutSample.take(1024), fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Composable
private fun Metric(k: String, v: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
        Text(k, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
        Text(v, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodyMedium)
    }
}

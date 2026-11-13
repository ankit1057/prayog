package com.ankit1057.prayog.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.ankit1057.prayog.Inference
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
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(r.experiment, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
                        if (r.isInference) Badge(containerColor = MaterialTheme.colorScheme.primary) {
                            Text("inference", style = MaterialTheme.typography.labelSmall)
                        }
                    }
                    Text(r.startedAt, style = MaterialTheme.typography.bodySmall)
                    Spacer(Modifier.height(6.dp))
                    FlowChips {
                        if (r.isInference && r.inference != null) {
                            Chip("${"%.2f".format(r.inference.decodeTokS)} tok/s")
                            Chip("ttft ${"%.0f".format(r.inference.ttftS * 1000)}ms")
                            Chip("gen ${r.inference.generated}")
                        }
                        Chip("wall ${"%.2f".format(r.wallSec)}s")
                        Chip("rss ${"%.0f".format(r.maxRssMb)}MB")
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

// Simple wrap-row for chips. We avoid accompanist/FlowRow for a walking skeleton;
// if labels overflow on narrow screens, add compose.foundation.layout.FlowRow.
@Composable
private fun FlowChips(content: @Composable () -> Unit) {
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) { content() }
}

@Composable
private fun ReportDetail(r: Report) {
    androidx.compose.foundation.rememberScrollState().let { scroll ->
        Column(
            Modifier.fillMaxSize().padding(16.dp)
                .verticalScroll(scroll)
        ) {
            Text(r.experiment, style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
            Text(r.startedAt, style = MaterialTheme.typography.bodySmall)
            Spacer(Modifier.height(16.dp))

            if (r.inference != null) {
                SectionHeader("inference")
                InferenceGrid(r.inference)
                Spacer(Modifier.height(16.dp))
            }

            SectionHeader("process")
            Metric("wall",    "%.3f s".format(r.wallSec))
            Metric("maxrss",  "%.1f MB".format(r.maxRssMb))
            Metric("exit",    r.exitCode.toString())
            if (r.signal != 0) Metric("signal", r.signal.toString())

            Spacer(Modifier.height(16.dp))
            SectionHeader("cmd")
            Text(r.cmd, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
            if (r.argv.size > 1) {
                Spacer(Modifier.height(4.dp))
                Text(r.argv.drop(1).joinToString(" "),
                    fontFamily = FontFamily.Monospace,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f))
            }

            if (r.stdoutSample.isNotEmpty()) {
                Spacer(Modifier.height(16.dp))
                SectionHeader("stdout sample")
                Text(r.stdoutSample.take(2048),
                    fontFamily = FontFamily.Monospace,
                    style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

@Composable
private fun SectionHeader(text: String) {
    Text(text,
        style = MaterialTheme.typography.labelLarge,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier.padding(bottom = 4.dp))
}

@Composable
private fun InferenceGrid(inf: Inference) {
    Metric("decode",   "%.2f tok/s".format(inf.decodeTokS))
    Metric("prefill",  "%.2f tok/s".format(inf.prefillTokS))
    Metric("ttft",     "%.0f ms".format(inf.ttftS * 1000))
    Metric("prompt",   "${inf.promptTokens} tok")
    Metric("generated","${inf.generated} tok")
    Metric("weights",  "%.1f MB resident".format(inf.weightsResidentMb))
    Metric("kv cache", "%.1f MB".format(inf.kvCacheMb))
    Metric("streamed", "%.1f MB from disk".format(inf.streamedMb))
    Metric("context",  "${inf.contextUsed} / ${inf.contextMax}")
}

@Composable
private fun Metric(k: String, v: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
        Text(k, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
        Text(v, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodyMedium)
    }
}

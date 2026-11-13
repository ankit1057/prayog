package com.ankit1057.prayog.ui

import androidx.compose.foundation.background
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
import com.ankit1057.prayog.Agree
import com.ankit1057.prayog.AgreeBatch
import com.ankit1057.prayog.AgreeQuant
import com.ankit1057.prayog.BatchRow
import com.ankit1057.prayog.Entry
import com.ankit1057.prayog.Inference
import com.ankit1057.prayog.Report
import com.ankit1057.prayog.ReportStore
import com.ankit1057.prayog.Sweep
import com.ankit1057.prayog.SweepRow
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PrayogApp() {
    var entries by remember { mutableStateOf(emptyList<Entry>()) }
    var selected by remember { mutableStateOf<Entry?>(null) }
    var loading by remember { mutableStateOf(true) }

    LaunchedEffect(Unit) {
        entries = withContext(Dispatchers.IO) { ReportStore.loadAll() }
        loading = false
    }

    val title = when (val s = selected) {
        null -> "prayog — experiments"
        is Entry.Run   -> s.report.experiment
        is Entry.Of    -> "sweep: ${s.sweep.stem}"
        is Entry.Ag    -> "agree: ${s.agree.stem}"
        is Entry.Batch -> "batch: ${s.batch.stem}"
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(title) },
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
                selected is Entry.Run   -> ReportDetail((selected as Entry.Run).report)
                selected is Entry.Of    -> SweepDetail((selected as Entry.Of).sweep)
                selected is Entry.Ag    -> AgreeDetail((selected as Entry.Ag).agree)
                selected is Entry.Batch -> BatchDetail((selected as Entry.Batch).batch)
                entries.isEmpty()       -> EmptyState()
                else -> EntryList(entries) { selected = it }
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
private fun EntryList(entries: List<Entry>, onPick: (Entry) -> Unit) {
    LazyColumn(Modifier.fillMaxSize().padding(horizontal = 12.dp)) {
        items(entries, key = { keyFor(it) }) { e ->
            when (e) {
                is Entry.Run   -> RunCard(e.report)  { onPick(e) }
                is Entry.Of    -> SweepCard(e.sweep) { onPick(e) }
                is Entry.Ag    -> AgreeCard(e.agree) { onPick(e) }
                is Entry.Batch -> BatchCard(e.batch) { onPick(e) }
            }
        }
    }
}

private fun keyFor(e: Entry): String = when (e) {
    is Entry.Run   -> e.report.path
    is Entry.Of    -> e.sweep.path
    is Entry.Ag    -> e.agree.path
    is Entry.Batch -> e.batch.path
}

@Composable
private fun RunCard(r: Report, onClick: () -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp),
        onClick = onClick,
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

@Composable
private fun SweepCard(s: Sweep, onClick: () -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp),
        onClick = onClick,
    ) {
        Column(Modifier.padding(12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("sweep: ${s.stem}",
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.weight(1f))
                Badge(containerColor = MaterialTheme.colorScheme.secondary) {
                    Text("${s.runs.size} quants", style = MaterialTheme.typography.labelSmall)
                }
            }
            Text(s.startedAt, style = MaterialTheme.typography.bodySmall)
            Spacer(Modifier.height(6.dp))
            FlowChips {
                Chip("n=${s.n}")
                Chip("threads=${s.threads}")
                val fastest = s.runs.maxByOrNull { it.inference.decodeTokS }
                if (fastest != null) {
                    Chip("${fastest.quant}: ${"%.2f".format(fastest.inference.decodeTokS)} tok/s")
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
private fun AgreeCard(a: Agree, onClick: () -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp),
        onClick = onClick,
    ) {
        Column(Modifier.padding(12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                val q = a.quants.joinToString(" vs ") { it.quant }
                Text("agree: ${a.stem}  ($q)",
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.weight(1f))
                val ok = a.agreement.byteRate >= 0.5
                Badge(containerColor = if (ok) MaterialTheme.colorScheme.primary
                                       else    MaterialTheme.colorScheme.error) {
                    Text("${"%.0f".format(a.agreement.byteRate * 100)}%",
                         style = MaterialTheme.typography.labelSmall)
                }
            }
            Text(a.startedAt, style = MaterialTheme.typography.bodySmall)
            Spacer(Modifier.height(6.dp))
            FlowChips {
                Chip("n=${a.n}")
                Chip("seed=${a.seed}")
                Chip("${a.agreement.commonChars} chars common")
                Chip("${a.agreement.commonWords} words common")
            }
        }
    }
}

@Composable
private fun AgreeDetail(a: Agree) {
    val scroll = androidx.compose.foundation.rememberScrollState()
    Column(Modifier.fillMaxSize().padding(16.dp).verticalScroll(scroll)) {
        val q = a.quants.joinToString(" vs ") { it.quant }
        Text("agree: ${a.stem}  ($q)",
            style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        Text(a.startedAt, style = MaterialTheme.typography.bodySmall)
        Spacer(Modifier.height(12.dp))

        SectionHeader("config")
        Metric("prompt",  "\"${a.prompt}\"")
        Metric("tokens",  a.n.toString())
        Metric("seed",    a.seed.toString())
        Metric("threads", a.threads.toString())

        Spacer(Modifier.height(16.dp))
        SectionHeader("agreement")
        Metric("bytes",   "${a.agreement.commonBytes} common  (${"%.1f".format(a.agreement.byteRate * 100)}%)")
        Metric("chars",   "${a.agreement.commonChars} common")
        Metric("words",   "${a.agreement.commonWords} common  (${"%.1f".format(a.agreement.wordRate * 100)}%)")

        if (a.quants.size == 2) {
            Spacer(Modifier.height(16.dp))
            SectionHeader("divergence")
            val cut = a.agreement.commonBytes
            DivergenceView(
                prefix = a.quants[0].generated.substring(0, cut.coerceAtMost(a.quants[0].generated.length)),
                a = a.quants[0],
                b = a.quants[1],
                cut = cut,
            )
        }
    }
}

@Composable
private fun DivergenceView(prefix: String, a: AgreeQuant, b: AgreeQuant, cut: Int) {
    Column {
        Text("common prefix:",
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f))
        Text(prefix.ifEmpty { "(none — divergence from first token)" },
            fontFamily = FontFamily.Monospace,
            style = MaterialTheme.typography.bodySmall)
        Spacer(Modifier.height(10.dp))
        QuantTail(label = a.quant, generated = a.generated, cut = cut,
                  tint = MaterialTheme.colorScheme.primary)
        Spacer(Modifier.height(8.dp))
        QuantTail(label = b.quant, generated = b.generated, cut = cut,
                  tint = MaterialTheme.colorScheme.secondary)
    }
}

@Composable
private fun QuantTail(label: String, generated: String, cut: Int, tint: androidx.compose.ui.graphics.Color) {
    val tail = if (cut < generated.length) generated.substring(cut) else ""
    Column {
        Text(label,
            style = MaterialTheme.typography.labelLarge,
            color = tint)
        Text(tail.ifEmpty { "(agreed through end)" },
            fontFamily = FontFamily.Monospace,
            style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun BatchCard(b: AgreeBatch, onClick: () -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp),
        onClick = onClick,
    ) {
        Column(Modifier.padding(12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("batch: ${b.stem}  (${b.quantA} vs ${b.quantB})",
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.weight(1f))
                Badge(containerColor = MaterialTheme.colorScheme.tertiary) {
                    Text("${b.stats.count} prompts",
                         style = MaterialTheme.typography.labelSmall)
                }
            }
            Text(b.startedAt, style = MaterialTheme.typography.bodySmall)
            Spacer(Modifier.height(6.dp))
            FlowChips {
                Chip("mean ${"%.0f".format(b.stats.meanByteRate * 100)}%")
                Chip("median ${"%.0f".format(b.stats.medianByteRate * 100)}%")
                Chip("perfect ${b.stats.perfectCount}/${b.stats.count}")
                Chip("n=${b.n}")
            }
        }
    }
}

@Composable
private fun BatchDetail(b: AgreeBatch) {
    val scroll = androidx.compose.foundation.rememberScrollState()
    Column(Modifier.fillMaxSize().padding(16.dp).verticalScroll(scroll)) {
        Text("batch: ${b.stem}  (${b.quantA} vs ${b.quantB})",
            style = MaterialTheme.typography.headlineSmall,
            fontWeight = FontWeight.Bold)
        Text(b.startedAt, style = MaterialTheme.typography.bodySmall)
        Spacer(Modifier.height(12.dp))

        SectionHeader("config")
        Metric("prompts file", b.promptsPath)
        Metric("count",   b.stats.count.toString())
        Metric("tokens",  b.n.toString())
        Metric("seed",    b.seed.toString())
        Metric("threads", b.threads.toString())

        Spacer(Modifier.height(16.dp))
        SectionHeader("distribution (byte rate)")
        Metric("mean",    "%.1f%%".format(b.stats.meanByteRate * 100))
        Metric("median",  "%.1f%%".format(b.stats.medianByteRate * 100))
        Metric("p10",     "%.1f%%".format(b.stats.p10ByteRate * 100))
        Metric("p90",     "%.1f%%".format(b.stats.p90ByteRate * 100))
        Metric("min/max", "%.1f%% / %.1f%%".format(b.stats.minByteRate * 100, b.stats.maxByteRate * 100))
        Metric("perfect", "${b.stats.perfectCount} / ${b.stats.count}")

        Spacer(Modifier.height(16.dp))
        SectionHeader("histogram")
        AgreementHistogram(b.rows)

        Spacer(Modifier.height(16.dp))
        SectionHeader("per prompt")
        b.rows.forEach { r -> BatchRowItem(r) }
    }
}

@Composable
private fun AgreementHistogram(rows: List<BatchRow>) {
    // 10 buckets, 0-10%, 10-20%, ..., 90-100%. Simple bar chart with
    // hand-drawn boxes; no third-party chart lib for walking skeleton.
    val buckets = IntArray(10)
    rows.forEach {
        val idx = ((it.byteRate * 10).toInt()).coerceIn(0, 9)
        buckets[idx]++
    }
    val max = (buckets.max().takeIf { it > 0 } ?: 1).toFloat()
    Column(Modifier.fillMaxWidth()) {
        buckets.forEachIndexed { i, count ->
            Row(
                Modifier.fillMaxWidth().padding(vertical = 2.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text("${i*10}-${(i+1)*10}%",
                    style = MaterialTheme.typography.labelSmall,
                    fontFamily = FontFamily.Monospace,
                    modifier = Modifier.width(60.dp))
                Box(
                    Modifier
                        .weight(count / max)
                        .height(14.dp)
                        .background(MaterialTheme.colorScheme.primary)
                )
                if (count > 0 && count / max < 0.95f) {
                    Spacer(Modifier.weight(1f - count / max))
                }
                Text("  $count",
                    style = MaterialTheme.typography.labelSmall,
                    fontFamily = FontFamily.Monospace)
            }
        }
    }
}

@Composable
private fun BatchRowItem(r: BatchRow) {
    val pct = (r.byteRate * 100).toInt()
    val tint = when {
        pct >= 90 -> MaterialTheme.colorScheme.primary
        pct >= 50 -> MaterialTheme.colorScheme.secondary
        else      -> MaterialTheme.colorScheme.error
    }
    Column(Modifier.fillMaxWidth().padding(vertical = 6.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(r.prompt,
                fontFamily = FontFamily.Monospace,
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.weight(1f))
            Text("$pct%",
                style = MaterialTheme.typography.labelMedium,
                color = tint,
                fontFamily = FontFamily.Monospace)
        }
        if (r.commonBytes < kotlin.math.max(r.lenA, r.lenB) && r.snippetA.isNotEmpty()) {
            Text("  Δa: ${r.snippetA}",
                fontFamily = FontFamily.Monospace,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.55f))
            Text("  Δb: ${r.snippetB}",
                fontFamily = FontFamily.Monospace,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.55f))
        }
    }
}

@Composable
private fun SweepDetail(s: Sweep) {
    val scroll = androidx.compose.foundation.rememberScrollState()
    Column(Modifier.fillMaxSize().padding(16.dp).verticalScroll(scroll)) {
        Text("sweep: ${s.stem}", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        Text(s.startedAt, style = MaterialTheme.typography.bodySmall)
        Spacer(Modifier.height(12.dp))
        SectionHeader("config")
        Metric("prompt",  "\"${s.prompt}\"")
        Metric("tokens",  s.n.toString())
        Metric("threads", s.threads.toString())
        Spacer(Modifier.height(16.dp))
        SectionHeader("matrix")
        SweepMatrix(s.runs)
        Spacer(Modifier.height(16.dp))
        SectionHeader("verdict")
        val bestDecode = s.runs.maxByOrNull { it.inference.decodeTokS }
        val lightest   = s.runs.minByOrNull { it.maxRssMb }
        val fastestTtft= s.runs.minByOrNull { it.inference.ttftS }
        if (bestDecode != null)  Metric("best decode",  "${bestDecode.quant}  (${"%.2f".format(bestDecode.inference.decodeTokS)} tok/s)")
        if (lightest != null)    Metric("lightest",     "${lightest.quant}  (${"%.0f".format(lightest.maxRssMb)} MB peak RSS)")
        if (fastestTtft != null) Metric("fastest TTFT", "${fastestTtft.quant}  (${"%.0f".format(fastestTtft.inference.ttftS * 1000)} ms)")
    }
}

@Composable
private fun SweepMatrix(rows: List<SweepRow>) {
    // Hand-rolled table — 6 columns (quant, size, decode, prefill, TTFT, RSS).
    // Not using a real DataTable component to stay in Compose Material3 stock.
    val headers = listOf("quant", "size MB", "decode t/s", "prefill t/s", "TTFT ms", "RSS MB")
    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
            headers.forEach { h ->
                Text(h,
                    style = MaterialTheme.typography.labelSmall,
                    fontFamily = FontFamily.Monospace,
                    modifier = Modifier.weight(1f))
            }
        }
        HorizontalDivider()
        rows.forEach { r ->
            Row(Modifier.fillMaxWidth().padding(vertical = 6.dp)) {
                listOf(
                    r.quant,
                    "%.0f".format(r.modelSizeMb),
                    "%.2f".format(r.inference.decodeTokS),
                    "%.2f".format(r.inference.prefillTokS),
                    "%.0f".format(r.inference.ttftS * 1000),
                    "%.0f".format(r.maxRssMb),
                ).forEach { v ->
                    Text(v,
                        style = MaterialTheme.typography.bodySmall,
                        fontFamily = FontFamily.Monospace,
                        modifier = Modifier.weight(1f))
                }
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

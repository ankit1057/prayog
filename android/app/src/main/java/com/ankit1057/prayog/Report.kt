package com.ankit1057.prayog

import org.json.JSONObject
import java.io.File

/**
 * Mirrors of the JSON schemas emitted by engine/src/prayog.cpp:
 *   - prayog.run/1, prayog.run/2  → [Report]
 *   - prayog.sweep/1              → [Sweep]
 *
 * Schema bumps are backwards-additive — v1 readers must still parse v2 output
 * (unknown fields are dropped). Keep this in sync with prayog.cpp:kSchema.
 */
data class Inference(
    val decodeTokS: Double,
    val prefillTokS: Double,
    val ttftS: Double,
    val promptTokens: Int,
    val generated: Int,
    val weightsResidentMb: Double,
    val kvCacheMb: Double,
    val streamedMb: Double,
    val contextUsed: Int,
    val contextMax: Int,
)

data class SweepRow(
    val quant: String,
    val modelPath: String,
    val modelSizeMb: Double,
    val runReportPath: String,
    val exitCode: Int,
    val wallSec: Double,
    val maxRssMb: Double,
    val inference: Inference,
)

data class AgreeQuant(
    val quant: String,
    val modelPath: String,
    val generated: String,
    val runReportPath: String,
    val wallSec: Double,
    val decodeTokS: Double,
)

data class Agreement(
    val commonBytes: Int,
    val commonChars: Int,
    val commonWords: Int,
    val byteRate: Double,
    val wordRate: Double,
)

data class Agree(
    val path: String,
    val startedAt: String,
    val stem: String,
    val prompt: String,
    val n: Int,
    val seed: Int,
    val threads: Int,
    val engine: String,
    val agreement: Agreement,
    val quants: List<AgreeQuant>,
) {
    companion object {
        fun parse(file: File): Agree? = try {
            val root = JSONObject(file.readText())
            if (!root.optString("schema").startsWith("prayog.agree/")) return null
            val agg = root.optJSONObject("agreement") ?: return null
            val quantsArr = root.optJSONArray("quants") ?: return null
            val qs = buildList {
                for (i in 0 until quantsArr.length()) {
                    val q = quantsArr.getJSONObject(i)
                    add(AgreeQuant(
                        quant        = q.optString("quant"),
                        modelPath    = q.optString("model"),
                        generated    = q.optString("generated"),
                        runReportPath= q.optString("run_report"),
                        wallSec      = q.optDouble("wall_sec", 0.0),
                        decodeTokS   = q.optDouble("decode_tok_s", 0.0),
                    ))
                }
            }
            Agree(
                path = file.absolutePath,
                startedAt = root.optString("started_at"),
                stem   = root.optString("stem"),
                prompt = root.optString("prompt"),
                n = root.optInt("n", 0),
                seed = root.optInt("seed", 0),
                threads = root.optInt("threads", 0),
                engine = root.optString("engine"),
                agreement = Agreement(
                    commonBytes = agg.optInt("common_bytes", 0),
                    commonChars = agg.optInt("common_chars", 0),
                    commonWords = agg.optInt("common_words", 0),
                    byteRate    = agg.optDouble("byte_rate", 0.0),
                    wordRate    = agg.optDouble("word_rate", 0.0),
                ),
                quants = qs,
            )
        } catch (t: Throwable) {
            null
        }
    }
}

data class BatchRow(
    val prompt: String,
    val commonBytes: Int,
    val commonChars: Int,
    val commonWords: Int,
    val lenA: Int,
    val lenB: Int,
    val wordsA: Int,
    val wordsB: Int,
    val byteRate: Double,
    val wordRate: Double,
    val wallA: Double,
    val wallB: Double,
    val snippetA: String,
    val snippetB: String,
)

data class BatchStats(
    val count: Int,
    val perfectCount: Int,
    val meanByteRate: Double,
    val meanWordRate: Double,
    val medianByteRate: Double,
    val p10ByteRate: Double,
    val p90ByteRate: Double,
    val minByteRate: Double,
    val maxByteRate: Double,
)

data class AgreeBatch(
    val path: String,
    val startedAt: String,
    val stem: String,
    val quantA: String,
    val quantB: String,
    val promptsPath: String,
    val n: Int,
    val seed: Int,
    val threads: Int,
    val stats: BatchStats,
    val rows: List<BatchRow>,
) {
    companion object {
        fun parse(file: File): AgreeBatch? = try {
            val root = JSONObject(file.readText())
            if (!root.optString("schema").startsWith("prayog.agree-batch/")) return null
            val stats = root.optJSONObject("stats") ?: return null
            val rowsArr = root.optJSONArray("rows") ?: return null
            val rows = buildList {
                for (i in 0 until rowsArr.length()) {
                    val r = rowsArr.getJSONObject(i)
                    add(BatchRow(
                        prompt      = r.optString("prompt"),
                        commonBytes = r.optInt("common_bytes", 0),
                        commonChars = r.optInt("common_chars", 0),
                        commonWords = r.optInt("common_words", 0),
                        lenA        = r.optInt("len_a", 0),
                        lenB        = r.optInt("len_b", 0),
                        wordsA      = r.optInt("words_a", 0),
                        wordsB      = r.optInt("words_b", 0),
                        byteRate    = r.optDouble("byte_rate", 0.0),
                        wordRate    = r.optDouble("word_rate", 0.0),
                        wallA       = r.optDouble("wall_a", 0.0),
                        wallB       = r.optDouble("wall_b", 0.0),
                        snippetA    = r.optString("snippet_a"),
                        snippetB    = r.optString("snippet_b"),
                    ))
                }
            }
            AgreeBatch(
                path = file.absolutePath,
                startedAt = root.optString("started_at"),
                stem   = root.optString("stem"),
                quantA = root.optString("quant_a"),
                quantB = root.optString("quant_b"),
                promptsPath = root.optString("prompts_path"),
                n = root.optInt("n", 0),
                seed = root.optInt("seed", 0),
                threads = root.optInt("threads", 0),
                stats = BatchStats(
                    count         = stats.optInt("count", 0),
                    perfectCount  = stats.optInt("perfect_count", 0),
                    meanByteRate  = stats.optDouble("mean_byte_rate", 0.0),
                    meanWordRate  = stats.optDouble("mean_word_rate", 0.0),
                    medianByteRate= stats.optDouble("median_byte_rate", 0.0),
                    p10ByteRate   = stats.optDouble("p10_byte_rate", 0.0),
                    p90ByteRate   = stats.optDouble("p90_byte_rate", 0.0),
                    minByteRate   = stats.optDouble("min_byte_rate", 0.0),
                    maxByteRate   = stats.optDouble("max_byte_rate", 0.0),
                ),
                rows = rows,
            )
        } catch (t: Throwable) {
            null
        }
    }
}

data class Sweep(
    val path: String,
    val startedAt: String,
    val stem: String,
    val prompt: String,
    val n: Int,
    val threads: Int,
    val engine: String,
    val runs: List<SweepRow>,
) {
    companion object {
        fun parse(file: File): Sweep? = try {
            val root = JSONObject(file.readText())
            if (!root.optString("schema").startsWith("prayog.sweep/")) return null
            val runsArr = root.optJSONArray("runs") ?: return null
            val rows = buildList {
                for (i in 0 until runsArr.length()) {
                    val r = runsArr.getJSONObject(i)
                    val inf = r.optJSONObject("inference") ?: JSONObject()
                    add(SweepRow(
                        quant         = r.optString("quant"),
                        modelPath     = r.optString("model"),
                        modelSizeMb   = r.optDouble("model_size_mb", 0.0),
                        runReportPath = r.optString("run_report"),
                        exitCode      = r.optInt("exit_code", -1),
                        wallSec       = r.optDouble("wall_sec", 0.0),
                        maxRssMb      = r.optDouble("maxrss_mb", 0.0),
                        inference = Inference(
                            decodeTokS       = inf.optDouble("decode_tok_s", 0.0),
                            prefillTokS      = inf.optDouble("prefill_tok_s", 0.0),
                            ttftS            = inf.optDouble("ttft_s", 0.0),
                            promptTokens     = 0,
                            generated        = inf.optInt("generated", 0),
                            weightsResidentMb= inf.optDouble("weights_resident_mb", 0.0),
                            kvCacheMb        = 0.0,
                            streamedMb       = inf.optDouble("streamed_mb", 0.0),
                            contextUsed      = 0,
                            contextMax       = 0,
                        ),
                    ))
                }
            }
            Sweep(
                path = file.absolutePath,
                startedAt = root.optString("started_at"),
                stem = root.optString("stem"),
                prompt = root.optString("prompt"),
                n = root.optInt("n", 0),
                threads = root.optInt("threads", 0),
                engine = root.optString("engine"),
                runs = rows,
            )
        } catch (t: Throwable) {
            null
        }
    }
}

data class Report(
    val path: String,
    val schema: String,
    val startedAt: String,
    val experiment: String,
    val cmd: String,
    val argv: List<String>,
    val exitCode: Int,
    val signal: Int,
    val wallSec: Double,
    val maxRssMb: Double,
    val inference: Inference?,
    val stdoutSample: String,
    val stderrSample: String,
) {
    val isInference: Boolean get() = inference != null

    companion object {
        fun parse(file: File): Report? = try {
            val root = JSONObject(file.readText())
            val metrics = root.optJSONObject("metrics") ?: JSONObject()
            val argvArr = root.optJSONArray("argv")
            val argv = buildList {
                if (argvArr != null) for (i in 0 until argvArr.length()) add(argvArr.getString(i))
            }
            val inf = root.optJSONObject("inference")?.let { j ->
                Inference(
                    decodeTokS       = j.optDouble("decode_tok_s", 0.0),
                    prefillTokS      = j.optDouble("prefill_tok_s", 0.0),
                    ttftS            = j.optDouble("ttft_s", 0.0),
                    promptTokens     = j.optInt("prompt_tokens", 0),
                    generated        = j.optInt("generated", 0),
                    weightsResidentMb= j.optDouble("weights_resident_mb", 0.0),
                    kvCacheMb        = j.optDouble("kv_cache_mb", 0.0),
                    streamedMb       = j.optDouble("streamed_mb", 0.0),
                    contextUsed      = j.optInt("context_used", 0),
                    contextMax       = j.optInt("context_max", 0),
                )
            }
            Report(
                path = file.absolutePath,
                schema = root.optString("schema", "prayog.run/1"),
                startedAt = root.optString("started_at"),
                experiment = root.optString("experiment", "(unnamed)"),
                cmd = root.optString("cmd"),
                argv = argv,
                exitCode = root.optInt("exit_code", -1),
                signal = root.optInt("signal", 0),
                wallSec = metrics.optDouble("wall_sec", 0.0),
                maxRssMb = metrics.optDouble("maxrss_mb", 0.0),
                inference = inf,
                stdoutSample = root.optString("stdout_sample"),
                stderrSample = root.optString("stderr_sample"),
            )
        } catch (t: Throwable) {
            null
        }
    }
}

/**
 * A list-row entry. Sweep summaries are rendered as one card with a matrix;
 * individual runs are rendered as standalone rows. Per-quant child reports
 * written by sweep are kept in the list too (filename contains "sweep-") so
 * you can drill into any single row.
 */
sealed interface Entry {
    val startedAt: String
    data class Run(val report: Report)   : Entry { override val startedAt get() = report.startedAt }
    data class Of(val sweep: Sweep)      : Entry { override val startedAt get() = sweep.startedAt }
    data class Ag(val agree: Agree)      : Entry { override val startedAt get() = agree.startedAt }
    data class Batch(val batch: AgreeBatch) : Entry { override val startedAt get() = batch.startedAt }
}

/**
 * Where we look for reports on-device. The engine writes to $PRAYOG_HOME/reports;
 * from the Android app's POV that lives on shared storage so both sides can see it.
 */
object ReportStore {
    /**
     * Default report dir on Android shared storage. Create this from Termux with:
     *     mkdir -p /storage/emulated/0/prayog/reports
     *     ln -sfn /storage/emulated/0/prayog ~/storage/shared/prayog   # optional
     * Then export PRAYOG_HOME=/storage/emulated/0/prayog before running the engine.
     */
    const val DEFAULT_PATH = "/storage/emulated/0/prayog/reports"

    fun loadAll(dir: File = File(DEFAULT_PATH)): List<Entry> {
        if (!dir.isDirectory) return emptyList()
        return dir.listFiles { f -> f.isFile && f.name.endsWith(".json") }
            .orEmpty()
            .mapNotNull<File, Entry> { f ->
                AgreeBatch.parse(f)?.let(Entry::Batch)
                    ?: Agree.parse(f)?.let(Entry::Ag)
                    ?: Sweep.parse(f)?.let(Entry::Of)
                    ?: Report.parse(f)?.let(Entry::Run)
            }
            .sortedByDescending { it.startedAt }
    }
}

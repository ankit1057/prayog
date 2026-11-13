package com.ankit1057.prayog

import org.json.JSONObject
import java.io.File

/**
 * Mirror of the JSON schema `prayog.run/2` emitted by engine/src/prayog.cpp.
 * Backwards-compatible with `prayog.run/1`: missing inference block → null.
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

    fun loadAll(dir: File = File(DEFAULT_PATH)): List<Report> {
        if (!dir.isDirectory) return emptyList()
        return dir.listFiles { f -> f.isFile && f.name.endsWith(".json") }
            .orEmpty()
            .mapNotNull(Report::parse)
            .sortedByDescending { it.startedAt }
    }
}

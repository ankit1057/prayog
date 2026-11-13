package com.ankit1057.prayog

import org.json.JSONObject
import java.io.File

/**
 * Minimal model of a `prayog run` JSON report. Mirrors the schema emitted by
 * engine/src/prayog.cpp — keep in sync if you add fields there.
 *
 * We parse with org.json (stdlib on Android) rather than pulling in Moshi/
 * kotlinx.serialization for a walking skeleton.
 */
data class Report(
    val path: String,
    val startedAt: String,
    val experiment: String,
    val cmd: String,
    val argv: List<String>,
    val exitCode: Int,
    val signal: Int,
    val wallSec: Double,
    val maxRssMb: Double,
    val stdoutSample: String,
    val stderrSample: String,
) {
    companion object {
        fun parse(file: File): Report? = try {
            val root = JSONObject(file.readText())
            val metrics = root.optJSONObject("metrics") ?: JSONObject()
            val argvArr = root.optJSONArray("argv")
            val argv = buildList {
                if (argvArr != null) for (i in 0 until argvArr.length()) add(argvArr.getString(i))
            }
            Report(
                path = file.absolutePath,
                startedAt = root.optString("started_at"),
                experiment = root.optString("experiment", "(unnamed)"),
                cmd = root.optString("cmd"),
                argv = argv,
                exitCode = root.optInt("exit_code", -1),
                signal = root.optInt("signal", 0),
                wallSec = metrics.optDouble("wall_sec", 0.0),
                maxRssMb = metrics.optDouble("maxrss_mb", 0.0),
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

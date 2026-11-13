// prayog — on-device AI research lab engine.
//
//   prayog bench <cmd> [args...]            measure wall/rss/exit of a command
//   prayog run <experiment.toml>            orchestrate a declared experiment
//   prayog compare <a.json> <b.json>        diff two run reports
//   prayog report <run.json>                pretty-print a report as markdown
//   prayog version
//
// Writes JSON reports to $PRAYOG_HOME/reports (default: ~/prayog/reports).
// No external libraries. C++17, aarch64-first.

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace prayog {

constexpr const char* kVersion     = "0.4.0";
constexpr const char* kSchema      = "prayog.run/2";
constexpr const char* kSweepSchema = "prayog.sweep/1";
constexpr const char* kAgreeSchema = "prayog.agree/1";

static std::string trim(std::string_view s);

// --- utilities -------------------------------------------------------------

static std::string home_dir() {
    if (const char* h = std::getenv("PRAYOG_HOME")) return h;
    if (const char* h = std::getenv("HOME")) return std::string(h) + "/prayog";
    return "./prayog";
}

static std::string reports_dir() { return home_dir() + "/reports"; }
static std::string models_dir()  { return home_dir() + "/models"; }

static void mkpath(const std::string& p) {
    std::string cur;
    for (size_t i = 0; i <= p.size(); ++i) {
        if (i == p.size() || p[i] == '/') {
            if (!cur.empty() && cur != "/") ::mkdir(cur.c_str(), 0755);
        }
        if (i < p.size()) cur.push_back(p[i]);
    }
}

static std::string iso8601_now() {
    auto t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

static std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else out += c;
        }
    }
    return out;
}

// --- bench: run a command, measure wall + rss + exit -----------------------

struct InferenceMetrics {
    bool present = false;
    double decode_tok_s   = 0.0;
    double prefill_tok_s  = 0.0;
    double ttft_s         = 0.0;
    int    prompt_tokens  = 0;
    int    generated      = 0;
    double weights_resident_mb = 0.0;
    double kv_cache_mb    = 0.0;
    double streamed_mb    = 0.0;
    int    context_used   = 0;
    int    context_max    = 0;
};

struct BenchResult {
    std::string cmd;
    std::vector<std::string> argv;
    int exit_code = -1;
    int signal = 0;
    double wall_sec = 0.0;
    long maxrss_kb = 0;      // child RU_MAXRSS, kilobytes on Linux
    long utime_us = 0;
    long stime_us = 0;
    std::string stdout_full; // full captured (capped at 1 MB) — used for parsing
    std::string stderr_full;
    std::string stdout_sample; // first N bytes — used for display
    std::string stderr_sample;
    std::string started_at;
    InferenceMetrics inference;
};

static BenchResult run_and_measure(const std::vector<std::string>& argv,
                                   size_t sample_bytes = 4096,
                                   size_t full_bytes   = 1 << 20) {
    BenchResult r;
    r.argv = argv;
    r.cmd = argv.empty() ? "" : argv[0];
    r.started_at = iso8601_now();

    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) < 0 || pipe(err_pipe) < 0) {
        std::fprintf(stderr, "prayog: pipe failed: %s\n", std::strerror(errno));
        r.exit_code = -1;
        return r;
    }

    auto t0 = std::chrono::steady_clock::now();
    pid_t pid = fork();
    if (pid < 0) {
        std::fprintf(stderr, "prayog: fork failed: %s\n", std::strerror(errno));
        r.exit_code = -1;
        return r;
    }
    if (pid == 0) {
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);

        std::vector<char*> cargv;
        cargv.reserve(argv.size() + 1);
        for (auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
        cargv.push_back(nullptr);
        execvp(cargv[0], cargv.data());
        std::fprintf(stderr, "prayog: execvp(%s) failed: %s\n", cargv[0], std::strerror(errno));
        _exit(127);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);

    auto drain = [&](int fd, std::string& full) {
        char buf[4096];
        while (true) {
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n <= 0) break;
            if (full.size() < full_bytes) {
                size_t take = std::min<size_t>(static_cast<size_t>(n), full_bytes - full.size());
                full.append(buf, take);
            }
        }
    };
    // Simple serial drain: stdout then stderr. Fine for the batch workloads we
    // run (help text, one short inference). For long streams of tokens a poll()
    // loop would be better — add when a real interactive mode lands.
    drain(out_pipe[0], r.stdout_full);
    drain(err_pipe[0], r.stderr_full);
    r.stdout_sample = r.stdout_full.substr(0, std::min(r.stdout_full.size(), sample_bytes));
    r.stderr_sample = r.stderr_full.substr(0, std::min(r.stderr_full.size(), sample_bytes));
    close(out_pipe[0]);
    close(err_pipe[0]);

    int status = 0;
    struct rusage ru{};
    if (wait4(pid, &status, 0, &ru) < 0) {
        std::fprintf(stderr, "prayog: wait4 failed: %s\n", std::strerror(errno));
    }
    auto t1 = std::chrono::steady_clock::now();
    r.wall_sec = std::chrono::duration<double>(t1 - t0).count();

    if (WIFEXITED(status))   r.exit_code = WEXITSTATUS(status);
    if (WIFSIGNALED(status)) { r.signal = WTERMSIG(status); r.exit_code = 128 + r.signal; }

    r.maxrss_kb = ru.ru_maxrss; // Linux: kilobytes
    r.utime_us = static_cast<long>(ru.ru_utime.tv_sec) * 1000000 + ru.ru_utime.tv_usec;
    r.stime_us = static_cast<long>(ru.ru_stime.tv_sec) * 1000000 + ru.ru_stime.tv_usec;
    return r;
}

// --- inference-metric scraper ----------------------------------------------
// Recognises the stderr lines emitted by sipllm's llm-engine (see sipllm/main.cpp
// around line 1136 — the "decode: %.2f tok/s" summary block). Any line that
// doesn't match is simply ignored, so this is safe to call on arbitrary output.

static bool scan_double(std::string_view haystack, std::string_view needle, double& out) {
    auto i = haystack.find(needle);
    if (i == std::string_view::npos) return false;
    i += needle.size();
    while (i < haystack.size() && (haystack[i] == ' ' || haystack[i] == '\t')) ++i;
    std::string num;
    while (i < haystack.size() && (std::isdigit(static_cast<unsigned char>(haystack[i])) ||
                                   haystack[i] == '.' || haystack[i] == '-' ||
                                   haystack[i] == 'e' || haystack[i] == '+')) {
        num.push_back(haystack[i++]);
    }
    if (num.empty()) return false;
    try { out = std::stod(num); } catch (...) { return false; }
    return true;
}

static bool scan_int(std::string_view haystack, std::string_view needle, int& out) {
    double tmp = 0.0;
    if (!scan_double(haystack, needle, tmp)) return false;
    out = static_cast<int>(tmp);
    return true;
}

static InferenceMetrics parse_inference_metrics(const std::string& stderr_full,
                                                const std::string& stdout_full) {
    InferenceMetrics m;
    // Metrics come on stderr from llm-engine; stdout carries generated tokens.
    // Scan both to be safe.
    auto hunt = [&](const std::string& src) {
        if (scan_double(src, "decode:",          m.decode_tok_s))      m.present = true;
        if (scan_double(src, "prefill:",         m.prefill_tok_s))     m.present = true;
        if (scan_double(src, "TTFT:",            m.ttft_s))            m.present = true;
        scan_int   (src, "prompt tokens:",       m.prompt_tokens);
        scan_int   (src, "generated:",           m.generated);
        scan_double(src, "weights resident:",    m.weights_resident_mb);
        scan_double(src, "kv cache:",            m.kv_cache_mb);
        scan_double(src, "streamed:",            m.streamed_mb);
        // "context:         N / M" — scan_int picks up the first number.
        scan_int   (src, "context:",             m.context_used);
        // For context_max, find the "/" after "context:" and read past it.
        auto i = src.find("context:");
        if (i != std::string::npos) {
            auto slash = src.find('/', i);
            if (slash != std::string::npos) {
                double dmax = 0.0;
                if (scan_double(src.substr(slash), "/", dmax)) m.context_max = static_cast<int>(dmax);
            }
        }
    };
    hunt(stderr_full);
    hunt(stdout_full);
    return m;
}

static std::string serialize(const BenchResult& r, const std::string& experiment = "") {
    std::ostringstream o;
    o << "{\n";
    o << "  \"schema\": \"" << kSchema << "\",\n";
    o << "  \"started_at\": \"" << r.started_at << "\",\n";
    if (!experiment.empty())
        o << "  \"experiment\": \"" << json_escape(experiment) << "\",\n";
    o << "  \"cmd\": \"" << json_escape(r.cmd) << "\",\n";
    o << "  \"argv\": [";
    for (size_t i = 0; i < r.argv.size(); ++i) {
        if (i) o << ", ";
        o << "\"" << json_escape(r.argv[i]) << "\"";
    }
    o << "],\n";
    o << "  \"exit_code\": " << r.exit_code << ",\n";
    o << "  \"signal\": " << r.signal << ",\n";
    o << "  \"metrics\": {\n";
    o << "    \"wall_sec\": " << r.wall_sec << ",\n";
    o << "    \"maxrss_kb\": " << r.maxrss_kb << ",\n";
    o << "    \"maxrss_mb\": " << (r.maxrss_kb / 1024.0) << ",\n";
    o << "    \"utime_us\": " << r.utime_us << ",\n";
    o << "    \"stime_us\": " << r.stime_us << "\n";
    o << "  },\n";
    if (r.inference.present) {
        const auto& m = r.inference;
        o << "  \"inference\": {\n";
        o << "    \"decode_tok_s\": "       << m.decode_tok_s      << ",\n";
        o << "    \"prefill_tok_s\": "      << m.prefill_tok_s     << ",\n";
        o << "    \"ttft_s\": "             << m.ttft_s            << ",\n";
        o << "    \"prompt_tokens\": "      << m.prompt_tokens     << ",\n";
        o << "    \"generated\": "          << m.generated         << ",\n";
        o << "    \"weights_resident_mb\": "<< m.weights_resident_mb<< ",\n";
        o << "    \"kv_cache_mb\": "        << m.kv_cache_mb       << ",\n";
        o << "    \"streamed_mb\": "        << m.streamed_mb       << ",\n";
        o << "    \"context_used\": "       << m.context_used      << ",\n";
        o << "    \"context_max\": "        << m.context_max       << "\n";
        o << "  },\n";
    }
    o << "  \"stdout_sample\": \"" << json_escape(r.stdout_sample) << "\",\n";
    o << "  \"stderr_sample\": \"" << json_escape(r.stderr_sample) << "\"\n";
    o << "}\n";
    return o.str();
}

static std::string next_report_path(const std::string& experiment) {
    mkpath(reports_dir());
    auto t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    std::string stem = experiment.empty() ? "run" : experiment;
    // sanitize: keep alnum, dash, underscore
    for (char& c : stem) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) c = '_';
    }
    return reports_dir() + "/" + buf + "-" + stem + ".json";
}

// --- TOML-lite loader for experiments --------------------------------------
// Supports:  key = "string"   key = 123   # comment
// Flat key/value pairs only. Enough for a walking-skeleton experiment file.

struct Experiment {
    std::string name;
    std::string description;
    std::string cmd;
    std::vector<std::string> args;
};

static std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b-1]))) --b;
    return std::string(s.substr(a, b - a));
}

static std::vector<std::string> split_args(std::string_view s) {
    // Mini shell-like tokenizer: whitespace splits; "..." groups; \" and \\
    // inside a quoted segment produce a literal " / \. An unescaped " toggles
    // quote state. Bug fixed 0.4.0: earlier versions treated \" as toggle-plus-
    // literal and left a stray backslash in the prompt.
    std::vector<std::string> out;
    std::string cur;
    bool in_q = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (in_q && c == '\\' && i + 1 < s.size() && (s[i+1] == '"' || s[i+1] == '\\')) {
            cur.push_back(s[++i]);
            continue;
        }
        if (c == '"') { in_q = !in_q; continue; }
        if (!in_q && std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static std::optional<Experiment> load_experiment(const std::string& path) {
    std::ifstream in(path);
    if (!in) { std::fprintf(stderr, "prayog: cannot open %s\n", path.c_str()); return std::nullopt; }
    Experiment e;
    std::string line;
    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(std::string_view(line).substr(0, eq));
        std::string val = trim(std::string_view(line).substr(eq + 1));
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
        if (key == "name")        e.name = val;
        else if (key == "description") e.description = val;
        else if (key == "cmd")    e.cmd = val;
        else if (key == "args")   e.args = split_args(val);
    }
    if (e.cmd.empty()) { std::fprintf(stderr, "prayog: experiment %s missing cmd=\n", path.c_str()); return std::nullopt; }
    return e;
}

// --- JSON-lite reader for compare/report -----------------------------------
// Toy single-level parser: pulls scalar fields by key. Good enough for our own reports.

static std::string find_str(std::string_view s, std::string_view key) {
    std::string pat = std::string("\"") + std::string(key) + "\"";
    auto i = s.find(pat);
    if (i == std::string_view::npos) return "";
    i = s.find(':', i);
    if (i == std::string_view::npos) return "";
    ++i;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i >= s.size() || s[i] != '"') return "";
    ++i;
    std::string out;
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) { out.push_back(s[i+1]); i += 2; continue; }
        out.push_back(s[i++]);
    }
    return out;
}

static double find_num(std::string_view s, std::string_view key) {
    std::string pat = std::string("\"") + std::string(key) + "\"";
    auto i = s.find(pat);
    if (i == std::string_view::npos) return 0.0;
    i = s.find(':', i);
    if (i == std::string_view::npos) return 0.0;
    ++i;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    std::string num;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == '-' || s[i] == 'e' || s[i] == '+'))
        num.push_back(s[i++]);
    return num.empty() ? 0.0 : std::stod(num);
}

static std::string slurp(const std::string& path) {
    std::ifstream in(path);
    if (!in) return "";
    std::ostringstream o; o << in.rdbuf(); return o.str();
}

// --- subcommands -----------------------------------------------------------

static void print_inference_summary(const InferenceMetrics& m) {
    if (!m.present) return;
    std::fprintf(stderr,
        "        inference: decode=%.2f tok/s  prefill=%.2f tok/s  ttft=%.3fs"
        "  prompt=%d  gen=%d  weights=%.1fMB  streamed=%.1fMB\n",
        m.decode_tok_s, m.prefill_tok_s, m.ttft_s,
        m.prompt_tokens, m.generated,
        m.weights_resident_mb, m.streamed_mb);
}

static int cmd_bench(int argc, char** argv) {
    // prayog bench [--experiment NAME] [--inference] <cmd> [args...]
    std::string experiment;
    bool inference = false;
    int i = 2;
    while (i < argc && std::string_view(argv[i]).substr(0, 2) == "--") {
        std::string_view flag = argv[i];
        if (flag == "--experiment" && i + 1 < argc) { experiment = argv[i+1]; i += 2; }
        else if (flag == "--inference")             { inference = true; ++i; }
        else if (flag == "--")                      { ++i; break; }
        else { std::fprintf(stderr, "prayog bench: unknown flag %s\n", argv[i]); return 2; }
    }
    if (i >= argc) {
        std::fprintf(stderr, "usage: prayog bench [--experiment NAME] [--inference] <cmd> [args...]\n");
        return 2;
    }
    std::vector<std::string> cmd(argv + i, argv + argc);
    auto r = run_and_measure(cmd);
    if (inference) r.inference = parse_inference_metrics(r.stderr_full, r.stdout_full);
    auto json = serialize(r, experiment);
    auto out = next_report_path(experiment);
    std::ofstream f(out);
    f << json;
    std::fprintf(stderr, "prayog: wrote %s\n", out.c_str());
    std::fprintf(stderr, "        wall=%.3fs maxrss=%.1fMB exit=%d\n",
                 r.wall_sec, r.maxrss_kb / 1024.0, r.exit_code);
    print_inference_summary(r.inference);
    std::fwrite(json.data(), 1, json.size(), stdout);
    return r.exit_code == 0 ? 0 : 1;
}

static int cmd_run(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: prayog run <experiment.toml>\n"); return 2; }
    auto exp = load_experiment(argv[2]);
    if (!exp) return 2;
    std::vector<std::string> cmd;
    cmd.push_back(exp->cmd);
    for (auto& a : exp->args) cmd.push_back(a);
    auto r = run_and_measure(cmd);
    // Any experiment whose cmd ends in "llm-engine" is auto-parsed for inference
    // metrics. The scraper is cheap and no-op on non-matching output, so even
    // false positives cost nothing.
    bool is_llm = exp->cmd.find("llm-engine") != std::string::npos
               || exp->cmd.find("sipllm")     != std::string::npos;
    if (is_llm) r.inference = parse_inference_metrics(r.stderr_full, r.stdout_full);
    auto json = serialize(r, exp->name);
    auto out = next_report_path(exp->name);
    std::ofstream f(out);
    f << json;
    std::fprintf(stderr, "prayog: experiment=%s\n", exp->name.c_str());
    std::fprintf(stderr, "        wrote %s\n", out.c_str());
    std::fprintf(stderr, "        wall=%.3fs maxrss=%.1fMB exit=%d\n",
                 r.wall_sec, r.maxrss_kb / 1024.0, r.exit_code);
    print_inference_summary(r.inference);
    return r.exit_code == 0 ? 0 : 1;
}

// --- pull: download a GGUF by name or URL ----------------------------------
// We delegate the transport to curl rather than reinvent HTTP. Named models
// come from a tiny built-in registry (TinyLlama, SmolLM2 defaults) and from
// an optional user file at $PRAYOG_HOME/models.conf (TSV: name<TAB>url).

static std::string builtin_model_url(std::string_view name) {
    // Mirrors a subset of sipllm's registry — keep this short; the models.conf
    // file is the escape hatch for anything bigger.
    if (name == "tinyllama" || name == "tinyllama:q4_k_m")
        return "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf";
    if (name == "tinyllama:q8_0")
        return "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q8_0.gguf";
    if (name == "tinyllama:q5_k_m")
        return "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q5_K_M.gguf";
    if (name == "smollm2:135m")
        return "https://huggingface.co/bartowski/SmolLM2-135M-Instruct-GGUF/resolve/main/SmolLM2-135M-Instruct-Q8_0.gguf";
    if (name == "smollm2:360m")
        return "https://huggingface.co/bartowski/SmolLM2-360M-Instruct-GGUF/resolve/main/SmolLM2-360M-Instruct-Q8_0.gguf";
    return "";
}

static std::string user_registry_url(std::string_view name) {
    std::string path = home_dir() + "/models.conf";
    std::ifstream in(path);
    if (!in) return "";
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto tab = line.find('\t');
        if (tab == std::string::npos) tab = line.find(' ');
        if (tab == std::string::npos) continue;
        if (std::string_view(line).substr(0, tab) == name) return trim(std::string_view(line).substr(tab + 1));
    }
    return "";
}

static std::string basename_of(const std::string& url) {
    auto q = url.find('?');
    std::string clean = q == std::string::npos ? url : url.substr(0, q);
    auto s = clean.find_last_of('/');
    return s == std::string::npos ? clean : clean.substr(s + 1);
}

static int cmd_pull(int argc, char** argv) {
    // prayog pull <name|url> [--as filename]
    if (argc < 3) {
        std::fprintf(stderr,
            "usage: prayog pull <name|url> [--as filename]\n"
            "  known: tinyllama, tinyllama:q8_0, tinyllama:q5_k_m, smollm2:135m, smollm2:360m\n"
            "  user registry: %s/models.conf (TSV: name<TAB>url)\n",
            home_dir().c_str());
        return 2;
    }
    std::string target = argv[2];
    std::string override_name;
    for (int j = 3; j < argc; ++j) {
        if (std::string_view(argv[j]) == "--as" && j + 1 < argc) { override_name = argv[j+1]; ++j; }
    }

    std::string url;
    if (target.rfind("http://", 0) == 0 || target.rfind("https://", 0) == 0) {
        url = target;
    } else {
        url = user_registry_url(target);
        if (url.empty()) url = builtin_model_url(target);
        if (url.empty()) {
            std::fprintf(stderr, "prayog pull: unknown model '%s'. Pass a URL or add to %s/models.conf\n",
                         target.c_str(), home_dir().c_str());
            return 1;
        }
    }

    mkpath(models_dir());
    std::string out_path = models_dir() + "/" + (override_name.empty() ? basename_of(url) : override_name);

    std::fprintf(stderr, "prayog: fetching %s\n", url.c_str());
    std::fprintf(stderr, "        to     %s\n", out_path.c_str());

    std::vector<std::string> cmd = {
        "curl", "-L", "--fail", "--progress-bar",
        "-C", "-",                 // resume if partial file exists
        "-o", out_path,
        url,
    };
    auto r = run_and_measure(cmd, /*sample_bytes=*/256, /*full_bytes=*/4096);
    if (r.exit_code != 0) {
        std::fprintf(stderr, "prayog pull: curl exit %d\n%s\n", r.exit_code, r.stderr_sample.c_str());
        return r.exit_code;
    }
    struct stat st{};
    if (::stat(out_path.c_str(), &st) == 0) {
        std::fprintf(stderr, "prayog: %s  (%.1f MB)\n", out_path.c_str(), st.st_size / 1048576.0);
    }
    return 0;
}

// --- sweep: run one prompt across a matrix of quants ----------------------
// Decomposes `stem:quant` back into a URL via the registry, resolves the GGUF
// filename, errors cleanly if a model isn't pulled. Each quant is one child
// llm-engine invocation; its metrics become one row of the sweep matrix.

struct SweepRun {
    std::string quant;
    std::string model_path;
    double model_size_mb = 0.0;
    BenchResult bench;
    std::string run_report_path;
};

static std::vector<std::string> split_csv(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') { if (!cur.empty()) out.push_back(trim(cur)); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(trim(cur));
    return out;
}

static std::string resolve_model_file(const std::string& stem, const std::string& quant) {
    std::string full_name = stem + ":" + quant;
    std::string url = user_registry_url(full_name);
    if (url.empty()) url = builtin_model_url(full_name);
    if (url.empty()) return "";
    // Look under $PRAYOG_HOME/models AND $HOME/.sipllm/models (sipllm may have
    // pulled the same file; we accept either so you don't have to double-pull).
    std::string base = basename_of(url);
    std::vector<std::string> candidates = {
        models_dir() + "/" + base,
        std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") + "/.sipllm/models/" + base,
    };
    for (auto& c : candidates) {
        struct stat st{};
        if (::stat(c.c_str(), &st) == 0 && S_ISREG(st.st_mode)) return c;
    }
    return "";
}

static int cmd_sweep(int argc, char** argv) {
    // prayog sweep [--stem NAME] --quants Q1,Q2,... [--prompt "..."] [-n N]
    //              [--threads T] [--engine PATH]
    std::string stem    = "tinyllama";
    std::string quants_csv;
    std::string prompt  = "The capital of France is";
    int         n       = 16;
    int         threads = 4;
    std::string engine  = (std::getenv("HOME") ? std::string(std::getenv("HOME")) : ".")
                          + "/.sipllm/bin/llm-engine";

    for (int i = 2; i < argc; ++i) {
        std::string_view f = argv[i];
        if      (f == "--stem"    && i + 1 < argc) stem    = argv[++i];
        else if (f == "--quants"  && i + 1 < argc) quants_csv = argv[++i];
        else if (f == "--prompt"  && i + 1 < argc) prompt  = argv[++i];
        else if (f == "-n"        && i + 1 < argc) n       = std::atoi(argv[++i]);
        else if (f == "--threads" && i + 1 < argc) threads = std::atoi(argv[++i]);
        else if (f == "--engine"  && i + 1 < argc) engine  = argv[++i];
        else { std::fprintf(stderr, "prayog sweep: unknown flag %s\n", argv[i]); return 2; }
    }
    if (quants_csv.empty()) {
        std::fprintf(stderr,
            "usage: prayog sweep [--stem NAME] --quants Q1,Q2,... [--prompt \"...\"] [-n N]\n"
            "       e.g. prayog sweep --stem tinyllama --quants q4_k_m,q8_0 -n 16\n");
        return 2;
    }
    auto quants = split_csv(quants_csv);
    if (quants.empty()) { std::fprintf(stderr, "prayog sweep: no quants parsed\n"); return 2; }

    // Preflight: all models must be present.
    std::vector<std::pair<std::string, std::string>> resolved; // (quant, path)
    for (auto& q : quants) {
        std::string path = resolve_model_file(stem, q);
        if (path.empty()) {
            std::fprintf(stderr,
                "prayog sweep: %s:%s not found in %s or ~/.sipllm/models.\n"
                "  run: prayog pull %s:%s\n",
                stem.c_str(), q.c_str(), models_dir().c_str(),
                stem.c_str(), q.c_str());
            return 1;
        }
        resolved.emplace_back(q, path);
    }

    std::fprintf(stderr, "prayog sweep: stem=%s quants=[%s] n=%d threads=%d prompt=%.40s%s\n",
                 stem.c_str(), quants_csv.c_str(), n, threads,
                 prompt.c_str(), prompt.size() > 40 ? "..." : "");

    std::vector<SweepRun> runs;
    runs.reserve(resolved.size());
    for (auto& [q, path] : resolved) {
        std::fprintf(stderr, "\n[%zu/%zu] %s:%s\n", runs.size() + 1, resolved.size(),
                     stem.c_str(), q.c_str());

        SweepRun r;
        r.quant = q;
        r.model_path = path;
        struct stat st{};
        if (::stat(path.c_str(), &st) == 0) r.model_size_mb = st.st_size / 1048576.0;

        std::vector<std::string> cmd = {
            engine, path,
            "-p", prompt,
            "-n", std::to_string(n),
            "--greedy",
            "--threads", std::to_string(threads),
        };
        r.bench = run_and_measure(cmd);
        r.bench.inference = parse_inference_metrics(r.bench.stderr_full, r.bench.stdout_full);

        std::string tag = "sweep-" + stem + "-" + q;
        std::string json = serialize(r.bench, tag);
        r.run_report_path = next_report_path(tag);
        std::ofstream f(r.run_report_path);
        f << json;

        std::fprintf(stderr, "      wall=%.3fs maxrss=%.1fMB exit=%d\n",
                     r.bench.wall_sec, r.bench.maxrss_kb / 1024.0, r.bench.exit_code);
        print_inference_summary(r.bench.inference);
        runs.push_back(std::move(r));
    }

    // Emit sweep summary JSON.
    auto t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char ts[64];
    std::strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", &tm);
    std::string summary_path = reports_dir() + "/" + ts + "-sweep-" + stem + ".json";
    {
        std::ofstream f(summary_path);
        f << "{\n";
        f << "  \"schema\": \"" << kSweepSchema << "\",\n";
        f << "  \"started_at\": \"" << iso8601_now() << "\",\n";
        f << "  \"stem\": \""    << json_escape(stem)   << "\",\n";
        f << "  \"prompt\": \""  << json_escape(prompt) << "\",\n";
        f << "  \"n\": "         << n       << ",\n";
        f << "  \"threads\": "   << threads << ",\n";
        f << "  \"engine\": \""  << json_escape(engine) << "\",\n";
        f << "  \"runs\": [\n";
        for (size_t i = 0; i < runs.size(); ++i) {
            const auto& r = runs[i];
            const auto& m = r.bench.inference;
            f << "    {\n";
            f << "      \"quant\": \"" << json_escape(r.quant) << "\",\n";
            f << "      \"model\": \"" << json_escape(r.model_path) << "\",\n";
            f << "      \"model_size_mb\": " << r.model_size_mb << ",\n";
            f << "      \"run_report\": \"" << json_escape(r.run_report_path) << "\",\n";
            f << "      \"exit_code\": " << r.bench.exit_code << ",\n";
            f << "      \"wall_sec\": "  << r.bench.wall_sec  << ",\n";
            f << "      \"maxrss_mb\": " << (r.bench.maxrss_kb / 1024.0) << ",\n";
            f << "      \"inference\": {\n";
            f << "        \"decode_tok_s\":  " << m.decode_tok_s  << ",\n";
            f << "        \"prefill_tok_s\": " << m.prefill_tok_s << ",\n";
            f << "        \"ttft_s\": "        << m.ttft_s        << ",\n";
            f << "        \"generated\": "     << m.generated     << ",\n";
            f << "        \"weights_resident_mb\": " << m.weights_resident_mb << ",\n";
            f << "        \"streamed_mb\": "   << m.streamed_mb   << "\n";
            f << "      }\n";
            f << "    }" << (i + 1 < runs.size() ? "," : "") << "\n";
        }
        f << "  ]\n";
        f << "}\n";
    }

    // Print matrix markdown to stdout for immediate consumption.
    std::printf("\n# sweep: %s (n=%d, threads=%d)\n\n", stem.c_str(), n, threads);
    std::printf("prompt: `%s`\n\n", prompt.c_str());
    std::printf("| quant      | size MB | decode tok/s | prefill tok/s | TTFT ms | peak RSS MB | wall s |\n");
    std::printf("|------------|---------|--------------|---------------|---------|-------------|--------|\n");
    for (auto& r : runs) {
        const auto& m = r.bench.inference;
        std::printf("| %-10s | %7.1f | %12.2f | %13.2f | %7.0f | %11.1f | %6.2f |\n",
                    r.quant.c_str(),
                    r.model_size_mb,
                    m.decode_tok_s, m.prefill_tok_s, m.ttft_s * 1000,
                    r.bench.maxrss_kb / 1024.0,
                    r.bench.wall_sec);
    }
    std::fprintf(stderr, "\nprayog: wrote %s\n", summary_path.c_str());
    return 0;
}

// --- agree: same prompt + seed through two quants, diff the outputs --------
// This is a cheap, honest quality proxy that does NOT need a tokenizer or
// logit access: run both quants with --greedy --seed S, capture generated
// text, find the common prefix. Where divergence happens (and after how
// many tokens the quant goes off-rails) is the signal.
//
// Not perplexity. But perplexity needs per-position logits, which the
// current llm-engine doesn't expose. Agreement rate answers the research
// question an engineer actually asks: "Does this quant change my outputs?"

struct AgreeOut {
    std::string quant;
    std::string model_path;
    std::string generated; // stdout minus the echoed prompt
    BenchResult bench;
    std::string run_report_path;
};

// Count UTF-8 code points in a byte-length slice. Rough — doesn't validate
// continuation bytes; good enough for divergence reporting.
static size_t utf8_count(std::string_view s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

static std::vector<std::string> split_ws(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static int cmd_agree(int argc, char** argv) {
    // prayog agree [--stem NAME] --quants Q1,Q2 [--prompt "..."] [-n N]
    //              [--seed S] [--threads T] [--engine PATH]
    std::string stem    = "tinyllama";
    std::string quants_csv;
    std::string prompt  = "The capital of France is";
    int         n       = 32;
    int         seed    = 42;
    int         threads = 4;
    std::string engine  = (std::getenv("HOME") ? std::string(std::getenv("HOME")) : ".")
                          + "/.sipllm/bin/llm-engine";

    for (int i = 2; i < argc; ++i) {
        std::string_view f = argv[i];
        if      (f == "--stem"    && i + 1 < argc) stem    = argv[++i];
        else if (f == "--quants"  && i + 1 < argc) quants_csv = argv[++i];
        else if (f == "--prompt"  && i + 1 < argc) prompt  = argv[++i];
        else if (f == "-n"        && i + 1 < argc) n       = std::atoi(argv[++i]);
        else if (f == "--seed"    && i + 1 < argc) seed    = std::atoi(argv[++i]);
        else if (f == "--threads" && i + 1 < argc) threads = std::atoi(argv[++i]);
        else if (f == "--engine"  && i + 1 < argc) engine  = argv[++i];
        else { std::fprintf(stderr, "prayog agree: unknown flag %s\n", argv[i]); return 2; }
    }
    auto quants = split_csv(quants_csv);
    if (quants.size() != 2) {
        std::fprintf(stderr,
            "prayog agree needs exactly 2 quants (got %zu).\n"
            "usage: prayog agree --stem NAME --quants Q1,Q2 [--prompt \"...\"] [-n N] [--seed S]\n",
            quants.size());
        return 2;
    }

    std::vector<AgreeOut> outs;
    outs.reserve(2);
    for (auto& q : quants) {
        std::string path = resolve_model_file(stem, q);
        if (path.empty()) {
            std::fprintf(stderr,
                "prayog agree: %s:%s not found. run: prayog pull %s:%s\n",
                stem.c_str(), q.c_str(), stem.c_str(), q.c_str());
            return 1;
        }
        std::fprintf(stderr, "\n[%zu/2] %s:%s\n", outs.size() + 1, stem.c_str(), q.c_str());

        AgreeOut o;
        o.quant = q;
        o.model_path = path;
        std::vector<std::string> cmd = {
            engine, path,
            "-p", prompt,
            "-n", std::to_string(n),
            "--greedy",
            "--threads", std::to_string(threads),
            "--seed", std::to_string(seed),
        };
        o.bench = run_and_measure(cmd);
        o.bench.inference = parse_inference_metrics(o.bench.stderr_full, o.bench.stdout_full);
        // Strip the echoed prompt. llm-engine prints prompt then generated
        // tokens on stdout; if the prefix matches, trim it. Otherwise keep
        // the whole output (model may have tokenised oddly).
        o.generated = o.bench.stdout_full;
        if (o.generated.rfind(prompt, 0) == 0) o.generated = o.generated.substr(prompt.size());

        std::string tag = "agree-" + stem + "-" + q;
        std::string json = serialize(o.bench, tag);
        o.run_report_path = next_report_path(tag);
        std::ofstream f(o.run_report_path);
        f << json;

        std::fprintf(stderr, "      wall=%.3fs maxrss=%.1fMB exit=%d\n",
                     o.bench.wall_sec, o.bench.maxrss_kb / 1024.0, o.bench.exit_code);
        print_inference_summary(o.bench.inference);
        outs.push_back(std::move(o));
    }

    const auto& a = outs[0].generated;
    const auto& b = outs[1].generated;

    size_t common_bytes = 0;
    while (common_bytes < a.size() && common_bytes < b.size() && a[common_bytes] == b[common_bytes])
        ++common_bytes;
    // Don't split a code point: back off to the start of the character.
    while (common_bytes > 0 && (static_cast<unsigned char>(a[common_bytes]) & 0xC0) == 0x80)
        --common_bytes;

    size_t common_chars = utf8_count(a.substr(0, common_bytes));
    auto wa = split_ws(a);
    auto wb = split_ws(b);
    size_t common_words = 0;
    while (common_words < wa.size() && common_words < wb.size() && wa[common_words] == wb[common_words])
        ++common_words;

    double char_rate = (a.empty() && b.empty()) ? 1.0
                      : double(common_bytes) / std::max(a.size(), b.size());
    double word_rate = (wa.empty() && wb.empty()) ? 1.0
                      : double(common_words) / std::max(wa.size(), wb.size());

    // Emit summary JSON.
    auto t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char ts[64];
    std::strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", &tm);
    std::string summary_path = reports_dir() + "/" + ts + "-agree-" + stem + ".json";
    {
        std::ofstream f(summary_path);
        f << "{\n";
        f << "  \"schema\": \"" << kAgreeSchema << "\",\n";
        f << "  \"started_at\": \"" << iso8601_now() << "\",\n";
        f << "  \"stem\": \""    << json_escape(stem)   << "\",\n";
        f << "  \"prompt\": \""  << json_escape(prompt) << "\",\n";
        f << "  \"n\": "         << n        << ",\n";
        f << "  \"seed\": "      << seed     << ",\n";
        f << "  \"threads\": "   << threads  << ",\n";
        f << "  \"engine\": \""  << json_escape(engine)  << "\",\n";
        f << "  \"agreement\": {\n";
        f << "    \"common_bytes\": " << common_bytes << ",\n";
        f << "    \"common_chars\": " << common_chars << ",\n";
        f << "    \"common_words\": " << common_words << ",\n";
        f << "    \"byte_rate\": "    << char_rate    << ",\n";
        f << "    \"word_rate\": "    << word_rate    << "\n";
        f << "  },\n";
        f << "  \"quants\": [\n";
        for (size_t i = 0; i < 2; ++i) {
            const auto& o = outs[i];
            f << "    {\n";
            f << "      \"quant\": \""     << json_escape(o.quant)       << "\",\n";
            f << "      \"model\": \""     << json_escape(o.model_path)  << "\",\n";
            f << "      \"generated\": \"" << json_escape(o.generated)   << "\",\n";
            f << "      \"run_report\": \""<< json_escape(o.run_report_path) << "\",\n";
            f << "      \"wall_sec\": "    << o.bench.wall_sec           << ",\n";
            f << "      \"decode_tok_s\": "<< o.bench.inference.decode_tok_s << "\n";
            f << "    }" << (i == 0 ? "," : "") << "\n";
        }
        f << "  ]\n";
        f << "}\n";
    }

    // Pretty markdown on stdout.
    std::printf("\n# agree: %s  (n=%d, seed=%d, threads=%d)\n\n", stem.c_str(), n, seed, threads);
    std::printf("prompt: `%s`\n\n", prompt.c_str());
    std::printf("| quant      | decode tok/s | wall s | generated |\n");
    std::printf("|------------|--------------|--------|-----------|\n");
    for (auto& o : outs) {
        std::string g = o.generated;
        if (g.size() > 60) g = g.substr(0, 57) + "...";
        for (char& c : g) if (c == '\n' || c == '|') c = ' ';
        std::printf("| %-10s | %12.2f | %6.2f | %s |\n",
                    o.quant.c_str(), o.bench.inference.decode_tok_s,
                    o.bench.wall_sec, g.c_str());
    }
    std::printf("\n**agreement:** %.1f%% of chars (%zu / %zu), %.1f%% of words (%zu / %zu)\n",
                char_rate * 100.0, common_chars,
                std::max(utf8_count(a), utf8_count(b)),
                word_rate * 100.0, common_words,
                std::max(wa.size(), wb.size()));
    if (common_bytes < std::min(a.size(), b.size())) {
        std::string prefix = a.substr(0, common_bytes);
        std::string tail_a = a.substr(common_bytes, 40);
        std::string tail_b = b.substr(common_bytes, 40);
        for (std::string* s : {&prefix, &tail_a, &tail_b})
            for (char& c : *s) if (c == '\n') c = ' ';
        std::printf("\n**divergence at char %zu**:\n", common_chars);
        std::printf("  common: ...%.40s\n", prefix.size() > 40 ? prefix.substr(prefix.size() - 40).c_str() : prefix.c_str());
        std::printf("  %-10s: %s...\n", outs[0].quant.c_str(), tail_a.c_str());
        std::printf("  %-10s: %s...\n", outs[1].quant.c_str(), tail_b.c_str());
    } else {
        std::printf("\n**no divergence** — outputs agree over %zu bytes.\n", common_bytes);
    }
    std::fprintf(stderr, "\nprayog: wrote %s\n", summary_path.c_str());
    return 0;
}

static int cmd_models(int /*argc*/, char** /*argv*/) {
    std::string dir = models_dir();
    DIR* d = opendir(dir.c_str());
    if (!d) {
        std::fprintf(stderr, "prayog: no models yet (dir %s is empty)\n", dir.c_str());
        return 0;
    }
    std::vector<std::pair<std::string, off_t>> entries;
    while (auto* ent = readdir(d)) {
        if (ent->d_name[0] == '.') continue;
        std::string full = dir + "/" + ent->d_name;
        struct stat st{};
        if (::stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode))
            entries.emplace_back(ent->d_name, st.st_size);
    }
    closedir(d);
    std::sort(entries.begin(), entries.end());
    std::printf("models in %s:\n", dir.c_str());
    if (entries.empty()) { std::printf("  (none)\n"); return 0; }
    for (auto& [n, sz] : entries) {
        std::printf("  %-50s  %7.1f MB\n", n.c_str(), sz / 1048576.0);
    }
    return 0;
}

static int cmd_compare(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: prayog compare <a.json> <b.json>\n"); return 2; }
    auto a = slurp(argv[2]);
    auto b = slurp(argv[3]);
    if (a.empty() || b.empty()) { std::fprintf(stderr, "prayog: cannot read reports\n"); return 1; }
    auto name_a = find_str(a, "experiment");
    auto name_b = find_str(b, "experiment");
    double wa = find_num(a, "wall_sec"), wb = find_num(b, "wall_sec");
    double ma = find_num(a, "maxrss_mb"), mb = find_num(b, "maxrss_mb");
    std::printf("# prayog compare\n\n");
    std::printf("| metric      | %-20s | %-20s | delta |\n",
                name_a.empty() ? argv[2] : name_a.c_str(),
                name_b.empty() ? argv[3] : name_b.c_str());
    std::printf("|-------------|----------------------|----------------------|-------|\n");
    std::printf("| wall_sec    | %20.3f | %20.3f | %+.3f (%+.1f%%) |\n",
                wa, wb, wb - wa, wa > 0 ? 100.0 * (wb - wa) / wa : 0.0);
    std::printf("| maxrss_mb   | %20.1f | %20.1f | %+.1f (%+.1f%%) |\n",
                ma, mb, mb - ma, ma > 0 ? 100.0 * (mb - ma) / ma : 0.0);
    return 0;
}

static int cmd_report(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: prayog report <run.json>\n"); return 2; }
    auto s = slurp(argv[2]);
    if (s.empty()) { std::fprintf(stderr, "prayog: cannot read %s\n", argv[2]); return 1; }
    auto name = find_str(s, "experiment");
    auto cmd  = find_str(s, "cmd");
    auto when = find_str(s, "started_at");
    double wall = find_num(s, "wall_sec");
    double rss  = find_num(s, "maxrss_mb");
    double exit_code = find_num(s, "exit_code");
    std::printf("# %s\n\n", name.empty() ? "prayog run" : name.c_str());
    std::printf("- **started_at:** %s\n", when.c_str());
    std::printf("- **cmd:** `%s`\n", cmd.c_str());
    std::printf("- **exit:** %d\n", static_cast<int>(exit_code));
    std::printf("- **wall:** %.3f s\n", wall);
    std::printf("- **maxrss:** %.1f MB\n", rss);
    return 0;
}

static int cmd_version() {
    std::printf("prayog %s\n", kVersion);
    std::printf("home: %s\n", home_dir().c_str());
    std::printf("reports: %s\n", reports_dir().c_str());
    return 0;
}

static int usage() {
    std::fprintf(stderr,
        "prayog — on-device AI research lab\n"
        "\n"
        "usage:\n"
        "  prayog bench [--experiment NAME] [--inference] <cmd> [args...]\n"
        "  prayog run     <experiment.toml>\n"
        "  prayog sweep   [--stem NAME] --quants Q1,Q2,... [--prompt \"...\"] [-n N]\n"
        "  prayog agree   [--stem NAME] --quants Q1,Q2 [--prompt \"...\"] [-n N] [--seed S]\n"
        "  prayog pull    <name|url> [--as filename]\n"
        "  prayog models\n"
        "  prayog compare <a.json> <b.json>\n"
        "  prayog report  <run.json>\n"
        "  prayog version\n"
        "\n"
        "env:\n"
        "  PRAYOG_HOME  default $HOME/prayog\n");
    return 2;
}

} // namespace prayog

int main(int argc, char** argv) {
    if (argc < 2) return prayog::usage();
    std::string_view sub = argv[1];
    if (sub == "bench")   return prayog::cmd_bench(argc, argv);
    if (sub == "run")     return prayog::cmd_run(argc, argv);
    if (sub == "sweep")   return prayog::cmd_sweep(argc, argv);
    if (sub == "agree")   return prayog::cmd_agree(argc, argv);
    if (sub == "pull")    return prayog::cmd_pull(argc, argv);
    if (sub == "models")  return prayog::cmd_models(argc, argv);
    if (sub == "compare") return prayog::cmd_compare(argc, argv);
    if (sub == "report")  return prayog::cmd_report(argc, argv);
    if (sub == "version" || sub == "--version" || sub == "-v") return prayog::cmd_version();
    if (sub == "help" || sub == "--help" || sub == "-h") { prayog::usage(); return 0; }
    return prayog::usage();
}

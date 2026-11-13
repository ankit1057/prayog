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

constexpr const char* kVersion = "0.1.0";

// --- utilities -------------------------------------------------------------

static std::string home_dir() {
    if (const char* h = std::getenv("PRAYOG_HOME")) return h;
    if (const char* h = std::getenv("HOME")) return std::string(h) + "/prayog";
    return "./prayog";
}

static std::string reports_dir() { return home_dir() + "/reports"; }

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

struct BenchResult {
    std::string cmd;
    std::vector<std::string> argv;
    int exit_code = -1;
    int signal = 0;
    double wall_sec = 0.0;
    long maxrss_kb = 0;      // child RU_MAXRSS, kilobytes on Linux
    long utime_us = 0;
    long stime_us = 0;
    std::string stdout_sample; // first N bytes
    std::string stderr_sample;
    std::string started_at;
};

static BenchResult run_and_measure(const std::vector<std::string>& argv, size_t sample_bytes = 4096) {
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

    auto drain = [&](int fd, std::string& sink) {
        char buf[4096];
        while (true) {
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n <= 0) break;
            if (sink.size() < sample_bytes) {
                size_t take = std::min<size_t>(static_cast<size_t>(n), sample_bytes - sink.size());
                sink.append(buf, take);
            }
        }
    };
    // Simple serial drain; child output is small for the kinds of commands we run.
    // For long-running streams, a select()/poll() loop would be better — add later.
    drain(out_pipe[0], r.stdout_sample);
    drain(err_pipe[0], r.stderr_sample);
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

static std::string serialize(const BenchResult& r, const std::string& experiment = "") {
    std::ostringstream o;
    o << "{\n";
    o << "  \"schema\": \"prayog.run/1\",\n";
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
    std::vector<std::string> out;
    std::string cur;
    bool in_q = false;
    for (char c : s) {
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

static int cmd_bench(int argc, char** argv) {
    // prayog bench [--experiment NAME] <cmd> [args...]
    std::string experiment;
    int i = 2;
    while (i < argc && std::string_view(argv[i]).substr(0, 2) == "--") {
        std::string_view flag = argv[i];
        if (flag == "--experiment" && i + 1 < argc) { experiment = argv[i+1]; i += 2; }
        else if (flag == "--") { ++i; break; }
        else { std::fprintf(stderr, "prayog bench: unknown flag %s\n", argv[i]); return 2; }
    }
    if (i >= argc) {
        std::fprintf(stderr, "usage: prayog bench [--experiment NAME] <cmd> [args...]\n");
        return 2;
    }
    std::vector<std::string> cmd(argv + i, argv + argc);
    auto r = run_and_measure(cmd);
    auto json = serialize(r, experiment);
    auto out = next_report_path(experiment);
    std::ofstream f(out);
    f << json;
    std::fprintf(stderr, "prayog: wrote %s\n", out.c_str());
    std::fprintf(stderr, "        wall=%.3fs maxrss=%.1fMB exit=%d\n",
                 r.wall_sec, r.maxrss_kb / 1024.0, r.exit_code);
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
    auto json = serialize(r, exp->name);
    auto out = next_report_path(exp->name);
    std::ofstream f(out);
    f << json;
    std::fprintf(stderr, "prayog: experiment=%s\n", exp->name.c_str());
    std::fprintf(stderr, "        wrote %s\n", out.c_str());
    std::fprintf(stderr, "        wall=%.3fs maxrss=%.1fMB exit=%d\n",
                 r.wall_sec, r.maxrss_kb / 1024.0, r.exit_code);
    return r.exit_code == 0 ? 0 : 1;
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
        "  prayog bench [--experiment NAME] <cmd> [args...]\n"
        "  prayog run   <experiment.toml>\n"
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
    if (sub == "compare") return prayog::cmd_compare(argc, argv);
    if (sub == "report")  return prayog::cmd_report(argc, argv);
    if (sub == "version" || sub == "--version" || sub == "-v") return prayog::cmd_version();
    if (sub == "help" || sub == "--help" || sub == "-h") { prayog::usage(); return 0; }
    return prayog::usage();
}

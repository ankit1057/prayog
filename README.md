# prayog — on-device AI research lab

> *prayog* (प्रयोग) — Sanskrit for "experiment, application, usage."

A walking skeleton for running controlled AI inference experiments on an
Android phone, **first-class**. Two surfaces:

- a native **C++17 engine** (`engine/`) that times, measures, and reports;
- an **Android Jetpack Compose app** (`android/`) that reads reports off
  shared storage and shows them.

No PyTorch, no ONNX, no ggml, no BLAS, no Python. The engine wraps whatever
inference binary you want to study (first target: [`sipllm`](https://github.com/ankit1057/sipllm)'s
`llm-engine`) and emits structured JSON reports that the app visualises.

## end-to-end quickstart (Termux side)

```bash
cd ~/prayog/engine
make
./build/prayog run ../experiments/001-sipllm-coldstart.toml
./build/prayog run ../experiments/002-sh-coldstart.toml
./build/prayog compare reports/*-sipllm-coldstart.json reports/*-sh-coldstart.json
```

Reports land in `$PRAYOG_HOME/reports` (default `~/prayog/reports`).

### install the binary

```bash
cd ~/prayog/engine
make install                 # copies to ~/.local/bin/prayog
# or:
make install PREFIX_DIR=$PREFIX/bin
```

## end-to-end (Android app)

The app reads JSON reports from `/storage/emulated/0/prayog/reports`. One-time
setup from Termux:

```bash
mkdir -p /storage/emulated/0/prayog/reports
export PRAYOG_HOME=/storage/emulated/0/prayog
prayog run ~/prayog/experiments/001-sipllm-coldstart.toml
```

Then launch the `prayog` app. Reports appear newest-first, tap for detail.

### building the APK

The Android project is a standard Gradle module (AGP 8.7, Kotlin 2.0, Compose
BOM 2024.10). Build on a workstation with the Android SDK installed:

```bash
cd ~/prayog/android
./gradlew :app:assembleDebug
adb install app/build/outputs/apk/debug/app-debug.apk
```

Termux-side APK builds are *possible* (install `openjdk-21`, point `ANDROID_HOME`
at a sideloaded SDK) but slow — the walking skeleton assumes a workstation build.

## subcommand reference

| subcommand                 | what it does |
|----------------------------|--------------|
| `prayog bench [--inference] CMD [ARGS]`  | time one command, write report; `--inference` scrapes llm-engine metrics |
| `prayog run EXP.toml`      | run a declared experiment (auto-scrapes if cmd is llm-engine) |
| `prayog sweep --stem NAME --quants Q1,Q2,... [--prompt "..."] [-n N] [--threads T]` | run one prompt across a quant matrix |
| `prayog agree --stem NAME --quants Q1,Q2 [--prompt "..."] [-n N] [--seed S] [--threads T]` | same prompt+seed through two quants, report text agreement |
| `prayog pull NAME\|URL [--as FILE]` | download a GGUF to `$PRAYOG_HOME/models/` |
| `prayog models`            | list downloaded models with sizes |
| `prayog compare A.json B.json` | side-by-side metric diff |
| `prayog report R.json`     | pretty-print one report as markdown |
| `prayog version`           | version + paths |

### known model names

`tinyllama`, `tinyllama:q8_0`, `tinyllama:q5_k_m`, `smollm2:135m`, `smollm2:360m`.
Add your own at `$PRAYOG_HOME/models.conf` (one line per model: `name<TAB>url`).

### reference inference numbers (this device, 2026-11-13)

TinyLlama 1.1B, greedy, 16 tokens, 4 threads, prompt `"The capital of France is"`,
Termux aarch64:

| quant  | size MB | decode tok/s | prefill tok/s | TTFT ms | peak RSS MB | wall s |
|--------|---------|--------------|---------------|---------|-------------|--------|
| q4_k_m |   637.8 |         4.58 |          8.42 |     712 |       706.4 |   4.92 |
| q8_0   |  1116.5 |         4.24 |         10.24 |     586 |      1154.1 |   8.05 |

Q4_K_M wins decode throughput on this CPU (less dequant work per token
amortised against fewer bytes to touch); Q8_0 wins prefill and TTFT.

Reproduce:

```bash
prayog pull tinyllama:q4_k_m
prayog pull tinyllama:q8_0
prayog sweep --stem tinyllama --quants q4_k_m,q8_0 \
             --prompt "The capital of France is" -n 16 --threads 4
```

## experiment file format

Flat TOML-lite (quoted strings + `#` comments):

```toml
name        = "sipllm-tinyllama-greedy-32"
description = "32-token greedy decode on TinyLlama Q8_0"
cmd         = "/data/data/com.termux/files/home/.sipllm/bin/llm-engine"
args        = "~/.sipllm/models/tinyllama.gguf -p \"hello\" -n 32 --greedy"
```

`args` is a single shell-ish string; quoted segments survive splitting.

## layout

```
prayog/
├── engine/        C++17 CLI (prayog)
│   ├── src/prayog.cpp
│   └── Makefile
├── android/       Jetpack Compose app
│   └── app/src/main/java/com/ankit1057/prayog/
├── experiments/   TOML experiment declarations
├── reports/       JSON output (gitignored by default)
└── docs/
```

## status

Walking skeleton, 2026-11-13. Built on-device in Termux (aarch64), engine
builds with clang 21.1.8, exercises two real experiments end-to-end
(sipllm cold-start vs bash cold-start).

Next, per the sipllm-adjacent axis of work:

- real inference experiments (download `tinyllama.gguf`, measure tok/s)
- `prayog profile` — strace/perf wrapper for first-token latency decomposition
- quant sweep — same prompt across Q4/Q5/Q6/Q8 of one model, perplexity table
- Android app: tail-follow mode for in-flight runs

# prayog — project-level instructions for future Claude sessions

Scope: everything under `~/prayog/`. For cross-cutting device truths see
`~/CLAUDE.md`; for persistent user facts see the memory index.

## what this is

`prayog` is a **research harness**, not an inference engine. It *drives*
existing inference binaries (currently `~/.sipllm/bin/llm-engine`) and
produces structured JSON reports. The lab's unique value is not inference —
it's **reproducible measurement + comparison on-device, Android-first**.

If you catch yourself writing a matmul kernel here, stop. That belongs in
sipllm. Write it there and benchmark it from here.

## architectural invariants

- **Zero dependencies.** The engine is a single `.cpp` + a `Makefile`. No
  third-party libs. Match sipllm's anti-framework stance: if the stdlib can
  do it in 30 lines, use the stdlib.
- **Reports are the contract.** The JSON schema `prayog.run/2` (as of 0.2.0)
  written by `engine/src/prayog.cpp` is the sole handoff between engine and
  app. Any new metric must land in JSON; the Android `Report.kt` is a mirror,
  not a definition. Schema bumps are backwards-additive — v1 readers must
  still parse v2 output (unknown fields are dropped).
- **Android app is read-only.** It never mutates reports, never spawns
  processes. The engine CLI owns measurement; the app owns display. This
  one-way boundary makes the app testable without Termux.
- **Termux-first toolchain.** Everything must build with `clang++` out of
  `pkg install clang` and `make`. No autotools, no cmake unless strictly
  necessary. The APK is the only artefact that needs a workstation.

## building

```bash
cd ~/prayog/engine && make
```

Clang 21.1.8 for aarch64 Android target. `-O2 -march=armv8-a`. Binary at
`engine/build/prayog`, ~55KB stripped.

## verified on

- Termux `pkg` clang 21.1.8 on Android, aarch64, 2026-11-13.
- Run `./engine/build/prayog run experiments/001-sipllm-coldstart.toml` and
  confirm you get a report with `exit_code: 0` and `wall_sec < 0.1`.

## naming conventions

- Experiment TOML files: `NNN-short-slug.toml`, three-digit zero-padded.
- Reports: engine stamps `YYYYMMDD-HHMMSS-<slug>.json` on write. Don't
  rename — the Android app sorts by `started_at`, not by filename.

## do not

- Add a TOML parser dependency. The flat-key format in `load_experiment()`
  is intentional; nested tables are out of scope until a real use case lands.
- Add a JSON parser dependency on the engine side. The `find_str`/`find_num`
  helpers are sufficient for self-produced reports. If you need to read
  foreign JSON, write a new tool.
- Turn the Android app into a terminal emulator. It's a report viewer.
  "Run from phone" is a separate future spike (ProcessBuilder + jniLibs
  extraction) — not v1.

## known gaps (as of 0.2.0)

- No perplexity eval harness yet. `prayog eval <model> <wikitext.txt>`
  is the natural next subcommand — needs a tokenizer round-trip and a logit
  dump path on `llm-engine`.
- No quant-sweep runner. Would live as `prayog sweep <model-stem>`,
  pulling each quant variant and calling `run` over a matrix.
- Android app: no run-triggering, no history graphing, no diff view. All
  reasonable adds now that the JSON schema has stabilised at v2.
- Download resumability relies on curl's `-C -`. If a partial file is
  corrupt, delete it and retry.

## how this file stays honest

If you add a subcommand, update the subcommand table in README.md AND the
layout tree in README.md AND (if it changes the JSON schema) the Android
`Report.kt`. Anything that changes the build invocation goes under "building"
above. If you remove a feature, delete its mention here — don't annotate it
as deprecated.

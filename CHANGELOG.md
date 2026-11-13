# Changelog

## 0.3.0 — 2026-11-13

**Quant sweeps.**

### engine
- New `prayog sweep --stem NAME --quants Q1,Q2,... [--prompt "..."] [-n N]
  [--threads T] [--engine PATH]` subcommand. Runs one prompt across a matrix
  of quantizations, writes per-quant run reports *and* a sweep summary
  (schema `prayog.sweep/1`). Preflight checks look for models in both
  `$PRAYOG_HOME/models` and `~/.sipllm/models`, so pulls done via sipllm
  are reusable.
- Sweep prints a 6-column markdown matrix to stdout: quant, size MB,
  decode tok/s, prefill tok/s, TTFT ms, peak RSS MB, wall s.

### reference measurement (Termux aarch64, this device, 2026-11-13)
TinyLlama 1.1B greedy, 16 tokens, 4 threads, prompt "The capital of France is":

| quant  | size MB | decode tok/s | prefill tok/s | TTFT ms | peak RSS MB | wall s |
|--------|---------|--------------|---------------|---------|-------------|--------|
| q4_k_m |   637.8 |         4.58 |          8.42 |     712 |       706.4 |   4.92 |
| q8_0   |  1116.5 |         4.24 |         10.24 |     586 |      1154.1 |   8.05 |

Note: Q4_K_M wins decode throughput (4.58 vs 4.24 tok/s) despite lower
precision — fewer bytes to touch per token offsets the extra dequant work on
this CPU. Q8_0 wins TTFT (586 ms vs 712 ms) and prefill tok/s. Half the
memory footprint for Q4.

### android
- `Sweep`, `SweepRow`, `Entry` added. `ReportStore.loadAll` returns
  `List<Entry>` sorted newest-first; sweeps and runs intermix.
- Sweep cards in the list show `n`, `threads`, and the fastest quant's
  decode tok/s.
- Sweep detail screen: config section, 6-column matrix, verdict section
  (best decode / lightest / fastest TTFT).
- Run cards unchanged; schema `prayog.run/1` still parses.

## 0.2.0 — 2026-11-13

**Real inference metrics.**

### engine
- New JSON schema `prayog.run/2` adds an `inference` block when the parser
  recognises sipllm's `llm-engine` summary block:
  `decode_tok_s`, `prefill_tok_s`, `ttft_s`, `prompt_tokens`, `generated`,
  `weights_resident_mb`, `kv_cache_mb`, `streamed_mb`, `context_used`,
  `context_max`. Backwards-compatible: readers that ignore the field see v1.
- `prayog bench --inference` opts into the scraper for ad-hoc commands.
- `prayog run` auto-detects inference runs by looking for `llm-engine` or
  `sipllm` in the cmd path.
- `prayog pull <name|url> [--as filename]` downloads a GGUF to
  `$PRAYOG_HOME/models/` via curl, resumable (`-C -`). Built-in names:
  `tinyllama`, `tinyllama:q8_0`, `tinyllama:q5_k_m`, `smollm2:135m`,
  `smollm2:360m`. User registry at `$PRAYOG_HOME/models.conf` (TSV).
- `prayog models` lists what's downloaded.
- Full stdout/stderr (up to 1 MB) captured for parsing, sample unchanged
  in reports.

### experiments
- `003-tinyllama-greedy-16.toml` — first real inference bench:
  TinyLlama 1.1B Q8_0, greedy decode, 16 tokens, 4 threads.

### reference measurement (Termux aarch64, this device, 2026-11-13)
- TinyLlama 1.1B Q8_0, greedy, 16 tokens, 4 threads:
  decode **4.19 tok/s**, prefill **12.72 tok/s**, TTFT **629 ms**,
  weights resident 1099 MB, streamed 1030 MB from disk,
  peak RSS 1154 MB, wall 5.47 s.

### android
- `Report` model + `Inference` sub-model mirror `prayog.run/2`.
- List row shows `tok/s`, `ttft`, `gen` chips for inference runs; falls
  back to wall/rss/exit for non-inference.
- Detail screen: dedicated `inference` section (decode, prefill, ttft,
  prompt/generated tokens, weights, kv cache, streamed, context).
- Detail view is now vertically scrollable.

## 0.1.0 — 2026-11-13

Walking skeleton: engine (bench/run/compare/report), schema `prayog.run/1`,
Android Compose read-only viewer, two cold-start experiments.

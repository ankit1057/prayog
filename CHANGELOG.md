# Changelog

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

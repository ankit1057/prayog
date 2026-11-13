# Changelog

## 0.6.0 — 2026-11-13

**Runtime pivot: sipllm → llama.cpp.**

Prayog 0.1–0.5 drove sipllm's `llm-engine`. Sipllm is retired as a runtime
for this repo; `llama.cpp`'s `llama-cli` is the new backend. Reasons:
stability (industry-standard, broadly tested), aarch64 NEON that we don't
have to maintain, `llama-bench` as a known-good peer benchmark we can
cross-check against. If a second runtime ever lands (ollama, ExecuTorch),
it should be a `--engine` selector, not a replacement.

### engine
- Default engine path is now `~/llama.cpp/build/bin/llama-cli` (overridable
  via `--engine` on any subcommand).
- `parse_inference_metrics` now understands two output formats:
    - **Format 1 — classic `llama_perf_context_print` on stderr** (emitted
      by `llama-simple` and older `llama-cli` builds). Parses prefill /
      decode tok/s, prompt/gen token counts, TTFT, and `n_ctx`.
    - **Format 2 — chat-mode one-liner on stdout** (emitted by newer
      `llama-cli` builds like b1-5676dfb, which no longer print the
      perf_context block even with `--perf`). Parses
      `[ Prompt: X t/s | Generation: Y t/s ]` for prefill / decode tok/s.
      Token counts, TTFT, and `n_ctx` are not available in this format
      and come through as 0.
  Format 1 takes precedence when both are present.
- argv composition (sweep / agree / agree-batch) switched from positional
  `<model>` + `--greedy --threads` to llama-cli's flags: `-m MODEL`,
  `--temp 0.0`, `-t N`, `-s SEED`, `-st --simple-io --no-display-prompt`.
  `-no-cnv` does not exist in current llama.cpp; `-st` (single-turn) plus
  `--simple-io` gives equivalent "one prompt, one response, exit" behavior.
- `cmd_run` inference auto-detect now matches `llama-cli` / `llama-bench`
  in the cmd path.
- **TOML args parser bugfix.** `load_experiment` now unescapes `\"` and
  `\\` (TOML basic-string escapes) after stripping the outer quotes.
  Previously, an `args = "... -p \"prompt\" ..."` line produced a literal
  backslash in a non-quoted context; the subsequent split_args() saw an
  unescaped `"` and swallowed the rest of the line into a single argument,
  silently dropping every flag after the prompt.

### experiments
- `001-sipllm-coldstart.toml` renamed experiment to `llama-cli-coldstart`
  (filename kept for git-history continuity).
- `003-tinyllama-greedy-16.toml` updated to invoke `llama-cli` with the
  correct flag names; still TinyLlama 1.1B Q8_0 greedy 16 tokens.

### runtime fields temporarily missing
- `weights_resident_mb`, `streamed_mb`, `kv_cache_mb` were populated by
  sipllm's custom summary block. llama.cpp doesn't print them in the same
  line-oriented form; the JSON fields remain in the schema but hold `0`
  until a dedicated model-loader-line parser is added. `prayog.run/2`
  schema is unchanged — no Android changes needed.
- Under chat-mode llama-cli (Format 2), `ttft_s`, `prompt_tokens`,
  `generated`, and `context_max` also come through as 0. Switch to
  `llama-simple` or an older llama-cli if these are needed.

### build pre-req
Prayog now requires llama.cpp built locally. Termux NDK has a libc++
`__hash_memory` symbol mismatch that fails llama.cpp's default shared-lib
link; workaround is `-DBUILD_SHARED_LIBS=OFF` (see README quickstart).

## 0.5.0 — 2026-11-13

**Batch agreement over a prompt corpus.**

Single-prompt agreement is noisy — one lucky (or unlucky) next-token
decides the whole run. Averaging across a mix of prompt styles (factual,
code, structured, open-ended) collapses per-quant quality into a single
number you can trend over time. Bimodal distributions are the typical
result: code/structured prompts agree at 100%, open-ended at <10%, with
little in between.

### engine
- New `prayog agree-batch [--stem NAME] --quants Q1,Q2 --prompts FILE
  [-n N] [--seed S] [--threads T] [--engine PATH]` subcommand.
- Schema `prayog.agree-batch/1` with `stats { count, perfect_count,
  mean/median/p10/p90/min/max_byte_rate, mean_word_rate }` plus `rows[]`
  of per-prompt `{prompt, common_bytes/chars/words, len_a/b, byte_rate,
  word_rate, wall_a/b, snippet_a/b}`.
- Prompt file format: one per line, `#` for comments, blank lines skipped.
- Prints a markdown per-prompt table + summary stats.

### corpus
- New `experiments/prompts/basic.txt` — 15 mixed prompts
  (factual / code / structured / open-ended).

### reference measurement (Termux aarch64, this device, 2026-11-13)
TinyLlama 1.1B q4_k_m vs q8_0, greedy, seed 42, n=24, basic.txt (14 prompts):

| category                 | byte rate |
|--------------------------|-----------|
| `def factorial(n):`      |  100% |
| `function add(a, b) {`   |  100% |
| `Here is a JSON object:` |  100% |
| `The speed of light is`  |  66%  |
| `SELECT * FROM users`    |  62%  |
| `Once upon a time`       |  38%  |
| `The capital of France`  |  23%  |
| `In the morning I like`  |  11%  |
| `The chemical symbol`    |  6%   |
| rest                     |  1-2% |

**Summary:** mean 36.5%, median 10.5%, p10 1.0%, p90 100%, perfect 3/14.

The bimodal shape is the finding: code and structured prompts survive
quantisation cleanly; free-form / factual prompts cascade divergence from
one wrong next-token. For meaningful quality signals across quants, test
with structured prompts.

### android
- `AgreeBatch`, `BatchRow`, `BatchStats` data classes.
- `Entry.Batch` variant intermixed in the list.
- Batch card: shows mean / median / perfect-count / `n` chips.
- Batch detail: config + distribution stats + hand-drawn histogram
  (10 buckets of 10%) + per-prompt list with colour-coded % badges and
  divergence snippets.

## 0.4.0 — 2026-11-13

**Quality signal: agreement rate.**

The lab measured speed, memory, and TTFT; it didn't measure quality. Full
perplexity needs per-position logits (which llm-engine doesn't expose), so
0.4.0 adds a cheaper but honest proxy: run the same prompt + seed through two
quants with greedy sampling and diff the generated text. The common-prefix
length and the divergence point answer the research question an engineer
actually asks — *"does this quant change my outputs?"* — without a tokenizer.

### engine
- New `prayog agree [--stem NAME] --quants Q1,Q2 [--prompt "..."] [-n N]
  [--seed S] [--threads T] [--engine PATH]` subcommand.
- Schema `prayog.agree/1` with `agreement { common_bytes, common_chars,
  common_words, byte_rate, word_rate }` + per-quant generated text, run
  reports, and inference metrics.
- Prints a markdown divergence report to stdout (common prefix shown, then
  each quant's tail after the divergence point).
- UTF-8-safe common-prefix: won't split a multi-byte code point.
- **Fix** (long-standing): TOML `args = "...  -p \"foo\"..."` now parses
  `\"` and `\\` correctly inside quoted strings. Before 0.4.0, prayog's
  TOML-lite loader passed literal backslashes into argv, corrupting prompts
  in experiment files. Experiments 001/002 weren't affected (their
  args don't use escapes); 003 was.

### reference measurement (Termux aarch64, this device, 2026-11-13)
TinyLlama 1.1B greedy 32 tok, seed 42, "The capital of France is":

| quant  | decode tok/s | wall s | generated (first chars after prompt) |
|--------|--------------|--------|--------------------------------------|
| q4_k_m | 4.90         | 7.93   | ` Paris.\n\n2. B.C. The capital of ancient Rome was Rome...` |
| q8_0   | 2.04         | 21.47  | ` Paris.\n\n2. B. C. The capital of Canada is Ottawa...` |

**Agreement: 14.7% of chars (14 / 95), 10.5% of words (2 / 19).**
Divergence at char 14: both say "Paris.\n\n2. B.", then Q4 goes "C. ancient
Rome" and Q8 goes " C. Canada is Ottawa". Low agreement on free-generation
is expected — single-token divergence cascades. For meaningful numbers,
run agree over structured prompts where the next token is near-deterministic.

### android
- `Agree`, `AgreeQuant`, `Agreement` data classes mirror `prayog.agree/1`.
- `Entry.Ag` variant intermixed into the list, sorted by `started_at`.
- Agree card shows the % agreement as a colour-coded badge (green ≥50%, red <50%),
  plus chips for `n`, `seed`, and common counts.
- Agree detail: config + agreement sections + a three-pane divergence view
  showing the common prefix, then each quant's tail after the divergence
  point in its own colour.

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

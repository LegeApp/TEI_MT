# TEI_MT

High-throughput TEI XML translation pipeline using HY-MT GGUF + `llama.cpp`, built in C++.

This project is aimed at batch translation of CBETA-style TEI corpora while preserving XML structure.

## Features

- Recursive TEI XML input scanning.
- Output directory mirrors input directory structure.
- Translation writes back to TEI XML (default output is XML only).
- Optional Markdown sidecars (`--emit-markdown`).
- Parallel segment translation with per-thread contexts.
- Resume-by-default mode:
  - skips files if output is newer and already has expected translation notes.
- Progress bar + per-file runtime stats.
- CUDA-capable build path for NVIDIA GPUs.

## Translation Strategy

For each eligible TEI segment (`p`, `l`, `ab`, `head`, `seg` in body), output is inserted as:

```xml
<note type="translation" xml:lang="en">...</note>
```

Original XML structure is retained.

## Requirements

- CMake >= 3.20
- C++ compiler with C++23 support
- Git
- NVIDIA CUDA toolkit (optional, for GPU acceleration)

The project expects a local `llama.cpp` checkout at:

```text
../llama.cpp
```

relative to this repo root.

## Build

### CPU build

```bash
cmake -S . -B build
cmake --build build -j
```

### CUDA build (recommended on NVIDIA)

```bash
cmake -S . -B build-cuda -DHYMT_ENABLE_CUDA=ON
cmake --build build-cuda -j10
```

## Run

```bash
./build-cuda/tei_mt \
  --input /path/to/tei-dir-or-file \
  --output /path/to/output-dir \
  --model /path/to/HY-MT1.5-1.8B-Q8_0.gguf \
  --workers 2 \
  --threads 8 \
  --ctx 2048 \
  --max-tokens 192 \
  --n-gpu-layers -1
```

## CLI Options

- `--input <path>`: input XML file or directory (required)
- `--output <path>`: output directory/file (default: input folder name + `t`)
- `--model <path>`: GGUF model path (default: `HY-MT1.5-1.8B-Q8_0.gguf` in exe directory)
- `--workers <n>`: worker threads
- `--threads <n>`: llama.cpp CPU threads per context
- `--ctx <n>`: context window
- `--max-tokens <n>`: max generated tokens per segment
- `--n-gpu-layers <n>`: GPU layers (`-1` = all possible)
- `--emit-markdown`: write `*.en.md` sidecar files
- `--no-progress`: disable progress bar
- `--no-resume`: disable resume skipping
- `--overwrite-existing-translations`: replace existing translation notes
- `--interactive-drilldown`: interactive category/subcategory selection before queue start
- `--drilldown <expr>`: noninteractive drill-down selector (repeatable, max 2 categories)
- `--drilldown-help`: print available drill-down categories/subcategories for current dataset and exit
- `--sorting-data <path>`: CBETA sorting metadata JSON (default: `buddhist_metadata_analysis.json` in exe directory)
- `--filter-canon <v>`: canon filter (repeatable, CSV supported)
- `--filter-tradition <v>`: tradition filter (repeatable, CSV supported)
- `--filter-period <v>`: historical period filter (repeatable, CSV supported)
- `--filter-origin <v>`: geographic origin filter (repeatable, CSV supported)

Single-file mode:
- If `--input` is one XML file, `--output` may be either:
  - a directory, or
  - a specific output XML file path (e.g. `/tmp/out.xml`).

Model auto-download:
- If `--model` path does not exist, `tei_mt` auto-downloads HY-MT Q8_0 GGUF from official Hugging Face URL using `curl` and shows transfer progress in the terminal.

CBETA metadata queue filtering:
- Combine `--sorting-data` with one or more `--filter-*` flags to select a subset of XML files, then process that queue normally (1-by-1 through the existing batch loop).
- Matching is AND across categories and OR within a category.
- `--interactive-drilldown` provides a guided menu: pick primary category, pick primary subcategory (with count), optionally pick a secondary category/subcategory (with count), then confirm before translation begins.
- `--drilldown` supports:
  - `category=value` or `category:value`
  - category keys: `canon`, `tradition` (aliases: `traditions`, `sect`), `period` (aliases: `dynasty`, `timeperiod`), `origin` (aliases: `geography`, `geo`)
  - repeat `--drilldown` up to two times for combinations (AND semantics across categories)
- `--drilldown-help` prints all categories/subcategories with counts for your current `--input` scope.

Default behavior shortcuts:
- You can run with only `--input` for direct translation (single file or whole folder).
- If you use drill-down/filter flags and omit `--sorting-data`, the program loads `buddhist_metadata_analysis.json` from the exe directory.
- If you omit `--model`, the program uses `HY-MT1.5-1.8B-Q8_0.gguf` from the exe directory.

Example: Tang dynasty Chan/Zen batch

```bash
./build-cuda/tei_mt \
  --input /path/to/CbetaZenTexts/xml-p5 \
  --output /path/to/out \
  --model /path/to/HY-MT1.5-1.8B-Q8_0.gguf \
  --sorting-data /path/to/CBETA_Sorting_Data/buddhist_metadata_analysis.json \
  --filter-period Tang \
  --filter-tradition "Chan/Zen"
```

Example: input-only folder translation (auto output `<input>t`)

```bash
./build-cuda/tei_mt --input /path/to/CbetaZenTexts/xml-p5
```

Example: input-only single-file translation

```bash
./build-cuda/tei_mt --input /path/to/CbetaZenTexts/xml-p5/T/T01/T01n0001.xml
```

Example: interactive drill-down (e.g., Geography -> Unknown Origin)

```bash
./build-cuda/tei_mt \
  --input /path/to/CbetaZenTexts/xml-p5 \
  --output /path/to/out \
  --model /path/to/HY-MT1.5-1.8B-Q8_0.gguf \
  --sorting-data /path/to/CBETA_Sorting_Data/buddhist_metadata_analysis.json \
  --interactive-drilldown
```

Example: noninteractive Tang -> Chan/Zen

```bash
./build-cuda/tei_mt \
  --input /path/to/CbetaZenTexts/xml-p5 \
  --output /path/to/out \
  --model /path/to/HY-MT1.5-1.8B-Q8_0.gguf \
  --sorting-data /path/to/CBETA_Sorting_Data/buddhist_metadata_analysis.json \
  --drilldown period=Tang \
  --drilldown tradition=Chan/Zen
```

Example: print dedicated drill-down help

```bash
./build-cuda/tei_mt \
  --input /path/to/CbetaZenTexts/xml-p5 \
  --sorting-data /path/to/CBETA_Sorting_Data/buddhist_metadata_analysis.json \
  --drilldown-help
```

## Performance Notes

- On RTX 4060M class hardware, best throughput is typically with low worker count (`1-2`) and moderate threads (`4-8`).
- `Q4_K_M` models are significantly faster than `Q8_0`, with quality/speed tradeoff.
- Keep `--max-tokens` as low as acceptable for your corpus.

## LCUI GUI (Scaffold)

An optional desktop wrapper exists in `lcui-gui/` for:

- Start/Pause/Resume/Cancel
- file counters + progress bar
- live logs

See `lcui-gui/README.md`.

## Lightweight TEI Viewer

For human-readable viewing of translated TEI XML in-browser (instead of raw XML source), use:

```bash
cd tei-viewer
./run_viewer.sh
```

Then open `http://127.0.0.1:8765/` and load any translated XML file.

Viewer docs: `tei-viewer/README.md`.

## Status

This repo is actively iterating toward full-volume CBETA translation workflows with reproducible batch runs and resumability.

## Licensing

- The HY-MT model family used by this project is governed by the **Tencent HY Community License Agreement**.
- A copy of that license is included in `LICENSE` (sourced from the official HY-MT release).
- Important: that license explicitly states territorial restrictions (it does not apply in the EU, UK, and South Korea).

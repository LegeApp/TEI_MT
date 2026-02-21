# LCUI GUI Wrapper for `tei_mt`

This is a desktop GUI scaffold for your existing translator binary (`tei_mt`).

What it does:
- Lets you set the same core options as CLI.
- Runs translation in a worker thread so UI stays responsive.
- Shows counters and a progress bar (`done files / total files`).
- Supports `Start`, `Pause`, `Resume`, `Cancel`.
- Streams logs from the CLI into the GUI log panel.

## Architecture

- `src/core_api.*`: runs `tei_mt` as a child process and parses output lines.
- `src/job_controller.*`: worker lifecycle + pause/resume/cancel state.
- `src/event_queue.*`: thread-safe event handoff.
- `src/ui_bindings.*`: UI wiring and state updates.
- `app/ui_layout.xml`, `app/ui_style.css`: LCUI view/style.

## Build (xmake)

Prerequisites:
- `xmake`
- LCUI package available to xmake (`add_requires("lcui")`)

Build:
```bash
cd lcui-gui
xmake f -m release
xmake
```

Run:
```bash
xmake run tei_mt_lcui_gui
```

The UI defaults `tei_mt` path to:
- `../build-cuda-patched/tei_mt`

Adjust it in the GUI if your binary is elsewhere.

## Notes

- Pause/Resume/Cancel process control is implemented for Linux (`SIGSTOP`, `SIGCONT`, `SIGTERM`).
- This wrapper intentionally reuses your proven CLI pipeline instead of duplicating model logic.
- If you want deeper in-process progress (segment-level), next step is exposing your C++ core as a library and consuming callbacks directly instead of parsing CLI output.

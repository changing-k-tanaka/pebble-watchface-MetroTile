# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**metro** is a Pebble SDK 3 watchapp (not a watchface) written in C. It targets all 7 Pebble hardware platforms: aplite, basalt, chalk, diorite, emery, flint, gabbro.

## Build & Run Commands

```bash
pebble build                                    # Compile for all target platforms
pebble install --emulator emery                 # Build + run in QEMU emulator (emery = Pebble Time 2, 200x228)
pebble logs --emulator emery                    # Stream app logs from emulator
pebble screenshot --no-open --emulator emery    # Capture screenshot from emulator
```

The build system uses `wscript` (Waf/Python). Output PBW files land in `build/`.

Use `APP_LOG(APP_LOG_LEVEL_DEBUG, "msg: %d", val)` for debug output, visible via `pebble logs`.

## Architecture

The entire app is `src/c/metro.c` (~60 lines). The structure follows the standard Pebble UI lifecycle:

```
main() → prv_init() → app_event_loop() → prv_deinit()
```

- `prv_init()` — creates the Window, registers click handlers and window lifecycle handlers, pushes onto the window stack
- `prv_window_load()` / `prv_window_unload()` — allocate/destroy layers (TextLayer lives here, not in global scope)
- `prv_click_config_provider()` — subscribes UP/SELECT/DOWN buttons to their handlers
- Global state: `s_window` (Window*) and `s_text_layer` (TextLayer*)

**Key pattern**: Layers are created in `window_load` and destroyed in `window_unload`. Windows are created in `init` and destroyed in `deinit`. This prevents resource leaks on Pebble's constrained heap.

## Extending the App

- **Add UI layers**: create in `prv_window_load`, add with `layer_add_child`, destroy in `prv_window_unload`
- **Add timers**: register with `tick_timer_service_subscribe(MINUTE_UNIT, tick_handler)` in `prv_init`
- **Add phone communication (AppMessage)**: implement in C + create `src/pkjs/index.js` for JS side; `enableMultiJS` is already set in `package.json`
- **Add resources** (fonts, images): place in `resources/`, declare in `package.json` under `pebble.resources.media`
- **Restrict platforms**: edit `targetPlatforms` in `package.json`

## Pebble Watchface Skill

A `pebble-watchface` skill is available (`.claude/skills/pebble-watchface/`) for generating complete watchfaces with QEMU testing and App Store publishing support. Invoke it when the task involves creating a new watchface.

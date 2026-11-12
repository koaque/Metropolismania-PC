# Settings

`python tools/pipeline.py play` starts the game in a window with no time limit, set up by `settings.ini` in the
repository. The first `play` writes that file with the defaults below and comments; edit it with any text editor. The file
is yours: it is not committed (`.gitignore`).

The executable reads it at start-up (`src/overrides/settings.cpp`): `key = value` lines, `[section]` headers and `#` or `;`
comments are skipped. Each key stands for a switch the runtime already has, and a switch set in the environment wins over
the file, so `--env` and one-off variables still work. The start of the log names the file and every switch it set, and
lists the lines it did not understand.

| Key | Values | Default | Switch |
|---|---|---|---|
| `renderer` | `native`, `emulated`, `split` | `native` | `MMPC_RENDERER` ([RENDERER.md](RENDERER.md)): native draws the game at any resolution as one progressive picture a frame; emulated is the PS2's graphics chip, as the original draws; split shows both side by side |
| `render_scale` | `auto`, `1` to `8` | `auto` | `MMPC_RENDER_SCALE`: the native picture is 640x448 times this; auto takes the largest that fits the window's height |
| `msaa` | `1`, `2`, `4`, `8` | `1` | `MMPC_RENDER_MSAA`: samples a pixel (native renderer) |
| `filter_2d` | `sharp`, `smooth`, `ps2` | `sharp` | `MMPC_RENDER_FILTER_2D`: how the 2D drawn one texel to one pixel (text, the HUD, movies) is enlarged (native renderer): sharp keeps each texel crisp; smooth blends between texels where the PS2 does (its text and pictures); ps2 shows the PS2's own 640x448 picture of it, enlarged |
| `widescreen` | `off`, `on` (16:9), `16:9`, `21:9`, any `W:H` | `off` | `MMPC_WIDESCREEN`: the city's view opened wider at the same height, the HUD at its proportions (native renderer; [details](RENDERER.md#widescreen-n6)) |
| `fullscreen` | `on`, `off` | `off` | `PS2X_FULLSCREEN`: borderless fullscreen; F11 switches while playing |
| `window_size` | `WxH` | 640x448 | `PS2X_WINDOW_SIZE` |
| `window_position` | `X,Y` | centred | `PS2X_WINDOW_POS` |
| `gpu` | `auto`, `d3d12`, `vulkan` | `auto` | `PS2X_GPU`: the graphics API under SDL3 |
| `vsync` | `on`, `off` | `off` | `PS2X_VSYNC`: wait for the monitor's refresh when showing a frame (no tearing in exclusive fullscreen; windowed, the desktop already prevents it); it adds up to a refresh of delay |
| `game_rate` | `ps2`, `host`, a number | `ps2` | `PS2X_VSYNC_HZ`: the game's vertical blank; `host` runs it at the monitor's rate when that is within 58 to 62 Hz (60.00 instead of 59.94, so no frame is shown twice every few seconds) |
| `sound` | `on`, `off` | `on` | `PS2X_SPU2=0` when off |
| `skip_movies` | `on`, `off` | `off` | `MMPC_SKIP_MOVIES`: skip the opening movies |
| any `PS2X_...` or `MMPC_...` name | its value | | that switch as it is (for the developer switches) |

Where the file is read from: the file named by `MMPC_SETTINGS` (`play` sets it to the repository's `settings.ini`), or
else `settings.ini` beside the executable. `MMPC_SETTINGS=none` reads no file; the test commands of `tools/pipeline.py`
(`run`, `capture`, `difftest`, the VU1 tests) set that, so a capture or a comparison never depends on someone's settings.
Without a file the game starts as it always has: the emulated renderer, 4:3, in a window.

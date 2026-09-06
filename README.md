# dotfiles-utils

Native helper utilities for my Hyprland dotfiles.

## Build And Install

Release install to `~/.local`:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build/release --parallel
cmake --install build/release
```

Debug build:

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --parallel
```

The CMake project also defaults the install prefix to `$HOME/.local`. If no build type is provided, a build directory named `release` defaults to `Release`; other build directory names default to `Debug`.

## Installed Tools

- `lock-menu`
- `power-menu`
- `layout-menu`
- `screenshot-menu`
- `screenrecord-menu`
- `idle-menu`
- `clocks`
- `minibar`
- `screenfreeze`
- `battery-daemon`

## Hyprland Lua Example

```lua
hl.bind(
    mainMod .. ' + SHIFT + L',
    hl.dsp.exec_cmd('~/.local/bin/lock-menu', {
        float = true,
        center = true,
        stay_focused = true,
    }),
    { description = 'Open lock menu' }
)
```

## Minibar GPU Load

`minibar-gpu-load.c` builds the `minibar-gpu-load` target. It streams the i915
render-engine busy percentage once per second and exits when its stdin pipe closes.
It uses one perf counter, without per-process scanning or video/copy-engine metrics.

This target is deliberately excluded from the normal user-prefix install. The
chezmoi dotfiles' `install-minibar-gpu` script builds it incrementally, installs it
as root-owned `/usr/local/bin/minibar-gpu-load`, and ensures `CAP_PERFMON` is set.
The chezmoi after hook runs that idempotent installer on every apply, repairing
missing binaries or permissions without replacing unchanged installations.

To build just the helper:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --target minibar-gpu-load
```

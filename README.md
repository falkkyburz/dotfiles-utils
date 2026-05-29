# Qt Menus

Small Qt/QML menus for Hyprland.

## Build

```sh
cmake --preset debug
cmake --build --preset debug
cmake --install build/debug
```

## Hyprland Launch Commands

Lock menu:

```sh
hyprctl dispatch 'hl.dsp.exec_cmd("/home/falk/Work/Debug/qt-lock-menu/build/debug/lock-menu", { float = true, center = true, stay_focused = true })'
```

Power profile menu:

```sh
hyprctl dispatch 'hl.dsp.exec_cmd("/home/falk/Work/Debug/qt-lock-menu/build/debug/power-menu", { float = true, center = true, stay_focused = true })'
```

Screenshot menu:

```sh
hyprctl dispatch 'hl.dsp.exec_cmd("/home/falk/Work/Debug/qt-lock-menu/build/debug/screenshot-menu", { float = true, center = true, stay_focused = true })'
```

Screen recording menu:

```sh
hyprctl dispatch 'hl.dsp.exec_cmd("/home/falk/Work/Debug/qt-lock-menu/build/debug/screenrecord-menu", { float = true, center = true, stay_focused = true })'
```

Idle inhibit menu:

```sh
hyprctl dispatch 'hl.dsp.exec_cmd("/home/falk/Work/Debug/qt-lock-menu/build/debug/idle-menu", { float = true, center = true, stay_focused = true })'
```

## Lua Bind Example

```lua
hl.bind(
    mainMod .. ' + SHIFT + L',
    hl.dsp.exec_cmd('/home/falk/Work/Debug/qt-lock-menu/build/debug/lock-menu', {
        float = true,
        center = true,
        stay_focused = true,
    }),
    { description = 'Open lock menu' }
)
```

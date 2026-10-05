# LocationChangeRepro

A small Win32 app that shows how repainting a layered child window can flood the system with `EVENT_OBJECT_LOCATIONCHANGE` events.

## Background

The app opens a small, opaque, topmost window. Inside it is a `WS_EX_LAYERED` child window that is repainted about 32 times a second with `UpdateLayeredWindowIndirect()`. This copies how HUD's indicator window (`IndicatorWindowUI::Paint()`) works.

If `UPDATELAYEREDWINDOWINFO::psize` is passed on every frame, user32 repositions the window each time. That raises `EVENT_OBJECT_LOCATIONCHANGE` even though nothing moved. Occlusion trackers, such as Chrome/Edge's `NativeWindowOcclusionTrackerWin`, listen for these events across processes and end up doing extra work.

The fix is to pass `psize` only when the size actually changes.

## Controls

| Key     | Action |
|---------|--------|
| `Space` | Switch between passing `psize` on every frame (**ON** = the bug) and only when the size changes (**OFF** = the fix). Resets the counters. |
| `P`     | Pause or resume repainting |
| `Esc`   | Exit |

The window title shows the current mode, the number of frames painted, and how many `EVENT_OBJECT_LOCATIONCHANGE` events the app saw for its child window. It counts them with an in-process `SetWinEventHook`.

In **ON** mode the event count rises by about one per frame. In **OFF** mode it stays flat.

## Requirements

- Windows 8 or later. Layered child windows need this, and `app.manifest` declares it.
- Visual Studio with the **Desktop development with C++** workload (C++20).

## Building

Open `LocationChangeRepro.slnx`

```powershell
msbuild LocationChangeRepro\LocationChangeRepro.vcxproj /p:Configuration=Debug /p:Platform=x64
```

The output is `LocationChangeRepro\x64\Debug\LocationChangeRepro.exe`.

## Project layout

- `LocationChangeRepro/main.cpp`: the whole app (window setup, painting, WinEvent hook)
- `LocationChangeRepro/app.manifest`: declares Windows 8+ support so layered child windows are allowed
- `LocationChangeRepro/LocationChangeRepro.vcxproj`: the project file (Windows subsystem)

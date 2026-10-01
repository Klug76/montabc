# montabc

**EN** | [RU](README_RU.md)

A pure C rewrite of [montab](https://github.com/faxenoff/montab) — a Windows
sidebar taskbar with **always-on live previews** of every open window. Half a
dozen terminals look identical in the regular taskbar; here you glance at the
panel and instantly see which one is which. One panel per monitor, each
showing only the windows living on it. Docks to the left or right edge and
reserves the work area, so maximized windows never overlap the panel. Previews
update in real time straight from the DWM compositor — practically free in
terms of resources.

The port keeps the original's behavior and feature set but drops .NET
entirely: pure C17 over raw WinAPI, and the Release build is a zero-CRT
**~63 KB** exe importing only six system DLLs (no runtime, no dependencies).

## Features

- A panel on every monitor, listing only that monitor's windows.
- Live DWM previews with aspect ratio preserved; the active window gets an
  accent frame.
- Two sections: live previews on top, minimized windows below as compact
  strips with icon and title.
- Click to switch, double-click to minimize, ✕ to close, right-click to
  minimize, drag to reorder, Ctrl+wheel to zoom ×1–5, hover magnifier,
  overlay scrollbar.
- Drag the panel to switch edges or move it to another monitor; drag the
  inner edge to resize (3–50% of monitor width).
- Tray icon: hide/restore all panels, per-monitor menu, autostart.
- Settings per monitor in `%APPDATA%\montabc\settings.ini`.
- RU/EN interface, follows the system UI language.

See the [original README](https://github.com/faxenoff/montab#readme) for the
full controls table and the note about frozen Chromium browser previews.

## Differences from the original

A brief summary; the original is C#/.NET 11 (NativeAOT):

- **Zero-CRT build**: single ~63 KB exe vs ~2.1 MB; imports only 6 system DLLs
  and needs no .NET.
- **Flicker-free resize**: full AppBar negotiation (`ABM_QUERYPOS`/`SETPOS`)
  runs once on mouse release; during the drag only a light `SetWindowPos` with
  a forced redraw — maximized windows never jump or flicker mid-drag.
- **Architecture**: separate hidden host window instead of reusing the tray
  window; a fixed 512-window array instead of GC-managed collections.
- **Settings**: `%APPDATA%\montabc\settings.ini` (INI) — not compatible with
  the original's `settings.json`, no migration.
- **Windows 7 build**: extra `ReleaseWin7` configuration (v143 toolset,
  `WINVER 0x0601`); the original targets Windows 10 1809+ only.
- Everything else — window tracking/filtering, layout, previews, interaction,
  localization — is a deliberate 1:1 port.

Both versions can run side by side (different mutex, window class and
autostart registry names).

## Download

Grab a ready binary from the
[Releases](https://github.com/Klug76/montabc/releases) page:

- `montabc-x64.exe` — Windows 10 1809+ (x64)
- `montabc-win7-x64.exe` — Windows 7+ (x64)

Pushing a `v*` tag triggers a GitHub Actions workflow that builds both
configurations and publishes the release automatically.

## Building

Requires Visual Studio 2026 with the C++ workload (MSVC v145; the Win7
configuration uses the v143 toolset that ships with it) and MSBuild:

```sh
msbuild montabc.sln -m -p:Configuration=Release -p:Platform=x64
```

Output: `x64\Release\montabc.exe`. The `Debug` configuration builds a normal
CRT binary for debugging; `ReleaseWin7` produces the Windows 7-compatible
build in `x64\ReleaseWin7\`.

## License

[MIT](LICENSE). The original montab is © faxenoff; this C port is © Klug76.
Both copyright notices are preserved in the LICENSE file.

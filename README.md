# Cemu 2.6 Remote Toypad Build

A modified Cemu build that adds a local network listener for the LEGO Dimensions Toypad emulation, so figures can be loaded, removed, or moved without using the mouse-driven dialog. Pairs with [ToypadPicker](https://github.com/harrysof/LegoToypad), a standalone controller-driven app for browsing a tag library and sending figures to Cemu.

Cemu's existing Toypad dialog is untouched and still works normally. This is an additional interface, not a replacement.

## Why

Loading figures through Cemu's file dialog means alt-tabbing to a mouse every time. This build adds a listener so an external app can do it instead, controller in hand, no mouse required.

## How it works

```
ToypadPicker  --TCP (127.0.0.1)-->  Cemu listener  -->  g_dimensionstoypad
   (client)                           (server)          (existing Toypad API)
```

- Cemu opens a TCP listener on `127.0.0.1` only, once the emulated Toypad is attached.
- ToypadPicker scans a tag library, lets you pick a figure and slot with a controller, and sends it over the socket.
- Cemu decodes the message and calls the same `LoadFigure` / `RemoveFigure` / `MoveFigure` methods the GUI dialog already uses.

No authentication, no encryption — it's loopback-only by design.

## Setup

1. Enable the emulated Dimensions Toypad in Cemu's settings.
2. Note the listener port (`DimensionsToypadListenerPort`, defaults to `9191`).
3. Build [ToypadPicker](https://github.com/harrysof/LegoToypad) and drop your tag library in a folder named `Lego Dimensions Organized bins` — it's found automatically by walking up from the executable.
4. Set the same port in `ToypadPicker.ini`.
5. Launch Cemu, then ToypadPicker.

## ToypadPicker

The companion picker app lives in its own repo: **[harrysof/LegoToypad](https://github.com/harrysof/LegoToypad)**.

It's a standalone Win32 app (C++20, XInput + Winsock, no third-party UI libs) that scans your tag library, lets you pick a figure and slot with a controller, and sends it to this listener.

**Controls**

| Input | Action |
|---|---|
| D-pad / left stick | Move selection |
| A / Enter | Select figure / send to Cemu |
| B / Escape | Back out of slot selection |

Keyboard works too — no controller required.

**Controller handoff:** while ToypadPicker has focus, it signals Cemu (via a named event) to neutralize real controller input so gameplay doesn't react to picker navigation. Released immediately on minimize/close/focus loss. Requires the `Controller.cpp` change in *this* repo — a stock Cemu build won't honor the signal.

**Library:** recursively scans for `.bin` files exactly 180 bytes. Tested against 160 tags across 28 theme folders.

## Wire protocol

Every message starts with a 5-byte header:

| Offset | Field | Value |
|---|---|---|
| 0 | Command | `0x01` LOAD, `0x02` REMOVE, `0x03` MOVE |
| 1 | Dest pad | 1–3 |
| 2 | Dest slot | 0–6 |
| 3 | Old pad (MOVE only) | 0 for LOAD/REMOVE |
| 4 | Old slot (MOVE only) | 0 for LOAD/REMOVE |

| Command | Total size | Behavior |
|---|---|---|
| LOAD `0x01` | 185 bytes (header + 180 raw tag bytes) | `RemoveFigure(pad, index, true)` then `LoadFigure(tag_data, nullptr, pad, index)` |
| REMOVE `0x02` | 5 bytes | `RemoveFigure(pad, index, true)` |
| MOVE `0x03` | 5 bytes | `MoveFigure(pad, index, old_pad, old_index)` |

All fields are single bytes — no endianness concerns in v1.

A connection may carry multiple sequential messages; the listener reads exactly the byte count each command requires. Invalid pad/slot/reserved values or unknown commands are logged and the message is dropped, but unknown commands or truncated data close the connection, since the next message boundary can no longer be determined.

**Toypad slot → pad mapping** (as used by ToypadPicker):

| Slot | Pad |
|---|---|
| 0 | Center (2) |
| 1 | Left (1) |
| 2, 5, 6 | Right (3) |
| 3, 4 | Center (2) |

## Configuration

`DimensionsToypadListenerPort` — stored in `EmulatedUsbDevices` config section. Defaults to `9191`, accepts 1–65535.

## Code map

| Path | Purpose |
|---|---|
| `src/Cafe/OS/libs/nsyshid/DimensionsNetworkListener.h/.cpp` | Listener thread, TCP framing, validation |
| `src/Cafe/OS/libs/nsyshid/BackendEmulated.cpp/.h` | Starts listener after Toypad attach, stops on teardown |
| `src/config/CemuConfig.h/.cpp` | `DimensionsToypadListenerPort` load/save |
| `src/Cafe/CMakeLists.txt` | Adds listener sources to build |
| `src/gui/wxgui/EmulatedUSBDevices/EmulatedUSBDeviceFrame.cpp` | Unmodified — original dialog still works |
| `src/input/api/Controller.cpp` | Named-event check for ToypadPicker's controller handoff |

## Implementation notes

- Listener starts only after the emulated Toypad attaches, so it's gated by `EmulateDimensionsToypad` and inactive if a physical Toypad has priority.
- `LoadFigure`/`RemoveFigure` take `m_dimensionsMutex` internally; `MoveFigure` is the existing composition of both. The listener calls these directly and shares the GUI's threading model — no new locking was introduced.
- `DimensionsMini::Save()` still no-ops on a null `FileStream`, so network-loaded figures stay in memory only, matching existing GUI behavior.
- No divergence from `TOYPAD_TECHNICAL.md` found at any integration point.

## Known limitations / not yet verified

- Tag library (160 files) confirmed all 180 bytes; wire layout in `main.cpp` matches spec.
- No CMake/compiler available in the dev environment used to write this — ToypadPicker has not been compiled or run. End-to-end controller + socket testing still needed on an actual Windows machine.

## Design notes

ToypadPicker has no dependency on Cemu's source or memory — it only speaks the TCP protocol above. Anyone could write an alternate client against the same contract, and Cemu's stock dialog keeps working regardless of whether the listener or picker are in use.

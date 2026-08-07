# Cemu 2.6 Remote Toypad Build

This is a modified build of Cemu that adds remote control support for the LEGO Dimensions Toypad emulation. It works together with a separate companion app called ToypadPicker, which lets you browse your figure collection and load figures onto the virtual Toypad using a controller instead of Cemu's mouse-driven file dialog.

This repository contains the Cemu changes. ToypadPicker is a separate program you build alongside it.

## Explanation for regular users

Normally, loading a LEGO Dimensions figure into Cemu means opening a file browser with your mouse and picking a `.bin` file by hand every time you want to place a character on the Toypad. That works, but it is slow and awkward if you are sitting on a couch with a controller instead of a keyboard and mouse.

This build adds a small listener inside Cemu that waits for instructions on your own computer only, nothing over the internet or your home network. The companion app, ToypadPicker, connects to that listener and lets you:

- Scroll through your figure library with a controller
- Pick a figure
- Pick which Toypad position to place it on (left pad, right pad, or one of the three spots on the center pad)
- Send it straight to Cemu, instantly

While you are using the picker, it can also tell Cemu to ignore real controller input for a moment, so the game does not react while you are busy picking a figure. As soon as you close the picker, minimize it, or click back into Cemu, normal controller input to the game resumes.

Cemu's original Toypad dialog still works exactly as before. This does not replace anything, it just adds another way to load figures that is faster if you have a controller and a folder of tag files.

### What you need

- This Cemu build, with the emulated Dimensions Toypad turned on in settings
- The ToypadPicker app, built separately (see its own instructions)
- A folder of your own `.bin` tag files, each exactly 180 bytes
- Both programs pointed at the same port number (9191 by default)

### Basic setup

1. Turn on the emulated Dimensions Toypad in Cemu.
2. Leave the listener port as default (9191) or change it and remember the new number.
3. Build ToypadPicker and place your tag library in a folder named `Lego Dimensions Organized bins` somewhere it can find automatically by looking upward from where the program is running.
4. Make sure `ToypadPicker.ini` has the same port number as Cemu.
5. Start Cemu and load your game, then start ToypadPicker.
6. Use the controller or keyboard in ToypadPicker to pick a figure and a Toypad slot.

If something is not working, ToypadPicker shows a status line explaining what went wrong, for example a missing tag file or a failed connection to Cemu.

---

## Explanation for developers

This project is two pieces of software that talk to each other over a local TCP socket. Cemu is the server, ToypadPicker is the client. Nothing here touches Cemu's emulated memory directly, and ToypadPicker has no dependency on Cemu's source code at all, it only speaks the wire protocol described below.

### Overview of the Cemu-side change

A new loopback-only TCP listener was added beside the existing nsyshid Dimensions Toypad implementation. It is started only after the emulated Toypad device has successfully attached, so it is gated behind `EmulateDimensionsToypad` and never runs if a physical Toypad has taken priority instead.

The listener binds strictly to `127.0.0.1`. It never listens on a public or LAN interface. There is no authentication or encryption, this is an intentional decision based on the threat model that only local processes on the same machine can reach it.

A connection can carry more than one message in sequence. Because TCP is a stream and does not preserve message boundaries on its own, the listener always reads exactly the number of bytes required for the command it is currently parsing.

### Wire protocol

Every message begins with a five byte header:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | uint8 | Command: 0x01 LOAD, 0x02 REMOVE, 0x03 MOVE |
| 1 | uint8 | Destination pad: 1, 2, or 3 |
| 2 | uint8 | Destination slot index: 0 through 6 |
| 3 | uint8 | Old pad, used only by MOVE, must be 0 for LOAD and REMOVE |
| 4 | uint8 | Old slot index, used only by MOVE, must be 0 for LOAD and REMOVE |

- **LOAD (0x01)** is always exactly 185 bytes: the five byte header plus 180 raw tag bytes, kept in the same order they appear in the original `.bin` file. LOAD first clears the destination slot with `RemoveFigure(pad, index, true)`, then calls `LoadFigure(tag_data, nullptr, pad, index)`.
- **REMOVE (0x02)** is exactly the five byte header. It calls `RemoveFigure(pad, index, true)`.
- **MOVE (0x03)** is exactly the five byte header. Destination is bytes 1 and 2, source is bytes 3 and 4. It calls `MoveFigure(pad, index, old_pad, old_index)`.

Every field is a single byte, so byte order is not a concern in this version of the protocol.

Invalid pad or slot values, invalid reserved bytes, and unknown command bytes are logged and the specific message is ignored. However, an unknown command or a truncated message closes that client's connection entirely, because at that point the listener can no longer reliably tell where the next message starts.

### Configuration

`DimensionsToypadListenerPort` is a new setting stored in the existing `EmulatedUsbDevices` section of `CemuConfig`. It defaults to 9191 when not present, and accepts any port from 1 to 65535.

### Where the Cemu-side code lives

- `src/Cafe/OS/libs/nsyshid/DimensionsNetworkListener.h/.cpp`: the listener thread itself, TCP framing, message validation, and the calls into the existing `g_dimensionstoypad` API.
- `src/Cafe/OS/libs/nsyshid/BackendEmulated.cpp/.h`: owns the listener instance, starts it right after the emulated Toypad attaches, and stops and joins the thread during backend teardown.
- `src/config/CemuConfig.h/.cpp`: defines, loads, and saves `DimensionsToypadListenerPort`.
- `src/Cafe/CMakeLists.txt`: adds the new listener source files to the `CemuCafe` build target.

`src/gui/wxgui/EmulatedUSBDevices/EmulatedUSBDeviceFrame.cpp` was left untouched. Its existing Load, Create, Clear, and Move dialog still calls the same underlying `DimensionsUSB` methods, just through the mouse-driven UI instead of the socket. Both paths run side by side without conflict.

### Integration assumptions that were checked

`LoadFigure`, `RemoveFigure`, and `MoveFigure` are still declared in `Dimensions.h`, and `g_dimensionstoypad` is still defined in `Dimensions.cpp`, matching the version of Cemu this was built against. `LoadFigure` and `RemoveFigure` take `m_dimensionsMutex` internally, and `MoveFigure` is just the existing composition of those two calls that the GUI already used. Because of that, the listener calls these public methods directly and safely shares the same threading model the GUI already relies on, there is no separate locking scheme introduced.

`DimensionsMini::Save()` still returns immediately if its `FileStream` is null, which means figures loaded over the network intentionally stay in memory only. They are not written out to a local `.bin` file automatically. This matches existing Cemu behavior for the GUI path and was a deliberate choice, not an oversight.

No divergence from `TOYPAD_TECHNICAL.md` was found at any of these integration points.

### Overview of ToypadPicker

ToypadPicker is a standalone Windows application, written in native C++20, using Win32 and GDI for the window, XInput for controller polling, and Winsock for the TCP connection. No third-party UI or controller libraries are used, which keeps the binary small and the dependency list short.

Build it separately from Cemu:

```powershell
cmake -S ToypadPicker -B ToypadPicker/build
cmake --build ToypadPicker/build --config Release
```

### Figure library scanning

The picker automatically scans a folder named `Lego Dimensions Organized bins` for `.bin` files that are exactly 180 bytes, searching recursively through subfolders. It locates this folder by walking upward from its own executable directory, so building and running it from within this repository works without any manual path configuration.

The tag library used during development and testing contained 160 valid 180 byte tags spread across 28 theme folders. The automatic recursive scan replaced an earlier hardcoded test list from the original version 1 planning, since a real library of this size made a fixed list impractical.

### Controls

- D-pad or left stick: move the selection
- A or Enter: choose a figure, or send it to Cemu
- B or Escape: back out of slot selection

Keyboard input is also supported, so the app is usable without a controller connected.

### Controller exclusivity handoff

While ToypadPicker has focus, it signals Cemu through a small named-event mechanism to neutralize real controller input to the game, so Cemu is not simultaneously receiving both picker navigation and gameplay input from the same controller. That signal is released immediately when the picker is minimized, closed, or loses focus to Cemu, restoring normal controller input right away.

This feature depends on a corresponding change in Cemu's `Controller.cpp` included in this repository. An unmodified stock Cemu build cannot provide this exclusivity to an external XInput application, since it has no awareness of the named event the picker sets.

### Listener protocol as implemented by the picker

The picker implements the LOAD record exactly as Cemu expects it:

```
byte 0       0x01             LOAD command
byte 1       pad              1..3
byte 2       slot index       0..6
bytes 3..4   0x00, 0x00       reserved
bytes 5..184 raw tag data     exactly 180 bytes
```

It opens a TCP connection to `127.0.0.1` and sends all 185 bytes in one go. The target port is read from `ToypadPicker.ini` next to the executable and defaults to 9191. This must match Cemu's `DimensionsToypadListenerPort` setting or the connection will not reach the right place.

The seven selectable Toypad positions map to Cemu's existing pad layout as follows: slot 0 is center pad 2, slot 1 is left pad 1, slots 2, 5, and 6 are right pad 3, and slots 3 and 4 are also center pad 2.

### Status reporting and error handling

The picker's status line reports missing or invalid tag files, failure to connect to Cemu, interrupted sends, and successful sends. A successful send only confirms that the full 185 byte message was accepted by the local TCP connection, actual figure loading is then handled entirely by Cemu's listener and its normal `LoadFigure` call.

### What was and was not verified

The supplied 160 file tag library was scanned and confirmed to be entirely 180 byte files. The wire layout implemented in `main.cpp` was checked against `LISTENER_IMPLEMENTATION.md` and matches. This development environment did not have CMake or a C++ compiler available, so the standalone picker binary has not actually been compiled or run here. End-to-end verification of the controller polling and socket exchange still needs to happen on an actual Windows build machine.

### Design intent

ToypadPicker deliberately has no dependency on Cemu's source tree. It only relies on the documented socket contract described above, it does not read or write Cemu's process memory, and it does not modify or replace any existing Cemu GUI behavior. This keeps the two projects loosely coupled: anyone could write a different client that speaks the same protocol, and Cemu's existing Toypad dialog keeps working unchanged regardless of whether the picker or listener are used at all.

# DGO Reprint Route Audio Fix

Restores sound in the original *Densha de GO!* and *Densha de GO! 2* routes of the arcade **Densha de GO!! 復刻版 (reprint, `DGOREPR`)** when the game runs without TeknoParrot.

## The problem

The launcher and menus have sound, but as soon as a DGO1 or DGO2 route starts the game goes completely silent. Through TeknoParrot the same routes have sound.

## Why it happens

The game reads the cabinet's volume knob from the arcade's USB I/O board through `ArrowheadUSBIO.dll`. That DLL reports success even when no board is connected, and the volume knob then reads 0. The reprint routes take their volume from that knob, so they play at volume 0. The launcher uses a separate volume, so it still has sound. TeknoParrot's emulated board reports a real volume, which is why it works there.

## What this fix does

It adds a small stand-in `ArrowheadUSBIO.dll`. The stand-in loads the game's original DLL (renamed to `ArrowheadUSBIO_orig.dll`), passes every call straight through to it, and after each I/O update sets the volume knob reading to the level you choose. Nothing else changes: input, keyboard mode and everything else behave exactly as before.

No game files are included in this package. The installer works with the copy of the original DLL you already have.

## Requirements

- Densha de GO!! arcade (TG4AC) **version 5.80.02** with the reprint folder `DGOREPR`. Other versions are untested.
- 64-bit Windows.

## Install

1. Copy everything in this package into your `DGOREPR` folder (the one with `DgoRepr.exe`). The folder that contains `DGOREPR` also works.
2. Run `install.bat`.

The installer renames `DGOREPR\TG4AC\Binaries\Win64\dll\ArrowheadUSBIO.dll` to `ArrowheadUSBIO_orig.dll` and copies the fix and `ArrowheadUSBIO_volume.ini` into that folder. Running it again updates the fix without touching your volume setting.

To install by hand, do the same thing in that `dll` folder.

## Change the volume

Edit `DGOREPR\TG4AC\Binaries\Win64\dll\ArrowheadUSBIO_volume.ini`:

```ini
[Volume]
Percent=100
```

`Percent` goes from 0 to 100. The game treats anything under 1 as muted. The setting is read when the game starts.

## Uninstall

Close the game and run `uninstall.bat` from the same place you ran `install.bat`. It deletes the fix and renames `ArrowheadUSBIO_orig.dll` back to `ArrowheadUSBIO.dll`.

To uninstall by hand, delete `ArrowheadUSBIO.dll` and `ArrowheadUSBIO_volume.ini` from the `dll` folder and rename `ArrowheadUSBIO_orig.dll` back to `ArrowheadUSBIO.dll`.

## TeknoParrot

TeknoParrot still works with the fix installed: it emulates the board on its own, and the stand-in only passes calls through.

## Antivirus warnings

A DLL that stands in for another one can look suspicious to some antivirus programs. The full source is in `src\` (about 100 lines of C). You can read it, or build the DLL yourself with `build.bat` and [llvm-mingw](https://github.com/mstorsjo/llvm-mingw).

## Technical details

- `ArrowheadUSBIOUpdate` fills a struct the game passes in. The word at offset `0x12` is the 8th analog channel, the volume knob.
- The game computes `volume = value / 65535`. Below 0.01 counts as muted, and 1.0 is full volume.
- Without a board connected, `ArrowheadUSBIOOpen` still returns 1, so the game calls `ArrowheadUSBIOUpdate` every frame and reads 0 from that channel.
- The stand-in writes `Percent * 65535 / 100` to that word after forwarding each update.

## License

MIT. See `LICENSE`.

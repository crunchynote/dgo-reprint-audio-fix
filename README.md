# DGO Reprint Route Audio Fix

If you run the arcade Densha de GO!! 復刻版 (the `DGOREPR` reprint of DGO1 and DGO2) without TeknoParrot, you've probably noticed the launcher has sound but the routes are completely silent. This fixes that.

The game reads the cabinet's volume knob through the USB I/O board, via `ArrowheadUSBIO.dll`. With no board plugged in, that DLL still reports success and the knob reads as 0, so the old routes play at zero volume. The launcher has its own volume setting, which is why it isn't affected, and TeknoParrot's board emulation sends a real value, which is why the routes work there. Specifically, the knob is the 16-bit value at offset `0x12` of the struct `ArrowheadUSBIOUpdate` fills in, and the game uses it as `value / 65535`, treating anything under 0.01 as muted.

The fix is a small stand-in `ArrowheadUSBIO.dll`. It loads the game's original DLL (renamed to `ArrowheadUSBIO_orig.dll`), passes every call through untouched, and just sets the volume knob after each update. Controls and everything else work exactly as before, and TeknoParrot still works with it installed. No game files are included; the installer uses the copy you already have.

I've only tested it on version 5.80.02 (64-bit Windows). If you try another version, let me know in an issue whether it worked. Some antivirus programs get suspicious of DLLs like this, so the source is in `src/` (about 100 lines of C) if you want to check it or build it yourself with `build.bat` and [llvm-mingw](https://github.com/mstorsjo/llvm-mingw). The build is reproducible, so you should get the same file as the release.

MIT licensed, see `LICENSE`.

## Install

Copy everything from the zip into your `DGOREPR` folder (the one with `DgoRepr.exe`) and run `install.bat`. It renames `TG4AC\Binaries\Win64\dll\ArrowheadUSBIO.dll` to `ArrowheadUSBIO_orig.dll` and puts the fix and `ArrowheadUSBIO_volume.ini` in that folder. You can also do that by hand.

To change the volume, edit `Percent` (0 to 100) in `TG4AC\Binaries\Win64\dll\ArrowheadUSBIO_volume.ini`. It's read when the game starts, and running `install.bat` again won't overwrite it.

To uninstall, close the game and run `uninstall.bat`, or delete `ArrowheadUSBIO.dll` and `ArrowheadUSBIO_volume.ini` from that folder and rename `ArrowheadUSBIO_orig.dll` back to `ArrowheadUSBIO.dll`.

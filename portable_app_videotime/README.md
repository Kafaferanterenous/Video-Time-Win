# VideoTime

VideoTime is a portable native Windows application that reads the duration of
MP4 files and displays the combined running time as `HH:MM:SS`.

## Version 1 scope

- Select multiple `.mp4` files.
- Read duration directly from the MP4 movie header without decoding video.
- Show each filename, duration, and full path.
- Remove selected entries or clear the list.
- Show the summed duration.
- Enter an hourly charge and continuously calculate the exact prorated total.
- Use a larger 13-point interface with a 15-point bold total.
- Switch the complete interface between Segoe UI and Consolas monospace.
- Open all video files read-only.

## Portability

The release uses the static Microsoft C runtime and Windows system APIs. The
portable result is a single file:

`build\portable\videotime.exe`

No application-local DLLs, codecs, installer, or FFmpeg package are required.
Windows system DLLs remain provided and serviced by Windows itself.

The executable contains its own VideoTime film-and-clock application icon.

## Build

Run `build.cmd` from a Visual Studio/CMake-equipped Windows computer.

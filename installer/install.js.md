# Watchy Caffeine browser installer

The installer serves the release binary from the same origin as the page. The esptool-js 0.7.1 module is downloaded from jsDelivr and loaded only after its Subresource Integrity SHA-256 matches. [esptool-js release](https://github.com/espressif/esptool-js/releases/tag/v0.7.1), [Web Serial API](https://developer.chrome.com/docs/capabilities/serial).

| Function | Purpose |
| --- | --- |
| `showStatus` | Set the live status message and indicate an error. |
| `appendLog` | Show the local loader log without the board's MAC address. |
| `loadFlasher` | Load the pinned flasher and release its temporary module URL. |
| `loadFirmware` | Check the release size, sector-rounded app bounds, and SHA-256 before use. |
| `checkWatch` | Require an ESP32-PICO-D4 with 4 MiB flash and the tested partition table at `0x8000`. |
| `installFirmware` | Request the port from the button click, check the hardware, write the single app at `0x10000`, verify its flash MD5, then reset and close the port. |

The flash command uses `keep` for flash mode, size, and frequency and disables whole-chip erase. esptool-js verifies the written bytes inside `writeFlash` using the precomputed MD5 of the SHA-256-checked payload. The supported app partition ends at `0x26d000`. A new release needs the size, hashes, and labels updated together.

The installer stays at 115200 baud to keep the browser from closing and reopening the serial port after the loader starts. The option under Connection details connects to an ESP32 already in download mode and skips the automatic reset sequence.

`WatchyLoader.readPacket` allows 350 ms for ESP32 ROM sync responses. Other commands retain the upstream timeouts. Connection details shows boot mode, sync failures, and serial stream errors with the raw serial trace omitted.

On an affected Chromium build on Linux, an idle serial port with `VMIN=0` and `VTIME=0` can be reported as disconnected as soon as Web Serial opens its read stream. The setup page links to the README workaround for the watch’s port. [Chromium fix](https://chromium-review.googlesource.com/c/chromium/src/+/8425442).

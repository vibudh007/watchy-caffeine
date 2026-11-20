const release = {
  url: new URL("../watchy_caffeine.bin", import.meta.url),
  size: 969264,
  sha256: "c76239a3045c2ee5966804deafcfa10561b644d11300d59c993ae1afd8bff51f",
  md5: "41706be8d7ab19e9128f3061599d6e1d",
};

const appStart = 0x10000;
const appLimit = 0x26d000;
const partitionTable = { start: 0x8000, size: 3072, md5: "aa35068fdadceccec82a93dcd21fa2d8" };
const loaderUrl = "https://cdn.jsdelivr.net/npm/esptool-js@0.7.1/bundle.js";
const loaderIntegrity = "sha256-1qSwt8m3R6c3DPqXYWSpYvVjMtX6p8O0XNuVoqix1FY=";

const install = document.querySelector("#install");
const resume = document.querySelector("#resume");
const status = document.querySelector("#status");
const progress = document.querySelector("#progress");
const details = document.querySelector("#details");
const log = document.querySelector("#log");

function showStatus(message, error = false) {
  status.textContent = message;
  status.toggleAttribute("data-error", error);
}

function appendLog(message) {
  if (/^MAC:\s/i.test(message.trimStart())) return;
  log.textContent += message;
  log.scrollTop = log.scrollHeight;
}

async function loadFlasher() {
  const response = await fetch(loaderUrl, { integrity: loaderIntegrity });
  if (!response.ok) throw new Error("The USB flasher could not be downloaded.");
  const moduleUrl = URL.createObjectURL(await response.blob());
  try {
    return await import(moduleUrl);
  } finally {
    URL.revokeObjectURL(moduleUrl);
  }
}

async function loadFirmware() {
  const response = await fetch(release.url);
  if (!response.ok) throw new Error("The firmware could not be downloaded.");
  const data = new Uint8Array(await response.arrayBuffer());
  const roundedSize = Math.ceil(data.length / 4096) * 4096;
  if (data.length !== release.size || appStart + roundedSize > appLimit) {
    throw new Error("The firmware size is unexpected.");
  }
  const hash = new Uint8Array(await crypto.subtle.digest("SHA-256", data));
  const sha256 = Array.from(hash, byte => byte.toString(16).padStart(2, "0")).join("");
  if (sha256 !== release.sha256) throw new Error("The firmware checksum did not match this release.");
  return data;
}

async function checkWatch(loader, chipDescription) {
  if (loader.chip.CHIP_NAME !== "ESP32" || !chipDescription.startsWith("ESP32-PICO-D4 (revision ")) {
    throw new Error("Select an ESP32-PICO-D4 Watchy 2.0.");
  }
  if (await loader.detectFlashSize() !== "4MB") {
    throw new Error("This release requires 4 MiB of flash.");
  }
  const digest = await loader.flashMd5sum(partitionTable.start, partitionTable.size);
  if (digest !== partitionTable.md5) throw new Error("The watch does not have the supported stock partition layout.");
}

async function installFirmware(resetMode) {
  install.disabled = resume.disabled = true;
  log.textContent = "";
  progress.hidden = true;
  progress.value = 0;
  let port;
  let transport;
  let loader;
  let verified = false;
  let reset = false;
  try {
    showStatus("Select the Watchy’s USB port.");
    port = await navigator.serial.requestPort();
    showStatus("Downloading and checking the firmware.");
    const [{ ESPLoader, Transport }, data] = await Promise.all([loadFlasher(), loadFirmware()]);
    transport = new Transport(port);
    loader = new ESPLoader({
      transport,
      baudrate: 115200,
      terminal: {
        clean() { log.textContent = ""; },
        write: appendLog,
        writeLine(message) { appendLog(message + "\n"); },
      },
    });
    showStatus("Checking the watch’s hardware and partition layout.");
    const chip = await loader.main(resetMode);
    await checkWatch(loader, chip);
    progress.hidden = false;
    showStatus("Writing firmware. Keep the watch connected.");
    await loader.writeFlash({
      fileArray: [{ data, address: appStart }],
      flashSize: "keep",
      flashMode: "keep",
      flashFreq: "keep",
      eraseAll: false,
      compress: true,
      calculateMD5Hash: () => release.md5,
      reportProgress: (_index, written, total) => {
        progress.value = Math.floor(written * 100 / total);
        if (written === total) showStatus("Verifying the firmware on the watch.");
      },
    });
    verified = true;
    await loader.after("hard_reset");
    reset = true;
    showStatus("Watchy Caffeine 0.1.0 is installed and verified. The watch is restarting.");
  } catch (error) {
    if (verified) {
      showStatus("The firmware was installed and verified. Unplug the USB cable and restart the watch.", true);
    } else if (!port && error?.name === "NotFoundError") {
      showStatus("No port selected. Connect the watch and try again.");
    } else {
      showStatus(`Installation stopped. ${error?.message ?? error}`, true);
      details.open = true;
    }
  } finally {
    if (transport && (port.readable || port.writable)) {
      if (loader && !reset) await loader.after("hard_reset").catch(() => {});
      await transport.disconnect().catch(() => {});
    }
    install.disabled = resume.disabled = false;
  }
}

if (window.isSecureContext && navigator.serial && crypto.subtle) {
  showStatus("Ready to connect.");
  install.disabled = resume.disabled = false;
  install.addEventListener("click", () => installFirmware("default_reset"));
  resume.addEventListener("click", () => installFirmware("no_reset"));
} else {
  showStatus("Open this page over HTTPS in desktop Chrome or Edge to install over USB.");
}

# Third-party notices

The [GNU GPLv3](LICENSE) applies to the original project source. The items below retain their upstream terms. The included application image contains Espressif SDK objects and the BMA423 feature image alongside the project code.

| Component | Source and notices |
| --- | --- |
| Project Nayuki QR Code generator | The MIT license is in the bundled [C source](source/watch/vendor/qrcodegen.c) and [header](source/watch/vendor/qrcodegen.h). |
| Barlow, Geist, and Fraunces font data | The SIL Open Font License and copyright notices are in the [combined font notice](source/watch/ui_font-license.txt) and the [Fraunces notice](source/watch/face_exp-Fraunces-OFL.txt). |
| BMA423 feature image | [`bma423.config`](source/candidate/main/bma423.config) contains the Bosch Sensortec 2020 BSD-3-Clause feature image extracted byte for byte from the [pinned Bosch driver](https://github.com/wasp-os/BMA423-Sensor-API/blob/e65f82683cc2e0d2d4bd8dcfa14089c54bf8787d/bma423.c). The [Bosch BSD-3-Clause license](source/candidate/main/bosch-notice.txt) accompanies the image. The [SQFMI notice](licenses/SQFMI-MIT.txt) credits the reference project. |
| ESP-IDF v5.5.6 | The compiled image uses ESP-IDF, including prebuilt Espressif radio components. See the [Apache 2.0 license](licenses/Espressif-Apache-2.0.txt), the [bundled SDK notices](licenses/ESP-IDF-Copyrights.rst), and the [additional permission for the exact linked prebuilt archives](ADDITIONAL-PERMISSION.md). |

The included Bosch feature image has SHA-256 `112f81c8baba6d8abbf000c01e24fa56abd9f0c55e0415600d49109c189a2d3e`. The pinned version uses a proprietary sensor instruction format and is supplied upstream as the C byte array used by this application.

Buildable ESP-IDF source and its pinned submodules are available through the [release's SDK commit](https://github.com/espressif/esp-idf/tree/880a203ae603d08a94d06aa01e0d6761958e5e9e). The build commands and display asset sources are described in the [README](README.md#build-from-source).

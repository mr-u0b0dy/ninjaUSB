# ninjaUSB
[![CC BY-NC 4.0][cc-by-nc-shield]][cc-by-nc]

## Overview

**ninjaUSB** is a project based on the nRF52840 dongle that aims to build Zephyr-based firmware for a Bluetooth Low Energy (BLE) Human Interface Device (HID) keyboard. This firmware will receive keystrokes from a PC via BLE and send those keystrokes through USB HID, enabling a seamless interaction between the two devices.

### Features

- **BLE HID Keyboard**: Connects to a PC over BLE to receive keystrokes.
- **USB HID Output**: Sends received keystrokes through USB HID interface.
- **Future Expansion**: Plans to develop advanced BadUSB functionalities, enhancing the capabilities of the device.


## Getting Started

Before getting started, make sure you have a proper Zephyr development
environment. Follow the official
[Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/getting_started/index.html).


## Prerequisite

Clone the project inside workspace directory of Zephyr and init west.

```shell
cd ~/zephyr-workspace
git clone https://github.com/mr-u0b0dy/ninjaUSB.git
cd ninjaUSB
```

### Building and running

To build the application, run the following command:

```shell
west build -b nrf52840dongle/nrf52840 app
```

## Contributing

We welcome contributions to the **ninjaUSB** project! 

## License

This work is licensed under a
[Creative Commons Attribution-NonCommercial 4.0 International License][cc-by-nc].

[![CC BY-NC 4.0][cc-by-nc-image]][cc-by-nc]

[cc-by-nc]: https://creativecommons.org/licenses/by-nc/4.0/
[cc-by-nc-image]: https://licensebuttons.net/l/by-nc/4.0/88x31.png
[cc-by-nc-shield]: https://img.shields.io/badge/License-CC%20BY--NC%204.0-lightgrey.svg

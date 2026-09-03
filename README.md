# Custom ESP32-S3 MP3 Player

A standalone, battery-powered digital audio player built from scratch using an ESP32-S3 microcontroller, custom designed PCB, and an integrated OLED interface.

---

## Technical Overview

This project was designed and built to create a lightweight, feature-rich audio player tailored for smooth navigation and efficient power management. combines custom PCB hardware design with low-level firmware development to process audio files from storage to analog output.

---

## Hardware Specifications & Features

* **Microcontroller:** ESP32-S3
* **Display:** 0.96" OLED Display (I2C)
* **User Interface:** Rotary Encoder with dedicated hardware pull-up resistors for precise menu navigation
* **Audio Output:** DAC output driving standard 3.5mm headphone audio
* **Power Management:** Battery-powered system with an integrated USB-C charging circuit, power switch, and real-time battery fuel gauge on the display
* **Storage & File System:** MicroSD card storage with custom library capabilities for playlist management

---

## Software & Firmware Features

* **Now Playing Screen:** Displays current track details alongside play/pause/skip and volume controls.
* **Settings Menu:** Customizable UI brightness, font options, and track shuffle/repeat toggles.
* **Navigation Architecture:** Features a custom boot screen, Main Menu, All Songs view, Playlists view, and a small About page.

---

## Tools & Software Used

* **PCB Design:** KiCad (Schematic Capture, Symbol Editor, and PCB Layout)
* **Firmware Development:** Arduino IDE (C / C++)
* **Protocols & Interfaces:** SPI (MicroSD), I2S/DAC (Audio Output), I2C (Display)

---

## Repository Contents

* `mp3_player_pcb.pdf` — Complete multi-layer PCB design files.
* `mp3_player_schematic.pdf` — Detailed schematic showing SPI communication, I2S routing, and power distribution.
* `mp3playercode.ino` — Complete C/C++ firmware source code.
* `product_list.txt` — Everything I purchased and soldered onto the board. all custom footprints on the PCB design were built with these specific materials in mind.  

Pictures/GIFs of finished project:

<img width="3176" height="2314" alt="IMG_0164" src="https://github.com/user-attachments/assets/1099c068-5498-4b49-b5b8-6d4a6cee238b" />

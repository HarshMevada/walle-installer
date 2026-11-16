# Chitu Robot — Web Installer

Firmware for **Chitu**, a little AI desk robot built on a Seeed XIAO ESP32-S3 Sense,
running the open-source [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) voice assistant.

**Install page:** https://harshmevada.github.io/walle-installer/ (Chrome or Edge on a computer, USB data cable)

## What Chitu can do
- AI voice chat via xiaozhi.me, wake word picked on the install page
- Animated drawn eyes with 20+ expressions (bounce, shake, hearts, sleeping)
- Vision: "what do you see?" sends a camera photo to the AI
- Tank-tread driving by voice, dancing, auto-roam with TOF10120 obstacle stop
- Phone console (`https://<robot-ip>/`): live camera, drive pad, hold-to-talk with the
  phone mic, Chitu's voice on the phone, room listening, text + emoji messages on the screen
- Pet mode: looks around, pulls faces, chirps, wiggles, naps, speaks up now and then
- Wi-Fi auto-retry, hardware test sketches for each part

## How it is built
Every push to `main` runs GitHub Actions: it fetches xiaozhi-esp32 (pinned commit), adds the
board in `firmware/walle-xiao-s3/`, builds one firmware per wake word and publishes the page
from `site/` on GitHub Pages. Tunable settings (pins, speeds, distances, pet mode) are in
`firmware/walle-xiao-s3/config.h`.

Hardware test sketches (Arduino) are in `tests/`.

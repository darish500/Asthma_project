# Asthma Risk Monitor

ESP32 firmware that reads particulate and gas sensors, estimates an asthma-risk
classification with a logistic-regression model, displays readings on an OLED,
and sounds a buzzer for a high-risk result. When connected to Wi-Fi, it sends
readings to Firebase Realtime Database; while offline, it queues readings in
LittleFS and retries after reconnecting.

> **Important:** This project is for experimentation and education only. It is
> not a medical device and must not be used to diagnose, treat, or make
> healthcare decisions. The gas-sensor conversions and calibration values are
> approximate; calibrate sensors for your hardware and environment before
> interpreting measurements.

## Hardware

The sketch is configured for an ESP32 and these sensors:

- PMS3003 particulate-matter sensor
- MQ135, MQ7, and MQ131 gas sensors
- DHT22 temperature/humidity sensor
- 128x64 SSD1306 I2C OLED
- Buzzer and switch

Default ESP32 pin assignments:

| Component | ESP32 pin |
| --- | --- |
| DHT22 data | GPIO 4 |
| MQ135 analog output | GPIO 34 |
| MQ7 analog output | GPIO 35 |
| MQ131 analog output | GPIO 32 |
| Buzzer | GPIO 25 |
| Switch | GPIO 27 |
| PMS3003 TX to ESP32 RX | GPIO 16 |
| PMS3003 RX to ESP32 TX | GPIO 17 |
| OLED SDA | GPIO 21 |
| OLED SCL | GPIO 22 |

Check your sensor breakout boards' voltage and wiring requirements before
connecting them. The MQ sensor load-resistor and reference-resistance values in
the sketch are starting points, not calibrated values.

## Arduino libraries

Install these libraries using Arduino IDE's Library Manager:

- Adafruit GFX Library
- Adafruit SSD1306
- DHT sensor library (Adafruit)
- ArduinoJson

`WiFi`, `HTTPClient`, `Wire`, and `LittleFS` are provided by the ESP32 Arduino
core. Select an ESP32 board and its serial port in Arduino IDE.

## Configure credentials

The sketch includes a local `secrets.h` file so Wi-Fi and Firebase settings are
not committed to Git:

1. Copy `secrets.example.h` to `secrets.h` in this folder.
2. Set your Wi-Fi name/password and Firebase URL, bucket, and device ID in
   `secrets.h`.
3. Compile and upload `asthma_monitor.ino`.

`secrets.h` and `.env` are ignored by Git. The `.env`/`.env.example` files are
provided as local/reference values only: Arduino firmware does not load dotenv
files, so configure the actual values in `secrets.h`. Do not put real
credentials in `secrets.example.h`.

The current firmware sends Firebase requests without an authentication token.
With restrictive database rules, writes will fail. Do not make the database
publicly writable to get the sketch working; add Firebase authentication to the
firmware and restrict database rules before using a real deployment.

## Model and sensor calibration

`model_parameters.h` and `scaler_parameters.h` contain the model weights and
feature scaling values used by the sketch. Keep their feature order aligned
with the order documented in `scaler_parameters.h`.

The MQ135/MQ7/MQ131 curve fits and `RO_*` values in the sketch are approximate
and require sensor-specific calibration. In particular, the MQ131 conversion
constants are placeholders. Validate readings with suitable reference
equipment before relying on them.

## Push to GitHub

Run these commands from this project folder. First check the repository root:

```sh
git rev-parse --show-toplevel
git status --short
```

The reported root should be this project folder. If it is a parent folder,
create a separate repository here with `git init` before staging anything;
otherwise Git may include unrelated files from that parent repository. Confirm
that `.env` and `secrets.h` are ignored:

```sh
git check-ignore .env secrets.h
```

Then stage only the files intended for GitHub:

```sh
git add README.md .gitignore .env.example secrets.example.h \
  asthma_monitor.ino model_parameters.h scaler_parameters.h
git status --short
git commit -m "Add asthma risk monitor firmware"
git branch -M main
git remote add origin https://github.com/YOUR_USERNAME/YOUR_REPOSITORY.git
git push
```

Replace the GitHub URL with your repository URL. If an `origin` remote is
already configured, keep it instead of adding it again. Never stage `.env` or
`secrets.h`. If credentials have already been pushed to a remote repository,
remove them from the source and rotate/revoke them; deleting them in a later
commit does not remove them from Git history.

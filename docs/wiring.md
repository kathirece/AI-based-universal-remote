# Hardware connections

The OLED and MPU6050 share the ESP32 I2C bus. The three physical buttons remain available as a fallback while gesture recognition is being trained or tested.

| Module | Module pin | ESP32 connection | Notes |
|---|---|---|---|
| SSD1306 OLED | SDA | GPIO 21 | Shared I2C data |
| SSD1306 OLED | SCL | GPIO 22 | Shared I2C clock |
| SSD1306 OLED | VCC | 3.3 V | Confirm the voltage supported by the module |
| SSD1306 OLED | GND | GND | Common ground |
| MPU6050 | SDA | GPIO 21 | Default address `0x68` when AD0 is low |
| MPU6050 | SCL | GPIO 22 | Shared I2C clock |
| MPU6050 | VCC | 3.3 V | Use the voltage specified for the breakout board |
| MPU6050 | GND | GND | Common ground |
| IR transmitter driver | Signal | GPIO 18 | IR output used by `IRremoteESP8266` |
| UP button | One terminal | GPIO 33 | Other terminal to GND; internal pull-up enabled |
| DOWN button | One terminal | GPIO 25 | Other terminal to GND; internal pull-up enabled |
| SELECT button | One terminal | GPIO 32 | Other terminal to GND; internal pull-up enabled |

## IR LED safety

Do not power a high-current IR LED directly from an ESP32 GPIO. Use a transistor or MOSFET driver and a suitable LED current-limiting resistor. The exact resistor and driver values depend on the LED, supply voltage, and transistor/module used. All modules must share a common ground.

## Power

The project report and prototype use a regulated supply/buck converter. Confirm its output voltage with a multimeter before connecting the ESP32 and sensors.

Note: All modifications to your device are at your own risk.

This project makes use of the following libraries:

 * [homie-esp8266][1]
 * [Adruino HLW8012][2]

Please install them and any dependencies prior to using this project.

In order to program the KCM 70011 Wifi Smart Plug, it's necessary to
solder leads onto the esp8266 carrier (this device uses a [TYWE3S][3]
version of the esp8266).  Use the following connections:

![Wiring](/docs/wiring.jpg)

The pinout is:

 * TXD0 (left, bottom-most pin when viewed as above, orange)
 * RXD0 (directly above TXD0, yellow)
 * GND (left, top-most pin, brown)
 * Vcc (right, top-most pin, red)

Be sure your programming device uses 3.3V Vcc and signaling.

Configure the Arduino IDE as a Generic ESP8266 Module (enable esp
modules support via the board manager), default settings are used
with an SPIFFS config of 1M/128K (I used 115200 baud).  As this
program uses SPIFFS data, you'll need to both upload the data
files ([Tools-ESP8266 Sketch Data Upload])[4] and upload the program
to the device.  In order to enter programming mode, hold the
button on the device (GPIO0) while applying power and release.
This will need to be done once for each the data and the program.

Note that while the SPIFFS data files fit within a 64K SPIFFS config,
at the time of this writing, 128K is necessary for it to [work][5].

A nice feature of homie-esp8266 is that it supports "over the air"
update using MQTT.  An example command line for this, using the
python scripts provided in the homie-esp8266 library:

```
# python ota_updater.py -l <MQTT server> -t "homie/" -i "<device name>" /tmp/arduino_build_xxxxxx/HomieSwitch.ino.bin
```

Other observations, ADC, GPIO16, and GPIO2 appear to be unused on the
device, potentially allowing the addition of an analog temperature
sensor or various other devices, possibly including an I2C bus.

[1]:https://github.com/marvinroger/homie-esp8266
[2]:https://bitbucket.org/xoseperez/hlw8012
[3]:https://docs.tuya.com/en/hardware/WiFi-module/wifi-e3s-module.html
[4]:http://esp8266.github.io/Arduino/versions/2.0.0/doc/filesystem.html#uploading-files-to-file-system
[5]:https://github.com/marvinroger/homie-esp8266/issues/469

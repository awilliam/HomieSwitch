Note: All modifications to your device are at your own risk.

This project makes use of the following libraries:

 * [homie-esp8266][1]
 * [Arduino HLW8012][2]

Please install them and any dependencies prior to using this project.

In order to program the KMC 70011/30130WB/30401WA Wifi Smart Plug, it's
necessary to solder leads onto the esp8266 carrier (this device uses a
[TYWE3S][3] version of the esp8266).  Use the following connections:

KMC 70011:
![Wiring](/docs/wiring.jpg)

KMC 30130WB:
![1port-wiring-1](/docs/1port-wiring-1.jpg)
![1port-wiring-2](/docs/1port-wiring-2.jpg)

KMC 30401WA:
![4port-overview](/docs/4port-overview.jpg)
![4port-wiring](/docs/4port-wiring.jpg)

The TYWE3S pinout is:

 * TXD0 (left, bottom-most pin when viewed as above, orange)
 * RXD0 (directly above TXD0, yellow)
 * GND (left, top-most pin, brown)
 * Vcc (right, top-most pin, red)

Be sure your programming device uses 3.3V Vcc and signaling.

Configure the Arduino IDE as a Generic ESP8266 Module (enable esp
modules support via the board manager), default settings are used with
an SPIFFS config of 1M/128K (I used 115200 baud).  As this program
uses SPIFFS data, you'll need to both upload the data files
([Tools-ESP8266 Sketch Data Upload][4]) and upload the program to the
device.  Be sure to select either the 1-port or 4-port #define in the
code before compiling.  In order to enter programming mode, hold the
button on the device (GPIO0) while applying power and release.
This will need to be done once for each the data and the program.

Note that the 4-port module uses GPIO16 for the button, making it
a bit more difficult to enter programming mode.  I used tweezers
to bridge GPIO0 to ground while inserting the UART wiring harness
into the USB FTDI adapter.  GPIO0 on the TYWE3S is the middle pin
between the RX and ground connection, the third pin from either.

Note that while the SPIFFS data files fit within a 64K SPIFFS config,
at the time of this writing, 128K is necessary for it to [work][5].

A nice feature of homie-esp8266 is that it supports "over the air"
update using MQTT.  An example command line for this, using the
python scripts provided in the homie-esp8266 library:

```
# python ota_updater.py -l <MQTT server> -t "homie/" -i "<device name>" /tmp/arduino_build_xxxxxx/HomieSwitch.ino.bin
```

For the backend, I use Moquitto MQTT server and node-red.  You can
example node-red flow looks like this:

![Flow](/docs/node-red-editor.png)

With a resulting dashboard ui of:

![UI](/docs/node-red-ui.jpg)

An older flow, prior to making use of ranges, can be found in [docs](/docs/node-red.json)

Other observations (KMC 70011): ADC, GPIO16, and GPIO2 appear to be
unused on the device, potentially allowing the addition of an analog
temperature sensor or various other devices, possibly including an I2C
bus.  Of course RXD0 (GPIO3) and TXD0 (GPIO1) are also free after
programming, and they're already soldered for programming...  I chose
not to purse this because ground on the ESP8266 is not referenced to
earth ground, it seems to run on a 120V carrier, beware.

Code tested on both v1.3 and v1.4 KMC 70011 PCB versions, tested on
PCB version as pictured on others.

Note that while the electro-mechanical switch on all of these has a
static power draw when the relay is turned "on" (~1W), only the KMC
30130WB seems to have measurable power draw when the outlet is "off"
(as measured via kill-a-watt).

For the single port outlets, I've configured the button to toggle the
switch.  Holding for 10s will enter configuration mode in Homie.  For
the 4-port outlet, the switch led is on when the module has power.  A
momentary press increments through the outlets and the LED will blink
to indicate the selected outlet.  A longer press (1-5s) will toggle
the state of the selected outlet.  Selection will timeout after 15s.

[1]:https://github.com/marvinroger/homie-esp8266
[2]:https://bitbucket.org/xoseperez/hlw8012
[3]:https://docs.tuya.com/en/hardware/WiFi-module/wifi-e3s-module.html
[4]:http://esp8266.github.io/Arduino/versions/2.0.0/doc/filesystem.html#uploading-files-to-file-system
[5]:https://github.com/marvinroger/homie-esp8266/issues/469

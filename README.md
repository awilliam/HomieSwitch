Note: All modifications to your device are at your own risk.

This project makes use of the following libraries:

 * [homie-esp8266][1]
 * [Arduino HLW8012][2]

Please install them and any dependencies prior to using this project.

In order to program the KMC 70011/30130WB/30401WA Wifi Smart Plug or
KMC 20405/20406/20504-1406WA Wifi Smart Power strip, it's necessary to
solder leads onto the esp8266 carrier (these device uses a [TYWE3S][3]
version of the esp8266, except for the 20405 which uses an
[ESP-12F][4]).  Use the following connections:

KMC 70011:
![Wiring](/docs/wiring.jpg)

KMC 30130WB:
![1port-wiring-1](/docs/1port-wiring-1.jpg)
![1port-wiring-2](/docs/1port-wiring-2.jpg)

KMC 30401WA:
![4port-overview](/docs/4port-overview.jpg)
![4port-wiring](/docs/4port-wiring.jpg)

KMC 20405:
![20405-image](/docs/20405.jpg)

KMC 20406:
![20406-image](/docs/20406.jpg)

HausBell SM-PW701U:
![SM-PW701U-image](/docs/SM-PW701U.jpg)

If unclear from pictures, for all esp8266 carriers the pinout is the
same.  Using the orientation showin for the 70011 device:

 * TXD0 (left, bottom-most pin when viewed as above, orange)
 * RXD0 (directly above TXD0, yellow)
 * GND (left, top-most pin, brown)
 * Vcc (right, top-most pin, red)

Be sure your programming device uses 3.3V Vcc and signaling.

Configure the Arduino IDE as a Generic ESP8266 Module (enable esp
modules support via the board manager), default settings are used with
an SPIFFS config of 1M/128K (I used 115200 baud).  The ESP-12F
potentially has more flash, but we're not taking advantage of it, so
the same settings are used even for that model.  As this program uses
SPIFFS data, you'll need to both upload the data files
([Tools-ESP8266 Sketch Data Upload][5]) and upload the program to the
device.  Be sure to select either the 1-port, 4-port, or strip #define
in the code before compiling.  In order to enter programming mode,
GPIO0 needs to be held low when the device first receives power from
the USB programming device.  For the 70011 and 30130WB Smart Plugs,
this is simply a matter of holding the button on the device while
inserting/attaching the programmer or leads.  For the other devices,
I use a pair of tweezers to bridge GPIO0 to ground while applying
power.  GPIO0 is middle pin between the GND pin and RXD0 pin, third
pin from each.  This will need to be done once for each the data and
the program.

Note that while the SPIFFS data files fit within a 64K SPIFFS config,
at the time of this writing, 128K is necessary for it to [work][6].

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
(as measured via kill-a-watt).  This is also the only device so far
where I observe that there is no transformer as part of the DC power
supply to the esp device.  The power transistor in this device gets
notably warm to the touch any time the device is plugged in.  I would
therefore disrecommend the KMC mini smart plug from my experience.
I also found that the current rating and operating temperature ranges
specified on the Amazon listings do not match that on the packaging
(16A advertised vs 15A on packaging, -20 to +70C advertised, -10 to
+40C on packaging) for the recieved items (also true for the 4-port
plug with three switched outlets).  I posted review comments on
Amazon.com about these issues, but they were rejected or later
removed.  In fact all of the review comments for the mini plug
(B078H4XHBM) were suspiciously removed on or about July 10, 2018.
In my personal opinion, it seems that Amazon is censoring reviews
that discuss any sort of reprogramming of these devices, and I'm
suspicious Amazon is not acting benevolently in regards to reviews
for certain vendors, including KMC.

For the single port outlets, I've configured the button to toggle the
switch with the LED state matching the outlet state.  For the 4-port
outlet and power strips, the switch led is on when the module has
power.  A momentary press increments through the outlets and the LED
will blink to indicate the selected outlet.  A longer press (1-5s)
will toggle the state of the selected outlet.  Selection will timeout
after 15s.  For all devices, holding for 10s will enter configuration
mode in Homie.

For Smart Power Strips, switch #5 controls the USB port.  The 20406
strip does not have an LED to indicate USB power (though it does have
unpopulated traces for it on the PCB), while the LED on the 20405 is
powered via the USB supply itself, which can have significant lag
turning on and off, the latter depending on the load on the USB ports.

For the HausBell SM-PW701U the red LED is hardwired with the relay, so
the blue LED is on to indicate power to the device.  There is no power
meter in this device, but the interior shows notably better quality
and the device is UL listed.

[1]:https://github.com/marvinroger/homie-esp8266
[2]:https://bitbucket.org/xoseperez/hlw8012
[3]:https://docs.tuya.com/en/hardware/WiFi-module/wifi-e3s-module.html
[4]:https://www.elecrow.com/download/ESP-12F.pdf
[5]:http://esp8266.github.io/Arduino/versions/2.0.0/doc/filesystem.html#uploading-files-to-file-system
[6]:https://github.com/marvinroger/homie-esp8266/issues/469

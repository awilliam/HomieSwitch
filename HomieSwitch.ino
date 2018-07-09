/*
 * KMC 70011/30130WB/30401WA Smart Plug implementing Homie convention MQTT
 * 
 * Copyright (C) 2018 by Alex Williamson <alex.l.williamson@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <Homie.h>
#include <HLW8012.h>

/* COMPILER SETTINGS: Board: Generic ESP8266 Module, 80MHz, 26MHz, 40MHz, 1M (128K SPIFFS) */

/* Pick ONE, and only one */
//#define KMC_4_OUTLET	// Supports KMC 30401WA
#define KMC_1_OUTLET	// Supports KMC 70011, 30130WB

#ifdef KMC_1_OUTLET
#define FW_NAME		"aw-kmc-1port-switch"
static uint8_t relays[] = { 14 };
#define PIN_BUTTON	0
#define PIN_LED		13
#else
#define FW_NAME		"aw-kmc-4port-switch"
static uint8_t relays[] = { 15, 13, 14 };
#define PIN_BUTTON	16
#define PIN_LED		1
#endif

#define FW_VERSION	"2.0.0"

/* Required for binary detection in homie-ota */
const char *__FLAGGED_FW_NAME = "\xbf\x84\xe4\x13\x54" FW_NAME "\x93\x44\x6b\xa7\x75";
const char *__FLAGGED_FW_VERSION = "\x6a\x3f\x3e\x0e\xe1" FW_VERSION "\xb0\x30\x48\xd4\x1a";

static uint8_t num_relays = sizeof(relays) / sizeof(relays[0]);

#define LED_ON		LOW

#define PIN_HLW_CF	4
#define PIN_HLW_CF1	5
#define PIN_HLW_SEL	12

/*
 * Can't really locate or read these in-circuit, but these give relatively reasonable
 * values and can be improved using the calibration interface.
 */
#define CURRENT_RESISTOR                ( 0.003 )
#define VOLTAGE_RESISTOR_UPSTREAM       ( 2300000 )
#define VOLTAGE_RESISTOR_DOWNSTREAM     ( 1000 )

static HomieNode controlNode("control", "switch");
static HomieNode monitorNode("monitor", "sensors");
static HomieNode settingsNode("settings", "configuration");

static HLW8012 hlw8012;

#define REPORT_INTERVAL	( 60 * 1000 )

#define BLINK_ON_INTERVAL	250
#define BLINK_OFF_INTERVAL	500
#define BLINK_END_INTERVAL	( 1 * 1000 )
#define BLINK_TIMEOUT		( 15 * 1000 )

static unsigned long last_report;

static bool ota_in_progress = false;

static uint8_t switch_state = 0;

static bool stateHandler(HomieRange range, String value)
{
	if (!range.isRange)
		return false;

	if (range.index < 1 || range.index > num_relays)
		return false;

	if (value != "on" && value != "off")
		return false;

	if (value == "on") {
		digitalWrite(relays[range.index - 1], HIGH);
		switch_state |= (1 << (range.index - 1));
		if (num_relays == 1)
			digitalWrite(PIN_LED, LED_ON);
	} else {
		digitalWrite(relays[range.index - 1], LOW);
		switch_state &= (~(1 << (range.index - 1)));
		if (num_relays == 1)
			digitalWrite(PIN_LED, !LED_ON);
	}

	controlNode.setProperty("state").setRange(range).send(value);
	return true;
}

/*
 * De-bounce the button ourselves, not sure if using Bounce2 would interfere with homie-esp8266
 * use of the button for re-programming.
 */
static void buttonHandler(void)
{
	static unsigned long buttonDownTime = 0;
	static byte lastButtonState = HIGH;
	static bool buttonPressHandled = false;
	static uint8_t blinkMode = 0, blinkStatus = 0;
	static unsigned long blinkTime = 0;
	static unsigned long blinkTimeout = 0;

	byte buttonState = digitalRead(PIN_BUTTON);

	if (buttonState != lastButtonState) {
		if (buttonState == LOW) {
			buttonDownTime = millis();
			buttonPressHandled = false;
		} else if (!buttonPressHandled) {
			unsigned long dt = millis() - buttonDownTime;
			if (dt >= 90 && dt <= 900) {
				if (num_relays > 1) {
					blinkMode++;
					if (blinkMode > num_relays)
						blinkMode = 0;

					blinkStatus = blinkMode * 2;
					blinkTime = 0;
				} else {
					HomieRange range = { .isRange = true, range.index = 1 };
					stateHandler(range, switch_state ? "off" : "on");
				}

				buttonPressHandled = true;
			} else if (num_relays > 1 && dt > 900 && dt <= 5000) {
				HomieRange range = { .isRange = true, range.index = blinkMode };
				stateHandler(range, switch_state & (1 << (blinkMode - 1)) ? "off" : "on");
				blinkMode = 0;
				digitalWrite(PIN_LED, LED_ON);

				buttonPressHandled = true;
			}

			blinkTimeout = millis();
		}

		lastButtonState = buttonState;
	}

	if (blinkMode) {
		if (millis() - blinkTimeout > BLINK_TIMEOUT) {
			blinkMode = 0;
			digitalWrite(PIN_LED, LED_ON);
		} else if (!blinkTime || (millis() - blinkTime > (blinkStatus ? (blinkStatus & 0x1 ? BLINK_ON_INTERVAL : BLINK_ON_INTERVAL) : BLINK_END_INTERVAL))) {
			if (!blinkStatus)
				blinkStatus = blinkMode * 2;
			else
				blinkStatus--;

			digitalWrite(PIN_LED, blinkStatus & 1 ? LED_ON : !LED_ON);
			blinkTime = millis();
		}
	}
}

static void monitorHandler(void)
{
	if (millis() - last_report > REPORT_INTERVAL) {
		monitorNode.setProperty("V").setRetained(false).send(String(hlw8012.getVoltage(), DEC));
		monitorNode.setProperty("A").setRetained(false).send(String(switch_state ? hlw8012.getCurrent() : 0.0, 3));
		monitorNode.setProperty("W").setRetained(false).send(String(switch_state ? hlw8012.getActivePower() : 0, DEC));
		monitorNode.setProperty("VA").setRetained(false).send(String(switch_state ? hlw8012.getApparentPower() : 0, DEC));
		monitorNode.setProperty("pf").setRetained(false).send(String(100 * (switch_state ? hlw8012.getPowerFactor() : 1.0), 1));
		monitorNode.setProperty("Ws").setRetained(false).send(String(hlw8012.getEnergy(), DEC));

		last_report = millis();
	}
}

static bool statsHandler(HomieRange range, String value)
{
	if (value != "true")
		return false;

	hlw8012.resetEnergy();
	settingsNode.setProperty("stats-reset").setRetained(false).send(String("OK"));
	return true;
}

/*
 * Pass "(int)Volts,(double)Amps,(int)Watts" to the calibrate node to fine tune the reporting data.
 * This should be done with the switch "on" and a resistive load, such as an incandescent light bulb,
 * attached and running long enough for the readings to stabilize.  The higher wattage and more stable
 * the load, the better.  Pass "0,0,0" to reset the calibration to stock.  The device will post the
 * new multipliers as a retained MQTT message for persistence, they are not stored in the device.
 */
static bool calibrateHandler(HomieRange range, String value)
{
	int i, j;

	i = value.indexOf(',');
	if (i > 0) {
		j = value.indexOf(',', i + 1);
		if (j > i) {
			unsigned int voltage, power;
			double current;

			voltage = value.substring(0, i).toInt();
			current = value.substring(i + 1, j).toFloat();
			power = value.substring(j + 1).toInt();

			if (voltage == 0 && current == 0 && power == 0) {
				hlw8012.resetMultipliers();
				settingsNode.setProperty("calibrate").setRetained(false).send(String("Reset"));
				settingsNode.setProperty("multipliers/set").setRetained(true).send("");
				return true;
			} else if (voltage > 0 && current > 0 && power > 0) {
				hlw8012.expectedCurrent(current);
				hlw8012.expectedVoltage(voltage);
				hlw8012.expectedActivePower(power);
				settingsNode.setProperty("calibrate").setRetained(false).send("OK");
				settingsNode.setProperty("multipliers/set").setRetained(true).send(String(String(hlw8012.getVoltageMultiplier()) + "," + String(hlw8012.getCurrentMultiplier()) + "," + String(hlw8012.getPowerMultiplier())));
				return true;
			} else
				settingsNode.setProperty("calibrate").setRetained(false).send(String("Invalid format, unable to parse data"));
		} else
			settingsNode.setProperty("calibrate").setRetained(false).send(String("Invalid format, no second comma"));
	} else
		settingsNode.setProperty("calibrate").setRetained(false).send(String("Invalid format, no first comma"));

	return false;
}

/*
 * multipliers is exposed as a settable node, but we expect that it's self set via the calibration above
 * and therefore an expected format.  It's not recommended to provide a trivial interface to this setting.
 */
static bool multipliersHandler(HomieRange range, String value)
{
	int i, j;
	double voltage, current, power;

	i = value.indexOf(',');
	j = value.indexOf(',', i + 1);

	voltage = value.substring(0, i).toFloat();
	current = value.substring(i + 1, j).toFloat();
	power = value.substring(j + 1).toFloat();

	hlw8012.setVoltageMultiplier(voltage);
	hlw8012.setCurrentMultiplier(current);
	hlw8012.setPowerMultiplier(power);

	settingsNode.setProperty("multipliers").setRetained(false).send("OK");
	
	return true;
}

static void ICACHE_RAM_ATTR hlw8012_cf1_interrupt(void)
{
	hlw8012.cf1_interrupt();
}

static void ICACHE_RAM_ATTR hlw8012_cf_interrupt(void)
{
	hlw8012.cf_interrupt();
}

static void loopHandler(void)
{
	/* Seems like a good idea to stay quiet during update */
	if (ota_in_progress)
		return;

	buttonHandler();
	monitorHandler();
}

static void setupHandler(void)
{
	hlw8012.begin(PIN_HLW_CF, PIN_HLW_CF1, PIN_HLW_SEL, HIGH, true);
	hlw8012.setResistors(CURRENT_RESISTOR, VOLTAGE_RESISTOR_UPSTREAM, VOLTAGE_RESISTOR_DOWNSTREAM);
	attachInterrupt(PIN_HLW_CF1, hlw8012_cf1_interrupt, CHANGE);
	attachInterrupt(PIN_HLW_CF, hlw8012_cf_interrupt, CHANGE);

	/*
	 * With multiple outlets, the LED indicates power to the module, with a single outlet the LED
	 * indicates the power state of the outlet.
	 */
	digitalWrite(PIN_LED, num_relays == 1 ? !LED_ON : LED_ON);

	monitorNode.setProperty("V").setRetained(false).send(String(0, DEC));
	monitorNode.setProperty("A").setRetained(false).send(String(0.0, 3));
	monitorNode.setProperty("W").setRetained(false).send(String(0, DEC));
	monitorNode.setProperty("VA").setRetained(false).send(String(0, DEC));
	monitorNode.setProperty("pf").setRetained(false).send(String(100.0, 1));
	monitorNode.setProperty("Ws").setRetained(false).send(String(0, DEC));
}

void onHomieEvent(const HomieEvent& event) {
	switch (event.type) {
	case HomieEventType::STANDALONE_MODE:
		break;
	case HomieEventType::CONFIGURATION_MODE:
		break;
	case HomieEventType::NORMAL_MODE:
		break;
	case HomieEventType::OTA_STARTED:
		ota_in_progress = true;
		break;
	case HomieEventType::OTA_PROGRESS:
		break;
	case HomieEventType::OTA_FAILED:
		ota_in_progress = false;
		break;
	case HomieEventType::OTA_SUCCESSFUL:
		ota_in_progress = false;
		break;
	case HomieEventType::ABOUT_TO_RESET:
		break;
	case HomieEventType::WIFI_CONNECTED:
		break;
	case HomieEventType::WIFI_DISCONNECTED:
		break;
	case HomieEventType::MQTT_READY:
		break;
	case HomieEventType::MQTT_DISCONNECTED:
		break;
	case HomieEventType::MQTT_PACKET_ACKNOWLEDGED:
		break;
	case HomieEventType::READY_TO_SLEEP:
		break;
	}
}

void setup(void)
{
	uint8_t i;

	for (i = 0; i < num_relays; i++) {
		pinMode(relays[i], OUTPUT);
		digitalWrite(relays[i], LOW);
	}

	pinMode(PIN_LED, OUTPUT);
	digitalWrite(PIN_LED, !LED_ON);

	Homie_setFirmware(FW_NAME, FW_VERSION);
	Homie_setBrand("HomieByAW");
	/* Holding the button for 10s will cause the device to enter config mode */
	Homie.setResetTrigger(PIN_BUTTON, LOW, 10000);
	Homie.setSetupFunction(setupHandler);
	Homie.setLoopFunction(loopHandler);
	Homie.onEvent(onHomieEvent);
	Homie.disableLogging();
	Homie.setLedPin(PIN_LED, LED_ON);

	controlNode.advertiseRange("state", 1, num_relays).settable(stateHandler);

	monitorNode.advertise("V");
	monitorNode.advertise("A");
	monitorNode.advertise("W");
	monitorNode.advertise("Ws");
	monitorNode.advertise("VA");
	monitorNode.advertise("pf");

	settingsNode.advertise("stats-reset").settable(statsHandler);
	settingsNode.advertise("calibrate").settable(calibrateHandler);
	settingsNode.advertise("multipliers").settable(multipliersHandler);

	Homie.setup();
}

void loop()
{
	Homie.loop();
}

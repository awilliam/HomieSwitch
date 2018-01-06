/*
 * KMC 70011 Smart Plug implementing Homie convention MQTT  
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

#define PIN_RELAY	14
#define PIN_LED		13
#define PIN_BUTTON	0
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

/*
 * Report data every 10s when on, 60s when off, can be modified via MQTT
 */
#define OFF_REPORT_INTERVAL	( 60 * 1000 )
#define ON_REPORT_INTERVAL	( 10 * 1000 )

static unsigned long off_report_interval = OFF_REPORT_INTERVAL;
static unsigned long on_report_interval = ON_REPORT_INTERVAL;

static unsigned long last_report;
static unsigned report_interval;

static bool switch_state;
static bool ota_in_progress = false;

static void setPower(bool on)
{
	digitalWrite(PIN_RELAY, on ? HIGH : LOW);
	digitalWrite(PIN_LED, on ? LOW : HIGH);
	switch_state = on;
	controlNode.setProperty("state").setRetained(true).send(on ? "on" : "off");
	Homie.getLogger() << "Swtich state is " << (on ? "on" : "off") << endl;

	report_interval = on ? on_report_interval : off_report_interval;
	last_report = millis() - report_interval + 2000; /* update report 2s from now */
}

static bool stateHandler(HomieRange range, String value)
{
	if (value != "on" && value != "off")
		return false;

	setPower(value == "on");
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

	byte buttonState = digitalRead(PIN_BUTTON);
	
	if (buttonState != lastButtonState) {
		if (buttonState == LOW) {
			buttonDownTime = millis();
			buttonPressHandled = false;
		} else if (!buttonPressHandled) {
			unsigned long dt = millis() - buttonDownTime;
			if (dt >= 90 && dt <= 900) {
				setPower(!switch_state);
				buttonPressHandled = true;
			}
		}
		lastButtonState = buttonState;
	}	
}

static void monitorHandler(void)
{
	if (millis() - last_report > report_interval) {
		monitorNode.setProperty("V").setRetained(false).send(String(hlw8012.getVoltage(), DEC));
		Homie.getLogger() << "Voltage: " << String(hlw8012.getVoltage(), DEC) << "V" << endl;

		monitorNode.setProperty("A").setRetained(false).send(String(hlw8012.getCurrent(), 3));
		Homie.getLogger() << "Current: " << String(hlw8012.getCurrent(), 3) << "A" << endl;

		monitorNode.setProperty("W").setRetained(false).send(String(hlw8012.getActivePower(), DEC));
		Homie.getLogger() << "Active Power: " << String(hlw8012.getActivePower(), DEC) << "W" << endl;

		monitorNode.setProperty("VA").setRetained(false).send(String(hlw8012.getApparentPower(), DEC));
		Homie.getLogger() << "Apparent Power: " << String(hlw8012.getApparentPower(), DEC) << "VA" << endl;

		monitorNode.setProperty("pf").setRetained(false).send(String(100 * hlw8012.getPowerFactor(), 1));
		Homie.getLogger() << "Power Factor: " << String(100 * hlw8012.getPowerFactor(), 1) << "%" << endl;

		monitorNode.setProperty("Ws").setRetained(false).send(String(hlw8012.getEnergy(), DEC));
		Homie.getLogger() << "Aggregate Energy: " << String(hlw8012.getEnergy(), DEC) << "Ws" << endl;

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

/*
 * The HLW8012 code seems to suggest a lower bound of 2s reporting, a value < 0 resets to defaults.  It's
 * expected that setting these intervals will be done using retained MQTT messages such that they are
 * reloaded after subscription.  These values are not stored on the device.
 */
static bool onintervalHandler(HomieRange range, String value)
{
	int val = value.toInt();
	
	if (val < 0) {
		on_report_interval = ON_REPORT_INTERVAL;
	} else {
		if (val < 2)
			val = 2;

		on_report_interval = val * 1000;
	}

	if (switch_state) {
		report_interval = on_report_interval;
		last_report = millis();
	}

	settingsNode.setProperty("on-reporting-interval").send(String(on_report_interval/1000, DEC));
	return true;
}

static bool offintervalHandler(HomieRange range, String value)
{
	int val = value.toInt();
	
	if (val < 0) {
		off_report_interval = OFF_REPORT_INTERVAL;
	} else {
		if (val < 2)
			val = 2;

		off_report_interval = val * 1000;
	}

	if (!switch_state) {
		report_interval = off_report_interval;
		last_report = millis();
	}

	settingsNode.setProperty("off-reorting-interval").send(String(off_report_interval/1000, DEC));
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

	/* A retained MQTT message can turn us "on" if that's the desired default state */
	setPower(false);
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
	Serial.begin(115200);
	pinMode(PIN_BUTTON, INPUT);
	pinMode(PIN_RELAY, OUTPUT);
	digitalWrite(PIN_RELAY, LOW);
	pinMode(PIN_LED, OUTPUT);
	digitalWrite(PIN_LED, HIGH);

	Homie_setFirmware("aw-hlw-switch", "1.0.0");
	Homie_setBrand("HomieByAW");
	/* Holding the button for 5s will cause the device to enter config mode */
	Homie.setResetTrigger(PIN_BUTTON, LOW, 5000);
	Homie.setSetupFunction(setupHandler);
	Homie.setLoopFunction(loopHandler);
	Homie.onEvent(onHomieEvent);

	controlNode.advertise("state").settable(stateHandler);

	monitorNode.advertise("V");
	monitorNode.advertise("A");
	monitorNode.advertise("W");
	monitorNode.advertise("Ws");
	monitorNode.advertise("VA");
	monitorNode.advertise("pf");

	settingsNode.advertise("stats-reset").settable(statsHandler);
	settingsNode.advertise("calibrate").settable(calibrateHandler);
	settingsNode.advertise("multipliers").settable(multipliersHandler);
	settingsNode.advertise("on-reporting-interval").settable(onintervalHandler);
	settingsNode.advertise("off-reporting-interval").settable(offintervalHandler);

	Homie.setup();
}

void loop()
{
	Homie.loop();
}

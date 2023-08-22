#include <RNG.h>
#include <WiFi.h>

#include "display.h"
#include "device.h"
#include "inet.h"
#include "lora.h"
#include "sdcard.h"
#include "daemon.h"

/* ************************************************************************** */

#include "config_id.h"
#include "config_device.h"

/* ************************************************************************** */

static bool setup_success;

void setup(void) {
	setup_success = false;

	LED::initialize();
	COM::initialize();
	OLED::initialize();
	setCpuFrequencyMhz(CPU_frequency);
	if (!SDCard::initialize()) goto error;
	if (!RTC::initialize()) goto error;
	if (!Sensor::initialize()) goto error;
	WIFI::initialize();
	if (!LORA::initialize()) goto error;
	DAEMON::run();

	setup_success = true;
	Display::println("Done setup");

error:
	OLED::display();
}

void loop(void) {
	if (!setup_success) {
		LED::flash();
		return;
	}
	LORA::Receive::packet();
	RNG.loop();
	yield();
}

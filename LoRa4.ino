#include <RNG.h>
#include <WiFi.h>

#include "display.h"
#include "device.h"
#include "inet.h"
#include "lora.h"
#include "daemon.h"

/* ************************************************************************** */

#include "config_id.h"
#include "config_device.h"

#if defined(CPU_FREQUENCY)
	#if !(CPU_FREQUENCY >= 20)
		#undef CPU_FREQUENCY
		#define CPU_FREQUENCY 20
	#endif
	#if defined(ENABLE_GATEWAY) && !(CPU_FREQUENCY >= 80)
		#undef CPU_FREQUENCY
		#define CPU_FREQUENCY 80
	#endif
#endif

#if !defined(SEND_INTERVAL)
	#define SEND_INTERVAL (ACK_TIMEOUT * (RESEND_TIMES + 2))
#endif

/* ************************************************************************** */

static bool setup_success;

void setup(void) {
	setup_success = false;

	LED::initialize();
	COM::initialize();
	OLED::initialize();

	#if defined(CPU_FREQUENCY)
		setCpuFrequencyMhz(CPU_FREQUENCY);
	#endif

	if (!Sensor::initialize()) goto error;
	Debug::println("DEBUG: Sensors initialized");
	WIFI::initialize();
	Debug::println("DEBUG: WiFi initialized");
	if (!LORA::initialize()) goto error;
	Debug::println("DEBUG: LoRa initialized");

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

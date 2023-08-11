#include <esp_pthread.h>
#include <RNG.h>
#include <WiFi.h>

#include "display.h"
#include "device.h"
#include "inet.h"
#include "lora.h"

/* ************************************************************************** */

#include "config_id.h"

#if !defined(ENABLE_GATEWAY)
	#define CPU_FREQUENCY 20
#endif

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

//	template <typename TYPE>
//	uint8_t rand_int(void) {
//		TYPE x;
//		RNG.rand((uint8_t *)&x, sizeof x);
//		return x;
//	}

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

	esp_pthread_cfg_t esp_pthread_cfg = esp_pthread_get_default_config();
	esp_pthread_cfg.stack_size = 4096;
	esp_pthread_cfg.pin_to_core = 1 ^ xPortGetCoreID() & 1;
	esp_pthread_set_cfg(&esp_pthread_cfg);
	Debug::println("DEBUG: pthread config set");

	WIFI::initialize();
	Debug::println("DEBUG: WiFi initialized");
	if (!LORA::initialize()) goto on_error;
	Debug::println("DEBUG: LoRa initialized");
	LORA::Send::ASKTIME();

	setup_success = true;
	Display::println("Done setup");

on_error:
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

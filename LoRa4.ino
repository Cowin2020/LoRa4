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
	#if !defined(NDEBUG)
		delay(5000);
	#endif
	setup_success = false;
	LED::initialize();
	COM::initialize();
	OLED::initialize();
	setCpuFrequencyMhz(CPU_frequency);
	if (!SDCard::initialize()) goto end;
	if (!RTC::initialize()) goto end;
	if (!Sensor::initialize()) goto end;
	WIFI::initialize();
	if (!LORA::initialize()) goto end;
	DAEMON::run();
	setup_success = true;
end:
	OLED::display();
}

void loop(void) {
	if (!setup_success) {
		LED::flash();
		return;
	}
	try {
		WIFI::loop();
		#if defined(ENABLE_OLED_SWITCH)
			static bool switched_off = false;
			pinMode(ENABLE_OLED_SWITCH, INPUT_PULLDOWN);
			if (digitalRead(ENABLE_OLED_SWITCH) == LOW) {
				if (!switched_off) {
					OLED::turn_off();
					switched_off = true;
				}
			}
			else {
				if (switched_off) {
					OLED::turn_on();
					switched_off = false;
				}
			}
		#endif
		#if defined(REBOOT_TIMEOUT)
			if (millis() - LORA::last_time > REBOOT_TIMEOUT)
				esp_restart();
		#endif
		RNG.loop();
		vTaskDelay(pdMS_TO_TICKS(IDLE_INTERVAL));  //	delay(IDLE_INTERVAL);
	}
	catch (...) {
		COM::println("ERROR: loop exception thrown");
	}
}

#include "display.h"
#include "device.h"

/* ************************************************************************** */

#ifdef ENABLE_LED
	namespace LED {
		void initialize(void) {
			pinMode(LED_BUILTIN, OUTPUT);
			digitalWrite(LED_BUILTIN, LOW);
		}

		void flash(void) {
			static bool light = true;
			digitalWrite(LED_BUILTIN, light ? HIGH : LOW);
			light = !light;
			delay(200);
		}
	}
#endif

namespace COM {
	#ifdef ENABLE_COM_OUTPUT
		void initialize(void) {
			if (CPU_frequency && CPU_frequency < 80)
				Serial.begin(COM_BAUD * 80 / CPU_frequency);
			else
				Serial.begin(COM_BAUD);
		}

		void dump(char const *const label, void const *const memory, size_t const size) {
			Serial.printf("%s (%04X)", label, size);
			for (size_t i = 0; i < size; ++i) {
				unsigned char const c = reinterpret_cast<unsigned char const *>(memory)[i];
				Serial.printf(" %02X", c);
			}
			Serial.write('\n');
		}
	#endif
}

namespace OLED {
	Adafruit_SSD1306 SSD1306(OLED_WIDTH, OLED_HEIGHT);
	std::mutex mutex;

	void turn_on(void) {
		std::lock_guard<std::mutex> lock(mutex);
		SSD1306.ssd1306_command(SSD1306_CHARGEPUMP);
		SSD1306.ssd1306_command(0x14);
		SSD1306.ssd1306_command(SSD1306_DISPLAYON);
	}

	void turn_off(void) {
		std::lock_guard<std::mutex> lock(mutex);
		SSD1306.ssd1306_command(SSD1306_CHARGEPUMP);
		SSD1306.ssd1306_command(0x10);
		SSD1306.ssd1306_command(SSD1306_DISPLAYOFF);
	}

	#if defined(ENABLE_OLED_OUTPUT)
		class String message;

		void initialize(void) {
			std::lock_guard<std::mutex> lock(mutex);
			SSD1306.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
			SSD1306.invertDisplay(false);
			SSD1306.setRotation(OLED_ROTATION);
			SSD1306.setTextSize(1);
			SSD1306.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
			SSD1306.clearDisplay();
			SSD1306.display();
			SSD1306.setCursor(0, 0);
		}
	#endif
}

/* ************************************************************************** */

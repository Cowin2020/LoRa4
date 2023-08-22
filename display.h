#ifndef INCLUDE_DISPLAY_H
#define INCLUDE_DISPLAY_H

/* ************************************************************************** */

#include <Adafruit_SSD1306.h>

#define CLOCK_PCF85063TP 1
#define CLOCK_DS1307 2
#define CLOCK_DS3231 3
#define BATTERY_GAUGE_DFROBOT 1
#define BATTERY_GAUGE_LC709203F 2

#include "config_device.h"

#if !defined(COM_BAUND)
	#define COM_BAUD 115200
#endif
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#if !defined(OLED_ROTATION)
	#define OLED_ROTATION 2
#endif
#if !defined(OLED_I2C_ADDR)
	#define OLED_I2C_ADDR 0x3C
#endif

/* ************************************************************************** */

namespace LED {
	#ifdef ENABLE_LED
		extern void initialize(void);
		extern void flash(void);
	#else
		inline static void initialize(void) {}
		inline static void flash(void) {}
	#endif
}

namespace COM {
	#ifdef ENABLE_COM_OUTPUT
		extern void initialize(void);

		template <typename TYPE>
		inline void print(TYPE const x) {
			Serial.print(x);
		}

		template <typename TYPE>
		inline void println(TYPE x) {
			Serial.println(x);
		}

		template <typename TYPE>
		inline void println(TYPE const x, int option) {
			Serial.println(x, option);
		}

		extern void dump(char const *const label, void const *const memory, size_t const size);
	#else
		inline static void initialize(void) {
			Serial.end();
		}

		template <typename TYPE> inline void print([[maybe_unused]] TYPE x) {}
		template <typename TYPE> inline void println([[maybe_unused]] TYPE x) {}
		template <typename TYPE> inline void print([[maybe_unused]] TYPE x, [[maybe_unused]] int option) {}
		template <typename TYPE> inline void println([[maybe_unused]] TYPE x, [[maybe_unused]] int option) {}
		inline static void dump(
			[[maybe_unused]] char const *const label,
			[[maybe_unused]] void const *const memory,
			[[maybe_unused]] size_t const size
		) {}
	#endif
}

namespace OLED {
	extern Adafruit_SSD1306 SSD1306;

	extern void turn_on(void);
	extern void turn_off(void);

	#if defined(ENABLE_OLED_OUTPUT)
		extern class String message;

		extern void initialize(void);

		inline static void home(void) {
			SSD1306.clearDisplay();
			SSD1306.setCursor(0, 0);
		}

		template <typename TYPE>
		inline void print(TYPE x) {
			SSD1306.print(x);
		}

		template <typename TYPE>
		inline void println(TYPE x) {
			SSD1306.println(x);
		}

		template <typename TYPE>
		inline void println(TYPE const x, int const option) {
			SSD1306.println(x, option);
		}

		inline static void set_message(class String const &string) {
			message = string;
		}

		inline static void print_message(void) {
			print(message);
		}

		inline static void draw_received(void) {
			OLED::SSD1306.drawRect(125, 61, 3, 3, SSD1306_WHITE);
			OLED::SSD1306.display();
		}

		inline static void display(void) {
			SSD1306.display();
		}
	#else
		inline static void initialize(void) {
			SSD1306.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
			turn_off();
		}
		inline static void home(void) {}
		template <typename TYPE> inline void print([[maybe_unused]] TYPE x) {}
		template <typename TYPE> inline void println([[maybe_unused]] TYPE x) {}
		template <typename TYPE> inline void println([[maybe_unused]] TYPE x, [[maybe_unused]] int option) {}
		inline static void set_message([[maybe_unused]] class String const &string) {}
		inline static void print_message(void) {}
		inline static void draw_received(void) {}
		inline static void display(void) {}
	#endif
}

namespace Display {
	template <typename TYPE>
	inline void print(TYPE x) {
		COM::print(x);
		OLED::print(x);
	}

	template <typename TYPE>
	inline void println(TYPE x) {
		COM::println(x);
		OLED::println(x);
	}

	template <typename TYPE>
	inline void println(TYPE x, int option) {
		COM::println(x, option);
		OLED::println(x, option);
	}
}

namespace Debug {
	template <typename TYPE>
	[[maybe_unused]]
	inline void print([[maybe_unused]] TYPE x) {
		#if !defined(NDEBUG)
			COM::print(x);
		#endif
	}

	template <typename TYPE>
	[[maybe_unused]]
	inline void println([[maybe_unused]] TYPE x) {
		#if !defined(NDEBUG)
			COM::println(x);
		#endif
	}

	inline void dump(
		[[maybe_unused]] char const *const label,
		[[maybe_unused]] void const *const memory,
		[[maybe_unused]] size_t const size
	) {
		#if !defined(NDEBUG)
			COM::dump(label, memory, size);
		#endif
	}

	[[maybe_unused]]
	inline static void flush(void) {
		#if !defined(NDEBUG) && defined(ENABLE_COM_OUTPUT)
			Serial.flush();
		#endif
	}

	#if defined(NDEBUG)
		inline static void println_core(char const *const message) {}
	#else
		extern void println_core(char const *message);
	#endif
}

/* ************************************************************************** */

#endif
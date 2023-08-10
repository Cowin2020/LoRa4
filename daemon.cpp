#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include <Arduino.h>

#include "display.h"
#include "id.h"
#include "device.h"
#include "lora.h"
#include "sdcard.h"
#include "inet.h"
#include "daemon.h"

/* ************************************************************************** */

namespace DAEMON {
	void thread_delay(unsigned long int const ms) {
		//	TickType_t const ticks = ms / portTICK_PERIOD_MS;
		TickType_t const ticks = pdMS_TO_TICKS(ms);
		vTaskDelay(ticks>0 ? ticks : 1);
	}

	namespace Sleep {
		static std::vector<Timer> threads;

		size_t register_thread(void) {
			/* TODO */
		}
	}

	namespace AskTime {
		static std::atomic<unsigned long int> last_sync(0);

		[[noreturn]]
		void loop(void) {
			for (;;) {
				unsigned long int now = millis();
				unsigned long int duration = now - last_sync;
				if (duration < SYNCHONIZE_INTERVAL) {
					thread_delay(SYNCHONIZE_INTERVAL - duration);
					continue;
				}
				LORA::Send::ASKTIME();
				thread_delay(SYNCHONIZE_INTERVAL);
			}
		}
	}

	namespace Push {
		static std::atomic<SerialNumber> current_serial(0);
		static std::atomic<SerialNumber> acked_serial(0);
		static std::mutex mutex;
		static std::condition_variable condition;

		static void send_data(struct Data const *const data) {
			if (enable_gateway) {
				if (WIFI::upload(my_device_id, ++current_serial, data)) {
					OLED::draw_received();
				}
				else {
					COM::print("HTTP: unable to send data: time=");
					COM::println(String(data->time));
				}
			}
			else {
				/* TODO: add routing */
				for (unsigned int t=0; t<RESEND_TIMES; ++t) {
					LORA::Send::SEND(Device(0), ++current_serial, data);
					thread_delay(SEND_INTERVAL);
					if (acked_serial.load() == current_serial.load())
						break;
				}
			}
		}

		void data(struct Data const *const data) {
			SDCard::add_data(data);
			condition.notify_one();
		}

		[[noreturn]]
		void loop(void) {
			for (;;) {
				std::unique_lock<std::mutex> lock(mutex);
				condition.wait_for(lock, std::chrono::duration<unsigned long int, std::milli>(SEND_INTERVAL));
				struct Data data;
				if (!SDCard::read_data(&data)) continue;
				send_data(&data);
			}
		}
	}

	namespace Measure {
		static void print(struct Data const *const data) {
			OLED::home();
			Display::print("Device ");
			Display::println(my_device_id);
			data->println();
			OLED::display();
		}

		[[noreturn]]
		void loop(void) {
			for (;;) {
				Debug::print("DEBUG: Measure::run ");
				struct Data data;
				if (!Sensor::measure(&data)) {
					thread_delay(12345);
					continue;
				}
				print(&data);
				/* TODO */
				thread_delay(MEASURE_INTERVAL);
			}
		}
	}

	void run(void) {
		std::thread(AskTime::loop).detach();
		std::thread(Push::loop).detach();
		std::thread(Measure::loop).detach();
	}
}

/* ************************************************************************** */

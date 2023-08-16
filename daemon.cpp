#include <limits>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include <esp_pthread.h>
#include <RNG.h>

#include "id.h"
#include "display.h"
#include "device.h"
#include "lora.h"
#include "sdcard.h"
#include "inet.h"
#include "daemon.h"

/* ************************************************************************** */

template <typename TYPE>
static uint8_t rand_int(void) {
	TYPE x;
	RNG.rand((uint8_t *)&x, sizeof x);
	return x;
}

/* ************************************************************************** */

namespace DAEMON {
	void thread_delay(unsigned long int const ms) {
		//	TickType_t const ticks = ms / portTICK_PERIOD_MS;
		TickType_t const ticks = pdMS_TO_TICKS(ms);
		vTaskDelay(ticks>0 ? ticks : 1);
	}

	#if defined(ENABLE_SLEEP)
		namespace Sleep {
			static std::mutex sleep_mutex;
			static std::condition_variable sleep_condition;
			static std::mutex timer_mutex;
			static std::vector<struct Timer> timers;

			size_t register_thread(void) {
				static struct Timer const initial = {.start = 0, .stop = 0};
				size_t const index = timers.size();
				timers.push_back(initial);
				return index;
			}

			void time(size_t const timer_index, unsigned long int const milliseconds) {
				{
					std::lock_guard<std::mutex> lock(timer_mutex);
					struct Timer &timer = timers[timer_index];
					unsigned long int const now = millis();
					timer.start = now;
					timer.stop = now + milliseconds;
				}
				sleep_condition.notify_one();
			}

			void loop(void) {
				for (;;) {
					bool awake = false;
					unsigned long int duration = std::numeric_limits<unsigned long int>::max();
					{
						std::lock_guard<std::mutex> lock(timer_mutex);
						unsigned long int const now = millis();
						for (struct Timer &timer : timers)
							if (now - timer.start < timer.stop - timer.start) {
								if (duration > timer.stop - now)
									duration = timer.stop - now;
							}
							else
								awake = true;
					}
					if (duration > MEASURE_INTERVAL)
						duration = MEASURE_INTERVAL;

					if (awake || duration <= SLEEP_MARGIN) {
						std::unique_lock<std::mutex> lock(sleep_mutex);
						sleep_condition.wait_for(lock, std::chrono::duration<unsigned long int, std::milli>(duration));
					}
					else {
						Debug::print("DEBUG: sleep ");
						Debug::print(duration);
						Debug::println("ms");
						Debug::flush();
						LORA::sleep();
						esp_sleep_enable_timer_wakeup(duration * 1000);
						esp_light_sleep_start();
					}
					Debug::print("DEBUG: DAEMON::Time::loop core=");
					Debug::println(xPortGetCoreID());
				}
			}
		}
	#else
		namespace Sleep {
			size_t register_thread(void) {
				return 0;
			}
			void time(size_t timer_index, unsigned long int milliseconds) {}
			void loop(void) {}
		}
	#endif

	namespace Time {
		static std::mutex mutex;
		static std::condition_variable condition;

		void run(void) {
			condition.notify_one();
		}

		[[noreturn]]
		void loop(void) {
			for (;;) {
				try {
					Debug::print("DEBUG: DAEMON::Time::loop core=");
					Debug::println(xPortGetCoreID());

					struct FullTime fulltime;
					if (!NTP::now(&fulltime)) return;
					RTC::set(&fulltime);
					LORA::Send::TIME(&fulltime);

					OLED::home();
					Display::println("Synchronize: ");
					Display::println(String(fulltime));
					OLED::display();

					std::unique_lock<std::mutex> lock(mutex);
					condition.wait_for(lock, std::chrono::duration<unsigned long int, std::milli>(SYNCHONIZE_INTERVAL));
				}
				catch (...) {
					COM::println("ERROR: DAEMON::Time::loop exception thrown");
				}
			}
		}
	}

	namespace AskTime {
		static std::atomic<unsigned long int> last_synchronization;

		void synchronized(void) {
			last_synchronization = millis();
		}

		[[noreturn]]
		void loop(void) {
			size_t const sleep = Sleep::register_thread();
			last_synchronization = 0;
			LORA::Send::ASKTIME();
			for (;;) {
				try {
					Debug::print("DEBUG: DAEMON::AskTime::loop core=");
					Debug::println(xPortGetCoreID());

					unsigned long int now = millis();
					unsigned long int passed = now - last_synchronization;
					if (passed < SYNCHONIZE_INTERVAL) {
						Sleep::time(sleep, SYNCHONIZE_INTERVAL - passed);
						thread_delay(SYNCHONIZE_INTERVAL - passed);
					}
					else {
						LORA::Send::ASKTIME();
						thread_delay(SYNCHONIZE_TIMEOUT);
						Sleep::time(sleep, SYNCHONIZE_INTERVAL - SYNCHONIZE_TIMEOUT);
						thread_delay(SYNCHONIZE_INTERVAL - SYNCHONIZE_TIMEOUT + rand_int<uint8_t>());
					}
				}
				catch (...) {
					COM::println("ERROR: DAEMON::AskTime::loop exception thrown");
				}
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
				if (WIFI::upload(my_device_id, ++current_serial, data))
					OLED::draw_received();
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

		void ack(SerialNumber const serial) {
			acked_serial = serial;
		}

		[[noreturn]]
		void loop(void) {
			size_t const sleep = Sleep::register_thread();
			for (;;) {
				try {
					Debug::print("DEBUG: DAEMON::Push:loop core=");
					Debug::println(xPortGetCoreID());

					struct Data data;
					if (!SDCard::read_data(&data)) {
						std::unique_lock<std::mutex> lock(mutex);
						Sleep::time(sleep, SEND_IDLE_INTERVAL);
						condition.wait_for(lock, std::chrono::duration<unsigned long int, std::milli>(SEND_IDLE_INTERVAL));
					}
					std::thread(send_data, &data).detach();
					Sleep::time(sleep, SEND_INTERVAL);
					thread_delay(SEND_INTERVAL);
				}
				catch (...) {
					COM::println("ERROR: DAEMON::Push::loop exception thrown");
				}
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
			size_t const sleep = Sleep::register_thread();
			for (;;) {
				try {
					Debug::print("DEBUG: DAEMON::Measure::loop core=");
					Debug::println(xPortGetCoreID());

					struct Data data;
					if (Sensor::measure(&data)) {
						print(&data);
						Push::send_data(&data);
					}
					Sleep::time(sleep, MEASURE_INTERVAL);
					thread_delay(MEASURE_INTERVAL);
				}
				catch (...) {
					COM::println("ERROR: DAEMON::Measure::loop exception thrown");
				}
			}
		}
	}

	void run(void) {
		esp_pthread_cfg_t core_default = esp_pthread_get_default_config();
		core_default.stack_size = 4096;
		core_default.pin_to_core = xPortGetCoreID();
		esp_pthread_cfg_t core_opposite = core_default;
		core_opposite.pin_to_core = 1 ^ xPortGetCoreID() & 1;

		esp_pthread_set_cfg(&core_opposite);

		#if defined(ENABLE_SLEEP)
			std::thread(Sleep::loop).detach();
		#endif

		if (enable_gateway) {
			std::thread(Time::loop).detach();
		}
		else {
			std::thread(AskTime::loop).detach();
		}

		if (enable_measure) {
			std::thread(Push::loop).detach();
			esp_pthread_set_cfg(&core_default);
			std::thread(Measure::loop).detach();
		}
	}
}

/* ************************************************************************** */

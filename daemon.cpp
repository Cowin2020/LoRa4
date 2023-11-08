#include <limits>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include <RNG.h>

#include "id.h"
#include "display.h"
#include "device.h"
#include "lora.h"
#include "sdcard.h"
#include "inet.h"
#include "daemon.h"

/* ************************************************************************** */

#if !defined(SEND_INTERVAL)
	#define SEND_INTERVAL (ACK_TIMEOUT * (RESEND_TIMES + 2))
#endif

static bool const enable_sleep =
	#if defined(ENABLE_SLEEP)
		!enable_gateway
	#else
		false
	#endif
	;

template <typename TYPE>
static TYPE rand_int(void) {
	TYPE x;
	RNG.rand((uint8_t *)&x, sizeof x);
	return x;
}

/* ************************************************************************** */

namespace DAEMON {
	static esp_pthread_cfg_t esp_pthread_cfg;

	void thread_delay(unsigned long int const ms) {
		//	TickType_t const ticks = ms / portTICK_PERIOD_MS;
		TickType_t const ticks = pdMS_TO_TICKS(ms);
		vTaskDelay(ticks>0 ? ticks : 1);
		//	std::this_thread::sleep_for(std::chrono::duration<unsigned long int, std::milli>(ms));
	}

	static void yield(void) {
		vTaskDelay(1);
		//	taskYIELD();
		//	sched_yield();
		//	std::this_thread::yield();
	}

	namespace Sleep {
		static std::mutex sleep_mutex;
		static std::condition_variable sleep_condition;
		static std::mutex timer_mutex;
		static std::vector<struct Timer> timers;

		std::atomic<bool> keep_awake(false);

		size_t register_thread(void) {
			static struct Timer const initial = {.start = 0, .stop = 0};
			size_t const index = timers.size();
			timers.push_back(initial);
			return index;
		}

		void time(size_t const timer_index, unsigned long int const milliseconds) {
			{
				DEBUG_LOCK(debug_lock);
				Debug::print("DEBUG: DAEMOM::Sleep::time timer_index=");
				Debug::print(timer_index);
				Debug::print(" milliseconds=");
				Debug::println(milliseconds);
				Debug::flush();
			}
			{
				std::lock_guard<std::mutex> lock(timer_mutex);
				struct Timer &timer = timers[timer_index];
				unsigned long int const now = millis();
				timer.start = now;
				timer.stop = now + milliseconds;
			}
			sleep_condition.notify_one();
			yield();
		}

		void woke(size_t const timer_index) {
			time(timer_index, 0);
		}

		void loop(void) {
			if (!enable_sleep) return;
			if (keep_awake.load()) return;
			for (;;)
				try {
					Debug::print_thread("DEBUG: DAEMON::Sleep::loop");
					bool awake = false;
					unsigned long int duration = MEASURE_INTERVAL;
					{
						std::lock_guard<std::mutex> lock(timer_mutex);
						unsigned long int const now = millis();
						for (struct Timer &timer : timers)
							if (now - timer.start >= timer.stop - timer.start)
								awake = true;
							else {
								unsigned long int const d = timer.stop - now;
								if (duration > d) duration = d;
							}
					}
					if (awake || duration <= SLEEP_MARGIN) {
						std::unique_lock<std::mutex> lock(sleep_mutex);
						sleep_condition.wait_for(lock, std::chrono::duration<unsigned long int, std::milli>(duration));
					}
					else {
						{
							DEBUG_LOCK(debug_lock);
							Debug::print("DEBUG: DAEMON::Sleep::loop sleep ");
							Debug::print(duration);
							Debug::println("ms");
							Debug::flush();
						}
						LORA::sleep();
						esp_sleep_enable_timer_wakeup(duration * 1000);
						esp_light_sleep_start();
						LORA::wake();
					}
					yield();
				}
				catch (...) {
					COM::println("ERROR: DAEMON::Sleep::loop exception thrown");
				}
		}
	}

	namespace LoRa {
		[[noreturn]]
		void loop(void) {
			for (;;)
				try {
					LORA::Receive::packet();
					yield();
				}
				catch (...) {
					COM::println("ERROR: DAEMON::LoRa::loop exception thrown");
				}
		}
	}

	namespace Time {
		static std::mutex mutex;
		static std::condition_variable condition;

		void run(void) {
			condition.notify_one();
			yield();
		}

		[[noreturn]]
		void loop(void) {
			for (;;)
				try {
					Debug::print_thread("DEBUG: DAEMON::Time::loop");
					struct FullTime fulltime;
					if (NTP::now(&fulltime)) {
						RTC::set(&fulltime);
						LORA::Send::TIME(&fulltime);

						OLED_LOCK(oled_lock);
						OLED::home();
						Display::print("Synchronize: ");
						Display::println(String(fulltime));
						OLED::display();
					}

					yield();
					std::unique_lock<std::mutex> lock(mutex);
					condition.wait_for(lock, std::chrono::duration<unsigned long int, std::milli>(SYNCHONIZE_INTERVAL - 1));
				}
				catch (...) {
					COM::println("ERROR: DAEMON::Time::loop exception thrown");
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
			thread_delay(SYNCHONIZE_TIMEOUT);
			LORA::Send::ASKTIME();
			thread_delay(SYNCHONIZE_TIMEOUT);
			for (;;)
				try {
					Debug::print_thread("DEBUG: DAEMON::AskTime::loop");
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

	namespace Push {
		static std::mutex mutex;
		static std::condition_variable condition;
		static std::atomic<SerialNumber> current_serial(0);
		static std::atomic<SerialNumber> acked_serial(0);
		static std::atomic<bool> send_success;

		static void send_data(struct Data const data) {
			if (enable_gateway) {
				if (WIFI::upload(my_device_id, ++current_serial, &data)) {
					send_success.store(true);
					OLED_LOCK(oled_lock);
					OLED::draw_received();
				}
				else {
					COM::print("HTTP unable to send data: time=");
					COM::println(String(data.time));
				}
			}
			else {
				/* TODO: add routing */
				Debug::println("DEBUG: DAEMON::Push::send_data start");
				Sleep::keep_awake.store(true);
				for (unsigned int t=0; t<RESEND_TIMES; ++t) {
					LORA::Send::SEND(my_device_id, ++current_serial, &data);
					thread_delay(ACK_TIMEOUT);
					if (acked_serial.load() == current_serial.load()) {
						send_success = true;
						SDCard::next_data();
						break;
					}
				}
				Sleep::keep_awake.store(false);
				Debug::println("DEBUG: DAEMON::Push::send_data end");
			}
		}

		void data(struct Data const *const data) {
			Debug::println("DEBUG: DAEMON::Push::data");
			SDCard::add_data(data);
			condition.notify_one();
			yield();
		}

		void ack(SerialNumber const serial) {
			acked_serial = serial;
		}

		[[noreturn]]
		void loop(void) {
			size_t const sleep = Sleep::register_thread();
			thread_delay(500);
			for (;;)
				try {
					Debug::print_thread("DEBUG: DAEMON::Push::loop");
					struct Data data;
					send_success.store(false);
					if (SDCard::read_data(&data)) {
						esp_pthread_set_cfg(&esp_pthread_cfg);
						std::thread(send_data, data).detach();
					}
					std::unique_lock<std::mutex> lock(mutex);
					#if SEND_IDLE_INTERVAL > SEND_INTERVAL
						if (!send_success.load()) {
							unsigned long int const wait = SEND_IDLE_INTERVAL - SEND_INTERVAL;
							Sleep::time(sleep, wait);
							condition.wait_for(lock, std::chrono::duration<unsigned long int, std::milli>(wait));
							Sleep::woke(sleep);
						}
					#endif
					thread_delay(SEND_INTERVAL);
				}
				catch (...) {
					COM::println("ERROR: DAEMON::Push::loop exception thrown");
				}
		}
	}

	namespace CleanLog {
		void loop(void) {
			for (;;)
				try {
					thread_delay(CLEANLOG_INTERVAL);
					Debug::print_thread("DEBUG: DAEMON::CleanLog::loop");
					SDCard::clean_up();
				}
				catch (...) {
					COM::println("ERROR: DAEMON::CleanData::loop exception thrown");
				}
		}
	}

	namespace Measure {
		static void print_data(struct Data const *const data) {
			OLED_LOCK(lock);
			OLED::home();
			Display::print("Device ");
			Display::println(my_device_id);
			data->println();
			OLED::display();
		}

		[[noreturn]]
		void loop(void) {
			size_t const sleep = Sleep::register_thread();
			thread_delay(1000);
			for (;;)
				try {
					Debug::print_thread("DEBUG: DAEMON::Measure::loop");
					struct Data data;
					if (Sensor::measure(&data)) {
						print_data(&data);
						Push::data(&data);
					}
					else COM::println("Failed to measure");
					{
						DEBUG_LOCK(debug_lock);
						Debug::print("DEBUG: DAEMON::Measure::loop sleep ");
						Debug::println(millis());
						Debug::flush();
					}
					Sleep::time(sleep, MEASURE_INTERVAL);
					thread_delay(MEASURE_INTERVAL);
					{
						DEBUG_LOCK(debug_lock);
						Debug::print("DEBUG: DAEMON::Measure::loop wake ");
						Debug::println(millis());
					}
				}
				catch (...) {
					COM::println("ERROR: DAEMON::Measure::loop exception thrown");
				}
		}
	}

	void run(void) {
		esp_pthread_cfg = esp_pthread_get_default_config();
		esp_pthread_cfg.stack_size = 4096;
		esp_pthread_cfg.inherit_cfg = true;

		esp_pthread_set_cfg(&esp_pthread_cfg);
		std::thread(LoRa::loop).detach();

		if (enable_gateway) {
			esp_pthread_set_cfg(&esp_pthread_cfg);
			std::thread(Time::loop).detach();
		}
		else {
			esp_pthread_set_cfg(&esp_pthread_cfg);
			std::thread(AskTime::loop).detach();
		}

		if (enable_measure) {
			esp_pthread_set_cfg(&esp_pthread_cfg);
			std::thread(Push::loop).detach();
			esp_pthread_set_cfg(&esp_pthread_cfg);
			std::thread(Measure::loop).detach();
			esp_pthread_set_cfg(&esp_pthread_cfg);
			#if defined(ENABLE_SDCARD)
				std::thread(CleanLog::loop).detach();
			#endif
		}

		#if defined(ENABLE_SLEEP)
			{
				esp_pthread_cfg_t sleep_cfg = esp_pthread_cfg;
				sleep_cfg.stack_size = 2048;
				esp_pthread_set_cfg(&sleep_cfg);
				std::thread(Sleep::loop).detach();
			}
		#endif
	}
}

/* ************************************************************************** */

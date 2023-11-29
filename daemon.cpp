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

	struct Alarm {
		std::mutex mutex;
		std::condition_variable condition_variable;
		std::atomic<bool> awake;
		void notify(void);
	};

	void Alarm::notify(void) {
		awake.store(true);
		condition_variable.notify_all();
	}

	namespace Schedule {
		struct Timer {
			struct Alarm *alarm;
			millis_t start, duration;
			#if !defined(NDEBUG)
				class String name;
			#endif
		};

		static struct Alarm alarm;
		static std::vector<struct Timer> timer_list;
		static std::mutex timer_mutex;

		static void add_timer(struct Alarm *const timer_alarm, char const *const name) {
			std::lock_guard<std::mutex> lock(timer_mutex);
			struct Timer const timer {
				.alarm = timer_alarm,
				.start = 0,
				.duration = 0
				#if !defined(NDEBUG)
					,
					.name = String(name)
				#endif
			};
			timer_list.push_back(timer);
		}

		static void remove_timer(struct Alarm *const timer_alarm) {
			std::lock_guard<std::mutex> lock(timer_mutex);
			for (size_t i = 0; i < timer_list.size(); ++i)
				if (timer_list[i].alarm == timer_alarm) {
					timer_list[i] = timer_list[timer_list.size()-1];
					timer_list.pop_back();
					return;
				}
		}

		static void sleep(struct Alarm *const timer_alarm, millis_t const duration) {
			{
				millis_t now = millis();
				{
					DEBUG_LOCK(debug_lock);
					Debug::print("DEBUG: DAEMON::Schedule::sleep ");
					Debug::print(now);
					Debug::print('+');
					Debug::print(duration);
					Debug::print('=');
					Debug::println(now + duration);
				}
				std::lock_guard<std::mutex> lock(timer_mutex);
				for (struct Timer &timer: timer_list)
					if (timer.alarm == timer_alarm) {
						timer.start = now;
						timer.duration = duration;
						goto bed;
					}
				COM::println("ERROR: DAEMON::Schedule::sleep unregistered condition");
				return;
			}
		bed:
			{
				{
					DEBUG_LOCK(debug_lock);
					Debug::println("DEBUG: DAEMON::Schedule::sleep goto bed");
				}
				timer_alarm->awake.store(false);
				alarm.notify();
				std::unique_lock<std::mutex> lock(timer_alarm->mutex);
				timer_alarm->condition_variable.wait(lock, [timer_alarm] {return timer_alarm->awake.load();});
			}
		}

		static void fall_asleep(millis_t const duration) {
			{
				DEBUG_LOCK(debug_lock);
				Debug::print("DEBUG: DAEMON::Schedule::fall_asleep ");
				Debug::println(duration);
				Debug::flush();
			}
			#if !defined(ENABLE_SLEEP)
				thread_delay(duration);
			#else
				if (duration < SLEEP_MARGIN)
					thread_delay(duration);
				else {
					DEVICE_LOCK(device_lock);
					LORA::sleep();
					esp_sleep_enable_timer_wakeup(1000 * (duration - SLEEP_MARGIN));
					esp_light_sleep_start();
					LORA::wake();
				}
			#endif
		}

		void loop(void) {
			thread_delay(12345);
			for (;;)
				try {
					alarm.awake.store(false);
					bool wake = false;
					struct Timer const *soonest = nullptr;
					millis_t const now = millis();
					{
						DEBUG_LOCK(debug_lock);
						Debug::print("DEBUG: DAEMON::Schedule::loop now = ");
						Debug::println(now);
					}
					{
						std::lock_guard<std::mutex> lock(timer_mutex);
						for (struct Timer &timer: timer_list) {
							{
								DEBUG_LOCK(debug_lock);
								Debug::print("DEBUG: DAEMON::Schedule::loop timer \"");
								Debug::print(timer.name);
								Debug::print("\": ");
								Debug::print(timer.start);
								Debug::print('+');
								Debug::print(timer.duration);
								Debug::print('=');
								Debug::println(timer.start + timer.duration);
								Debug::flush();
							}
							if (!timer.duration)
								wake = true;
							else if (timer.duration <= now - timer.start) {
								timer.duration = 0;
								timer.alarm->awake.store(true);
								timer.alarm->condition_variable.notify_all();
								wake = true;
							}
							else if (
								(soonest == nullptr && timer.duration > 0) ||
								timer.start + timer.duration - now < soonest->start + soonest->duration - now
							)
								soonest = &timer;
						}
						if (!wake && soonest != nullptr) {
							fall_asleep(soonest->start + soonest->duration - now);
							continue;
						}
					}
					{
						DEBUG_LOCK(debug_lock);
						Debug::println("DEBUG: DAEMON::Schedule::loop start wait for condition");
					}
					std::unique_lock<std::mutex> lock(alarm.mutex);
					alarm.condition_variable.wait(lock, [] {return alarm.awake.load();});
					{
						DEBUG_LOCK(debug_lock);
						Debug::println("DEBUG: DAEMON::Schedule::loop stop wait for condition");
					}
				}
				catch (...) {
					COM::println("ERROR: DAEMON::Schedule::loop exception thrown");
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
		static struct Alarm alarm;

		void run(void) {
			alarm.notify();
			yield();
		}

		[[noreturn]]
		void loop(void) {
			Schedule::add_timer(&alarm, "DAEMON::Time");
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
					Schedule::sleep(&alarm, SYNCHONIZE_INTERVAL);
				}
				catch (...) {
					COM::println("ERROR: DAEMON::Time::loop exception thrown");
				}
		}
	}

	namespace AskTime {
		static std::atomic<unsigned long int> last_synchronization(0);
		static struct Alarm alarm;

		void synchronized(void) {
			last_synchronization = millis();
		}

		[[noreturn]]
		void loop(void) {
			Schedule::add_timer(&alarm, "DAEMON::AskTime");
			thread_delay(SYNCHONIZE_TIMEOUT);
			LORA::Send::ASKTIME();
			thread_delay(SYNCHONIZE_TIMEOUT);
			for (;;)
				try {
					Debug::print_thread("DEBUG: DAEMON::AskTime::loop");
					LORA::Send::ASKTIME();
					thread_delay(SYNCHONIZE_TIMEOUT);
					Schedule::sleep(&alarm, SYNCHONIZE_INTERVAL - SYNCHONIZE_TIMEOUT + rand_int<uint8_t>());
				}
				catch (...) {
					COM::println("ERROR: DAEMON::AskTime::loop exception thrown");
				}
		}
	}

	namespace Push {
		static struct Alarm alarm;
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
				//	struct Alarm my_alarm;
				//	Schedule::add_timer(&my_alarm, "send_data");
				for (unsigned int t=0; t<RESEND_TIMES; ++t) {
					{
						DEBUG_LOCK(debug_lock);
						Debug::print("DEBUG: DAEMON::Push::send_data ");
						Debug::println(t);
					}
					LORA::Send::SEND(my_device_id, ++current_serial, &data);
					thread_delay(ACK_TIMEOUT);
					if (acked_serial.load() == current_serial.load()) {
						send_success.store(true);
						SDCard::next_data();
						break;
					}
				}
				//	Schedule::remove_timer(&my_alarm);
			}
			//	Schedule::alarm.notify();
		}

		void data(struct Data const *const data) {
			Debug::println("DEBUG: DAEMON::Push::data");
			SDCard::add_data(data);
		}

		void ack(SerialNumber const serial) {
			acked_serial = serial;
		}

		[[noreturn]]
		void loop(void) {
			Schedule::add_timer(&alarm, "DAEMON::Push");
			thread_delay(500);
			for (;;)
				try {
					Debug::print_thread("DEBUG: DAEMON::Push::loop");
					struct Data data;
					send_success.store(false);
					if (SDCard::read_data(&data)) {
						esp_pthread_set_cfg(&esp_pthread_cfg);
						//	std::thread(send_data, data).detach();
						send_data(data);
					}
					#if SEND_IDLE_INTERVAL > SEND_INTERVAL
						if (!send_success.load()) {
							{
								DEBUG_LOCK(debug_lock);
								Debug::println("DEBUG: DAEMON::Push::loop unsuccessed send");
							}
							Schedule::sleep(&alarm, SEND_IDLE_INTERVAL);
						}
						else
					#endif
							Schedule::sleep(&alarm, SEND_INTERVAL);
				}
				catch (...) {
					COM::println("ERROR: DAEMON::Push::loop exception thrown");
				}
		}
	}

	namespace CleanLog {
		static struct Alarm alarm;

		void loop(void) {
			Schedule::add_timer(&alarm, "DAEMON::CleanLog");
			for (;;)
				try {
					Schedule::sleep(&alarm, CLEANLOG_INTERVAL);
					Debug::print_thread("DEBUG: DAEMON::CleanLog::loop");
					SDCard::clean_up();
				}
				catch (...) {
					COM::println("ERROR: DAEMON::CleanData::loop exception thrown");
				}
		}
	}

	namespace Measure {
		static struct Alarm alarm;

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
			Schedule::add_timer(&alarm, "DAEMON::Measure");
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
						Debug::print("DEBUG: DAEMON::Measure::loop sleep, now=");
						Debug::println(millis());
						Debug::flush();
					}
					Schedule::sleep(&alarm, MEASURE_INTERVAL);
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

		esp_pthread_set_cfg(&esp_pthread_cfg);
		std::thread(Schedule::loop).detach();
	}
}

/* ************************************************************************** */

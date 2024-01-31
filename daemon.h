#ifndef INCLUDE_DAEMON_H
#define INCLUDE_DAEMON_H

#include <atomic>
#include <condition_variable>

#include <esp_pthread.h>

/* ************************************************************************** */

namespace DAEMON {
	extern void thread_delay(Millisecond ms);
	struct Alarm {
		std::mutex mutex;
		std::condition_variable condition_variable;
		std::atomic<bool> awake;
		void notify(void);
	};
	namespace Schedule {
		extern void loop(void);
		extern void add_timer(struct Alarm *timer_alarm, char const *name);
		extern void remove_timer(struct Alarm *timer_alarm);
	}
	namespace LoRa {
		[[noreturn]] extern void loop(void);
	}
	namespace Time {
		extern void run(void);
		[[noreturn]] extern void loop(void);
	}
	namespace AskTime {
		extern void synchronized(void);
		[[noreturn]] extern void loop(void);
	}
	namespace Push {
		extern void data(struct Data const *data);
		extern void ack(SerialNumber serial);
	}
	namespace Measure {
		void set_interval(Millisecond ms);
		[[noreturn]] extern void loop(void);
	}
	extern void run(void);
}

/* ************************************************************************** */

#endif // INCLUDE_DAEMON_H

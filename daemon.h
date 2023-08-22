#ifndef INCLUDE_DAEMON_H
#define INCLUDE_DAEMON_H

#include <atomic>

#include <esp_pthread.h>

/* ************************************************************************** */

namespace DAEMON {
	extern esp_pthread_cfg_t thread_core_unpin;
	extern esp_pthread_cfg_t thread_core_default;
	extern esp_pthread_cfg_t thread_core_opposite;
	extern void thread_delay(unsigned long int ms);
	namespace Sleep {
		struct Timer {
			unsigned long int start;
			unsigned long int stop;
		};
		extern std::atomic<bool> keep_await;
		extern size_t register_thread(void);
		extern void time(size_t timer_index, unsigned long int milliseconds);
		extern void woke(size_t const timer_index);
		extern void loop(void);
	}
	namespace Internet {
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
		[[noreturn]] extern void loop(void);
	}
	namespace Headless {
		extern void loop(void);
	}
	extern void run(void);
}

/* ************************************************************************** */

#endif // INCLUDE_DAEMON_H

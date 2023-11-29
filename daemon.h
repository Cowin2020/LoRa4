#ifndef INCLUDE_DAEMON_H
#define INCLUDE_DAEMON_H

#include <atomic>

#include <esp_pthread.h>

/* ************************************************************************** */

namespace DAEMON {
	typedef unsigned long int millis_t;
	extern void thread_delay(unsigned long int ms);
	namespace Schedule {
		void loop(void);
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
		[[noreturn]] extern void loop(void);
	}
	extern void run(void);
}

/* ************************************************************************** */

#endif // INCLUDE_DAEMON_H

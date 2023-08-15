#ifndef INCLUDE_DAEMON_H
#define INCLUDE_DAEMON_H

#include <atomic>

/* ************************************************************************** */

namespace DAEMON {
	extern void thread_delay(unsigned long int ms);
	namespace Sleep {
		struct Timer {
			unsigned long int start;
			unsigned long int stop;
		};
		extern size_t register_thread(void);
		extern void time(size_t timer_index, unsigned long int milliseconds);
		extern void loop(void);
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
	}
	namespace Measure {
		[[noreturn]] extern void loop(void);
	}
	extern void run(void);
}

/* ************************************************************************** */

#endif // INCLUDE_DAEMON_H

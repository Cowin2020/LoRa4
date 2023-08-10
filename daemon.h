#ifndef INCLUDE_DAEMON_H
#define INCLUDE_DAEMON_H

/* ************************************************************************** */

namespace DAEMON {
	extern void thread_delay(unsigned long int ms);
	namespace Sleep {
		struct Timer {
			unsigned long int start;
			unsigned long int stop;
			bool enable;
		};
		extern size_t register_thread(void);
	}
	namespace AskTime {
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

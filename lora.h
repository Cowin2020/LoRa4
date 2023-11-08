#ifndef INCLUDE_LORA_H
#define INCLUDE_LORA_H

#include "device.h"

/* ************************************************************************** */

namespace LORA {
	extern unsigned long int last_time;
	extern bool initialize(void);
	extern void sleep(void);
	extern void wake(void);
	namespace Send {
		extern void TIME(struct FullTime const *fulltime);
		extern void ASKTIME(void);
		extern void SEND(Device receiver, SerialNumber serial, Data const *data);
	}
	namespace Receive {
		void packet(void);
	}
}

/* ************************************************************************** */

#endif // INCLUDE_LORA_H

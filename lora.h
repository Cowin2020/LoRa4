#ifndef INCLUDE_LORA_H
#define INCLUDE_LORA_H

#include "device.h"

/* ************************************************************************** */

namespace LORA {
	namespace Send {
		extern void ASKTIME(void);
		extern void SEND(Device receiver, SerialNumber serial, Data const *data);
	}
	namespace Receive {
		void packet(void);
	}
	extern bool initialize(void);
}

/* ************************************************************************** */

#endif // INCLUDE_LORA_H

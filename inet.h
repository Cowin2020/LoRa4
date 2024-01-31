#ifndef INCLUDE_INET_H
#define INCLUDE_INET_H

/* ************************************************************************** */

#include "device.h"

namespace WIFI {
	extern void initialize(void);
	extern bool ready(void);
	extern bool upload(Device const device, SerialNumber const serial, struct Data const *data);
	extern void loop(void);
}

/* ************************************************************************** */

#endif // INCLUDE_INET_H

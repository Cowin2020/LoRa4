#ifndef INCLUDE_INET_H
#define INCLUDE_INET_H

/* ************************************************************************** */

#include "device.h"

namespace WIFI {
	extern void initialize(void);
	extern bool ready(void);
	struct upload__result {
		bool upload_success;
		bool update_configuration;
		class Configuration configuration;
	};
	extern struct upload__result upload(Device device, SerialNumber serial, struct Data const *data);
	extern void loop(void);
}

/* ************************************************************************** */

#endif // INCLUDE_INET_H

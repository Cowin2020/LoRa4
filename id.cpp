#include "config_id.h"
#include "id.h"

/* ************************************************************************** */

#if !defined(DEVICE_ID)
	#error "ERROR: undefined DEVICE_ID"
#endif

#if !defined(NUMBER_OF_DEVICES)
	#error "ERROR: undefined NUMBER_OF_DEVICES"
#endif

#if defined(ENABLE_GATEWAY) && DEVICE_ID
	#error "ERROR: the DEVICE_ID of gateway device is not zero"
#endif

/* ************************************************************************** */

Device const my_device_id = DEVICE_ID;
unsigned int const number_of_device = NUMBER_OF_DEVICES;

bool const enable_gateway =
	#if defined(ENABLE_GATEWAY)
		true
	#else
		false
	#endif
	;

bool const enable_measure =
	#if defined(ENABLE_MEASURE)
		true
	#else
		false
	#endif
	;

/* ************************************************************************** */

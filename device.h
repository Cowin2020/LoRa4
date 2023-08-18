#ifndef INCLUDE_DEVICE_H
#define INCLUDE_DEVICE_H

#include <NTPClient.h>
#include <WiFi.h>

#include "config_device.h"
#include "type.h"

/* ************************************************************************** */

extern unsigned long const CPU_frequency;

namespace RTC {
	extern bool initialize(void);
	extern void set(struct FullTime const* fulltime);
	extern bool now(struct FullTime *fulltime);
}

namespace NTP {
	extern void initialize(void);
	extern bool now(struct FullTime *fulltime);
	extern void synchronize(void);
}

struct [[gnu::packed]] Data {
	struct FullTime time;
	#ifdef ENABLE_BATTERY_GAUGE
		float battery_voltage;
		float battery_percentage;
	#endif
	#ifdef ENABLE_DALLAS
		float dallas_temperature;
	#endif
	#ifdef ENABLE_BME280
		float bme280_temperature;
		float bme280_pressure;
		float bme280_humidity;
	#endif
	#ifdef ENABLE_LTR390
		float ltr390_ultraviolet;
	#endif

	void writeln(class Print *print) const;
	bool readln(class Stream *stream);
	void println() const;
};

namespace Sensor {
	extern bool initialize(void);
	extern bool measure(struct Data *data);
}

/* ************************************************************************** */

#endif // INCLUDE_DEVICE_H

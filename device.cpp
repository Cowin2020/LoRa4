#include "type.h"
#include "id.h"
#include "display.h"
#include "device.h"

/* ************************************************************************** */

static char const data_file_path[] PROGMEM = "/data.csv";
static char const cleanup_file_path[] PROGMEM = "/cleanup.csv";

/* ************************************************************************** */

unsigned long const CPU_frequency =
	#if defined(CPU_FREQUENCY)
		enable_gateway
			? CPU_FREQUENCY >= 80
				? CPU_FREQUENCY
				: 80
			: CPU_FREQUENCY >= 20
				? CPU_FREQUENCY
				: 20
	#else
		0
	#endif
	;

#if !defined(ENABLE_CLOCK)
	#include <RTClib.h>

	namespace RTC {
		static bool clock_available;
		static class RTC_Millis internal_clock;

		bool initialize(void) {
			clock_available = false;
			return true;
		}

		void set(struct FullTime const *const fulltime) {
			class DateTime const datetime(
				fulltime->year, fulltime->month, fulltime->day,
				fulltime->hour, fulltime->minute, fulltime->second
			);
			if (clock_available) {
				internal_clock.adjust(datetime);
			}
			else {
				internal_clock.begin(datetime);
				clock_available = true;
			}
		}

		bool now(struct FullTime *const fulltime) {
			class DateTime const datetime = internal_clock.now();
			if (fulltime != NULL)
				*fulltime = {
					.year = (unsigned short int)datetime.year(),
					.month = (unsigned char)datetime.month(),
					.day = (unsigned char)datetime.day(),
					.hour = (unsigned char)datetime.hour(),
					.minute = (unsigned char)datetime.minute(),
					.second = (unsigned char)datetime.second()
				};
			return clock_available;
		}
	}
#elif ENABLE_CLOCK == CLOCK_PCF85063TP
	#include <PCF85063TP.h>

	namespace RTC {
		static class PCD85063TP external_clock;

		bool initialize(void) {
			external_clock.begin();
			external_clock.startClock();
			return true;
		}

		void set(struct FullTime const *const fulltime) {
			external_clock.stopClock();
			external_clock.fillByYMD(fulltime->year, fulltime->month, fulltime->day);
			external_clock.fillByHMS(fulltime->hour, fulltime->minute, fulltime->second);
			external_clock.setTime();
			external_clock.startClock();
		}

		bool now(struct FullTime *const fulltime) {
			external_clock.getTime();
			if (fulltime != NULL)
				*fulltime = {
					.year = (unsigned short int)(2000U + external_clock.year),
					.month = external_clock.month,
					.day = external_clock.dayOfMonth,
					.hour = external_clock.hour,
					.minute = external_clock.minute,
					.second = external_clock.second
				};
			static bool available = false;
			if (!available)
				available =
					1 <= external_clock.year       && external_clock.year       <= 99 &&
					1 <= external_clock.month      && external_clock.month      <= 12 &&
					1 <= external_clock.dayOfMonth && external_clock.dayOfMonth <= 30 &&
					0 <= external_clock.hour       && external_clock.hour       <= 23 &&
					0 <= external_clock.minute     && external_clock.minute     <= 59 &&
					0 <= external_clock.second     && external_clock.second     <= 59;
			return available;
		}
	}
#elif ENABLE_CLOCK == CLOCK_DS1307 || ENABLE_CLOCK == CLOCK_DS3231
	#include <RTClib.h>

	namespace RTC {
		#if ENABLE_CLOCK == CLOCK_DS1307
			static class RTC_DS1307 external_clock;
		#elif ENABLE_CLOCK == CLOCK_DS3231
			static class RTC_DS3231 external_clock;
		#endif

		bool initialize(void) {
			if (!external_clock.begin()) {
				Display::println("Clock not found");
				return false;
			}
			#if ENABLE_CLOCK == CLOCK_DS1307
				if (!external_clock.isrunning()) {
					Display::println("DS1307 not running");
					return false;
				}
			#endif
			return true;
		}

		void set(struct FullTime const *const fulltime) {
			class DateTime const datetime(
				fulltime->year, fulltime->month, fulltime->day,
				fulltime->hour, fulltime->minute, fulltime->second
			);
			external_clock.adjust(datetime);
		}

		bool now(struct FullTime *const fulltime) {
			class DateTime const datetime = external_clock.now();
			if (fulltime != NULL)
				*fulltime = {
					.year = datetime.year(),
					.month = datetime.month(),
					.day = datetime.day(),
					.hour = datetime.hour(),
					.minute = datetime.minute(),
					.second = datetime.second()
				};
			return datetime.isValid();
		}
	}
#endif

namespace NTP {
	static class WiFiUDP WiFiUDP;
	static class NTPClient NTPClient(WiFiUDP, NTP_SERVER, 0, NTP_INTERVAL);

	bool now(struct FullTime *const fulltime) {
		if (!NTPClient.isTimeSet()) return false;
		time_t const epoch = NTPClient.getEpochTime();
		struct tm time;
		gmtime_r(&epoch, &time);
		*fulltime = {
			.year = (unsigned short int)(1900U + time.tm_year),
			.month = (unsigned char)(time.tm_mon + 1),
			.day = (unsigned char)time.tm_mday,
			.hour = (unsigned char)time.tm_hour,
			.minute = (unsigned char)time.tm_min,
			.second = (unsigned char)time.tm_sec
		};
		return true;
	}

	void synchronize(void) {
		if (NTPClient.update()) {
			time_t const epoch = NTPClient.getEpochTime();
			struct tm time;
			gmtime_r(&epoch, &time);
			struct FullTime const fulltime = {
				.year = (unsigned short int)(1900U + time.tm_year),
				.month = (unsigned char)(time.tm_mon + 1),
				.day = (unsigned char)time.tm_mday,
				.hour = (unsigned char)time.tm_hour,
				.minute = (unsigned char)time.tm_min,
				.second = (unsigned char)time.tm_sec
			};
			RTC::set(&fulltime);
			COM::println("NTP update");
		}
	}
}
#if defined(ENABLE_BATTERY_GAUGE)
	#if ENABLE_BATTERY_GAUGE == BATTERY_GAUGE_DFROBOT
		#include <DFRobot_MAX17043.h>

		static class DFRobot_MAX17043 battery;
	#elif ENABLE_BATTERY_GAUGE == BATTERY_GAUGE_LC709203F
		#include <Adafruit_LC709203F.h>

		static class Adafruit_LC709203F battery;
	#endif
#endif

#ifdef ENABLE_DALLAS
	#include <OneWire.h>
	#include <DallasTemperature.h>

	static class OneWire onewire_thermometer(ENABLE_DALLAS);
	static class DallasTemperature dallas(&onewire_thermometer);
#endif

#ifdef ENABLE_BME280
	#include <Adafruit_Sensor.h>
	#include <Adafruit_BME280.h>

	static class Adafruit_BME280 BME;
#endif

#ifdef ENABLE_LTR390
	#include <Adafruit_LTR390.h>
	static class Adafruit_LTR390 LTR;
#endif

/* ************************************************************************** */

void Data::writeln(class Print *const print) const {
	print->printf(
		"%04u-%02u-%02uT%02u:%02u:%02uZ",
		this->time.year, this->time.month, this->time.day,
		this->time.hour, this->time.minute, this->time.second
	);
	#ifdef ENABLE_BATTERY_GAUGE
		print->printf(
			",%f,%f",
			this->battery_voltage, this->battery_percentage
		);
	#endif
	#ifdef ENABLE_DALLAS
		print->printf(
			",%f",
			this->dallas_temperature
		);
	#endif
	#ifdef ENABLE_BME280
		print->printf(
			",%f,%f,%f",
			this->bme280_temperature, this->bme280_pressure, this->bme280_humidity
		);
	#endif
	#ifdef ENABLE_LTR390
		print->printf(
			",%f",
			this->ltr390_ultraviolet
		);
	#endif
	print->write('\n');
}

bool Data::readln(class Stream *const stream) {
	/* Time */
	{
		class String const s = stream->readStringUntil(
			#if defined(ENABLE_DALLAS) || defined(ENABLE_BME280) || defined(ENABLE_LTR390)
				','
			#else
				'\n'
			#endif
		);
		if (
			sscanf(
				s.c_str(),
				"%4hu-%2hhu-%2hhuT%2hhu:%2hhu:%2hhuZ",
				&this->time.year, &this->time.month, &this->time.day,
				&this->time.hour, &this->time.minute, &this->time.second
			) != 6
		) return false;
	}

	/* Battery gauge */
	#ifdef ENABLE_BATTERY_GAUGE
		{
			class String const s = stream->readStringUntil(',');
			if (sscanf(s.c_str(), "%f", &this->battery_voltage) != 1) return false;
		}
		{
			class String const s = stream->readStringUntil(
				#if defined(ENABLE_DALLAS) || defined(ENABLE_BME280) || defined(ENABLE_LTR390)
					','
				#else
					'\n'
				#endif
			);
			if (sscanf(s.c_str(), "%f", &this->battery_percentage) != 1) return false;
		}
	#endif

	/* Dallas thermometer */
	#ifdef ENABLE_DALLAS
		{
			class String const s = stream->readStringUntil(
				#if defined(ENABLE_BME280) || defined(ENABLE_LTR390)
					','
				#else
					'\n'
				#endif
			);
			if (sscanf(s.c_str(), "%f", &this->dallas_temperature) != 1) return false;
		}
	#endif

	/* BME280 sensor */
	#ifdef ENABLE_BME280
		{
			class String const s = stream->readStringUntil(',');
			if (sscanf(s.c_str(), "%f", &this->bme280_temperature) != 1) return false;
		}
		{
			class String const s = stream->readStringUntil(',');
			if (sscanf(s.c_str(), "%f", &this->bme280_pressure) != 1) return false;
		}
		{
			class String const s = stream->readStringUntil(
				#if defined(ENABLE_LTR390)
					','
				#else
					'\n'
				#endif
			);
			if (sscanf(s.c_str(), "%f", &this->bme280_humidity) != 1) return false;
		}
	#endif

	/* LTR390 sensor */
	#ifdef ENABLE_LTR390
		{
			class String const s = stream->readStringUntil('\n');
			if (sscanf(s.c_str(), "%f", &this->ltr390_ultraviolet) != 1) return false;
		}
	#endif

	return true;
}

void Data::println(void) const {
	COM::print("Time: ");
	Display::println(String(this->time));

	#if defined(ENABLE_BATTERY_GAUGE)
		Display::print("Battery: ");
		Display::print(this->battery_voltage);
		Display::print("V ");
		Display::print(this->battery_percentage);
		Display::println("%");
	#endif

	#if defined(ENABLE_DALLAS)
		Display::print("Dallas temp.: ");
		Display::println(this->dallas_temperature);
	#endif

	#if defined(ENABLE_BME280)
		Display::print("BME temp.: ");
		Display::println(this->bme280_temperature);
		Display::print("BME pressure: ");
		Display::println(this->bme280_pressure, 0);
		Display::print("BME humidity: ");
		Display::println(this->bme280_humidity);
	#endif

	#if defined(ENABLE_LTR390)
		Display::print("LTR UV: ");
		Display::println(this->ltr390_ultraviolet);
	#endif
}

namespace Sensor {
	bool initialize(void) {
		if (!RTC::initialize()) return false;

		if (!enable_measure) return true;

		/* Initial battery gauge */
		#if defined(ENABLE_BATTERY_GAUGE)
			battery.begin();
		#endif

		/* Initialize Dallas thermometer */
		#if defined(ENABLE_DALLAS)
			dallas.begin();
			DeviceAddress thermometer_address;
			if (dallas.getAddress(thermometer_address, 0)) {
				Display::println("Thermometer 0 found");
			}
			else {
				Display::println("Thermometer 0 not found");
				return false;
			}
		#endif

		/* Initialize BME280 sensor */
		#if defined(ENABLE_BME280)
			if (BME.begin()) {
				Display::println("BME280 sensor found");
			}
			else {
				Display::println("BME280 sensor not found");
				return false;
			}
		#endif

		/* Initial LTR390 sensor */
		#if defined(ENABLE_LTR390)
			if (LTR.begin()) {
				LTR.setMode(LTR390_MODE_UVS);
				Display::println("LTR390 sensor found");
			}
			else {
				Display::println("LTR390 sensor not found");
				return false;
			}
		#endif

		return true;
	}

	bool measure(struct Data *const data) {
		if (!RTC::now(&data->time))
			return false;
		#if defined(ENABLE_BATTERY_GAUGE)
			#if ENABLE_BATTERY_GAUGE == BATTERY_GAUGE_DFROBOT
				data->battery_voltage = battery.readVoltage() / 1000;
				data->battery_percentage = battery.readPercentage();
			#elif ENABLE_BATTERY_GAUGE == BATTERY_GAUGE_LC709203F
				data.battery_voltage = battery.cellVoltage();
				data.battery_percentage = battery.cellPercent();
			#endif
		#endif
		#if defined(ENABLE_DALLAS)
			data->dallas_temperature = dallas.getTempCByIndex(0);
		#endif
		#if defined(ENABLE_BME280)
			data->bme280_temperature = BME.readTemperature();
			data->bme280_pressure = BME.readPressure();
			data->bme280_humidity = BME.readHumidity();
		#endif
		#if defined(ENABLE_LTR390)
			data->ltr390_ultraviolet = LTR.readUVS();
		#endif
		return true;
	}
}

/* ************************************************************************** */

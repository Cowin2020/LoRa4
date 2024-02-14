#include <cstdio>

#include "config_device.h"
#include "display.h"
#include "basic.h"
#include "daemon.h"

/* ************************************************************************** */

unsigned int parse_uint(char const **const next) {
	unsigned int x = 0;
	for (char const *s = *next; *s; ++s) {
		char const c = *s;
		if (c < '0' || c > '9') {
			*next = s;
			return x;
		}
		x = (x * 10) + (c - '0');
	}
}

FullTime::operator String(void) const {
	char buffer[24];
	snprintf(
		buffer, sizeof buffer,
		"%04u-%02u-%02uT%02u:%02u:%02uZ",
		this->year, this->month, this->day,
		this->hour, this->minute, this->second
	);
	return String(buffer);
}

Configuration::Configuration(void) : measure_interval(0) {}

bool Configuration::decode(class String const &string) {
	for (char const *p = string.c_str();; ++p)
		switch (*p) {
			case 0:
				return true;
			case ' ':
			case '\t':
			case '\v':
			case '\r':
			case '\n':
				break;
			case 'm': {
				++p;
				unsigned int const value = parse_uint(&p);
				if (*p != '.')
					return false;
				measure_interval = value;
				break;
			}
			default:
				return false;
		}
}

void Configuration::apply(void) const {
	if (measure_interval > SEND_INTERVAL || measure_interval <= 1000*60*60*24)
		DAEMON::Measure::set_interval(measure_interval);
}

/* ************************************************************************** */

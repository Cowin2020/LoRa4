#include <cstdio>

#include "type.h"

/* ************************************************************************** */

FullTime::operator String(void) const {
	char buffer[48];
	snprintf(
		buffer, sizeof buffer,
		"%04u-%02u-%02uT%02u:%02u:%02uZ",
		this->year, this->month, this->day,
		this->hour, this->minute, this->second
	);
	return String(buffer);
}

/* ************************************************************************** */

#ifndef INCLUDE_TYPE_H
#define INCLUDE_TYPE_H

/* ************************************************************************** */

#include <cstddef>
#include <cstdint>

#include <WString.h>

typedef uint8_t Device;
typedef uint32_t SerialNumber;
typedef unsigned long int Time;

struct [[gnu::packed]] FullTime {
	unsigned short int year;
	unsigned char month;
	unsigned char day;
	unsigned char hour;
	unsigned char minute;
	unsigned char second;

	explicit operator String(void) const;
};

/* ************************************************************************** */

#endif // INCLUDE_TYPE_H

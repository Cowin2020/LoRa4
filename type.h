#ifndef INCLUDE_TYPE_H
#define INCLUDE_TYPE_H

/* ************************************************************************** */

#include <cstddef>
#include <cstdint>

#include <WString.h>

typedef unsigned long int Millisecond;

typedef uint8_t Device;
typedef uint32_t SerialNumber;

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

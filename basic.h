#ifndef INCLUDE_BASIC_H
#define INCLUDE_BASIC_H

/* ************************************************************************** */

#include <cstddef>
#include <cstdint>

#include <WString.h>

/* ************************************************************************** */

typedef unsigned long int Millisecond;

typedef uint8_t Device;
typedef uint32_t SerialNumber;

unsigned int parse_uint(char const **next);

struct [[gnu::packed]] FullTime {
	unsigned short int year;
	unsigned char month;
	unsigned char day;
	unsigned char hour;
	unsigned char minute;
	unsigned char second;

	explicit operator String(void) const;
};

class Configuration {
public:
	unsigned int measure_interval;
	Configuration(void);
	bool decode(class String const &string);
	bool apply(void) const;
};

/* ************************************************************************** */

#endif // INCLUDE_BASIC_H

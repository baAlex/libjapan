/*-----------------------------

 [japan-string.h]
 - Alexander Brandt 2020
-----------------------------*/

#ifndef JAPAN_STRING_H
#define JAPAN_STRING_H

#ifdef JA_EXPORT_SYMBOLS
	#if defined(__clang__) || defined(__GNUC__)
	#define JA_EXPORT __attribute__((visibility("default")))
	#elif defined(_MSC_VER)
	#define JA_EXPORT __declspec(dllexport)
	#endif
#else
	#define JA_EXPORT // Whitespace
#endif

#include <stddef.h>
#include <stdint.h>

enum jaEncode
{
	JA_ASCII,
	JA_UTF8
};

JA_EXPORT int jaUnitValidateASCII(uint8_t byte);
JA_EXPORT int jaStringValidateASCII(const uint8_t* string, size_t n, size_t* out_bytes);

JA_EXPORT size_t jaUnitLengthUTF8(uint8_t head_byte);
JA_EXPORT int jaUnitValidateUTF8(const uint8_t* byte, size_t n, size_t* out_unit_len, uint32_t* out_unit_code);
JA_EXPORT int jaStringValidateUTF8(const uint8_t* string, size_t n, size_t* out_bytes, size_t* out_units);

#endif

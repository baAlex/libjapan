/*-----------------------------

MIT License

Copyright (c) 2019 Alexander Brandt

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

-------------------------------

 [format-sgi.c]
 - Alexander Brandt 2019-2020

 http://paulbourke.net/dataformats/sgirgb/sgiversion.html
 https://www.fileformat.info/format/sgiimage/egff.htm
-----------------------------*/

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "../common.h"
#include "japan-endianness.h"
#include "japan-image.h"


#define SGI_MAGIC 474

struct SgiHead
{
	int16_t magic;

	int8_t compression;
	int8_t precision; // In bytes per pixel component
	uint16_t dimension;

	uint16_t x_size;
	uint16_t y_size;
	uint16_t z_size; // Or 'channels'

	int32_t min;
	int32_t max;

	char dummy[4];
	char name[80];

	int32_t pixel_type;
};


int ImageExLoadSgi(FILE* file, struct jaImageEx* out, struct jaStatus* st);


static inline void sPlotPixel_8(size_t channel, size_t row, size_t col, struct jaImage* image, uint8_t value)
{
	uint8_t* data = (uint8_t*)image->data + (col + (image->width * row)) * image->channels;
	data[channel] = value;
}


static inline void sPlotPixel_16(size_t channel, size_t row, size_t col, struct jaImage* image, uint16_t value)
{
	uint16_t* data = (uint16_t*)image->data + (col + (image->width * row)) * image->channels;
	data[channel] = value;
}


/*-----------------------------

 sReadUncompressed()
-----------------------------*/
static int sReadUncompressed(FILE* file, struct jaImage* image)
{
	enum jaEndianness sys_endianness = jaEndianSystem();
	union {
		uint8_t u8;
		uint16_t u16;
	} pixel;

	if (jaBitsPerComponent(image->format) == 8)
	{
		for (size_t channel = 0; channel < image->channels; channel++)
		{
			for (long row = (long)(image->height - 1); row >= 0; row--)
				for (size_t col = 0; col < image->width; col++)
				{
					if (fread(&pixel.u8, 1, 1, file) != 1)
						return 1;

					sPlotPixel_8(channel, (size_t)row, col, image, pixel.u8);
				}
		}
	}
	else
	{
		for (size_t channel = 0; channel < image->channels; channel++)
		{
			for (long row = (long)(image->height - 1); row >= 0; row--)
				for (size_t col = 0; col < image->width; col++)
				{
					if (fread(&pixel.u16, sizeof(uint16_t), 1, file) != 1)
						return 1;

					sPlotPixel_16(channel, (size_t)row, col, image,
					              jaEndianToU16(pixel.u16, JA_ENDIAN_BIG, sys_endianness));
				}
		}
	}

	return 0;
}


/*-----------------------------

 sReadCompressed_8()
-----------------------------*/
static int sReadCompressed_8(FILE* file, struct jaImage* image)
{
	void* buffer = NULL;
	size_t table_len = image->height * image->channels;
	uint32_t* offset_table = NULL;
	uint32_t* size_table = NULL;

	// Offset and size tables of RLE scanlines
	if ((buffer = malloc(sizeof(uint32_t) * table_len * 2)) == NULL)
		return 1;

	offset_table = (uint32_t*)buffer;
	size_table = (uint32_t*)buffer + table_len;

	if (fread(offset_table, sizeof(uint32_t), table_len, file) != table_len)
		goto return_failure;

	if (fread(size_table, sizeof(uint32_t), table_len, file) != table_len)
		goto return_failure;

	for (size_t i = 0; i < table_len; i++)
	{
		offset_table[i] = jaEndianToU32(offset_table[i], JA_ENDIAN_BIG, JA_ENDIAN_SYSTEM);
		size_table[i] = jaEndianToU32(size_table[i], JA_ENDIAN_BIG, JA_ENDIAN_SYSTEM);
	}

	// Data
	for (size_t channel = 0; channel < image->channels; channel++)
	{
		for (size_t row = 0; row < image->height; row++)
		{
			size_t col = 0;

			if (fseek(file, (long)offset_table[channel * image->height + (image->height - row - 1)], SEEK_SET) != 0)
				goto return_failure;

			// RLE scanline
			for (uint32_t i = 0; i < size_table[channel * image->height + (image->height - row - 1)]; i++)
			{
				uint8_t instruction = 0;
				uint8_t value = 0;
				uint8_t steps = 0;

				if (fread(&instruction, 1, 1, file) != 1)
					goto return_failure;

				steps = (instruction & 0x7F); // 0b01111111

				// Repeat next byte value x steps
				if ((instruction >> 7) == 0)
				{
					if (instruction == 0) // Repeat 0 steps = stop
						break;

					if (fread(&value, 1, 1, file) != 1)
						goto return_failure;

					for (uint8_t s = 0; s < steps; s++)
						sPlotPixel_8(channel, row, col + s, image, value);
				}

				// Read following bytes values for x steps
				else
				{
					for (uint8_t s = 0; s < steps; s++)
					{
						if (fread(&value, 1, 1, file) != 1)
							goto return_failure;

						sPlotPixel_8(channel, row, col + s, image, value);
					}
				}

				col += steps;
			}

			// Failure if scanline is broken
			if (col != image->width)
				goto return_failure;
		}
	}

	// Bye!
	free(buffer);
	return 0;

return_failure:
	free(buffer);
	return 1;
}


/*-----------------------------

 CheckMagicSgi()
-----------------------------*/
bool CheckMagicSgi(uint16_t value)
{
	if (jaEndianToU16(value, JA_ENDIAN_BIG, JA_ENDIAN_SYSTEM) == SGI_MAGIC)
		return true;

	return false;
}


/*-----------------------------

 ImageLoadSgi()
-----------------------------*/
struct jaImage* ImageLoadSgi(FILE* file, const char* filename, struct jaStatus* st)
{
	struct jaImageEx ex = {0};
	struct jaImage* image = NULL;

	jaStatusSet(st, "ImageLoadSgi", JA_STATUS_SUCCESS, NULL);

	if (ImageExLoadSgi(file, &ex, st) != 0)
		goto return_failure;

	JA_DEBUG_PRINT("(Sgi) '%s':\n", filename);
	JA_DEBUG_PRINT(" - Dimensions: %zu x %zu px\n", ex.width, ex.height);
	JA_DEBUG_PRINT(" - Channels: %zu\n", ex.channels);
	JA_DEBUG_PRINT(" - Format: %i\n", ex.format);
	JA_DEBUG_PRINT(" - Endianness: %s\n", (ex.endianness == JA_ENDIAN_LITTLE) ? "little" : "big");
	JA_DEBUG_PRINT(" - Storage: %s (%u)\n", (ex.storage == JA_IMAGE_SGI_RLE) ? "'sgi rle'" : "'planar'", ex.storage);
	JA_DEBUG_PRINT(" - Uncompressed size: %zu bytes\n", ex.uncompressed_size);
	JA_DEBUG_PRINT(" - Data offset: 0x%zX\n", ex.data_offset);

	if (fseek(file, (long)ex.data_offset, SEEK_SET) != 0)
	{
		jaStatusSet(st, "ImageLoadSgi", JA_STATUS_UNEXPECTED_EOF, "at data seek ('%s')", filename);
		goto return_failure;
	}

	if ((image = jaImageCreate(ex.format, ex.width, ex.height, ex.channels)) == NULL)
	{
		jaStatusSet(st, "ImageLoadSgi", JA_STATUS_MEMORY_ERROR, NULL);
		goto return_failure;
	}

	if (ex.storage == JA_IMAGE_SGI_RLE)
	{
		if (jaBitsPerComponent(ex.format) == 8 && sReadCompressed_8(file, image) != 0)
		{
			jaStatusSet(st, "ImageLoadSgi", JA_STATUS_UNEXPECTED_EOF, "reading 8 bits compressed data ('%s')", filename);
			goto return_failure;
		}
		//	else if (sReadCompressed_16(file, image) != 0)
		//	{
		//		jaStatusSet(st, "ImageLoadSgi", JA_STATUS_UNEXPECTED_EOF, "reading 16 bits compressed data ('%s')",
		// filename); 		goto return_failure;
		//	}
	}
	else if (sReadUncompressed(file, image) != 0)
	{
		jaStatusSet(st, "ImageLoadSgi", JA_STATUS_UNEXPECTED_EOF, "reading uncompressed data ('%s')", filename);
		goto return_failure;
	}

	// Bye!
	return image;

return_failure:
	if (image != NULL)
		jaImageDelete(image);

	return NULL;
}


/*-----------------------------

 ImageExLoadSgi()
-----------------------------*/
int ImageExLoadSgi(FILE* file, struct jaImageEx* out, struct jaStatus* st)
{
	struct SgiHead head;
	enum jaEndianness sys_endianness = jaEndianSystem();

	jaStatusSet(st, "ImageExLoadSgi", JA_STATUS_SUCCESS, NULL);

	if (fread(&head, sizeof(struct SgiHead), 1, file) != 1)
	{
		jaStatusSet(st, "ImageExLoadSgi", JA_STATUS_UNEXPECTED_EOF, "near head");
		return 1;
	}

	out->width = (size_t)jaEndianToU16(head.x_size, JA_ENDIAN_BIG, sys_endianness);
	out->height = (size_t)jaEndianToU16(head.y_size, JA_ENDIAN_BIG, sys_endianness);
	out->channels = (size_t)jaEndianToU16(head.z_size, JA_ENDIAN_BIG, sys_endianness);
	out->data_offset = 512;
	out->endianness = JA_ENDIAN_BIG;

	if (head.compression == 0)
		out->storage = JA_IMAGE_UNCOMPRESSED_PLANAR;
	else if (head.compression == 1)
		out->storage = JA_IMAGE_SGI_RLE;
	else
	{
		jaStatusSet(st, "ImageExLoadSgi", JA_STATUS_ERROR, "sgi compression (%i)", head.compression);
		return 1;
	}

	out->storage = (head.compression == 0) ? JA_IMAGE_UNCOMPRESSED_PLANAR : JA_IMAGE_SGI_RLE;

	switch (jaEndianToI32(head.pixel_type, JA_ENDIAN_BIG, sys_endianness))
	{
	case 1: jaStatusSet(st, "ImageExLoadSgi", JA_STATUS_OBSOLETE_FEATURE, "dithered image"); return 1;
	case 2: jaStatusSet(st, "ImageExLoadSgi", JA_STATUS_OBSOLETE_FEATURE, "indexed image"); return 1;
	case 3: jaStatusSet(st, "ImageExLoadSgi", JA_STATUS_OBSOLETE_FEATURE, "palette data"); return 1;
	case 0: break;
	}

	switch (jaEndianToU16(head.dimension, JA_ENDIAN_BIG, sys_endianness))
	{
	case 1: // One dimensional grayscale image (only uses width/x_size)
		out->height = 1;
		out->channels = 1;
		break;

	case 2: // Two dimensional grayscale image (uses width/x and height/y_size)
		out->channels = 1;
		break;

		// case 3: // We converted the previous two into this case (that uses all x, y and z_size)
		// From here we can ignore the 'dimension' field
	}

	if (head.precision == 1)
	{
		out->format = JA_IMAGE_U8;
		out->uncompressed_size = 1 * out->width * out->height * out->channels;
	}
	else if (head.precision == 2)
	{
		out->format = JA_IMAGE_U16;
		out->uncompressed_size = 2 * out->width * out->height * out->channels;
	}
	else
	{
		jaStatusSet(st, "ImageExLoadSgi", JA_STATUS_UNSUPPORTED_FEATURE, "precision (%i)", head.precision);
		return 1;
	}

	return 0;
}


/*-----------------------------

 jaImageSaveSgi()
-----------------------------*/
int jaImageSaveSgi(const struct jaImage* image, const char* filename, struct jaStatus* st)
{
	struct SgiHead head = {0};
	FILE* file = NULL;
	enum jaEndianness sys_endianness = jaEndianSystem();

	jaStatusSet(st, "jaImageSaveSgi", JA_STATUS_SUCCESS, NULL);

	if (image->width > UINT16_MAX || image->height > UINT16_MAX)
	{
		jaStatusSet(st, "jaImageSaveSgi", JA_STATUS_UNSUPPORTED_FEATURE, "image dimensions ('%s')", filename);
		return 1;
	}

	if (image->format != JA_IMAGE_U8 && image->format != JA_IMAGE_U16) // The two formats supported by Sgi
	{
		jaStatusSet(st, "jaImageSaveSgi", JA_STATUS_UNSUPPORTED_FEATURE, "image format ('%s')", filename);
		return 1;
	}

	if ((file = fopen(filename, "wb")) == NULL)
	{
		jaStatusSet(st, "jaImageSaveSgi", JA_STATUS_FS_ERROR, "'%s'", filename);
		return 1;
	}

	// Head
	head.magic = jaEndianToI16(SGI_MAGIC, sys_endianness, JA_ENDIAN_BIG);
	head.compression = 0;
	head.precision = (int8_t)jaBitsPerComponent(image->format) / 8;
	head.dimension = jaEndianToU16(3, sys_endianness, JA_ENDIAN_BIG);

	head.x_size = jaEndianToU16((uint16_t)image->width, sys_endianness, JA_ENDIAN_BIG);
	head.y_size = jaEndianToU16((uint16_t)image->height, sys_endianness, JA_ENDIAN_BIG);
	head.z_size = jaEndianToU16((uint16_t)image->channels, sys_endianness, JA_ENDIAN_BIG);

	head.pixel_type = 0;

	if (fwrite(&head, sizeof(struct SgiHead), 1, file) != 1)
	{
		jaStatusSet(st, "jaImageSaveSgi", JA_STATUS_IO_ERROR, "head ('%s')", filename);
		goto return_failure;
	}

	// Data
	union {
		uint8_t* u8;
		uint16_t* u16;
		void* raw;
	} src;

	src.raw = image->data;

	if (fseek(file, 512, SEEK_SET) != 0) // Data start at offset 512
	{
		jaStatusSet(st, "jaImageSaveSgi", JA_STATUS_IO_ERROR, "at data seek ('%s')", filename);
		goto return_failure;
	}

	for (size_t ch = 0; ch < image->channels; ch++)
	{
		if (jaBitsPerComponent(image->format) == 8)
		{
			src.u8 = (uint8_t*)image->data + ch;

			for (long row = (long)(image->height - 1); row >= 0; row--)
				for (size_t col = 0; col < image->width; col++)
				{
					if (fwrite(&src.u8[(image->width * (size_t)row + col) * image->channels], 1, 1, file) != 1)
					{
						jaStatusSet(st, "jaImageSaveSgi", JA_STATUS_IO_ERROR, "data ('%s')", filename);
						goto return_failure;
					}
				}
		}
		else
		{
			src.u16 = (uint16_t*)image->data + ch;
			uint16_t pixel = 0;

			for (long row = (long)(image->height - 1); row >= 0; row--)
				for (size_t col = 0; col < image->width; col++)
				{
					pixel = jaEndianToU16(src.u16[(image->width * (size_t)row + col) * image->channels], sys_endianness,
					                      JA_ENDIAN_BIG);

					if (fwrite(&pixel, sizeof(uint16_t), 1, file) != 1)
					{
						jaStatusSet(st, "jaImageSaveSgi", JA_STATUS_IO_ERROR, "data ('%s')", filename);
						goto return_failure;
					}
				}
		}
	}

	// Bye!
	fclose(file);
	return 0;

return_failure:
	fclose(file);
	return 1;
}

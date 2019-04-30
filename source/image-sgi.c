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

 [image-sgi.c]
 - Alexander Brandt 2019

 http://paulbourke.net/dataformats/sgirgb/sgiversion.html
 https://www.fileformat.info/format/sgiimage/egff.htm
-----------------------------*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "endianness.h"
#include "image.h"

#ifdef DEBUG
#define DEBUG_PRINT(fmt, ...) printf(fmt, __VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...)
#endif

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


/*-----------------------------

 sPlotInterleavedPixel()
-----------------------------*/
static void sPlotInterleavedPixel(int channel, int total_channels, int row, int col, struct Image* image, uint8_t value)
{
	// The old code is broken :(
}


/*-----------------------------

 sReadUncompressed_8()
-----------------------------*/
static int sReadUncompressed_8(FILE* file, struct SgiHead* head, struct Image* image)
{
	uint8_t pixel = 0;

	for (int channel = 0; channel < head->z_size; channel++)
	{
		for (int row = (head->y_size - 1); row >= 0; row--)
		{
			for (int col = 0; col < head->x_size; col++)
			{
				if (fread(&pixel, 1, 1, file) != 1)
					return 1;

				sPlotInterleavedPixel(channel, head->z_size, row, col, image, pixel);
			}
		}
	}

	return 0;
}


/*-----------------------------

 sReadCompressed_8()
-----------------------------*/
static int sReadCompressed_8(FILE* file, struct SgiHead* head, struct Image* image)
{
	void* buffer = NULL;
	size_t table_len = head->y_size * head->z_size;
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
		offset_table[i] = EndianBigToSystem_32(offset_table[i], ENDIAN_UNKNOWN);
		size_table[i] = EndianBigToSystem_32(size_table[i], ENDIAN_UNKNOWN);
	}

	// Data
	for (int channel = 0; channel < head->z_size; channel++)
	{
		for (int row = 0; row < head->y_size; row++)
		{
			int col = 0;

			if (fseek(file, offset_table[channel * head->y_size + (head->y_size - row - 1)], SEEK_SET) != 0)
				goto return_failure;

			// RLE scanline
			for (uint32_t i = 0; i < size_table[channel * head->y_size + (head->y_size - row - 1)]; i++)
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
						sPlotInterleavedPixel(channel, head->z_size, row, col + s, image, value);
				}

				// Read following bytes values for x steps
				else
				{
					for (uint8_t s = 0; s < steps; s++)
					{
						if (fread(&value, 1, 1, file) != 1)
							goto return_failure;

						sPlotInterleavedPixel(channel, head->z_size, row, col + s, image, value);
					}
				}

				col += steps;
			}

			// Failure if scanline is broken
			if (col != head->x_size)
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

 sReadUncompressed_16()
-----------------------------*/
static int sReadUncompressed_16(FILE* file, struct SgiHead* head, struct Image* image) { return 1; }


/*-----------------------------

 sReadCompressed_16()
-----------------------------*/
static int sReadCompressed_16(FILE* file, struct SgiHead* head, struct Image* image) { return 1; }


/*-----------------------------

 CheckMagicSgi()
-----------------------------*/
bool CheckMagicSgi(uint16_t value)
{
	if (EndianBigToSystem_16(value, ENDIAN_UNKNOWN) == SGI_MAGIC)
		return true;

	return false;
}


/*-----------------------------

 ImageLoadSgi()
-----------------------------*/
extern struct Image* ImageLoadSgi(FILE* file, const char* filename, struct Error* e)
{
	struct Image* image = NULL;
	struct SgiHead head;
	enum Endianness sys_endianness = EndianSystem();
	enum ImageFormat format = 0;
	int (*read_function)(FILE*, struct SgiHead*, struct Image*);

	ErrorSet(e, NO_ERROR, NULL, NULL);

	// Head
	if (fread(&head, sizeof(struct SgiHead), 1, file) != 1)
	{
		ErrorSet(e, ERROR_BROKEN, "ImageLoadSgi", "head ('%s')", filename);
		goto return_failure;
	}

	head.dimension = EndianBigToSystem_16(head.dimension, sys_endianness);
	head.x_size = EndianBigToSystem_16(head.x_size, sys_endianness);
	head.y_size = EndianBigToSystem_16(head.y_size, sys_endianness);
	head.z_size = EndianBigToSystem_16(head.z_size, sys_endianness);
	head.pixel_type = EndianBigToSystem_32(head.pixel_type, sys_endianness);

	DEBUG_PRINT("(Sgi) '%s':\n", filename);
	DEBUG_PRINT(" - Compression: %s\n", (head.compression == 0) ? "no" : "rle");
	DEBUG_PRINT(" - Precision: %u bits\n", head.precision * 8);
	DEBUG_PRINT(" - Dimension: %u\n", head.dimension);
	DEBUG_PRINT(" - Size: %ux%u px, %u channels\n", head.x_size, head.y_size, head.z_size);

	if (head.pixel_type != 0)
	{
		switch (head.pixel_type)
		{
		case 1:
			ErrorSet(e, ERROR_OBSOLETE, "ImageLoadSgi", "dithered image ('%s')", head.precision, filename);
			goto return_failure;
		case 2:
			ErrorSet(e, ERROR_OBSOLETE, "ImageLoadSgi", "indexed image ('%s')", head.precision, filename);
			goto return_failure;
		case 3:
			ErrorSet(e, ERROR_OBSOLETE, "ImageLoadSgi", "palette data ('%s')", head.precision, filename);
			goto return_failure;
		}
	}

	switch (head.dimension)
	{
	case 1: // One dimensional grayscale image (only uses x_size)
		head.y_size = 1;
		head.z_size = 1;
		break;

	case 2: // Two dimensional grayscale image (uses x and y_size)
		head.z_size = 1;
		break;

		// case 3: // We converted the previous two into this case (that uses all x, y and z_size)
	}

	if (head.z_size != 1 && head.z_size != 2 && head.z_size != 3 && head.z_size != 4)
	{
		ErrorSet(e, ERROR_UNSUPPORTED, "ImageLoadSgi", "channels (%i, '%s')", head.z_size, filename);
		goto return_failure;
	}

	if (head.precision == 1)
	{
		// 8 bits per component image
		read_function = (head.compression == 0) ? sReadUncompressed_8 : sReadCompressed_8;

		switch (head.z_size)
		{
		case 1:
			format = IMAGE_GRAY8;
			break;
		case 2:
			format = IMAGE_GRAYA8;
			break;
		case 3:
			format = IMAGE_RGB8;
			break;
		case 4:
			format = IMAGE_RGBA8;
			break;
		}
	}
	else if (head.precision == 2)
	{
		// 16 bits per component imag
		read_function = (head.compression == 0) ? sReadUncompressed_16 : sReadCompressed_16;

		switch (head.z_size)
		{
		case 1:
			format = IMAGE_GRAY16;
			break;
		case 2:
			format = IMAGE_GRAYA16;
			break;
		case 3:
			format = IMAGE_RGB16;
			break;
		case 4:
			format = IMAGE_RGBA16;
			break;
		}
	}
	else
	{
		ErrorSet(e, ERROR_UNSUPPORTED, "ImageLoadSgi", "precision (%i, '%s')", head.precision, filename);
		goto return_failure;
	}

	// Data
	if (fseek(file, 512, SEEK_SET) != 0) // Data start at offset 512
	{
		ErrorSet(e, ERROR_BROKEN, "ImageLoadSgi", "data seek ('%s')", filename);
		goto return_failure;
	}

	if ((image = ImageCreate(format, head.x_size, head.y_size)) == NULL)
		goto return_failure;

	if (read_function(file, &head, image) != 0)
	{
		ErrorSet(e, ERROR_BROKEN, "ImageLoadSgi", "data ('%s')", filename);
		goto return_failure;
	}

	// Bye!
	return image;

return_failure:
	if (image != NULL)
		ImageDelete(image);

	return NULL;
}


/*-----------------------------

 ImageSaveSgi()
-----------------------------*/
struct Error ImageSaveSgi(struct Image* image, const char* filename)
{
	struct Error e = {.code = NO_ERROR};
	return e;
}

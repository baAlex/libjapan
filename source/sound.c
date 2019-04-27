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

 [sound.c]
 - Alexander Brandt 2019

 $ clang "source/sound.c" "source/sound-au.c" "source/endianness.c" "source/error.c"\
   -I"./include" -DDEBUG -DSTANDALONE_TEST -o "./test"
-----------------------------*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sound-local.h"


/*-----------------------------

 SoundCreate()
-----------------------------*/
struct Sound* SoundCreate(enum SoundFormat format, size_t length, size_t channels, size_t frequency)
{
	struct Sound* sound = NULL;
	size_t size = 0;

	switch (format)
	{
	case SOUND_I8:
		size = sizeof(int8_t) * length * channels;
		break;
	case SOUND_I16:
		size = sizeof(int16_t) * length * channels;
		break;
	case SOUND_I32:
		size = sizeof(int32_t) * length * channels;
		break;
	case SOUND_F32:
		size = sizeof(float) * length * channels;
		break;
	case SOUND_F64:
		size = sizeof(double) * length * channels;
		break;
	}

	if ((sound = malloc(sizeof(struct Sound) + size)) != NULL)
	{
		sound->frequency = frequency;
		sound->channels = channels;
		sound->length = length;
		sound->size = size;
		sound->format = format;
		sound->data = ((struct Sound*)sound + 1);
	}

	return sound;
}


/*-----------------------------

 SoundDelete()
-----------------------------*/
void SoundDelete(struct Sound* sound) { free(sound); }


/*-----------------------------

 SoundLoad()
-----------------------------*/
struct Sound* SoundLoad(const char* filename, struct Error* e)
{
	FILE* file = NULL;
	struct Sound* sound = NULL;
	uint32_t magic = 0;

	ErrorSet(e, NO_ERROR, NULL, NULL);

	if ((file = fopen(filename, "rb")) == NULL)
	{
		ErrorSet(e, ERROR_FS, "SoundLoad", "'%s'", filename);
		return NULL;
	}

	if (fread(&magic, sizeof(uint32_t), 1, file) != 1)
	{
		ErrorSet(e, ERROR_BROKEN, "SoundLoad", "magic ('%s')", filename);
		goto return_failure;
	}

	fseek(file, 0, SEEK_SET);

	if (CheckMagicAu(magic) == true)
		sound = SoundLoadAu(file, filename, e);
	// else if (CheckMagicWav(magic) == true)
	//	sound = SoundLoadWav(file, filename, e);
	else
	{
		ErrorSet(e, ERROR_UNKNOWN_FORMAT, "SoundLoad", "'%s'", filename);
		goto return_failure;
	}

	// Bye!
	fclose(file);
	return sound;

return_failure:
	fclose(file);
	return NULL;
}


/*-----------------------------

 SoundSaveRaw()
-----------------------------*/
struct Error SoundSaveRaw(struct Sound* sound, const char* filename)
{
	struct Error e = {.code = NO_ERROR};
	FILE* file = NULL;

	if ((file = fopen(filename, "wb")) == NULL)
	{
		ErrorSet(&e, ERROR_FS, "SoundSaveRaw", "'%s'", filename);
		return e;
	}

	if (fwrite(sound->data, sound->size, 1, file) != 1)
	{
		ErrorSet(&e, ERROR_IO, "SoundSaveRaw", "'%s'", filename);
		fclose(file);
		return e;
	}

	fclose(file);
	return e;
}


#ifdef STANDALONE_TEST
int main(int argc, char* argv[])
{
	struct Sound* sound = NULL;
	struct Error e = {0};

	if (argc == 1)
	{
		ErrorSet(&e, ERROR_ARGUMENT, "main", "no input specified");
		goto return_failure;
	}

	if ((sound = SoundLoad(argv[1], &e)) != NULL)
	{
		SoundSaveRaw(sound, "test.raw");
		SoundSaveAu(sound, "test.au");
		SoundDelete(sound);
	}
	else
		goto return_failure;

	// Bye
	return EXIT_SUCCESS;

return_failure:
	ErrorPrint(e);
	return EXIT_FAILURE;
}
#endif

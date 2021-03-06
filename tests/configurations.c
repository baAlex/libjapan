/*-----------------------------

 [configurations.c]
 - Alexander Brandt 2019-2020
-----------------------------*/


#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cmocka.h>

#define JA_WIP
#include "japan-configuration.h"


static void sWarningCallback(enum jaStatusCode code, int i, const char* key, const char* value)
{
	switch (i)
	{
	case 1: // "\trender.render" = "Yes"
		assert_true((code == JA_STATUS_INVALID_KEY_TOKEN));
		break;
	case 3: // "render.width" = "No"
		assert_true((code == JA_STATUS_INVALID_KEY_TOKEN));
		break;
	case 5: // "  -render.width " = "UwU"
		assert_true((code == JA_STATUS_INTEGER_CAST_ERROR));
		break;
	case 7: // "-render.width" = "X3"
		assert_true((code == JA_STATUS_INTEGER_CAST_ERROR));
		break;
	case 11: // "-sound.volume" = " +0.4 / 6"
		assert_true((code == JA_STATUS_DECIMAL_CAST_ERROR));
		break;
	case 19: // "-name" = No assignment
		assert_true((code == JA_STATUS_NO_ASSIGNMENT));
		break;
	default: break;
	}

	printf("[Intended error] Token %i: '%s' = '%s', %s\n", i, key, value, jaStatusCodeMessage(code));
}


void ConfigTest1(void** cmocka_state)
{
	(void)cmocka_state;

	struct jaConfiguration* cfg = jaConfigurationCreate();

	struct jaCvar* a;
	struct jaCvar* b;
	struct jaCvar* c;

	// Valid ones
	assert_true(jaCvarCreateInt(cfg, "render.width", 640, 0, INT_MAX, NULL) != NULL);
	assert_true(jaCvarCreateFloat(cfg, "sound.volume", 0.8f, 0.0f, 1.0f, NULL) != NULL);
	assert_true((a = jaCvarCreateInt(cfg, "render.height", 480, 0, INT_MAX, NULL)) != NULL);
	assert_true((b = jaCvarCreateString(cfg, "name", "Ranger", NULL, NULL, NULL)) != NULL);
	assert_true((c = jaCvarCreateInt(cfg, "render.fullscreen", 0, 0, 1, NULL)) != NULL);

	// Invalid names, arguments, etc
	assert_true(jaCvarCreateInt(cfg, "1render.width", 640, 0, INT_MAX, NULL) == NULL);
	assert_true(jaCvarCreateFloat(cfg, " sound..volume", 0.8f, 0.0f, 1.0f, NULL) == NULL);
	assert_true(jaCvarCreateFloat(cfg, "\tsound.volume..", 0.8f, 0.0f, 1.0f, NULL) == NULL);
	assert_true(jaCvarCreateInt(NULL, "render.height", 480, 0, INT_MAX, NULL) == NULL);
	assert_true(jaCvarCreateInt(cfg, ".render.height", 480, 0, INT_MAX, NULL) == NULL);
	assert_true(jaCvarCreateString(cfg, NULL, "Ranger", NULL, NULL, NULL) == NULL);
	assert_true(jaCvarCreateInt(cfg, "オウム", 0, 0, 1, NULL) == NULL);

	// Read arguments
	// includes whitespaces, out of range and incorrectly typed values
	const char* arg[] = {"<program_name>",
	                     "\trender.render",
	                     "Yes",
	                     "render.width",
	                     "No",
	                     "  -render.width  ",
	                     "UwU",
	                     "-render.width",
	                     "X3",
	                     "-render.height",
	                     "   240.2",
	                     "-sound.volume",
	                     " +0.4 / 6",
	                     "  -sound.volume  ",
	                     "-0.4",
	                     " \t-render.fullscreen\t ",
	                     "2",
	                     "-name",
	                     "OwO",
	                     "-name"};

	jaConfigurationArgumentsEx(cfg, JA_UTF8, JA_SKIP_FIRST, sWarningCallback, 20, arg);

	// Retrieve values
	union
	{
		int i;
		float f;
		const char* s;
	} value;

	jaCvarGetValueInt(jaCvarGet(cfg, "render.height"), &value.i, NULL);
	assert_true((value.i == 240)); // Value of "   240.2" rounded

	jaCvarGetValueFloat(jaCvarGet(cfg, "sound.volume"), &value.f, NULL);
	assert_true((value.f == 0.0f)); // Value of "-0.4" clamped

	jaCvarGetValueInt(jaCvarGet(cfg, "render.fullscreen"), &value.i, NULL);
	assert_true((value.i == 1)); // Value of "2" clamped

	jaCvarGetValueString(jaCvarGet(cfg, "name"), &value.s, NULL);
	assert_true((strcmp(value.s, "OwO") == 0));

	// Delete some
	jaCvarDelete(a);
	jaCvarDelete(b);
	jaCvarDelete(c);

	// Bye!
	jaConfigurationDelete(cfg);
}


#if 0
/*-----------------------------

 ConfigTest2_ParseFile()
-----------------------------*/
#if 0
void sWarningsCallback(enum jaStatusCode code, int line_no, const char* token, const char* key)
{
	switch (code)
	{
	case JA_STATUS_EXPECTED_KEY_TOKEN:
		printf("[Warning] Expected an configuration key in line %i, found the unknown token '%s' instead\n", line_no,
		       token);
		break;
	case JA_STATUS_EXPECTED_EQUAL_TOKEN:
		printf("[Warning] Expected an equal sign for key '%s' in line %i, found the token '%s' instead\n", key, line_no,
		       token);
		break;
	case JA_STATUS_STATEMENT_OPEN:
		printf("[Warning] Statement in line %i isn't properly closed, '%s' assignment ignored\n", line_no, key);
		break;
	case JA_STATUS_NO_ASSIGNMENT:
		printf("[Warning] Configuration '%s' in line %i didn't have a value assigned\n", key, line_no);
		break;
	default: break;
	}
}
#endif

void ConfigTest2_ParseFile(void** cmocka_state)
{
	(void)cmocka_state;
	struct jaStatus st;

	struct jaConfiguration* cfg = jaConfigurationCreate();

	assert_true(jaCvarCreateString(cfg, "osc.shape", "square", NULL, NULL, NULL) != NULL);
	assert_true(jaCvarCreateInt(cfg, "osc.frequency", 220, INT_MIN, INT_MAX, NULL) != NULL);
	assert_true(jaCvarCreateFloat(cfg, "osc.sub_volume", 0.5f, 0.0f, 1.0f, NULL) != NULL);
	assert_true(jaCvarCreateFloat(cfg, "env.attack", 50.0f, 0.0f, 1000.0f, NULL) != NULL);
	assert_true(jaCvarCreateFloat(cfg, "env.decay", 0.0f, 0.0f, 1000.0f, NULL) != NULL);
	assert_true(jaCvarCreateFloat(cfg, "env.sustain", 1.0f, 0.0f, 1.0f, NULL) != NULL);
	assert_true(jaCvarCreateFloat(cfg, "env.hold", 0.0f, 0.0f, 1000.0f, NULL) != NULL);
	assert_true(jaCvarCreateFloat(cfg, "env.release", 200.0f, 0.0f, 1000.0f, NULL) != NULL);

	printf("\nconfig1.cfg\n\n");

	if (jaConfigurationFile(cfg, "tests/config1.cfg", &st) != 0)
		jaStatusPrint("Config1", st);

	printf("\nconfig1a.cfg\n\n");

	if (jaConfigurationFile(cfg, "tests/config1a.cfg", &st) != 0)
		jaStatusPrint("Config1A", st);

	jaConfigurationDelete(cfg);
}

#endif

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

 [dictionary.c]
 - Alexander Brandt 2019

 https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
 https://stackoverflow.com/a/30874878
 https://stackoverflow.com/a/29787467
-----------------------------*/

#include "dictionary.h"
#include "buffer.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef DEBUG
#include <stdio.h>
#define DEBUG_PRINT(fmt, ...) printf(fmt, __VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...) // Whitespace
#endif

#ifdef EXPORT_SYMBOLS
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT // Whitespace
#endif


#define INITIAL_BUCKETS 8
#define BUCKET_DEPTH 2
#define THRESHOLD 75

#define FNV_OFFSET_BASIS 0xCBF29CE484222325
#define FNV_PRIME 0x100000001B3


struct Bucket
{
	struct DictionaryItem* item[BUCKET_DEPTH];
	struct Bucket* overflow_next;
};

struct CycleBucketState
{
	struct Bucket* bucket;
	size_t depth;

	struct Bucket* previous_bucket;
};

struct Dictionary
{
	size_t level;
	size_t pointer;
	size_t buckets_no;
	size_t items_no;

	union {
		struct Bucket* buckets;
		struct Buffer buckets_buffer;
	};
};


/*-----------------------------

 sHash()
-----------------------------*/
static inline uint64_t sHash(const char* key)
{
	uint64_t hash = FNV_OFFSET_BASIS;
	size_t size = strlen(key) + 1;

	for (size_t i = 0; i < size; i++)
	{
		hash = hash ^ key[i];
		hash = hash * FNV_PRIME;
	}

	return hash;
}


/*-----------------------------

 sPow()
-----------------------------*/
static inline int sPow(int base, int exp)
{
	int result = 1;

	while (exp != 0)
	{
		if (exp & 1)
			result *= base;

		exp /= 2;
		base *= base;
	}

	return result;
}


/*-----------------------------

 sAppendRemoveBuckets()
-----------------------------*/
static inline int sAppendRemoveBuckets(struct Dictionary* d, int how_many)
{
	if (BufferResizeZero(&d->buckets_buffer, (d->buckets_no + how_many) * sizeof(struct Bucket)) == NULL)
		return 1;

	d->buckets_no += how_many;
	return 0;
}


/*-----------------------------

 sCycleBucket()
-----------------------------*/
static inline int sCycleBucket(struct CycleBucketState* state, struct DictionaryItem*** out)
{
	if (state->bucket != NULL)
	{
		*out = &state->bucket->item[state->depth];
		state->depth += 1;

		if (state->depth == BUCKET_DEPTH)
		{
			state->depth = 0;
			state->previous_bucket = state->bucket;
			state->bucket = state->bucket->overflow_next;
		}

		return 0;
	}

	return 1;
}


/*-----------------------------

 sLocateInBucket()
-----------------------------*/
static int sLocateInBucket(struct Dictionary* dictionary, struct DictionaryItem* item, size_t address)
{
	struct CycleBucketState state = {0};
	struct DictionaryItem** item_slot = NULL;

	// Add item into a bucket
	state.bucket = &dictionary->buckets[address];

	while (sCycleBucket(&state, &item_slot) != 1)
	{
		if (item_slot != NULL && *item_slot == NULL)
		{
			*item_slot = item;
			break;
		}
	}

	// ... Into an overflow bucket
	if (item_slot == NULL || *item_slot != item)
	{
		struct Bucket* previous_bucket = state.previous_bucket;

		if (previous_bucket == NULL)
			previous_bucket = &dictionary->buckets[address];

		if ((previous_bucket->overflow_next = malloc(sizeof(struct Bucket))) != NULL)
		{
			memset(previous_bucket->overflow_next, 0, sizeof(struct Bucket));
			previous_bucket->overflow_next->item[0] = item;
		}
		else
			goto return_failure;
	}

	// Bye!
	return 0;

return_failure:
	return 1;
}


/*-----------------------------

 sRehashPointedItem()
-----------------------------*/
static int sRehashPointedItem(struct Dictionary* dictionary, struct DictionaryItem** slot)
{
	struct DictionaryItem* item = *slot;
	uint64_t hash = 0;
	size_t address = 0;

	// Address calculation
	hash = sHash(item->key);
	address = hash % (INITIAL_BUCKETS * sPow(2, dictionary->level));

	if (address < dictionary->pointer + 1) // +1 = Anticipating next 'update counters' operation, (*a)
		address = hash % (INITIAL_BUCKETS * sPow(2, dictionary->level + 1));

	if (address == dictionary->pointer) // New rehash produce the same address
		return 0;

	// Relocate in a bucket
	if (sLocateInBucket(dictionary, item, address) == 0)
	{
		*slot = NULL;
		DEBUG_PRINT(" - Rehashing '%s', address: %03lu -> %03lu , hash: 0x%016lX\n", item->key, dictionary->pointer,
					address, hash);
		return 0;
	}

	// Unsuccessfully bye!
	return 1;
}


/*-----------------------------

 DictionaryCreate()
-----------------------------*/
EXPORT struct Dictionary* DictionaryCreate()
{
	struct Dictionary* dictionary = NULL;

	if ((dictionary = malloc(sizeof(struct Dictionary))) != NULL)
	{
		dictionary->level = 0;
		dictionary->pointer = 0;
		dictionary->buckets_no = INITIAL_BUCKETS;
		dictionary->items_no = 0;

		memset(&dictionary->buckets_buffer, 0, sizeof(struct Buffer));

		if (BufferResizeZero(&dictionary->buckets_buffer, dictionary->buckets_no * sizeof(struct Bucket)) == NULL)
		{
			free(dictionary);
			dictionary = NULL;
		}
	}

	return dictionary;
}


/*-----------------------------

 DictionaryDelete()
-----------------------------*/
EXPORT void DictionaryDelete(struct Dictionary* dictionary)
{
	struct CycleBucketState state = {0};
	struct DictionaryItem** item_slot = NULL;

	if (dictionary != NULL)
	{
		for (size_t i = 0; i < dictionary->buckets_no; i++)
		{
			state.bucket = &dictionary->buckets[i];
			state.previous_bucket = NULL;

			while (sCycleBucket(&state, &item_slot) != 1)
			{
				if (*item_slot != NULL)
					free(*item_slot);

				// Overflow buckets
				if (state.depth == 0 && state.previous_bucket != &dictionary->buckets[i])
					free(state.previous_bucket);
			}
		}

		BufferClean(&dictionary->buckets_buffer);
		free(dictionary);
	}
}


/*-----------------------------

 DictionaryAdd()
-----------------------------*/
EXPORT struct DictionaryItem* DictionaryAdd(struct Dictionary* dictionary, const char* key, void* data,
											size_t data_size)
{
	struct DictionaryItem* item = NULL;
	struct DictionaryItem** item_slot = NULL;
	struct CycleBucketState state = {0};

	size_t key_size = 0;
	uint64_t hash = 0;
	size_t address = 0;

	if (dictionary == NULL && key == NULL)
		return NULL;

	key_size = strlen(key) + 1;

	if ((item = malloc(sizeof(struct DictionaryItem) + key_size + data_size)) != NULL)
	{
		item->dictionary = dictionary;
		strcpy(item->key, key);

		if (data_size == 0)
			item->data = data;
		else
		{
			item->data = (void*)((struct DictionaryItem*)item + 1);
			item->data = (void*)((uint8_t*)item->data + key_size);

			if (data != NULL)
				memcpy(item->data, data, data_size);
		}
	}
	else
		goto return_failure;

	// Address calculation (TODO: repeated code)
	hash = sHash(key);
	address = hash % (INITIAL_BUCKETS * sPow(2, dictionary->level));

	if (address < dictionary->pointer)
		address = hash % (INITIAL_BUCKETS * sPow(2, dictionary->level + 1));

	// Add to a bucket
	if (sLocateInBucket(dictionary, item, address) != 0)
		goto return_failure;

	DEBUG_PRINT("(DictionaryAdd) key: '%s', address: %03lu, hash: 0x%016lX\n", key, address, hash);

	// Grown mechanism
	if ((dictionary->items_no * 100) / (dictionary->buckets_no * BUCKET_DEPTH) >= THRESHOLD)
	{
		if (sAppendRemoveBuckets(dictionary, +1) != 0)
			goto return_failure;

		// Rehash pointed bucket
		state.bucket = &dictionary->buckets[dictionary->pointer];
		state.depth = 0;

		while (sCycleBucket(&state, &item_slot) != 1)
		{
			if (*item_slot != NULL)
			{
				// TODO: Free empty overflow buckets

				if (sRehashPointedItem(dictionary, item_slot) != 0)
					goto return_failure;
			}
		}

		// Update counters (*a)
		if (dictionary->pointer < (size_t)(INITIAL_BUCKETS * sPow(2, dictionary->level)))
			dictionary->pointer += 1;
		else
		{
			dictionary->pointer = 0;
			dictionary->level += 1;
		}

		DEBUG_PRINT(" - Buckets: %lu\n", dictionary->buckets_no);
	}

	// Bye!
	dictionary->items_no++;
	return item;

return_failure:
	if (item != NULL)
		free(item);

	return NULL;
}


/*-----------------------------

 DictionaryGet()
-----------------------------*/
EXPORT struct DictionaryItem* DictionaryGet(const struct Dictionary* dictionary, const char* key)
{
	struct CycleBucketState state = {0};
	struct DictionaryItem** item_slot = NULL;

	// Address calculation (TODO: repeated code)
	uint64_t hash = sHash(key);
	size_t address = hash % (INITIAL_BUCKETS * sPow(2, dictionary->level));

	if (address < dictionary->pointer)
		address = hash % (INITIAL_BUCKETS * sPow(2, dictionary->level + 1));

	DEBUG_PRINT("(DictionaryGet) key: '%s', address: %03lu, hash: 0x%016lX\n", key, address, hash);

	state.bucket = &dictionary->buckets[address];
	while (sCycleBucket(&state, &item_slot) != 1)
	{
		if (*item_slot != NULL && strcmp((*item_slot)->key, key) == 0)
			return *item_slot;
	}

	return NULL;
}


/*-----------------------------

 DictionaryRemove()
-----------------------------*/
EXPORT void DictionaryRemove(struct DictionaryItem* item)
{
	if (DictionaryDetach(item) == 0)
		free(item);
}


/*-----------------------------

 DictionaryDetach()
-----------------------------*/
EXPORT int DictionaryDetach(struct DictionaryItem* item)
{
	struct CycleBucketState state = {0};
	struct DictionaryItem** item_slot = NULL;

	// Address calculation (TODO: repeated code)
	uint64_t hash = sHash(item->key);
	size_t address = hash % (INITIAL_BUCKETS * sPow(2, item->dictionary->level));

	if (address < item->dictionary->pointer)
		address = hash % (INITIAL_BUCKETS * sPow(2, item->dictionary->level + 1));

	DEBUG_PRINT("(DictionaryDetach) key: '%s', address: %03lu, hash: 0x%016lX\n", item->key, address, hash);

	state.bucket = &item->dictionary->buckets[address];
	while (sCycleBucket(&state, &item_slot) != 1)
	{
		if (*item_slot != NULL && strcmp((*item_slot)->key, item->key) == 0)
			*item_slot = NULL;
	}

	item->dictionary->items_no -= 1;
	item->dictionary = NULL;

	// Shrink mechanism
	{
		// TODO
	}

	return 0;
}


/*-----------------------------

 DictionaryIterate()
-----------------------------*/
EXPORT void DictionaryIterate(struct Dictionary* dictionary, void (*callback)(struct DictionaryItem*, void*),
							  void* extra_data)
{
	struct CycleBucketState state = {0};
	struct DictionaryItem** item_slot = NULL;

	for (size_t i = 0; i < dictionary->buckets_no; i++)
	{
		state.bucket = &dictionary->buckets[i];
		state.previous_bucket = NULL;

		while (sCycleBucket(&state, &item_slot) != 1)
		{
			if (*item_slot != NULL)
				callback(*item_slot, extra_data);
		}
	}
}

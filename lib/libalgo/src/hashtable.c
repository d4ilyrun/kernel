#include <libalgo/hashtable.h>

#include <arch.h>

#define hashtable_entry_key(entry) ({ entry->key; })

/*
 * Hash function for 32bit keys.
 *
 * http://web.archive.org/web/20071223173210/http://www.concentric.net/~Ttwang/tech/inthash.htm
 */
static uint32_t hash32(uint32_t a)
{
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23c) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09) ^ (a >> 16);

    return a;
}

/*
 * Compute the hash value of an entry's key.
 *
 * @return The entry's index into the hashtable's list of buckets.
 */
static inline unsigned long hash_key(struct hashtable *table, const void *key)
{
#if ARCH_WORD_SIZE == 4
    return hash32((u32)key) % table->size;
#elif ARCH_WORD_SIZE == 8
    /* Too lazy to implement a real 64b hash function, so we perform a 32b hash
     * of the lower 32bits. */
    return hash32((u64)key) % table->size;
#else
#error No hash function for this architecture.
#endif
}

/*
 * Compare an hash entry against a given key for lookup.
 */
static inline bool
hash_entry_is(const struct hashtable_entry *entry, const void *key)
{
    return hashtable_entry_key(entry) == key;
}

/*
 *
 */
void __hashtable_init(struct hashtable *table, size_t size)
{
    table->size = size;

    for (size_t i = 0; i < size; ++i)
        INIT_LLIST(table->buckets[i]);
}

/*
 *
 */
void __hashtable_insert(struct hashtable *table, struct hashtable_entry *entry)
{
    llist_add(&table->buckets[hash_key(table, hashtable_entry_key(entry))],
              &entry->this);
}

/*
 *
 */
struct hashtable_entry *
____hashtable_find(struct hashtable *table, const void *key, bool remove)
{
    struct hashtable_entry *entry;
    llist_t *bucket;

    bucket = &table->buckets[hash_key(table, key)];
    FOREACH_LLIST_ENTRY (entry, bucket, this) {
        if (hash_entry_is(entry, key)) {
            if (remove)
                llist_remove(&entry->this);
            break;
        }
    }

    if (&entry->this == llist_head(bucket))
        return NULL;

    return entry;
}

/*
 *
 */
struct hashtable_entry *__hashtable_find(struct hashtable *table,
                                         const void *key)
{
    return ____hashtable_find(table, key, false);
}

/*
 *
 */
struct hashtable_entry *
__hashtable_remove(struct hashtable *table, const void *key)
{
    return ____hashtable_find(table, key, true);
}

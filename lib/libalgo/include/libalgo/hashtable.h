/**
 * @file libalgo/hashtable.h
 *
 * @defgroup libalgo_hashtable Hash table
 * @ingroup libalgo
 *
 * # Hash table
 *
 * This is our implementation for hash tables.
 *
 * ## Key
 *
 * This implementation only supports pointers for keys. The user should make
 * sure that all keys are unique since this is not verified by this library.
 *
 * @{
 */

#ifndef LIBALGO_HASHTABLE_H
#define LIBALGO_HASHTABLE_H

#include <libalgo/linked_list.h>

/**
 * Structure used to store entries inside an hashtable.
 */
struct hashtable_entry {
    void *key;                    /*!< The entry's unique key. */
    struct linked_list_node this; /*!< Used to build the hashtable's list. */
};

#define hashtable_fields size_t size;

/**
 *
 */
struct hashtable {
    hashtable_fields;
    llist_t buckets[];
};

/** Declare a hashtable structure. */
#define DECLARE_HASHTABLE(name, size) \
    union {                           \
        struct hashtable table;       \
        struct {                      \
            hashtable_fields;         \
            llist_t buckets[size];    \
        };                            \
    } name

void __hashtable_init(struct hashtable *, size_t hashtable_size);
void __hashtable_insert(struct hashtable *, struct hashtable_entry *);
struct hashtable_entry *__hashtable_remove(struct hashtable *, const void *key);
struct hashtable_entry *__hashtable_find(struct hashtable *, const void *key);

/**
 * Initialize an empty hashtable.
 *
 * @param hashtable The hashtable union declared using \c DECLARE_HASHTABLE().
 */
#define hashtable_init(hashtable) \
    __hashtable_init(&(hashtable)->table, ARRAY_SIZE((hashtable)->buckets))

/**
 * Add an entry into an hashtable.
 *
 * @param hashtable The hashtable union declared using \c DECLARE_HASHTABLE().
 * @param entry     Pointer to the hashtable entry to add.
 */
#define hashtable_insert(hashtable, entry) \
    __hashtable_insert(&(hashtable)->table, entry)

/** Remove an entry from an hashtable.
 *
 * @param hashtable The hashtable union declared using \c DECLARE_HASHTABLE().
 * @param entry     Key used to find the entry to remove.
 *
 * @return The removed entry, or NULL if it did not exist.
 */
#define hashtable_remove(hashtable, key) \
    __hashtable_remove(&(hashtable)->table, key)

/** Find an entry inside an hastable.
 *
 * @param hashtable The hashtable union declared using \c DECLARE_HASHTABLE().
 * @param entry     Key used to find the entry.
 *
 * @return The entry, or NULL if it did not exist.
 */
#define hashtable_find(hashtable, key) \
    __hashtable_find(&(hashtable)->table, key)

#endif /* LIBALGO_HASHTABLE_H */

/** @} */

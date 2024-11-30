/**
 * @file libpath.h
 * @brief Path processing library
 *
 * @author Léo DUBOIN <leo@duboin.com>
 *
 * @defgroup libpath Libpath
 * @brief Path handling library
 *
 * # Libpath
 *
 * ## Description
 *
 * This library allows us to parse and walk along a path (backward and forward).
 * It does so by splitting a path into different \c segments, around the '/'
 * separator. Once a segment has been parsed, you can retrieve the previous and
 * next ones, thus effectively walking along the original path
 * (see \ref libpath_walk for more details).
 *
 * @note This library does **not** create a path nor modify the original string,
 * and simply uses a safer simple string wrapper as well as substrings to find
 * its way around the original raw string.
 *
 * @{
 */

#ifndef LIB_LIBPATH_H
#define LIB_LIBPATH_H

#include <kernel/types.h>

#include <utils/compiler.h>

#include <stdbool.h>
#include <stddef.h>

/** Path separator. Anything else is considered as a regular character. */
#define LIBPATH_SEPARATOR '/'

/** @struct libpath_path
 *  @brief Safer wrapper around a string symbolizing a path
 */
typedef struct libpath_path {
    const char *path; ///< Original raw string containing the full path
    size_t len;       ///< The length of the raw string
} path_t;

/** Initialize a new path */
#define NEW_PATH(_path, _len)          \
    (path_t)                           \
    {                                  \
        .path = (_path), .len = (_len) \
    }

/** Dynamically create a path using libc's \c strlen function
 *  If you already know the string's length, use \ref NEW_PATH instead.
 */
#define NEW_DYNAMIC_PATH(_path) NEW_PATH((_path), strlen((_path)))

/** Check whether a path is an absolute one */
static inline bool path_is_absolute(const path_t *path)
{
    return path->len > 0 && path->path[0] == LIBPATH_SEPARATOR;
}

/** Check whether a path is empty */
static inline bool path_is_empty(const path_t *path)
{
    return path->len == 0 || (path_is_absolute(path) && path->len == 1);
}

/** Store the raw path of path's parent inside a string.
 *
 *  The string must be allocated beforehand, and be large enough to
 *  store the parent's raw path (including the NULL terminator).
 *
 *  @param parent The string inside which the parent's path is stored
 *  @param path The original path
 *  @param size The size of the \c parent buffer
 *
 *  @return The length of the parent path, -1 if the buffer wasn't large
 *          enough or if the path is empty.
 */
ssize_t path_load_parent(char *parent, const path_t *path, size_t size);

/**
 * @defgroup libpath_walk Path walking
 * @brief Walking along a path
 *
 * ## How to use
 *
 * 1. Find the first/last segment of the path (path_walk_first/path_walk_last)
 * 2. Walk forward/backward (path_walk_next/path_walk_prev)
 *
 * @{
 */

/** @struct libpath_segment
 *  @brief A single path component
 *
 * A path is split into multiple components (segments). For example, the
 * absolute path '/usr/bin/env' is split into 3 separate segments:
 * 'usr', 'bin' and 'env'
 *
 * This structure is used to walk along a path (forward or backward)
 */
typedef struct libpath_segment {
    const char *start;  ///< Start of the segment
    const char *end;    ///< End of the segment
    const path_t *path; ///< The original path the segment is a part of

    /// Start of the next segment.
    /// If this segment is the last one of the path, this is set to NULL.
    const char *next;

    /// Start of the previous segment.
    /// If this segment is the first one of the path, this is set to NULL.
    const char *prev;
} path_segment_t;

/** Loop over all the segments of a given path.
 *
 *  @param _segment Name of the iterator used by the loop to store the segment
 *  @param _path The original path
 *  @param _body The body placed inside the loop
 */
#define DO_FOREACH_SEGMENT(_segment, _path, _body)   \
    do {                                             \
        path_segment_t _segment;                     \
        if (path_walk_first((_path), &(_segment))) { \
            do {                                     \
                _body;                               \
            } while (path_walk_next(&(_segment)));   \
        }                                            \
    } while (0)

/** Check whether a segment is the first of ots containing path */
static ALWAYS_INLINE bool path_segment_is_first(const path_segment_t *segment)
{
    return segment->prev == NULL;
}

/** Check whether a segment is the first of ots containing path */
static ALWAYS_INLINE bool path_segment_is_last(const path_segment_t *segment)
{
    return segment->next == NULL;
}

/** Retieve the length of a segment's content */
static ALWAYS_INLINE size_t path_segment_length(const path_segment_t *segment)
{
    if (segment->end == NULL) {
        // If this is the last segment of a path not terminated by a separator
        // We need to compute the length based on the original full path length
        return (segment->path->len - (segment->start - segment->path->path));
    }

    return segment->end - segment->start;
}

/** Retrieve the first segment of a path
 *
 * This function returns the first segment of the path.
 *
 * @param path The path to walk along
 * @param[out] segment Pointer to a struct which the segment will be stored into
 *
 * @return \c false if no segment could be extracted, \c true otherwise
 */
bool path_walk_first(const path_t *, path_segment_t *);

/** Retrieve the last segment of a path
 *
 * This function returns the last segment of the path.
 *
 * @param path The path to walk along
 * @param[out] segment Pointer to a struct which the segment will be stored into
 *
 * @return \c false if no segment could be extracted, \c true otherwise
 */
bool path_walk_last(const path_t *, path_segment_t *);

/**
 * @brief Retrieve the next segment
 *
 * @warning The current content inside the segment's struct will be overriden
 *          with the information concerning the new segment.
 *
 * @param segment The current segment's information struct
 *
 * @return \c false if there is no next segment, \c true otherwise
 */
bool path_walk_next(path_segment_t *segment);

/** Retrieve the previous segment
 *
 * @warning The current content inside the segment's struct will be overriden
 *          with the information concerning the new segment.
 *
 * @param segment The current @ref segment's information struct
 *
 * @return \c false if there is no previous segment, \c true otherwise
 */
bool path_walk_prev(path_segment_t *segment);

/** @return \c true if the segment name corresponds to the string */
bool path_segment_is(const char *, const path_segment_t *);

/** @} */

/** @} */

#endif /* LIB_LIBPATH_H */

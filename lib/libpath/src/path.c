#include <lib/path.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** Find the first character matching a predicate inside a string */
static const char *find_first(const char *s, bool (*predicate)(char))
{
    if (s == NULL)
        return NULL;
    while (*s && !predicate(*s))
        s += 1;
    return *s ? s : NULL;
}

/** Find the first char inside a string matching a predicate in reverse order
 *
 * @param start The start of the string to look into
 * @param end The end of the string to look into
 */
static const char *
find_first_reverse(const char *start, const char *end, bool (*predicate)(char))
{
    if (start == NULL || end == NULL)
        return NULL;

    // Start looking from the last character
    end -= 1;
    while (start <= end && !predicate(*end))
        end -= 1;

    return (start <= end) ? end : NULL;
}

/** Checks for a path separator */
static inline bool issep(char c)
{
    return c == LIBPATH_SEPARATOR;
}

/** Negative check for a path separator */
static inline bool isnotsep(char c)
{
    return !issep(c);
}

/** Parse a segment's content
 *
 * This function only delimits the segment's name (start, end/next)
 * when trying to walk forward in a path. You still must manually set the \c
 * prev and \c path field yourself.
 *
 * This cannot be used when trying to walk backwards in a path.
 *
 * @param start The start of the segment's string
 */
static path_segment_t path_segment_parse(const char *start)
{
    path_segment_t segment = {
        // in case the segment begins with separators skip them (walk_first)
        .start = find_first(start, isnotsep),
    };

    segment.end = find_first(segment.start, issep);
    segment.next = find_first(segment.end, isnotsep);

    return segment;
}

static path_segment_t *path_segment_parse_prev(path_segment_t *prev)
{
    // If there are characters before the new segments, there *might* be another
    // segment before => we need to find the start of this previous segment

    if (prev->start == prev->path->path) {
        return prev;
    }

    // We skip the separators before the new segment.
    const char *end_prev =
        find_first_reverse(prev->path->path, prev->start, isnotsep) + 1;
    // Then we skip the previous segment's content
    prev->prev = find_first_reverse(prev->path->path, end_prev, issep);

    // If NULL && path is *relative*, this means that the previous block is
    // necessarily the first one (no separator at the start).
    if (prev->prev == NULL && !path_is_absolute(prev->path)) {
        prev->prev = prev->path->path;
    } else if (prev->prev != NULL) {
        // prev->prev is set to point to the separator otherwise
        prev->prev += 1;
    }

    return prev;
}

bool path_walk_first(const path_t *path, path_segment_t *segment)
{
    if (path_is_empty(path))
        return false;

    *segment = path_segment_parse(path->path);
    segment->path = path;

    return path_segment_length(segment) > 0;
}

bool path_walk_last(const path_t *path, path_segment_t *segment)
{
    if (path_is_empty(path))
        return false;

    // Find the last separator inside the complete path
    // Skip ending separators (e.g. '/usr/bin/bash/' => 'bash')
    const char *end =
        find_first_reverse(path->path, path->path + path->len, isnotsep) + 1;
    const char *start = find_first_reverse(path->path, end, issep);

    if (start == NULL) {
        start = path->path;
    } else {
        start += 1;
    }

    *segment = (path_segment_t){
        .start = start,
        .end = end,
        .next = NULL,
        .path = path,
    };

    path_segment_parse_prev(segment);

    return path_segment_length(segment) > 0;
}

bool path_walk_next(path_segment_t *segment)
{
    if (path_segment_is_last(segment))
        return false;

    path_segment_t next = path_segment_parse(segment->next);

    next.path = segment->path;
    next.prev = segment->start;

    *segment = next;

    return true;
}

bool path_walk_prev(path_segment_t *segment)
{
    if (path_segment_is_first(segment))
        return false;

    *segment = (path_segment_t){
        .start = segment->prev,
        .end = find_first(segment->prev, issep),
        .next = segment->start,
        .path = segment->path,
    };

    path_segment_parse_prev(segment);

    return true;
}

bool path_segment_is(const char *name, const path_segment_t *segment)
{
    size_t len = path_segment_length(segment);
    return strncmp(name, segment->start, len) == 0 && name[len] == '\0';
}

ssize_t path_load_parent(char *parent, const path_t *path, size_t size)
{
    path_segment_t segment;
    size_t parent_length;

    if (size == 0 || parent == NULL)
        return -1;

    if (!path_walk_last(path, &segment))
        return -1;

    if (path_segment_is_first(&segment)) {
        if (!path_is_absolute(path)) {
            parent[0] = '\0';
            return 0;
        }
        parent_length = 1;
    } else {
        path_walk_prev(&segment);
        parent_length = segment.end - path->path;
    }

    if (parent_length >= size)
        return -1;

    memcpy(parent, path->path, parent_length);
    parent[parent_length] = '\0';

    return parent_length;
}

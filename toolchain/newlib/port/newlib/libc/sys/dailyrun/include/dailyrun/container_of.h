#ifndef UTILS_CONTAINER_OF_H
#define UTILS_CONTAINER_OF_H

/**
 * @brief Cast a member of a structure out to the containing structure
 * @ingroup utils
 *
 * @param _ptr The pointer to the member
 * @param _type The type of the container struct
 * @param _field The name of the field within the struct
 */
#define container_of(_ptr, _struct, _field) \
    ((_struct *)(((void *)_ptr) - offsetof(_struct, _field)))

#endif /* UTILS_CONTAINER_OF_H */

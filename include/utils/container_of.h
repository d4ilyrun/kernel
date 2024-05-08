#ifndef UTILS_CONTAINER_OF_H
#define UTILS_CONTAINER_OF_H

#define container_of(_ptr, _struct, _field) \
    ((_struct *)(((void *)_ptr) - offsetof(_struct, _field)))

#endif /* UTILS_CONTAINER_OF_H */

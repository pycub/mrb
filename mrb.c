#include "mrb.h"

#include <clog.h>
#include <unistd.h>
#include <sys/mman.h>


#define _MIN(a, b) ((a) < (b)) ? (a) : (b)


int
mrb_init(struct mrb *b, size_t size) {
    int pagesize = getpagesize();
   
    /* Calculate the real size (multiple of pagesize). */
    if (size % pagesize) {
        ERROR(
            "Invalid size: %lu, size should be multiple of pagesize, see "
            "getpagesize(2).", 
            size
        );
        return -1;
    }

    b->size = size;
    b->writer = 0;
    b->reader = 0;

    /* Create a temporary file with requested size as the backend for mmap. */
    const int fd = fileno(tmpfile());
    ftruncate(fd, b->size);
  
    /* Allocate the underlying backed buffer. */
    b->buff = mmap(
            NULL, 
            b->size * 2,
            PROT_NONE, 
            MAP_ANONYMOUS | MAP_PRIVATE,
            -1, 
            0
        );
    if (b->buff == MAP_FAILED) {
        close(fd);
        return -1;
    }

    unsigned char *first = mmap(
            b->buff, 
            b->size, 
            PROT_READ | PROT_WRITE,
            MAP_FIXED | MAP_SHARED,
            fd,
            0
        );

    if (first == MAP_FAILED) {
        munmap(b->buff, b->size * 2);
        close(fd);
        return -1;
    }

    unsigned char *second = mmap(
            b->buff + b->size, 
            b->size, 
            PROT_READ | PROT_WRITE,
            MAP_FIXED | MAP_SHARED,
            fd,
            0
        );
 
    if (second == MAP_FAILED) {
        munmap(b->buff, b->size * 2);
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}


struct mrb *
mrb_create(size_t size) {
    struct mrb *b;

    /* Allocate memory for mrb structure. */
    b = malloc(sizeof(struct mrb));
    if (b == NULL) {
        return b;
    }

    if (mrb_init(b, size)) {
        free(b);
        return NULL;
    }

    return b;
}


int
mrb_deinit(struct mrb *b) {
    /* unmap second part */
    if (munmap(b->buff + b->size, b->size)) { 
        return -1;
    }

    /* unmap first part */
    if (munmap(b->buff, b->size)) {
        return -1;
    }

    /* unmap backed buffer */
    if (munmap(b->buff, b->size * 2)) {
        return -1;
    }
    return 0;
}


int
mrb_destroy(struct mrb *b) {
    if (mrb_deinit(b)) {
        return -1;
    }
    free(b);
    return 0;
}


/** Obtain the total buffer capacity of a VRB.
 */
size_t
mrb_space(struct mrb *b) {
    return b->size;
}


/** Obtain the length of empty space in the buffer.
 */
size_t
mrb_space_available(struct mrb *b) {
    // 11000111
    //   w  r
    if (b->writer < b->reader) {
        return b->reader - b->writer - 1;
    }
    
    // 00111100
    //   r   w
    return b->size - (b->writer - b->reader) - 1;
}


/** Obtain the length of data in the buffer.
 */
size_t
mrb_space_used(struct mrb *b) {
    // 00111000
    //   r  w
    if (b->writer >= b->reader) {
        return b->writer - b->reader;
    }
    
    // 11000111
    //   w  r
    return b->size - (b->reader - b->writer);
}


/** Determine if the buffer is currently empty.
 */
bool
mrb_is_empty(struct mrb *b) {
    return b->reader == b->writer;
}


/** Determine if the buffer is currently full.
 */
bool
mrb_is_full(struct mrb *b) {
    return b->size == (mrb_space_used(b) + 1);
}


/** Copy data from a caller location to the magic ring buffer. 
 */
size_t
mrb_put(struct mrb *b, char *source, size_t size) {
    size_t amount = _MIN(size, mrb_space_available(b));
    memcpy(b->buff + b->writer, source, amount);
    b->writer = (b->writer + amount) % b->size;
    return amount;
}


/** Copy data to the magic ring buffer only if all of it will fit.
 */
int
mrb_put_all(struct mrb *b, char *source, size_t size) {
    if (size > mrb_space_available(b)) {
        return -1;
    }
    memcpy(b->buff + b->writer, source, size);
    b->writer = (b->writer + size) % b->size;
    return 0;
}


/** Copy data from the magic ring buffer to a caller location.
 */
size_t
mrb_get(struct mrb *b, char *dest, size_t size) {
    size_t amount = _MIN(size, mrb_space_used(b));
    memcpy(dest, b->buff + b->reader, amount);
    b->reader = (b->reader + amount) % b->size;
    return amount;
}

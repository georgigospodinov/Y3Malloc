#include <stddef.h>
/*	Allocate 'size' bytes of memory. On success the function returns a pointer to
	the start of the allocated region. On failure NULL is returned. */
extern void *myalloc(int size);

/*	Release the region of memory pointed to by 'ptr'. */
extern void myfree(void *ptr);

extern void* myrealloc(void* ptr, size_t size);

extern void* mycalloc(size_t number_of_items, size_t item_size);
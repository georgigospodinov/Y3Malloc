#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <limits.h>
#include <errno.h>
#include <string.h>

#include "myalloc.h"

#define NODE_SIZE (sizeof(struct Node))
#define MAX_MMAPS 1024
// default page size is 1MB (2^20 Bytes)
#define SINGLE_PAGE_LENGTH (1024*1024)
#define WORD_SIZE_IN_BYTES 8
#define HEADER_SIZE WORD_SIZE_IN_BYTES
#define NO_PAGE_FOUND (-1)

typedef struct Node {
    struct Node *prev;
    struct Node *next;
} *Node;
Node free_lists[MAX_MMAPS];
int index_of_page = NO_PAGE_FOUND;
int pages_mapped = 0;

struct PageLength {
    void *addr;
    long number_of_pages;
};
/**
 * This array stores the lengths of the pages,
 * so that it can be searched when munmapping a page.
 */
struct PageLength lengths[MAX_MMAPS];

void index_of_page_containing(void *addr) {

    for (int i = 0; i < pages_mapped; i++) {
        char *start = lengths[i].addr;
        char *end = start + lengths[i].number_of_pages * SINGLE_PAGE_LENGTH;
        if (start <= (char *) addr && (char *) addr < end) {
            index_of_page = i;
            return;
        }
    }

    index_of_page = NO_PAGE_FOUND;
}

/**
 * To be called when a new page is mmaped.
 * Saves the following information:
 * page length in the header and the lengths array,
 * free list node in the address immediately after the header,
 * the page address to the lengths array.
 * 
 * @param addr the start address of the page
 * @param number_of_pages the length of the page
 * @return the address node at the first free space in the page
 */
Node addPageMeta(long *addr, long number_of_pages) {
    // Header
    *addr = number_of_pages * SINGLE_PAGE_LENGTH - WORD_SIZE_IN_BYTES;

    // Free list node
    Node n = (Node) (addr + 1);
    n->prev = NULL;
    n->next = NULL;
    free_lists[pages_mapped] = n;

    // Length references
    lengths[pages_mapped].addr = addr;
    lengths[pages_mapped].number_of_pages = number_of_pages;
    index_of_page = pages_mapped++;

    return n;
}

/**
 * mmaps a page long enough to store size bytes.
 * The length of the page is a multiple of DEFAULT_PAGE_LENGTH.
 * 
 * @param size storage capacity required in bytes
 * @return the first address usable by the user
 */
void *newPage(long size) {
    if (pages_mapped == MAX_MMAPS) return NULL;

    long number_of_pages = (size / SINGLE_PAGE_LENGTH) + 1;
    void *addr = mmap(NULL,  // preferred start address
                      (size_t) (number_of_pages * SINGLE_PAGE_LENGTH),
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      0,  // file descriptor
                      0);  // offset
    int err = errno;
    if (err) printf("ERROR: %s\n", strerror(err));

    return addPageMeta(addr, number_of_pages);
}

/**
 * Looks through all currently mapped pages for one that has space
 * for a block of the given size.
 * Returns the node at the start of this free block or NULL.
 * This function is to be used with the cutOut function that actually
 * does the allocation and prepares the next block.
 * 
 * @param size the number of bytes to look for
 * @return the node at the start of the block or NULL
 */
Node findFitting(int size) {
    for (int i = 0; i < pages_mapped; i++)
        for (Node current = free_lists[i]; current != NULL; current = current->next) {
            // The value preceding the node is the header (contains size of free space).
            if (*((long *) current - 1) >= size) {
                index_of_page = i;
                return current;
            }
        }

    index_of_page = NO_PAGE_FOUND;
    return NULL;
}

/**
 * Cuts out a memory block of the given size.
 * The block starts at the location of the Node,
 * and a new header and node are created after the block.
 * 
 * @param n the node (free space) to take
 * @param size the number of bytes to take
 * @return a pointer to the start of this block
 */
void *cutOut(Node n, int size) {

    long *old_header_location = (long *) n - 1;

    long free = *old_header_location;
    // Get whole block if no space for a new header and node.
    if (size <= free && size + HEADER_SIZE + NODE_SIZE > free) {

        // Disconnect node:
        if (n->next) n->next->prev = n->prev;
        if (n->prev) n->prev->next = n->next;
            /* if there was no previous node to the one found,
             * then this node is the head of its list and must be removed from the list.
             */
        else free_lists[index_of_page] = NULL;
        return n;
    }

    // Move and align to WORD_SIZE_IN_BYTES bytes.
    char *as_bytes = (char *) n;
    as_bytes += size;
    int remaining_to_align = size % WORD_SIZE_IN_BYTES;
    if (remaining_to_align)
        as_bytes += WORD_SIZE_IN_BYTES - remaining_to_align;

    long *new_header_location = (long *) as_bytes;
    // Recalculate Pointers:
    Node new_node = (Node) (new_header_location + 1);  // One word after header.
    new_node->prev = n->prev;
    new_node->next = n->next;

    if (n->next) n->next->prev = new_node;
    if (n->prev) n->prev->next = new_node;
        /* if there was no previous node to the one found,
         * set the new node as the first for the page.
         */
    else free_lists[index_of_page] = new_node;

    // Set new Header
    *new_header_location = *old_header_location - size - WORD_SIZE_IN_BYTES;
    if (remaining_to_align)
        *new_header_location -= WORD_SIZE_IN_BYTES - remaining_to_align;

    *old_header_location = (new_header_location - (long *) n) * WORD_SIZE_IN_BYTES;

    return n;
}

void *myalloc(int size) {

    // The allocated block should be able to fit a node when freed.
    size = size < NODE_SIZE ? (int) NODE_SIZE : size;
    // Find a page that can fit size bytes.
    Node ptr = findFitting(size);

    // If no such, new page.
    if (!ptr) ptr = newPage(size);

    // If page, cut out size, else NULL.
    return ptr ? cutOut(ptr, size) : NULL;
}

Node merge(Node left, Node right) {

    left->next = right->next;
    if (right->next)
        right->next->prev = left;

    // merge sizes
    long *left_header_location = (long *) left - 1;
    long *right_header_location = (long *) right - 1;
    *left_header_location = *left_header_location + *right_header_location + HEADER_SIZE;
    *right_header_location = 0;

    return left;
}

/**
 * Coalesce with neighbouring nodes.
 * Checks if the neighbouring nodes are in use
 * and merges with those that are not.
 * Then returns the leftmost header that has merged with the given one.
 * So coalescing with prev returns prev, otherwise returns h.
 * 
 * @param node Header to Coalesce
 * @return The leftmost header after coalescing
 */
Node coalesceWithNeighbours(Node node) {
    Node n = node->next, p = node->prev;
    long *header_location = (long *) node - 1;
    long distance_in_bytes = ((long *) n - (long *) node) * WORD_SIZE_IN_BYTES - HEADER_SIZE;

    if (n != NULL && distance_in_bytes == *header_location)
        merge(node, n);

    header_location = (long *) p - 1;
    distance_in_bytes = ((long *) node - (long *) p) * WORD_SIZE_IN_BYTES - HEADER_SIZE;
    if (p != NULL && distance_in_bytes == *header_location)
        return merge(p, node);

    return node;
}

void freePage(Node n) {
    // There must be no other nodes in the list.
    if (n->next || n->prev) return;
    long *page_start = (long *) n - 1;
    size_t block_size = (size_t) (lengths[index_of_page].number_of_pages * SINGLE_PAGE_LENGTH);

    // The pointer before the node, must be the start of the page.
    if (page_start != lengths[index_of_page].addr ||
        // The size of the block must be the whole mmaped region.
        block_size != *page_start + HEADER_SIZE)
        return;

    // free it.
    munmap(n, block_size);

    // Shift arrays.
    pages_mapped--;
    for (int i = index_of_page; i < pages_mapped; i++) {
        lengths[i] = lengths[i + 1];
        free_lists[i] = free_lists[i + 1];
    }
}

/**
 * Assumes ptr is an address given by myalloc.
 *
 * @param ptr the start address of the memory block to free
 * @return the inserted node in the free list
 */
Node insertInFreeList(void *ptr) {

    Node n = (Node) ptr;
    // Find the first node, positioned after the returned block.
    Node current = free_lists[index_of_page];
    if (!current) {  // list is empty, n must be first & only
        n->next = NULL;
        n->prev = NULL;
        free_lists[index_of_page] = n;
    }
    else  // find position in list
        while (1) {
            // insert n before current
            if (current > n) {
                n->next = current;
                n->prev = current->prev;
                if (current->prev)  // current has no previous if it's the start of the list
                    current->prev->next = n;
                else free_lists[index_of_page] = n;
                current->prev = n;
                break;
            }
            // insert n after current (as last)
            if (current->next == NULL) {
                n->next = NULL;
                n->prev = current;
                current->next = n;
                break;
            }
            current = current->next;
        }

    return n;
}

void myfree(void *ptr) {

    index_of_page_containing(ptr);
    if (index_of_page == NO_PAGE_FOUND) return;

    Node node = insertInFreeList(ptr);

    node = coalesceWithNeighbours(node);
    freePage(node);
}

void *mycalloc(size_t number_of_items, size_t item_size) {
    return myalloc((int) ((number_of_items * item_size) & INT_MAX));
}

void *myrealloc(void *ptr, size_t new_size) {
    char *new_bytes = myalloc((int) (new_size & INT_MAX));
    char *old_bytes = (char *) ptr;

    long *header_location = (long *) ptr - 1;
    long old_size = *header_location;
    long end_bound = new_size < old_size ? new_size : old_size;

    for (int i = 0; i < end_bound; i++)
        new_bytes[i] = old_bytes[i];

    myfree(old_bytes);

    for (int i = end_bound; i < new_size; i++)
        new_bytes[i] = 0;

    return new_bytes;
}
/*
 * mm.c - Implicit free list allocator with first fit.
 *
 * Every block starts with a header of SIZE_T_SIZE bytes. The header
 * stores the total size of the block (header + payload, always a
 * multiple of ALIGNMENT). Because the size is a multiple of 8, the
 * lowest bit is used as the allocated flag (1 = allocated, 0 = free).
 *
 * mm_malloc walks the heap from mem_heap_lo() to endPointer and takes
 * the first free block that is big enough (first fit). While walking,
 * neighbouring free blocks are merged. If the found block is too big,
 * it is split and the rest becomes a new free block. If nothing fits,
 * the heap is extended with mem_sbrk.
 *
 * mm_free clears the allocated bit and merges with the next block if
 * it is free (coalescing).
 *
 * mm_realloc tries to resize the block in place (shrink, absorb free
 * following blocks, or grow the heap if it is the last block). Only if
 * this is not possible a new block is allocated and the data copied.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* points to the first byte after the last block in the heap */
void *endPointer = NULL;

/*
 * mm_init - initialize the malloc package.
 *     The driver resets the heap before every trace, so the heap is
 *     empty here and endPointer is set to its beginning.
 */
int mm_init(void)
{
    endPointer = mem_heap_lo();
    return 0;
}

/*
 * splitBlock - mark the block at beginPointer as allocated with newsize
 *     bytes. If there are bytes left over, they become a new free block.
 */
static void splitBlock(void *beginPointer, size_t newsize, size_t actualSize)
{
    if (actualSize - newsize >= SIZE_T_SIZE)
    {
        *(size_t *)beginPointer = newsize | 1;
        *(size_t *)((char *)beginPointer + newsize) = actualSize - newsize;
    }
    else
    {
        *(size_t *)beginPointer = actualSize | 1;
    }
}

/*
 * mm_malloc - Allocate a block by searching the heap for a free block
 *     (first fit). If no block fits, the brk pointer is incremented.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    if (size == 0)
        return NULL;

    size_t newsize = ALIGN(size + SIZE_T_SIZE);
    void *beginPointer = mem_heap_lo();
    void *lastFree = NULL;

    while (beginPointer < endPointer)
    {
        size_t isFree = !(*((size_t *)beginPointer) & 1);
        size_t actualSize = *((size_t *)beginPointer) & ~0x7;

        if (isFree)
        {
            // coalescing with the following free blocks
            void *next = (void *)((char *)beginPointer + actualSize);
            while (next < endPointer && !(*((size_t *)next) & 1))
            {
                actualSize += *((size_t *)next) & ~0x7;
                *(size_t *)beginPointer = actualSize;
                next = (void *)((char *)beginPointer + actualSize);
            }

            if (newsize <= actualSize)
            {
                splitBlock(beginPointer, newsize, actualSize);
                return (void *)((char *)beginPointer + SIZE_T_SIZE);
            }

            // remember a free block at the end of the heap
            if (next >= endPointer)
                lastFree = beginPointer;
        }

        beginPointer = (void *)((char *)beginPointer + actualSize);
    }

    // last block is free but too small: only extend by the missing bytes
    if (lastFree != NULL)
    {
        size_t freeSize = *((size_t *)lastFree) & ~0x7;
        if (mem_sbrk(newsize - freeSize) == (void *)-1)
            return NULL;
        *(size_t *)lastFree = newsize | 1;
        endPointer = (void *)((char *)lastFree + newsize);
        return (void *)((char *)lastFree + SIZE_T_SIZE);
    }

    void *p = mem_sbrk(newsize);
    if (p == (void *)-1)
        return NULL;
    else
    {
        *(size_t *)p = newsize | 1;
        endPointer = (void *)((char *)p + newsize);
        return (void *)((char *)p + SIZE_T_SIZE);
    }
}

/*
 * mm_free - Mark the block as free and merge it with the next block
 *     if that one is free too.
 */
void mm_free(void *ptr)
{
    if (ptr == NULL)
        return;

    size_t *sizePointer = (size_t *)(((char *)ptr) - SIZE_T_SIZE);
    size_t size = *sizePointer & ~0x7;
    *sizePointer = size;

    // coalescing
    size_t *next = (size_t *)((char *)sizePointer + size);
    if ((void *)next < endPointer && ((*next) & 1) == 0)
    {
        size_t nextSize = *next & ~0x7;
        *sizePointer = size + nextSize;
    }
}

/*
 * mm_realloc - Resize the block in place if possible, otherwise
 *     implemented in terms of mm_malloc and mm_free.
 */
void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == NULL)
    {
        return mm_malloc(size);
    }

    else if (size == 0)
    {
        mm_free(ptr);
        return NULL;
    }

    else
    {
        void *oldptr = ptr;
        void *newptr;
        size_t copySize;

        size_t *sizePointer = (size_t *)(((char *)oldptr) - SIZE_T_SIZE);
        size_t oldSize = *sizePointer & ~0x7;
        size_t newsize = ALIGN(size + SIZE_T_SIZE);

        // old payload size, needed for copying later
        copySize = oldSize - SIZE_T_SIZE;

        // absorb the following free blocks
        void *next = (void *)((char *)sizePointer + oldSize);
        while (next < endPointer && !(*((size_t *)next) & 1))
        {
            oldSize += *((size_t *)next) & ~0x7;
            *sizePointer = oldSize | 1;
            next = (void *)((char *)sizePointer + oldSize);
        }

        // block is big enough: shrink or keep in place
        if (newsize <= oldSize)
        {
            splitBlock(sizePointer, newsize, oldSize);
            return oldptr;
        }

        // last block in the heap: just extend the heap
        if (next >= endPointer)
        {
            if (mem_sbrk(newsize - oldSize) == (void *)-1)
                return NULL;
            *sizePointer = newsize | 1;
            endPointer = (void *)((char *)sizePointer + newsize);
            return oldptr;
        }

        newptr = mm_malloc(size);
        if (newptr == NULL)
        {
            return NULL;
        }

        if (size < copySize)
        {
            copySize = size;
        }

        memcpy(newptr, oldptr, copySize);
        mm_free(oldptr);
        return newptr;
    }
}

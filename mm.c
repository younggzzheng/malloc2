#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "./mm.h"
#include "./memlib.h"
#include "./mminline.h"


// rounds up to the nearest multiple of WORD_SIZE
static inline size_t align(size_t size) {
  return (((size) + (WORD_SIZE - 1)) & ~(WORD_SIZE - 1));
}

int mm_check_heap(void);


/*
*                             _       _ _
*     _ __ ___  _ __ ___     (_)_ __ (_) |_
*    | '_ ` _ \| '_ ` _ \    | | '_ \| | __|
*    | | | | | | | | | | |   | | | | | | |_
*    |_| |_| |_|_| |_| |_|___|_|_| |_|_|\__|
*                       |_____|
*
* initializes the dynamic storage allocator (allocate initial heap space)
* arguments: none
* returns: 0, if successful
*         -1, if an error occurs
*/
block_t *prologue;
block_t *epilogue;
block_t *first_block;
int counter = 0;

int mm_init(void) {
  void *start;
  start = mem_sbrk(2 * TAGS_SIZE);  // 16 + 16 bytes for pro and epilogue,
  if (start == (void *) -1) {
    fprintf(stderr, "%s\n", "My Error: Ran out of memory");
    return 1;
  }

  assert(start == mem_heap_lo());
  prologue = (block_t *) start;
  block_set_size_and_allocated (prologue, TAGS_SIZE, 1);

  epilogue = block_next(prologue);
  block_set_size_and_allocated (epilogue, TAGS_SIZE, 1);
  flist_first = NULL;
  return 0;
}
/*
returns a pointer to the first free block in the free list that has a paylod of at least size.
we are subtracting 16 from the block size instead of 32 because there is actually 16 bytes in there
that can be used as payload.
returns NULL if nothing fits.
*/
block_t *first_fit(size_t size) {
  block_t *curr_block = flist_first;
  if (curr_block == NULL) {  // the free list has not yet been initalized, so return null.
    return NULL;
  }
  do {
    assert(!block_allocated(curr_block));
    size_t payload_size = block_size(curr_block) - 16;
    if (payload_size >= size) { // if the payload is at least size big, return that space.
      return curr_block;
    }
    curr_block = block_next_free(curr_block); // only goes for the next free block.
  } while (curr_block != flist_first);
  return NULL;
}

/*

coalesces the input block_t curr_block with blocks around it.
returns the coaesced block.

Case 1: prev and next are allocated: Do nothing.
Case 2: prev allocated, next is free. merge current with next.
Case 3: prev free, next allocated. merge current with prev.
Case 4: both surrounding are free, merge with both.

*/
block_t *coalesce (block_t * curr_block) { //TODO: error check all calls to inlines

  int current_free = !block_allocated(curr_block);
  assert (current_free);
  int prev_free = !block_allocated(block_prev(curr_block));
  int next_free = !block_allocated(block_next(curr_block));
  size_t current_size = block_size (curr_block);
  size_t prev_size = block_prev_size(curr_block);
  size_t next_size = block_next_size(curr_block);
  block_t *prev = block_prev(curr_block);
  block_t *next = block_next(curr_block);
  // Case 1 prev and next are allocated: Do nothing.
  if (!prev_free && !next_free) {
    //printf("%s\n", "Case 1");
    //mm_check_heap();
    return curr_block;
  }
  // Case 2: prev allocated, next is free. merge current with next.
  if (!prev_free && next_free) {
    pull_free_block(next);
    block_set_size(curr_block, current_size + next_size);
    assert(!block_allocated(curr_block));
    return curr_block;
  }
  // Case 3: prev free, next allocated. merge current with prev.
  if (prev_free && !next_free) {
    pull_free_block(curr_block);
    block_set_size(prev, prev_size + current_size); // increase size of previous block.
    assert(!block_allocated(prev));
    return prev;
  }
  // Case 4: both surrounding are free, merge with both.
  if (prev_free && next_free) {
    pull_free_block(next); // pull out the current and next blocks
    pull_free_block(curr_block);
    block_set_size(prev, prev_size + current_size + next_size); // increase size of previous block.
    assert(!block_allocated(prev));
    return prev;
  }
  fprintf(stderr, "%s\n", "coalescing failed.");
  return NULL; // if none of these, return NULL.
}

/*
Creates a new block that is the size of how much you want to extend the heap by.
Epilogue will be overwritten (new one will be created at end of new, larger heap)
and a new block the size of block will be placed in the same place epilogue used to be.
Insures everything is coalesced. Does so using mem_sbrk

input: size_t size that is how much you want to extend the heap by.
output: block_t*, a pointer to the block that is at the end of the extended heap to be used.
*/
block_t *mm_extend_heap (size_t size) { 
  size = align(size);  // making sure it's aligned.
  if (size < 32) {
    fprintf(stderr, "%s\n", "must extend heap by at least size of a block.");
  }

  if(size < 640) {
    size = 640;
  }

  if (mem_sbrk(size + TAGS_SIZE) == (void *) -1) {
    fprintf(stderr, "%s\n", "Ran out of memory");
    return NULL;
  }
  block_t *new_block;
  new_block = epilogue;
  block_set_size_and_allocated(new_block, size + TAGS_SIZE , 0);  // initializing new free block
  epilogue = block_next(new_block);
  block_set_size_and_allocated (epilogue, TAGS_SIZE, 1);  // initializing new epilogue
  insert_free_block(new_block);  // inserting new block.
  return coalesce (new_block); // insures that heap is still coalesced. returns ptr to last free block.
}

/*
return 0 on success, 1 on failure.

split_block will first check to see if a block is able to be split. If the size of block - size > MINBLOCKSIZE,
then we are good to split. else it'll just return.

split_block will construct a new block of size size + TAGS_SIZE by updating the pointer to block with its new size. it will
set it to allocated, because its about to get returned.

next, it will call next_block on this newly created block. this block will be our new block, and will be set to size

block_size(original_block) - block_size(newly_created_block). it will be set to free, and will call coalesce on it.

*/
// note that size_of_first_block indicates the entire size of block with tags included.
int split_block(block_t* original_block, size_t size_of_first_block) {
  size_t original_block_size = block_size(original_block);
  size_t leftover_size = original_block_size - size_of_first_block;
  if (leftover_size < original_block_size / 2 || leftover_size < 32) {  // because a TA said to do this.
    return 1;
  }
  block_t *first_block = original_block;
  block_set_size_and_allocated(first_block, size_of_first_block, 1);
  block_t *leftover_block = block_next(first_block);
  block_set_size_and_allocated(leftover_block, leftover_size, 0);  // size is anything left over.
  insert_free_block(leftover_block);
  coalesce(leftover_block);
  return 0;
}



/*     _ __ ___  _ __ ___      _ __ ___   __ _| | | ___   ___
*    | '_ ` _ \| '_ ` _ \    | '_ ` _ \ / _` | | |/ _ \ / __|
*    | | | | | | | | | | |   | | | | | | (_| | | | (_) | (__
*    |_| |_| |_|_| |_| |_|___|_| |_| |_|\__,_|_|_|\___/ \___|
*                       |_____|
*
* allocates a block of memory and returns a pointer to that block's payload
* arguments: size: the desired payload size for the block
* returns: a pointer to the newly-allocated block's payload (whose size
*          is a multiple of ALIGNMENT), or NULL if an error occurred
*/
/*
Order of operations:
(1) Ignore spurious requests
(2) Adjust block size to include overhead and alignment requests
(3) Search the free list for a flist_first
(4) No fit found. Use mm_extend_heap to get more memory and get block.
*/

void *mm_malloc(size_t size) {
  block_t *to_return = NULL;
  // (1) Ignore spurious requests
  if (size == 0) {
    return to_return;
  }
  // (2) Adjust block size to include overhead and alignment requests
  if (size <= 32) {
    size = 32;  //TODO: this is not very compact. how to change?
  } else {
    size = align(size);  // making sure it's aligned.
  }
  // (3) Search the free list for a fit
  to_return = first_fit(size); // to_return has a payload of at least size.
  /*  NO FIT FOUND */
  if (to_return == NULL) {
    to_return = mm_extend_heap(size);
  }
  pull_free_block(to_return);
  split_block(to_return, size + TAGS_SIZE);  // keep in mind that split_block's second argument asks for FULL SIZE of desired block.
  block_set_allocated(to_return, 1);
  return to_return->payload;
}


/*                              __
*     _ __ ___  _ __ ___      / _|_ __ ___  ___
*    | '_ ` _ \| '_ ` _ \    | |_| '__/ _ \/ _ \
*    | | | | | | | | | | |   |  _| | |  __/  __/
*    |_| |_| |_|_| |_| |_|___|_| |_|  \___|\___|
*                       |_____|
*
* frees a block of memory, enabling it to be reused later
* arguments: ptr: pointer to the block's payload
* returns: nothing
*/
void mm_free(void *ptr) {
  if (ptr == NULL) {
    printf("%s\n", "trying to free a null.");
    return;
  }

  block_t *block_to_free = payload_to_block(ptr);
  block_set_allocated(block_to_free, 0);
  insert_free_block(block_to_free);
  coalesce(block_to_free);
  return;
}

/*
*                                            _ _
*     _ __ ___  _ __ ___      _ __ ___  __ _| | | ___   ___
*    | '_ ` _ \| '_ ` _ \    | '__/ _ \/ _` | | |/ _ \ / __|
*    | | | | | | | | | | |   | | |  __/ (_| | | | (_) | (__
*    |_| |_| |_|_| |_| |_|___|_|  \___|\__,_|_|_|\___/ \___|
*                       |_____|
*
* reallocates a memory block to update it with a new given size
* arguments: ptr: a pointer to the memory block's payload
*            size: the desired new block size
* returns: a pointer to the new memory block's payload
*/

/*
  requested size < original_payload_size:
    (1) malloc a new block of requested size and copy over the data. then free it
    (2) split the block so that unused space is freed and coalesced <-- TODO: be sure to check if all the data is retained. tag could
    be overwriting payload.
  requested size > original_payload_size:
    (1) free the whole block, coalesce, malloc a new one. <-- this can't be right...
    (2) check the neighboring blocks. if either one of them is free, then we might have a winner!
        bc that means we wont need to malloc anything new. well, isn't that just the same as freeing it
        and then mallocing? because it'll check in the free list, it'll coalesce it with anything it needs to, and then
        pick the best place to put it. I think it's actually the way to go.
*/
void *mm_realloc(void *ptr, size_t size) {
  if (ptr == NULL) {
    return mm_malloc(size);
  }
  if (size <= 0) {
    mm_free(ptr);
    return ptr;
  }
  size = align(size);  // making sure it's aligned.
  if(size + TAGS_SIZE < MINBLOCKSIZE){
    size = MINBLOCKSIZE - TAGS_SIZE;
  }
  block_t *original_block = payload_to_block(ptr);
  size_t original_payload_size = block_size(original_block) - TAGS_SIZE;
  if (size == original_payload_size) {// do nothing
    return ptr;
  }
  if (size < original_payload_size) {
    return ptr;
  }
  if (size > original_payload_size) {
    block_t *prev = block_prev(original_block);
    block_t *next = block_next(original_block);
    int prev_free = !block_allocated(prev);
    int next_free = !block_allocated(next);
    size_t prev_size = block_size(prev);
    size_t next_size = block_size(next);
    size_t available_space = block_size(original_block);
    if (prev_free) {
      available_space += prev_size;
    }
    if (next_free) {
      available_space += next_size;
    }
    if (available_space >= (size + TAGS_SIZE)) {

        // CASE 1: prev is free, next is allocated
      if (prev_free && !next_free) {
        pull_free_block(prev);
        memmove(prev->payload, original_block->payload, original_payload_size);
        block_set_size_and_allocated(prev, available_space, 1);
        split_block(prev, size + TAGS_SIZE);  // size + TAGS_SIZE is the size of the reallocated block.
        return prev->payload;
      }
       // CASE 2: prev is free, next is free
      if (prev_free && next_free) {
        pull_free_block(prev);
        pull_free_block(next);
        memmove(prev->payload, original_block->payload, original_payload_size);
        block_set_size_and_allocated(prev, available_space, 1);
        split_block(prev, size + TAGS_SIZE);  // size + TAGS_SIZE is the size of the reallocated block.
        return prev->payload;
      }

      // CASE 3: prev is allocated, next is free
      if (!prev_free && next_free) {
        pull_free_block(next);
        block_set_size_and_allocated(original_block, available_space, 1);
        split_block(original_block, size + TAGS_SIZE);  // size + TAGS_SIZE is the size of the reallocated block.
        return original_block->payload;
      }
    } else {  // the neighbors don't have enough space, so we are gonna have to call malloc.
      void *payload = mm_malloc(size);
      if(payload == NULL){
        return NULL;
      }
      memcpy(payload, original_block->payload, original_payload_size);
      mm_free(ptr);
      return payload;
    }
  }
  return NULL;
}


/*
* checks the state of the heap for internal consistency and prints informative
* error messages
* arguments: none
* returns: 0, if successful
*          nonzero, if the heap is not consistent
*/
int mm_check_heap(void) {
  //printf("%s\n", "entering mm_check_heap");
  block_t *curr_block = prologue;
  /*
  (1) check to make sure that size is multiple of 8
  (2) make sure there are no overlaps, meaning that current + size is a new block's header.
  (3) subtract 8 from current + size to get the footer. make sure footer is same as header.
  (4) are there any adjacent free blocks?
  (5) within bounds of pro and epilogue
  (6) free list stuff
  (a) every block in free list is marked as free
  (b) are all the blocks in the free list valid?
  */
  curr_block = block_next(curr_block); // skipping over checking the prologue.
  while (curr_block != epilogue) { // heap iterator
    // (1) checking for size 8
    if (block_size(curr_block) % 8)  {
      fprintf(stderr, "heap error: %s\n block information: problem block's address: %p,  size: %zu\n", "not aligned to 8.",
      (void *) curr_block, block_size(curr_block));
      return -1;
    }

    // (3) TODO: check header and foot to see if they're the same.
    if (block_size(curr_block) != block_end_size(curr_block) ||
    block_allocated(curr_block) != block_end_allocated(curr_block)) {
      fprintf(stderr, "heap error: %s\n block information: problem block's address: %p,  size: %zu\n",
      "header and footer not equal.", (void *) curr_block, block_size(curr_block));
      return -1;
    }
    // (4) adjacent free blocks
    if (!block_allocated(curr_block) && !block_next_allocated(curr_block)) { // they're both free
    fprintf(stderr, "heap error: %s\n block information: problem block's address: %p,  size: %zu\n",
    "there are contiguous free blocks.", (void *) curr_block, block_size(curr_block));
    return -1;
  }
  // (5) check to see if it's between prologue and epilogue
  if (curr_block > epilogue || curr_block < prologue) {
    fprintf(stderr, "heap error: %s\n block information: problem block's address: %p,  size: %zu\n",
    "block is not within bounds of epilogue / prologue.", (void *) curr_block, block_size(curr_block));
    return -1;
  }
  curr_block = block_next(curr_block);
}
curr_block = flist_first;
if (curr_block == NULL) {
  return 0;
}
do {
  if (block_allocated(curr_block)) {
    fprintf(stderr, "heap error: %s\n block information: problem block's address: %p,  size: %zu\n",
    "block in free list is not marked free.", (void *) curr_block, block_size(curr_block));
    return -1;
  }
  curr_block = block_next_free(curr_block);
} while (curr_block != flist_first);
return 0;
}

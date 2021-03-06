
# Memory Reallocation Suite
## The main code is contained in mm.c.


HOW TO RUN:
Use the provided Makefile to compile the program. The main code is contained in mm.c. Run mdriver to test the memory allocation compaction.


### Optimizations and Compaction:

First, let's talk about malloc. 

`malloc` checks to see if there's a suitable sized free block in the free list instead of just mem_sbrking off a new block. Let say there isn't a free block of adequate size. Then it calls mm_extend_heap.

`mm_extend_heap` extends the heap by some amount that the user asks for. then it coalesces so that a possibly
bigger free block may be returned. This maintains compaction because it allows for left over space to be used in a subsequent call. malloc uses this free block as the block to return.

if there is one in the free list, malloc will set that block's payload as the thing to return.

before either of them (extended block or free list block) returns, malloc will check for splitting. specifically, it'll check the
size of malloc against the size of whatever block the to_return pointer is pointing to.

`split_block` will first check to see if a block is able to be split. If the size of block - size > MINBLOCKSIZE,
then we are good to split.

split_block will construct a new block of size size + TAGS_SIZE by updating the pointer to block with its new size. it will set it to allocated, because its about to get returned.

next, it will call `next_block` on this newly created block. this block will be our new block, and will be set to size `block_size(original_block) - block_size(newly_created_block)`. it will be set to free, and will call coalesce on it.

This maintains compaction because if a free block is too big, then a lot of the space isn't getting utilized. This means that we're going to have to extend the heap more often, which means less utilization. 

Another optimization I made was for both throughput and utilization. When I split, instead of splitting at the minimum size of a block, I instead split only when the size request is at least half of the original block's size. This is so that (1) split is called fewer times and (2) there aren't a bunch of really tiny free blocks that are probably never going to get used -- in other words, reducing fragmentation. 

In `mm_extend_heap`, I did something similar where if the requested size of extention is less than 640, I simply extend by 640. This is so that mm_extend_heap is called less, as it is a very expensive operation. 

Inside of `mm_free`, I also call `coalesce` so that anything that's freed is guarenteed to have two allocated blocks on either side of it, reducing fragmentation. 


### How does realloc work?

There are two main cases in realloc: 

  requested size < original_payload_size:
    • malloc a new block of requested size and copy over the data. then free the old block.
    • split the block so that unused space is freed and coalesced 
  requested size > original_payload_size:
    • check the neighboring blocks. if either one of them is free, then we might be able to merge the surrounding ones to fulfill the size that we are requesting. If we can, then we go through three cases:
    	*CASE 1:* prev is free, next is allocated -- move data into prev, extend the size of prev to prev+current, split the block.
    	*CASE 2:* prev is free, next is free -- move data into prev, extend the size of prev to prev+current+next, split the block.
    	*CASE 3:* prev is allocated, next is free -- extend size of current to include size of next. split the block.


### Heap Checker:

  (1) check to make sure that size is multiple of 8
  (2) make sure there are no overlaps, meaning that current + size is a new block's header.
  (3) subtract 8 from current + size to get the footer. make sure footer is same as header.
  (4) are there any adjacent free blocks?
  (5) within bounds of pro and epilogue
  (6) free list stuff
	  (a) every block in free list is marked as free
	  (b) are all the blocks in the free list valid?

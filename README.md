# MyAlloc

This repository contains a custom memory allocator implemented in C. The
allocator is designed to handle both small and large memory allocations
efficiently using a combination of page-sized blocks and linked lists.

## Overview

The allocator supports the following operations:

- `malloc(size_t size)`: Allocates a memory block of the specified size.
- `calloc(size_t nmemb, size_t size)`: Allocates an array of `nmemb` elements, each of size `size`, and initializes the memory to zero.
- `realloc(void *ptr, size_t size)`: Resizes the previously allocated memory block to the new size.
- `free(void *ptr)`: Frees the previously allocated memory block.

## Structure

The allocator uses different data structures for managing memory:

### Small Pages

For small allocations, memory is managed using a linked list of free blocks within a page-sized region.

### Large Pages

For large allocations, memory is managed using a single large page with a header and a free list node.

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#define PAGE_SIZE getpagesize()
#define ALIGNMENT sizeof(size_t)
#define SMALL_SIZE_THRES PAGE_SIZE / 2

// Round input up to the nearest power of 2
size_t align_small(size_t size) {
	size--;
	size |= size >> 1;
	size |= size >> 2;
	size |= size >> 4;
	size |= size >> 8;
	size |= size >> 16;
	size++;

	return size;
}

// Round input up to nearest multiple of ALIGNMENT
size_t align_word(size_t size) {
	return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

// Round input up to nearest multiple of PAGE_SIZE
size_t align_page(size_t size) {
	return (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

// Size: 16
typedef struct List {
	size_t offset;
	struct List *next;
} List;

// Store free linked list for small pages
List *freeLists[11] = {NULL};

// Size: 16
typedef struct Header {
	size_t size;
	size_t segmentSize;
} Header;

// Store small pages
void *smallPages[11] = {NULL};

void *malloc(size_t size) {
	if (size == 0)
		return NULL;

	if (size <= SMALL_SIZE_THRES) {
		/* Handling small page allocation */

		size_t alignSize = align_small(size);
		int index = log2(alignSize) - 1;
		int segmentSize = alignSize + sizeof(List);

		if (smallPages[index] == NULL) {
			/* Small pages doesn't exist */

			// Create one page
			void *page = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
			                  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

			if (page == MAP_FAILED)
				return NULL;

			// Insert the header at beginning of the page
			Header header = {PAGE_SIZE, alignSize};
			Header *header_ptr = (Header *)page;
			*header_ptr = header;

			// Iterate through the page and place free linked list node
			for (size_t p = sizeof(Header);
			     p < PAGE_SIZE - 1 - segmentSize;
			     p += segmentSize) {
				List new = {p, NULL};
				List *newPtr = (List *)(page + p);
				*newPtr = new;

				if (freeLists[index] == NULL) {
					freeLists[index] = newPtr;
				} else {
					newPtr->next = freeLists[index];
					freeLists[index] = newPtr;
				}
			}

			// Add page to the array
			smallPages[index] = page;
		}

		// Store the returned pointer
		void *ret;

		if (freeLists[index] == NULL) {
			/* Free list is empty */
			
			// Calculate the new size of page (old size + PAGE_SIZE)
			size_t origSize = ((Header *)smallPages[index])->size;
			size_t newSize = origSize + PAGE_SIZE;

			// Extend the small page
			void *newPage = mmap(smallPages[index], newSize,
			                     PROT_READ | PROT_WRITE,
			                     MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

			if (newPage == MAP_FAILED)
				return NULL;

			// Insert new header
			Header header = {newSize, alignSize};
			Header *headerPtr = (Header *)newPage;
			*headerPtr = header;

			// Calculate the end of the last memory region in the old small pages
			size_t numSegment = floor((double)(origSize - sizeof(Header)) /
			                          segmentSize);
			size_t lastAddr = numSegment * segmentSize;

			// Iterate from the last memory region in the old small pages to the end
			// of the new page and place free linked list node
			for (size_t p = lastAddr;
			     p < newSize - 1 - segmentSize;
			     p += segmentSize) {
				List new = {p, NULL};
				List *newPtr = (List *)(newPage + p);
				*newPtr = new;

				if (freeLists[index] == NULL) {
					freeLists[index] = newPtr;
				} else {
					newPtr->next = freeLists[index];
					freeLists[index] = newPtr;
				}
			}
		}

		// Pop a free list node
		ret = freeLists[index];
		freeLists[index] = freeLists[index]->next;

		// Return the end of popped free list node
		return ret + sizeof(List);
	} else {
		/* Handling big pages */

		// Calculate the number of pages to request
		size_t alignSize = align_word(size);
		size_t allocSize = align_page(alignSize + sizeof(Header) + sizeof(List));

		// Create big page
		void *page = mmap(NULL, allocSize, PROT_READ | PROT_WRITE,
		                  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

		if (page == MAP_FAILED)
			return NULL;

		// Insert header at beginning of page
		Header header = {allocSize, alignSize};
		Header *header_ptr = (Header *)page;
		*header_ptr = header;

		// Insert the free list node after header
		page += sizeof(Header);
		List freeList = {sizeof(Header), NULL};
		List *freeListPtr = (List *)page;
		*freeListPtr = freeList;

		return page + sizeof(List);
	}
}

void *calloc(size_t nmemb, size_t size) {
	if (nmemb == 0 || size == 0)
		return NULL;

	size_t allocSize = nmemb * size;

	void *ptr = malloc(size * nmemb);

	// If pointer is in small page, make sure to initialize data to 0
	// since memory region might contains data from previous allocation
	//
	// Else, big page is created and initialized to 0 so no point
	// in setting the region to 0 again
	if (allocSize <= SMALL_SIZE_THRES)
		memset(ptr, 0, allocSize);

	return ptr;
}

void *realloc(void *ptr, size_t size) {
	if (ptr == NULL)
		return malloc(size);

	// same as freeing pointer
	if (ptr != NULL && size == 0) {
		free(ptr);
		return NULL;
	}

	// Create the new allocation
	void *newPtr = malloc(size);

	if (newPtr == NULL)
		return NULL;

	// Get the size of the passed in pointer
	void *freePtr = ptr - sizeof(List);
	size_t offset = ((List *)freePtr)->offset;
	size_t segmentSize = ((Header *)(freePtr - offset))->segmentSize;
	size_t alignSize = align_word(size);

	// Copy the content using the smaller size
	if (alignSize > segmentSize)
		memcpy(newPtr, ptr, segmentSize);
	else
		memcpy(newPtr, ptr, size);

	// Free old pointer
	free(ptr);

	// Return new pointer
	return newPtr;
}

void free(void *ptr) {
	if (ptr == NULL)
		return;

	// Get the size of the pointer
	void *freePtr = ptr - sizeof(List);
	size_t offset = ((List *)freePtr)->offset;

	void *headerPtr = freePtr - offset;
	size_t segmentSize = ((Header *)headerPtr)->segmentSize;

	if (segmentSize <= SMALL_SIZE_THRES) {
		/* Handling small pages */

		// Get the index in page array
		int index = log2(segmentSize) - 1;

		// Place free list node
		// before the free memory region
		List newFree = {offset, NULL};
		List *newFreePtr = (List *)freePtr;
		*newFreePtr = newFree;

		// Add node to free list
		if (freeLists[index] == NULL) {
			freeLists[index] = newFreePtr;
		} else {
			newFreePtr->next = freeLists[index];
			freeLists[index] = newFreePtr;
		}
	} else {
		/* Handling big pages */

		// Get the size of page
		size_t size = ((Header *)headerPtr)->size;

		// Unallocate page
		munmap(headerPtr, size);
	}
}

__attribute__((destructor)) void library_done() {
	for (int i = 0; i < 10; ++i) {
		if (smallPages[i] != NULL)
			munmap(smallPages[i], ((Header *)smallPages[i])->size);
	}
}

/**
 * Buddy Allocator
 *
 * For the list library usage, see http://www.mcs.anl.gov/~kazutomo/list/
 */

/**************************************************************************
 * Conditional Compilation Options
 **************************************************************************/
#define USE_DEBUG 0

/**************************************************************************
 * Included Files
 **************************************************************************/
#include <stdio.h>
#include <stdlib.h>

#include "buddy.h"
#include "list.h"

/**************************************************************************
 * Public Definitions
 **************************************************************************/
#define MIN_ORDER 12
#define MAX_ORDER 20

#define PAGE_SIZE (1<<MIN_ORDER)
/* page index to address */
#define PAGE_TO_ADDR(page_idx) (void *)((page_idx*PAGE_SIZE) + g_memory)

/* address to page index */
#define ADDR_TO_PAGE(addr) ((unsigned long)((void *)addr - (void *)g_memory) / PAGE_SIZE)

/* find buddy address */
#define BUDDY_ADDR(addr, o) (void *)((((unsigned long)addr - (unsigned long)g_memory) ^ (1<<o)) \
									 + (unsigned long)g_memory)

#if USE_DEBUG == 1
#  define PDEBUG(fmt, ...) \
	fprintf(stderr, "%s(), %s:%d: " fmt,			\
		__func__, __FILE__, __LINE__, ##__VA_ARGS__)
#  define IFDEBUG(x) x
#else
#  define PDEBUG(fmt, ...)
#  define IFDEBUG(x)
#endif

/**************************************************************************
 * Public Types
 **************************************************************************/
typedef struct {
	struct list_head list;
	int order;
	int index;
	void* memory;
} page_t;

/**************************************************************************
 * Global Variables
 **************************************************************************/
/* free lists*/
struct list_head free_area[MAX_ORDER+1];

/* memory area */
char g_memory[1<<MAX_ORDER];

/* page structures */
page_t g_pages[(1<<MAX_ORDER)/PAGE_SIZE];

/**************************************************************************
 * Public Function Prototypes
 **************************************************************************/
int get_order(int x);

void split_page(page_t* page, int i, int order);
/**************************************************************************
 * Local Functions
 **************************************************************************/

/**
 * Initialize the buddy system
 */
void buddy_init()
{
	int i;
	int n_pages = (1<<MAX_ORDER) / PAGE_SIZE;
	for (i = 0; i < n_pages; i++){
		g_pages[i].order = -1;
		g_pages[i].index = i;
		g_pages[i].memory = PAGE_TO_ADDR(i);
	}

	/* initialize freelist */
	for (i = MIN_ORDER; i <= MAX_ORDER; i++){
		INIT_LIST_HEAD(&free_area[i]);
	}

	/* add the entire memory as a freeblock */
	list_add(&g_pages[0].list, &free_area[MAX_ORDER]);
}

/**
 * Allocate a memory block.
 *
 * On a memory request, the allocator returns the head of a free-list of the
 * matching size (i.e., smallest block that satisfies the request). If the
 * free-list of the matching block size is empty, then a larger block size will
 * be selected. The selected (large) block is then splitted into two smaller
 * blocks. Among the two blocks, left block will be used for allocation or be
 * further splitted while the right block will be added to the appropriate
 * free-list.
 *
 * @param size size in bytes
 * @return memory block address
 */
void *buddy_alloc(int size)
{
	int order = get_order(size);
	if (order != -1){
		for (int i = order; i <= MAX_ORDER; i++){
			if (!list_empty(&free_area[i])){
				page_t* page = list_entry(free_area[i].next, page_t, list);
				if( order == i){
					list_del_init(&(page->list));
					page->order = order;
					return (page->memory);
				}
				else{
					list_del_init(&(page->list));
					split_page(page, i, order);
					page->order = order;
					return (page->memory);
				}
			}
		}
	}
	return NULL;
}

/**
 * Splits a page into two buddies recursively, until the desired order is reached.
 * The function operates on the page and its buddies, which are represented as a 
 * binary tree. The page is split into two buddies of half the size of the original 
 * page, until the desired order is reached.
 *
 * @param page  pointer to the page to be split
 * @param i     the order of the page
 * @param order the desired order to split the page into
 */
void split_page(page_t* page, int i, int order){
	if (i != order){
		int new_page_order = i-1;
		page_t* buddy_page = &g_pages[ADDR_TO_PAGE(BUDDY_ADDR(page->memory, new_page_order))];
		buddy_page->order = new_page_order;
		list_add(&(buddy_page->list), &free_area[new_page_order]);
		split_page(page, new_page_order, order);
	}
}

/**
 * Free an allocated memory block.
 *
 * Whenever a block is freed, the allocator checks its buddy. If the buddy is
 * free as well, then the two buddies are combined to form a bigger block. This
 * process continues until one of the buddies is not free.
 *
 * @param addr memory block address to be freed
 */
void buddy_free(void *addr)
{
	page_t* current_page = &g_pages[ADDR_TO_PAGE(addr)];
	page_t* buddy_page = &g_pages[ADDR_TO_PAGE(BUDDY_ADDR(current_page->memory, current_page->order))];
	struct list_head *current_node;
	page_t* temp = NULL;
	int index = current_page->index;
	for (int i = current_page->order; i <= MAX_ORDER; i++){
		current_node = NULL;
		buddy_page = &g_pages[ADDR_TO_PAGE(BUDDY_ADDR(current_page->memory, i))];
		list_for_each(current_node, &free_area[i]){
			temp = list_entry(free_area[i].next, page_t, list);
			if(current_node == NULL){
				break;
			}
			if (list_entry(current_node, page_t, list) == buddy_page){
				break;
			}
		}
		if(current_node == NULL || temp != buddy_page){
			g_pages[index].order = -1;
			list_add(&g_pages[index].list, &free_area[i]);
			return;
		}
		if (buddy_page->memory < current_page->memory){
			current_page = buddy_page;
			index = current_page->index;
		}
		list_del_init(&buddy_page->list);
	}
}

/**
 * Print the buddy system status---order oriented
 *
 * print free pages in each order.
 */
void buddy_dump()
{
	int order_index;
	for (order_index = MIN_ORDER; order_index <= MAX_ORDER; order_index++){
		struct list_head *current_node;
		int count = 0;
		list_for_each(current_node, &free_area[order_index]){
			count++;
		}
		printf("%d:%dK ", count, (1<<order_index)/1024);
	}
	printf("\n");
}

/**
 * Returns the order of the smallest power of 2 that can fit a given size.
 * The size is expressed in number of bytes.
 * 
 * @param x the size to be allocated, in bytes
 * @return the order of the smallest power of 2 that can fit the given size, 
 *         or -1 if the size is larger than the maximum supported order
 */
int get_order(int x){
 	for (int order = MIN_ORDER; order <= MAX_ORDER; order++){
	 	if (1 << order >= x){
		 	return order;
	 	}
 	}
 	return -1;
}

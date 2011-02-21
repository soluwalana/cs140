#ifndef USERPROG_PAGEDIR_H
#define USERPROG_PAGEDIR_H

#include <stdbool.h>
#include <stdint.h>

#define PTE_AVL_MEMORY 0 /* 000 */ 
#define PTE_AVL_SWAP 1 /* 001 */
#define PTE_AVL_EXEC (1 << 1) /* 010 */
#define PTE_AVL_MMAP (1 << 2) /* 100 */ 

typedef uint8_t medium_t; /* used to represent one of the constants above */

uint32_t *pagedir_create (void);
void pagedir_destroy (uint32_t *pd);
bool pagedir_set_page (uint32_t *pd, void *upage, void *kpage, bool rw);
void *pagedir_get_page (uint32_t *pd, const void *upage);
void pagedir_clear_page (uint32_t *pd, void *upage);
bool pagedir_is_dirty (uint32_t *pd, const void *upage);
void pagedir_set_dirty (uint32_t *pd, const void *upage, bool dirty);
bool pagedir_is_accessed (uint32_t *pd, const void *upage);
void pagedir_set_accessed (uint32_t *pd, const void *upage, bool accessed);
void pagedir_activate (uint32_t *pd); 
bool pagedir_is_present(uint32_t *pd, const void *upage);
 

/* functions for supplimentary page table functionality */
void pagedir_set_medium (uint32_t *pd, void *upage, medium_t medium);
medium_t  pagedir_get_medium (uint32_t *pd, const void *upage);
void pagedir_set_aux (uint32_t *pd, void *upage, uint32_t location);
uint32_t pagedir_get_aux (uint32_t *pd, const void* upage);

bool pagedir_install_page (void *upage, void *kpage, bool writable);

bool pagedir_setup_demand_page(uint32_t* pd, void *uaddr, medium_t medium ,
										    uint32_t data, bool writable);

#endif /* userprog/pagedir.h */

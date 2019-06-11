#include<lib.h>
#include<memory.h>
static struct page_list pglists[MAX_REG];
static unsigned long osalloc_base;
static struct nodealloc_memory nodemem;

#define pgl_bmap_set(pgl, i)  do{ \
                                         u32 lword = i >> 5;\
                                         u32 bit = i - (lword << 5);\
                                         u32 *p = (u32 *)(pgl->bitmap + (lword << 2));\
                                         set_bit(*p , bit);\
                                    }while(0);

#define pgl_bmap_clear(pgl, i)  do{ \
                                         u32 lword = i >> 5;\
                                         u32 bit = i - (lword << 5);\
                                         u32 *p = (u32 *)(pgl->bitmap + (lword << 2));\
                                         clear_bit(*p , bit);\
                                    }while(0);

extern void* osmap(u64 pfn)
{
     return ((void *)(pfn << PAGE_SHIFT));  
}

static void init_pagelist(int type, u64 start, u64 end)
{
     u32 pages_to_reserve, i;
     struct page_list *pgl = &pglists[type];
     pgl->size = (end - start) >> PAGE_SHIFT;
     pgl->type = type;
     pgl->start_address = start;
     pgl->last_free_pos = 0;
     pages_to_reserve = (pgl->size >> 3);
      
     if(((pages_to_reserve >> PAGE_SHIFT) << PAGE_SHIFT) == pages_to_reserve)
           pages_to_reserve = pages_to_reserve >> PAGE_SHIFT;
     else
           pages_to_reserve = (pages_to_reserve >> PAGE_SHIFT) + 1;
    
     pgl->bitmap = (char *)pgl->start_address;
     pgl->last_free_pos = pages_to_reserve >> 5;

     for(i=0; i<pages_to_reserve; ++i)
           pgl_bmap_set(pgl, i);
           
     pgl->free_pages = pgl->size - pages_to_reserve;
     return;
}

static void init_pagelists()
{
   init_pagelist(OS_DS_REG, REGION_OS_DS_START, REGION_OS_PT_START); 
   init_pagelist(OS_PT_REG, REGION_OS_PT_START, REGION_USER_START);
   init_pagelist(USER_REG,  REGION_USER_START, ENDMEM);
}

static u32 get_free_page(struct page_list *pgl)
{
  u32 pfn, bit;
  u32 size_in_u32 = pgl->size >> 8, once = 0;
  u32 *scanner = ((u32 *)(pgl->bitmap)) + pgl->last_free_pos;
  
  while(*scanner == 0xffffffff && pgl->last_free_pos < size_in_u32){
         pgl->last_free_pos++;
         scanner++;
  }
  if(pgl->last_free_pos >= size_in_u32){
        pgl->last_free_pos = 0;
        scanner = (u32 *)((unsigned long)pgl->bitmap + pgl->last_free_pos);
   }
   
  for(bit=0; bit<32; ++bit){
      if(!is_set(*scanner, bit))
          break;
  }     
  
  pfn = (pgl->last_free_pos << 5) + bit;
  pgl_bmap_set(pgl, pfn);
  pfn += pgl->start_address >> PAGE_SHIFT;
  pgl->free_pages--;
  
//  printf("%s:%x\n", __func__, pfn);
  return pfn;
}

u32 os_pfn_alloc(u32 region)
{
    u32 pfn;
    struct page_list *pgl = &pglists[region];
    if(pgl->free_pages <= 0){
         printf("Opps.. out of memory for region %d\n", region);
         return 0;
    }
   pfn = get_free_page(pgl);
   if(region == OS_PT_REG){
       u64 addr = pfn;
       addr = addr << PAGE_SHIFT;
       bzero((char *)addr, PAGE_SIZE);
   }
   return pfn;
}
void *os_page_alloc(u32 region)
{
   u32 pfn = os_pfn_alloc(region);
   return ((void *) (((unsigned long) pfn) << PAGE_SHIFT));
}

void os_page_free(u32 region, void *paddress)
{
    struct page_list *pgl = &pglists[region];
    u32 pfn = ((unsigned long)paddress - pgl->start_address) >> PAGE_SHIFT;

    if((u64)paddress <= pgl->start_address || 
        (u64)paddress >= (pgl->start_address + (pgl->size << 12))){
         printf("Opps.. free address not in region %d\n", region);
         return;
    }
   pgl_bmap_clear(pgl, pfn); 
   pgl->free_pages++;
   return;
}
void os_pfn_free(u32 region, u64 pfn)
{
//   printf("Freeing PFN %x\n", pfn);
   os_page_free(region, (void *) (pfn << PAGE_SHIFT));
}


static void init_osalloc_chunks()
{
     struct osalloc_chunk *ck;
     int shift = 5, i;
     int numbytes = sizeof(struct osalloc_chunk) * OSALLOC_MAX;
     if(numbytes > PAGE_SIZE){
          printf("Reduce number of OSALLOC types\n");
          return;
     }   
   
    ck = os_page_alloc(OS_DS_REG);
    osalloc_base = (unsigned long) ck;
    for(i=0; i<OSALLOC_MAX; ++i){
         int bmap_entries = PAGE_SIZE >> shift;
         ck->chunk_size = 1 << shift;
         ck->free_position = 0;
         init_list(&ck->freelist);
         bzero(ck->bitmap, 16);
         ck->current_pfn = os_pfn_alloc(OS_DS_REG);
         shift++;
         ck++;
    }
   
    return;  
}

void *os_alloc(u32 size)
{
  unsigned long retval = 0;
  int i;
  struct osalloc_chunk *ck;
  u32 *ptr;
  if(!size || size > 2048)
       return NULL;
  for(i=11; i>5; --i){
     if(is_set(size, i))
          break;
  }
  
  if((1 << i) != size)
        ++i;
  ck = ((struct osalloc_chunk *) (osalloc_base)) + (i-5);
  if(!is_empty(&ck->freelist)){
      struct node *mem = dequeue_front(&ck->freelist);
      retval = mem->value;
      node_free(mem); 
      goto out;
  } 
  
  if(ck->free_position > (PAGE_SIZE >> i)){  /*All done with current pfn*/
         ck->current_pfn = os_pfn_alloc(OS_DS_REG);
         ck->free_position = 0;
  }
     
  ptr = (u32 *)(ck->bitmap + (ck->free_position >> 5));
  retval = (((u64) ck->current_pfn << PAGE_SHIFT) + (ck->free_position >> i)); 
  set_bit(*ptr, ck->free_position);
  ck->free_position++;
out:
  return (void *) retval;
}

void os_free(void *ptr, u32 size)
{
  struct osalloc_chunk *ck;
  int i;
  if(!size || size > 2048)
       return;
  for(i=11; i>5; --i){
     if(is_set(size, i))
          break;
  }
  ck = ((struct osalloc_chunk *) osalloc_base) + (i-5);
  enqueue_tail(&ck->freelist, (u64) ptr);
  return;  
}

static void init_nodealloc_memory()
{
   int i;
   u32 startpfn, prevpfn, currentpfn;
 
   startpfn = os_pfn_alloc(OS_DS_REG);
   prevpfn = startpfn;
   for(i=1; i<NODE_MEM_PAGES; ++i){
        currentpfn = os_pfn_alloc(OS_DS_REG);
        if(currentpfn != prevpfn + 1)
            printf("Error in initmem %s\n", __func__);
        prevpfn = currentpfn;
        nodemem.num_free += PAGE_SIZE / sizeof(struct node);
   }
   
   nodemem.nodes = (void *) ((u64) startpfn << PAGE_SHIFT);
   nodemem.next_free = 0;     
   nodemem.end = nodemem.nodes + (NODE_MEM_PAGES << PAGE_SHIFT);
}

/*XXX*/

struct node *node_alloc()
{
   struct node *n = ((struct node *) nodemem.nodes) + nodemem.next_free;
   if(!nodemem.num_free){
       printf("node memory full\n");
       return NULL;
   }

   if(!n->next)
         goto found;

   while(n->next){
         ++n; 
         if((void *)n >= nodemem.end)
             break;
         nodemem.next_free++;
   }
   
   if((void *)n >= nodemem.end){
          nodemem.next_free = 0;
          n = ((struct node *) nodemem.nodes) + nodemem.next_free;
          while(n->next){
              ++n; 
              nodemem.next_free++;
          }
   }
found:
    nodemem.num_free--;
    nodemem.next_free++;
    return n;
       
}

void node_free(struct node *n)
{
    nodemem.num_free++;
    n->next = NULL; 
      
}
int get_free_pages_region(u32 region)
{
  if(region >= MAX_REG) 
    return -1;
  return pglists[region].free_pages;  
}

void init_mems()
{
    init_pagelists();
    init_osalloc_chunks();
    init_nodealloc_memory();
}

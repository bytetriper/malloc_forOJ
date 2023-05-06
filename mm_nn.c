/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  Blocks are never coalesced or reused.  The size of
 * a block is found at the first aligned word before the block (we need
 * it for realloc).
 *
 * This code is correct and blazingly fast, but very bad usage-wise since
 * it never frees anything.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
typedef unsigned long lu;
#define SIMPLE_REALLOC
#define CHECK_HEAP 0
#define SIMPLE_MALLOC
#define DEBUG
#define DEBUG_OUT
#ifdef DEBUG
#define dbg_printf(...) \
  printf(__VA_ARGS__);  \
  fflush(stdout)

#else
#define dbg_printf(...)
#endif

#ifdef DEBUG_OUT
#define dbg_out(...)   \
  printf(__VA_ARGS__); \
  fflush(stdout)
#else
#define dbg_out(...)
#endif
/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */
int cnt = 0;
inline int max(int a, int b)
{
  return a > b ? a : b;
}
inline int min(int a, int b)
{
  return a < b ? a : b;
}
/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define SIZE_PTR(p) ((size_t *)(((char *)(p)) - SIZE_T_SIZE))

// set macro about the size of the segregated free list
#define MIN_SIZE 4
#define MIN_SIZE_TOLERANCE 40 // MINIMUM BLOCK SIZE TOLERANCE
#define MAX_SIZE 31
#define LIST_SIZE (MAX_SIZE - MIN_SIZE + 1)
#define LIST_SIZE_BYTE ((LIST_SIZE) << 3)
#define INIT_SIZE (1 << 5)
struct info_set
{
  size_t *start;
  size_t *end;
  size_t *heap_start;
  size_t *free_list[LIST_SIZE];
  size_t size;
};
struct info_set info;
// set macro about some frequently used operations
//  the first bit of the header-byte is used to indica te whether the block is free or not
#define GET_STATE(p) (((size_t)(p)) & 1)
#define GET_PREV_STATE(p) (((size_t)(p)) & 0x10000)
// the other 31 bits are used to store the size of the block(which is at least 1-byte aligned)
inline size_t *NEXT(size_t *p)
{
  if ((*((size_t *)p + 1) & 0xffff0000) == 0)
  {
    return (size_t *)NULL;
  }
  return (size_t *)((*((size_t *)p + 1) & 0xffff0000) + info.heap_start);
}
inline size_t *PREV(size_t *p)
{
  if ((*((size_t *)p + 1) & 0xffff) == 0)
  {
    return (size_t *)NULL;
  }
  return (size_t *)((*((size_t *)p + 1) & 0xffff) + info.heap_start);
}
inline void SET_NEXT(size_t *p, size_t *next)
{
  *((size_t *)p + 1) = ((size_t)((next == NULL ? info.heap_start : next) - info.heap_start) << 32) | (*((size_t *)p + 1) & 0xffff);
}
inline void SET_PREV(size_t *p, size_t *prev)
{
  *((size_t *)p + 1) = ((size_t)((prev == NULL ? info.heap_start : prev) - info.heap_start)) | (*((size_t *)p + 1) & 0xffff0000);
}
#define GET_START(p) ((size_t *)(p)-LIST_SIZE)
#define HEADER_LEN 2
#define HEADER_LEN_BYTE 16
#define HEADER_LEN_BIT 144
#define GET_LEN(p) (((size_t)(p)) & 0xfff8)
#define GET_PREV_LEN(p) (((size_t)(p)) & 0xfff80000)
#define GET_SIZE(p) (GET_LEN(p) + HEADER_LEN_BYTE)
#define FOOTER(p) ((size_t *)((char *)(p) + GET_SIZE(p)) + 1)
#define BACK_HEADER(p) ((size_t *)p - HEADER_LEN)
#define SKIP_HEADER(p) (((char *)p) + HEADER_LEN_BYTE)
inline size_t *GET_PHYSICAL_PREV(size_t *p)
{
  return (size_t *)((char *)p - GET_PREV_LEN(p) - HEADER_LEN_BYTE);
}
inline size_t *GET_PHYSICAL_NEXT(size_t *p)
{
  return (size_t *)((char *)p + GET_SIZE(p));
}
// see https://stackoverflow.com/questions/994593/how-to-do-an-integer-log2-in-c/994709#994709 for more details
#define HACK_GET_HIGH(x, y) asm volatile("\tbsr %1, %0\n" \
                                         : "=r"(y)        \
                                         : "r"(x));
inline int GET_HIGH(int p)
{
  int x;
  HACK_GET_HIGH(p, x);
  return min(max(x, MIN_SIZE), MAX_SIZE) - MIN_SIZE;
}
#define USE 1             // 11
#define FREE 0            // 00
#define CHECK_VALID_BIT 6 // 110
#define CHECK_VALID(p) ((((size_t)(p)) & CHECK_VALID_BIT) == CHECK_VALID_BIT)
/*mem_sbrk() is O(1) here,incredibly fast*/
// use a segregated free list
/*
 * mm_init - Called when a new trace starts.
 */
inline int CHECK_POINTER_VALID(void *p, int check_state, int line)
{
  size_t *ptr = (size_t *)p;
  if (ptr == NULL)
  {
    dbg_out("[Error]Line%d: pointer is NULL at %p\n", line, ptr);
    return 1;
  }
  if (ptr < info.start || ptr > info.end)
  {
    dbg_out("[Error]Line%d: pointer is out of range at %p\n", line, ptr);
    return 1;
  }
#ifdef CHECK_COMPLETE
  if (check_state && GET_STATE(*(ptr)) != USE)
  {
    dbg_out("[Error]Line%d: pointer is not in use at %p\n", line, ptr);
    return 1;
  }

  if (!CHECK_VALID(*(ptr)))
  {
    dbg_out("[Error]Line%d: pointer is not valid at %p\n", line, ptr);
    return 1;
  }

  if (GET_LEN(*(ptr)) != GET_LEN(*(FOOTER(ptr))))
  {
    dbg_out("[Error]Line%d: pointer discoperation at %p with %lu != %lu\n", line, p, GET_LEN(*ptr), GET_LEN(*(FOOTER(ptr))));
    return 1;
  }
  if (GET_LEN(*(ptr)) != ALIGN(GET_LEN(*(ptr))))
  {
    dbg_out("[Error]Line%d: pointer not aligned at %p with size:%lu\n", line, p, GET_LEN(*ptr));
    return 1;
  }
#endif
  return 0;
}
inline int DEBUG_SEQ_INFO(int output)
{
  // print a seperating line
  if (output)
  {
    dbg_out("-----------------CHECKING SEQUENTIAL-----------------------\n");
    // print info.start and end
    dbg_out("start: %p,end: %p\n", info.start, info.end);
  }
  // print all the list info
  size_t *p = info.start;
  size_t used = 0;
  while (p < info.end)
  {
    if (output)
    {
      dbg_out("size: %ld,pos: %p,nxt:%p; STATE:%s\n", GET_LEN(*p), p, FOOTER(p), GET_STATE(*p) ? "USE" : "FREE");
      if (GET_STATE(*p) == USE)
      {
        used += GET_LEN(*p);
      }
    }
    if (CHECK_POINTER_VALID(p, 0, __LINE__))
    {
      return 1;
    }
    if (GET_LEN(*p) == 0)
    {
      // report a bug'
      dbg_out("ERROR: size is 0\n");
      return 1;
    }
    p = FOOTER(p);
  }
  // print the total used size/total size
  if (output)
  {
    dbg_out("total used size: %ld, total size: %ld,use rate:%.5lf\n", used, (char *)info.end - (char *)info.start, (double)used / ((char *)info.end - (char *)info.start));
  }
  if (output)
  {
    // print a seperating line
    dbg_out("-----------------CHECKING END-----------------------\n");
  }

  return 0;
}
inline int DEBUG_LIST_INFO(int idx, int output)
{
  // print a seperating line
  if (output)
  {
    dbg_out("-----------------CHECKING LIST-----------------------\n");
  }
  if (idx == -1)
  {
    // print info.start and end
    if (output)
    {
      dbg_out("start: %p,end: %p\n", info.start, info.end);
    }
    // print all the list info
    for (int i = 0; i < LIST_SIZE; i++)
    {
      size_t *p = (size_t *)*info.free_list[i];
      if (p == NULL)
        continue;
      if (output)
      {
        dbg_out("list %d(size %d to %d): ", i, 1 << (i ? i + MIN_SIZE : 0), (1 << (i == MAX_SIZE ? 31 : (i + MIN_SIZE + 1))) - 1);
      }
      while (p != NULL)
      {
        if (output)
        {
          dbg_out("size: %ld,pos: %p;", GET_LEN(*p), p);
        }
        if (GET_LEN(*p) < ((lu)1 << (i ? i + MIN_SIZE : 0)) || GET_LEN(*p) > ((lu)1 << (i == MAX_SIZE ? 31 : (i + MIN_SIZE + 1))) - 1)
        {
          dbg_out("ERROR: invalid block of size:%ld\n", GET_LEN(*p));
          return 1;
        }
        if (p == (size_t *)*NEXT(p))
        {
          dbg_out("ERROR: next pointer is itself\n");
          return 1;
        }
        if ((size_t *)*PREV(p) == NULL)
        {
          dbg_out("ERROR: prev pointer is NULL\n");
          return 1;
        }
        p = (size_t *)*NEXT(p);
      }
      if (output)
      {
        dbg_out("\n");
      }
    }
  }
  else // do not provide checking for idx
  {
    // print the list info of idx
    if (output)
    {
      dbg_printf("list %d: ", idx);
    }
    size_t *p = (size_t *)*info.free_list[idx];
    while (p != NULL)
    {
      if (output)
      {
        dbg_printf("size: %lu,pos: %p;", GET_SIZE(*p), p);
      }
      p = (size_t *)*NEXT(p);
    }
    dbg_printf("\n");
  }
  // print a seperating line
  if (output)
  {
    dbg_printf("--------------------------------------------------\n");
  }
  return 0;
}
inline int check_correctness()
{
  // check the sequential info
  if (DEBUG_SEQ_INFO(0))
  {
    dbg_out("ERROR: sequential info error\n");
    return 1;
  }
  // check the list info
  if (DEBUG_LIST_INFO(-1, 0))
  {
    dbg_out("ERROR: list info error\n");
    return 1;
  }
  return 0;
}
inline void init_info_set(struct info_set *info)
{
  dbg_printf("init_info_set\n");
  info->start = NULL;
  info->end = NULL;
  info->size = INIT_SIZE;
  info->heap_start = NULL;
  for (int i = 0; i < LIST_SIZE; i++)
  {
    info->free_list[i] = NULL;
  }
}
inline size_t *alloc_block(int size, int state, size_t *p)
{
  *p = (*p & 0xffff) | (size_t)size | state;
  SET_NEXT(p, NULL);
  SET_PREV(p, NULL);
  size_t *footer = FOOTER(p);
  *footer = (((size_t)size | state) << 32) | (*footer & 0xffff);
  return footer; // return the next block(physical next)
}
inline size_t *alloc_block_compl(int size, int state, size_t *p, size_t *prev, size_t *next)
{
  *p = (*p & 0xffff) | (size_t)size | state;
  SET_NEXT(p, next);
  SET_PREV(p, prev);
  size_t *footer = FOOTER(p);
  *footer = (((size_t)size | state) << 32) | (*footer & 0xffff);
  return footer; // return the next block(physical next)
}
inline void list_push(size_t *p)
{
  int idx = GET_HIGH(GET_LEN(*p));
  if (*(info.free_list[idx]) != (size_t)NULL)
    SET_PREV((size_t *)*(info.free_list[idx]), p);
  SET_NEXT(p, (size_t *)(*(info.free_list[idx])));
  SET_PREV(p, (size_t *)(info.free_list[idx]));
  *(info.free_list[idx]) = (size_t)p;
}
inline void list_pop(size_t *p)
{
  size_t *next = (size_t *)(*NEXT(p));
  size_t *prev = (size_t *)(*PREV(p));
  // give detailed info about the block p
  dbg_printf("list_pop: p: %p, next: %p, prev: %p\n", p, next, prev);
  if (next != NULL)
    SET_PREV(next, prev);
  int idx = GET_HIGH(GET_LEN(*p));
  if (prev != info.free_list[idx])
  {
    SET_NEXT(prev, next);
  }
  else
  {
    *(info.free_list[idx]) = (size_t)next;
  }
}
inline void split_block(int size, size_t *p)
{
  // print a seperating line
  dbg_printf("-----------------SPLITTING BLOCK-----------------------\n");
  dbg_printf("split_block: size: %d, p: %p,block_size:%lu\n", size, p, GET_LEN(*p));
  size_t len = GET_LEN(*p);
  assert(len >= (size_t)size + HEADER_LEN_BYTE);
  list_pop(p);
  size_t *new = alloc_block(size, USE, p);
  dbg_printf("new_free_block: %p\n", new);
  // minus the size of the USE block, the size of the header and the size of the footer of the USE block and FREE block
  len -= size + (HEADER_LEN_BYTE);
  alloc_block(len, FREE, new);
  list_push(new);
  // print a seperating line
  dbg_printf("--------------------------------------------------\n");
}
inline int mm_init(void) // init a segregated free list with up to 32 1-byte block(memory up to 2^32)
{
  // print a seperating line
  dbg_printf("-----------------INITIALIZING-----------------------\n");
  init_info_set(&info);
  // 1 for header,1 for next, 1 for prev
  info.heap_start = (size_t *)mem_sbrk(info.size + HEADER_LEN_BYTE + LIST_SIZE_BYTE);
  info.start = info.heap_start + LIST_SIZE;
  info.end = (size_t *)((char *)info.start + info.size + HEADER_LEN_BYTE) - 1;
  info.heap_start -= 1; // to distinguish the first block and NULL
  for (int i = 0; i < LIST_SIZE; i++)
  {
    info.free_list[i] = GET_START(info.start) + i;
    *(info.free_list[i]) = (size_t)NULL;
  }
  *(info.free_list[GET_HIGH(info.size)]) = (size_t)info.start;
  alloc_block_compl(info.size, FREE, info.start, info.free_list[GET_HIGH(info.size)], NULL);
  dbg_printf("init_info_set: start: %p, end: %p, size: %lu\n", info.start, info.end, info.size);
  // print a seperating line
  dbg_printf("--------------------------------------------------\n");
  return 0;
}
/*
 * malloc - Allocate a block by incrementing the brk pointer.
 *      Always allocate a block whose size is a multiple of the alignment.
 */
inline void *malloc(size_t size)
{
  if (size == 0)
    return NULL;
  // print a seperating line containing "malloc"
  ++cnt;
  dbg_printf("----------------------malloc:op %d----------------------\n", cnt);
  dbg_printf("malloc %lu,aligned:%lu\n", size, ALIGN(size));
  size = ALIGN(size);

  int idx = GET_HIGH(size); // make sure the size of the block is larger than the size of the block in the list
  size_t *p = info.free_list[idx];
  dbg_printf("idx:%d,LIST_SIZE:%d\n", idx, LIST_SIZE);
#ifndef SIMPLE_MALLOC
  if (*p != (size_t)NULL)
  {
    size_t *min_size = NULL;
    size_t *ptr = (size_t *)(*p);
    while (*NEXT(ptr) != (size_t)NULL && GET_LEN(*ptr) < size)
    {
      ptr = (size_t *)(*NEXT(ptr));
    }
    if (GET_LEN(*ptr) >= size)
    {
      if (min_size == NULL || GET_LEN(*ptr) < GET_LEN(*min_size))
      {
        min_size = ptr;
      }
      // print a seperating line
    }
    if (min_size != NULL)
    {
      dbg_printf("malloc: block found, min_size: %lu, p: %p\n", GET_LEN(*min_size), min_size);
      if (GET_LEN(*min_size) >= size + MIN_SIZE_TOLERANCE)
      {
        split_block(size, min_size);
      }
      else
      {
        list_pop(min_size);
        alloc_block(GET_LEN(*min_size), USE, min_size);
      }
      // print a seperating line
      dbg_printf("--------------------malloc-end------------------\n");
      return SKIP_HEADER(min_size);
    }
  }
  if (idx == LIST_SIZE - 1)
  {
    size_t *new = mem_sbrk(size + HEADER_LEN_BYTE);
    info.end = (size_t *)((char *)info.end + size + HEADER_LEN_BYTE);
    alloc_block(size, USE, new);
    dbg_printf("malloc: no block found, mem_sbrk: %p\n", new);
    // print a seperating line
    dbg_printf("--------------------malloc-end------------------\n");
    return SKIP_HEADER(new);
  }
#endif
  if (idx < LIST_SIZE - 1)
  {
    p = info.free_list[++idx];
    while (idx < LIST_SIZE && *p == (size_t)NULL)
    {
      p = info.free_list[idx++];
    }
  }
  dbg_printf("final idx:%d\n", idx);
  if (*p == (size_t)NULL || idx >= LIST_SIZE - 1)
  {
    size_t *new = mem_sbrk(size + HEADER_LEN_BYTE);
    info.end = (size_t *)((char *)info.end + size + HEADER_LEN_BYTE);
    alloc_block(size, USE, new);
    dbg_printf("malloc: no block found, mem_sbrk: %p,info.end:%p\n", new, info.end);
    // print a seperating line
    dbg_printf("--------------------malloc-end------------------\n");
    DEBUG_SEQ_INFO(1);
    return SKIP_HEADER(new);
  }
  size_t *nxt = (size_t *)(*p);
  // split the block(when size>= 2^{MIN_SIZE}) or alloc the whole block to the user(when size< 2^{MIN_SIZE})
  assert(GET_LEN(*nxt) >= size);
  if (GET_LEN(*nxt) >= MIN_SIZE_TOLERANCE + size)
  {
    dbg_printf("find a block of size %lu\n", GET_SIZE(*nxt));
    dbg_printf("split the blocks\n");
    split_block(size, nxt);
  }
  else
  {
    dbg_printf("find a block of size %lu\n", GET_SIZE(*nxt));
    dbg_printf("alloc the whole block\n");
    // print detail information about nxt
    dbg_printf("nxt: %p, nxt_size: %lu, nxt_nxt: %p, nxt_prev: %p\n", nxt, GET_SIZE(*nxt), (size_t *)(*NEXT(nxt)), (size_t *)(*PREV(nxt)));
    list_pop(nxt);
    (*nxt) |= USE;
    *FOOTER(nxt) |= USE;
  }
  // print another seperating line
  dbg_printf("--------------------malloc-end------------------\n");
  // mm_checkheap(CHECK_HEAP);
  return SKIP_HEADER(nxt);
}
// try to merge the physical prev block, p here is the header pointer of the current block
inline void try_merge_physical_prev(size_t *p)
{
  // print detail information about p
  dbg_printf("try_merge_physical_prev p: %p, p_size: %lu, p_nxt: %p, p_prev: %p\n", p, GET_LEN(*p), (size_t *)(*NEXT(p)), (size_t *)(*PREV(p)));
  size_t *prev = GET_PHYSICAL_PREV(p);
  if (prev < info.start)
  {
    dbg_out("prev is out of range\n");
    exit(-1);
    return;
  }
  size_t len = GET_LEN(*p);
  if (GET_STATE(*prev) == FREE)
    list_pop(p);
  while (GET_STATE(*prev) == FREE)
  {
    if (CHECK_POINTER_VALID(prev, 0, __LINE__))
    {
      dbg_out("prev is out of range\n");
      exit(-1);
    }
    len += GET_SIZE(*prev);
    dbg_printf("merging a block(prev) at:%p, len:%lu;total len:%lu\n", prev, GET_LEN(*prev), len);
    list_pop(prev);
    alloc_block(len, FREE, prev);
    list_push(prev);
    prev = GET_PHYSICAL_PREV(prev);
    if (prev < info.start)
      break;
  }
}
// remember to merge next first,then prev!
inline void try_merge_physical_next(size_t *p)
{
  dbg_printf("try_merge_physical_next p: %p, p_size: %lu, p_nxt: %p, p_prev: %p\n", p, GET_LEN(*p), (size_t *)(*NEXT(p)), (size_t *)(*PREV(p)));
  size_t *next = GET_PHYSICAL_NEXT(p);
  if (next > info.end)
  {
    list_push(p);
    return;
  }
  size_t len = GET_LEN(*p);
  while (GET_STATE(*next) == FREE)
  {
    // if(CHECK_POINTER_VALID(next, 0, __LINE__))
    //{
    //   dbg_out("merging:next:%p is invalid\n", next);
    //   exit(0);
    // }
    len += GET_SIZE(*next);
    dbg_printf("merging a block(nxt) at:%p, len:%lu;total len:%lu\n", next, GET_LEN(*next), len);
    list_pop(next);
    next = GET_PHYSICAL_NEXT(next);
    if (next > info.end)
      break;
  }
  alloc_block(len, FREE, p);
  list_push(p);
}
inline void set_free(size_t *p)
{
  *p &= ~USE;
  *FOOTER(p) &= ~USE;
}
inline void set_use(size_t *p)
{
  *p |= USE;
  *FOOTER(p) |= USE;
}
/*
 * free - free a block by setting the state to free and adding it to the corresponding free list
 */
void free(void *ptr)
{
  // print a seperating line
  ++cnt;
  dbg_printf("--------------------free:op %d---------------------\n", cnt);
  size_t *p = BACK_HEADER(ptr);
  if (CHECK_POINTER_VALID(p, 1, __LINE__))
  {
    dbg_out("free fail: invalid pointer\n");
    return;
  }
  // print detail information about p
  dbg_printf("free block at:%p,len:%lu,phynxt:%p,phyprev:%p,STATE:%s\n", p, GET_LEN(*p), GET_PHYSICAL_NEXT(p), GET_PHYSICAL_PREV(p), GET_STATE(*p) == USE ? "USE" : "FREE");
  DEBUG_SEQ_INFO(1);
  printf("info.start:%p,info.end:%p\n", info.start, info.end);
  set_free(p);
  dbg_printf("set free\n");
  try_merge_physical_next(p);
  dbg_printf("try merge next\n");
  try_merge_physical_prev(p);
  dbg_printf("try merge prev\n");
  // print another seperating line
  dbg_printf("--------------------free-end------------------\n");
  // mm_checkheap(0);
}

/*
 * realloc - Change the size of the block by mallocing a new block,
 *      copying its data, and freeing the old block.  I'm too lazy
 *      to do better.
 */
void *realloc(void *oldptr, size_t size)
{
  dbg_printf("--------------------realloc---------------------\n");
  size = ALIGN(size);
  if (size == 0)
  {
    dbg_printf("realloc: size is 0\n");
    if (!CHECK_POINTER_VALID(oldptr, 1, __LINE__))
      free(oldptr);
    return NULL;
  }
  if (oldptr == NULL)
  {
    dbg_printf("realloc: oldptr is NULL\n");
    return malloc(size);
  }
  size_t *old = BACK_HEADER(oldptr), len = GET_LEN(*old);
#ifdef SIMPLE_REALLOC
  dbg_printf("realloc: oldptr:%p,len:%lu, size:%lu,aligned:%lu\n", oldptr, GET_LEN(*old), size, ALIGN(size));
  free(oldptr);
  size_t *new_direct = malloc(size);
  memcpy(new_direct, (char *)oldptr, min(size, len));
  return new_direct;
#else
  if (CHECK_POINTER_VALID(old, 1, __LINE__))
  {
    dbg_out("realloc failed\n");
    return NULL;
  }

  // give debug information about old
  dbg_printf("realloc: old block at:%p, len:%lu, nxt:%p, prev:%p, STATE:%s\n", old, GET_LEN(*old), (size_t *)*(NEXT(old)), (size_t *)*PREV(old), GET_STATE(*old) == USE ? "USE" : "FREE");
  set_free(old);
  try_merge_physical_next(old);
  if (GET_LEN(*old) >= MIN_SIZE_TOLERANCE + size)
  {
    dbg_printf("realloc: split a block with len:%lu\n", GET_LEN(*old));
    set_use(old);
    list_pop(old);
    split_block(size, old);
    return oldptr;
  }
  if (GET_LEN(*old) >= size)
  {
    dbg_printf("realloc: no need to split\n");
    set_use(old);
    list_pop(old);
    alloc_block(GET_LEN(*old), USE, old);
    return oldptr;
  }
  dbg_printf("realloc: need to malloc a new block\n");
  try_merge_physical_prev(old);
  size_t *new = malloc(size);
  dbg_printf("realloc: new block at:%p, len:%lu\n", BACK_HEADER(new), GET_LEN(*BACK_HEADER(new)));
  memcpy(new, oldptr, GET_LEN(*old));
  return new;
#endif
}

/*
 * calloc - Allocate the block and set it to zero.
 */
void *calloc(size_t nmemb, size_t size)
{
  dbg_printf("--------------------calloc---------------------\n");
  dbg_printf("calloc: nmemb:%lu, size:%lu\n", nmemb, size);
  exit(0);
  size_t bytes = nmemb * size;
  void *newptr;
  newptr = malloc(bytes);
  memset(newptr, 0, bytes);
  return newptr;
}

/*
 * mm_checkheap - There are so many bugs in my code, so I certainly need to check,
 * :(
 */
void mm_checkheap(int verbose)
{
  if (!verbose)
    return;
  if (check_correctness())
  {
    dbg_out("mm_checkheap: check_correctness failed at line:%d\n", __LINE__);
    exit(-1);
  }
  return;
}

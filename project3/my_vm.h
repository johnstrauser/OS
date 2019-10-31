#ifndef MY_VM_H_INCLUDED
#define MY_VM_H_INCLUDED
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include "sys/mman.h"
#include "sys/types.h"
#include "math.h"

#define PGSIZE 4096

#define MAX_MEMSIZE (3UL*1024*1024*1024 + 750*1024*1024)

#define MEMSIZE 2UL*1024*1024*1024

#define VABITS 32

// Represents a page table entry
typedef unsigned long pte_t;

// Represents a page directory entry
typedef unsigned long pde_t;

#define TLB_SIZE 512

//Structure to represents TLB
typedef struct tlb {
    unsigned long va;
    unsigned long pa;
} tlb;

void set_physical_mem();
pte_t* translate(pde_t *pgdir, void *va);
int page_map(pde_t *pgdir, void *va, void* pa);
void * get_next_avail(int num_pages);
bool check_in_tlb(void *va);
void put_in_tlb(void *va, void *pa);
void *a_malloc(unsigned int num_bytes);
void a_free(void *va, int size);
void put_value(void *va, void *val, int size);
void get_value(void *va, void *val, int size);
void mat_mult(void *mat1, void *mat2, int size, void *answer);
int getNthBit(char value, int n);

#endif

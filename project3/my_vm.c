#include "my_vm.h"

/* GLOBAL VARIABLES */
char * physical_bit_map = NULL;
char * virtual_bit_map = NULL;
char * physical_mem = NULL;
int va_offset_bits;
int va_pt_bits;
int va_pd_bits;
pde_t * pgDir;
int init = 0;
pthread_mutex_t mutex;
const unsigned long PHYSICAL_BIT_MAP_SIZE = ((MEMSIZE / PGSIZE) / 8);
const unsigned long VIRTUAL_BIT_MAP_SIZE = ((MAX_MEMSIZE / PGSIZE) / 8);
tlb *TLB = NULL;

/*
  Function responsible for allocating and setting your physical memory 
*/
void set_physical_mem() {
  init = 1;
  va_offset_bits = log2(PGSIZE);
  va_pd_bits = (VABITS - va_offset_bits) / 2;
  va_pt_bits = VABITS - va_offset_bits - va_pd_bits;
  physical_mem = (char * ) memalign(PGSIZE, MEMSIZE);
  pgDir = (pde_t * ) calloc((1 << va_pd_bits), sizeof(pde_t));
  physical_bit_map = calloc(PHYSICAL_BIT_MAP_SIZE, sizeof(char));
  virtual_bit_map = calloc(VIRTUAL_BIT_MAP_SIZE, sizeof(char));
  // Setting the first page in virtual bitmap to 1 so that we skip it - this prevents 0x0 from giving us a null exception
  virtual_bit_map[0] = 128;
  //Setup the tlb
  TLB = (tlb *)calloc(TLB_SIZE, sizeof(tlb));
}

//try to find a tlb entry for va
//if none exists return null, if it does return the physical address
pte_t * getTLB(unsigned long va){
	//isolate the first 20 bits of the VA
	unsigned long temp = va;
	// printf("va: %x\n", va);
  temp = temp >> va_offset_bits;
  	// printf("temp: %x\n", temp);

	int index = hash(temp);
// printf("index : %d\n", index);
// printf("tlb of index . va: %x\n", TLB[index].va);
	if(TLB[index].va == temp){
		unsigned long offset = (unsigned long) va;
		offset = offset << (va_pt_bits + va_offset_bits);
		offset = offset >> (va_pt_bits + va_offset_bits);
		return (pte_t *)(TLB[index].pa + offset);
	}
	return NULL;
}

int hash(unsigned long input) {
	return input % TLB_SIZE;
}

//add a tlb entry
void addTLB(unsigned long va, unsigned long pa){
	unsigned long temp = va;
	temp = temp >> va_offset_bits;
	int index = hash(temp);
	TLB[index].pa = pa;
	TLB[index].va = temp;
}

//try to remove va from tlb
//do this by setting valid to 0 is the va is found
void removeTLB(unsigned long va){
	unsigned long temp = va;
	temp = temp >> va_offset_bits;
	int index = hash(temp);
	if(TLB[index].va == temp){
		TLB[index].pa = 0;
		TLB[index].va = 0;
	}
	return;
}

/*
  The function takes a virtual address and page directories starting address and
  performs translation to return the physical address
*/
pte_t * translate(pde_t * pgdir, void * va) {
  //try to get this from the tlb
  pte_t * pa = getTLB((unsigned long)va);
  if(pa != NULL){
    // printf("tlb hit\n");
	  //the va exists in the tlb
	  return pa;
  }
  // printf("%x\n", va);
      // printf("tlb miss\n");

  // 1. Get the page directory index
  unsigned long pgDirOffset = (unsigned long) va >> (va_pt_bits + va_offset_bits);
  pte_t * pageTable = (pte_t * ) pgdir[pgDirOffset];
  if (pageTable == NULL) {
    // There is no PT for this virtual address
    return NULL;
  }

  // 2. Get 2nd level page table index
  unsigned long tableOffset = (unsigned long) va;
  tableOffset = tableOffset << va_pd_bits;
  tableOffset = tableOffset >> (va_pt_bits + va_offset_bits);
  pte_t pageTableEntry = pageTable[tableOffset];
  if (pageTableEntry == 0) {
    // There is no PTE for this virtual address
    return NULL;
  }

  // 3. Get the physical address
  unsigned long pageOffset = (unsigned long) va;
  pageOffset = pageOffset << (va_pt_bits + va_offset_bits);
  pageOffset = pageOffset >> (va_pt_bits + va_offset_bits);
  pte_t * physicalAddress = (pte_t * )(pageTableEntry + pageOffset);

  //add this to tlb
  addTLB((unsigned long)va,(unsigned long)(pageTableEntry));
  
  return physicalAddress;
}

unsigned long getTableOffset(unsigned long va) {
    unsigned long pageOffset = (unsigned long) va;
    pageOffset = pageOffset << (va_pt_bits + va_offset_bits);
    pageOffset = pageOffset >> (va_pt_bits + va_offset_bits);
}

/*
  The function takes a page directory address, virtual address, physical address
  as an argument, and sets a page table entry. This function will walk the page
  directory to see if there is an existing mapping for a virtual address. If the
  virtual address is not present, then a new entry will be added
*/
int page_map(pde_t * pgdir, void * va, void * pa) {
  unsigned long virtualAddress = (unsigned long) va;

  unsigned long pgDirOffset = virtualAddress >> (va_pt_bits + va_offset_bits);
  pte_t * pageTable = (pte_t * ) pgdir[pgDirOffset];
  if (pageTable == NULL) {
    pgdir[pgDirOffset] = (pde_t) calloc((PGSIZE / sizeof(pte_t)), sizeof(pte_t));
    pageTable = (pte_t *)pgdir[pgDirOffset];
  }

  unsigned long tableOffset = virtualAddress;
  tableOffset = tableOffset << va_pd_bits;
  tableOffset = tableOffset >> (va_pt_bits + va_offset_bits);
  pte_t pageTableEntry = (pte_t) pageTable[tableOffset];

  // Check if the pte is mapped or not - if not, map it to the physical address param: pa
  if (pageTableEntry == 0) {
    pageTable[tableOffset] = (unsigned long) pa;
    return 1;
  }
  return -1;
}

/*
  Responsible for releasing one or more memory pages using virtual address (va)
*/
void a_free(void * va, int size) {
  if (size < 0) {
    return;
  }
  pthread_mutex_lock(&mutex);
  if (init == 0) {
    set_physical_mem();
  }

  int numPagesToFree = size / PGSIZE;
  if (size % PGSIZE != 0) {
    numPagesToFree++;
  }

  if (isValidVa(va + size) == 0) {
    pthread_mutex_unlock( & mutex);
    return;
  }

  unsigned long virtualAddress = (unsigned long) va;
  unsigned long page = (virtualAddress >> va_offset_bits) << va_offset_bits;
  unsigned long virtualPage  = page >> va_offset_bits;
  int k;
  for (k = 0; k < numPagesToFree; k++) {
    unsigned long pgDirIndex = page >> (va_pt_bits + va_offset_bits);
    pte_t * pageTable = (pte_t *)pgDir[pgDirIndex];
    unsigned long pageTableIndex = (page << va_pd_bits) >> (va_pt_bits + va_offset_bits);
    pte_t pageTableEntry = pageTable[pageTableIndex];
    unsigned long pageTableOffset = pageTableEntry >> va_offset_bits;
    // Free entry in physical bitmap
    char * p = physical_bit_map + pageTableOffset/8;
    // Free page table entry
    pageTable[pageTableIndex] = 0;
    *p -= (1 << (7 - (pageTableOffset % 8)));
    // Free entry in virtual bitmap
    char * v = virtual_bit_map + (virtualPage / 8);
    *v -= (1 << (7 - (virtualPage % 8)));
    virtualPage++;
  }
  //remove the VA from the TLB
  removeTLB((unsigned long)va);
  
  pthread_mutex_unlock( & mutex);
}

int isValidVa(void * va) {
  unsigned long virtAddress = (unsigned long) va;
  unsigned long pageNum = virtAddress >> va_offset_bits;
  unsigned long numPages = 1024 * 1024;
  if (((int) pageNum) > -1 && ((int) pageNum) <= numPages) {
    return 1;
  }
  return -1;
}

/*
  The function copies data pointed by "val" to physical
 * memory pages using virtual address (va)
*/
void put_value(void * va, void * val, int size) {
  pthread_mutex_lock( & mutex);
  if (init == 0) {
    set_physical_mem();
  }

  if (va == NULL) {
    pthread_mutex_unlock( & mutex);
    return;
  }

  if (isValidVa(va) != 1) {
    pthread_mutex_unlock( & mutex);
    return;
  }

  char * pa;
  char * value = val;
  int i;
  for (i = 0; i < size; i++) {
    pa = (char *) translate(pgDir, va + i);
    *pa = *value;
    value++;
  }

  pthread_mutex_unlock( & mutex);
}

/*
  Given a virtual address, this function copies the contents of the page to val
*/
void get_value(void * va, void * val, int size) {
  pthread_mutex_lock( & mutex);

  if (init == 0) {
    set_physical_mem();
  }

  if (va == NULL) {
    pthread_mutex_unlock( & mutex);
    return;
  }

  if (isValidVa(va) != 1) {
    pthread_mutex_unlock( & mutex);
    return;
  }

  char * pa;
  char * value = val;
  int i;
  for (i = 0; i < size; i++) {
    pa = (char *) translate(pgDir, va + i);
    *value = *pa;
    value++;
  }

  pthread_mutex_unlock( & mutex);
}

int getNthBit(char value, int n) {
  return (int) value & (1 << n);
}

/*
  This function receives two matrices mat1 and mat2 as an argument with size
  argument representing the number of rows and columns. After performing matrix
  multiplication, copy the result to answer.
*/
void mat_mult(void * mat1, void * mat2, int size, void * answer) {
  int sum = 0;
  int a, b, i, j, k;
  for (i = 0; i < size; i++) {
    for (j = 0; j < size; j++) {
      sum = 0;
      for (k = 0; k < size; k++) {
        get_value(mat1 + (i * size + k) * sizeof(int), &a, sizeof(int));
        get_value(mat2 + (k * size + j) * sizeof(int), &b, sizeof(int));
        sum = sum + (a * b);
      }
      put_value(answer + (i * size + j) * sizeof(int), &sum, sizeof(int));
    }
  }
}

int enoughPhysPages(int numPages) {
  int pagesFound = 0;
  char * physMapPointer = physical_bit_map;
  while (physMapPointer < physical_bit_map + PHYSICAL_BIT_MAP_SIZE) {
    int i;
    for (i = 0; i <= 7; i++) {
      if ((*physMapPointer & 1 << i) == 0) {
        pagesFound++;
      }
    }
    if (pagesFound >= numPages) {
      break;
    }
    physMapPointer += 1;
  }
  if (pagesFound >= numPages) {
    return 1;
  }
  return 0;
}

/*
  Function that gets the next available page
*/
void * get_next_avail(int num_pages) {
  int pagesFound = 0;
  char * vmapPointer = virtual_bit_map;
  int startPage = -1;
  int currLoop = 0;

  for (vmapPointer = virtual_bit_map; vmapPointer < & virtual_bit_map[VIRTUAL_BIT_MAP_SIZE] && pagesFound < num_pages; vmapPointer++) {
    int i;
    for (i = 7; i >= 0 && pagesFound < num_pages; i--) {
      if (( * vmapPointer & 1 << i) == 0) {
        if (pagesFound == 0) {
          startPage = currLoop * 8 + 7 - i;
        }
        pagesFound++;
      } else {
        startPage = -1;
        pagesFound = 0;
      }
    }
    currLoop++;
  }

  if (startPage != -1) {
    int k = 0;
    int startPageTemp = startPage;
    for (k = 0; k < num_pages; k++) {
      char * p = virtual_bit_map + (startPageTemp / 8);
      int j = startPageTemp % 8;
      * p = * p | 1 << (7 - j);
      startPageTemp++;
    }
    return ((void * )(startPage * PGSIZE));
  }

  return NULL;
}

/* 
  Function responsible for allocating pages
  and used by the benchmark
*/
void * a_malloc(unsigned int num_bytes) {
  if (num_bytes <= 0 || num_bytes > MEMSIZE) {
    return NULL;
  }
  pthread_mutex_lock(&mutex);

  if (!init) {
    set_physical_mem();
  }

  int numPagesRequested = num_bytes / PGSIZE;
  if (num_bytes % PGSIZE != 0) {
    numPagesRequested++;
  }

  if (enoughPhysPages(numPagesRequested) != 1) {
    pthread_mutex_unlock( & mutex);
    return NULL;
  }

  void * va = get_next_avail(numPagesRequested);
  if (va == NULL) {
    pthread_mutex_unlock( & mutex);
    return NULL;
  }
  int i = 0;
  char * physMapPointer = physical_bit_map;
  int pageNum = 0;
  for (i = 0; i < numPagesRequested; i++) {
    int j;
    for (j = 7; j >= 0; j--) {
      if ((*physMapPointer & 1 << j) == 0) {
        pageNum += 7 - j;
        * physMapPointer |= 1 << j;
        break;
      }
    }
    page_map(pgDir, va, (char *) physical_mem + (pageNum * PGSIZE));
    pageNum -= 7 - j;
    va += PGSIZE;
  }

  pthread_mutex_unlock( & mutex);
  return va -= PGSIZE * numPagesRequested;
}

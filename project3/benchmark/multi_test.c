#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "../my_vm.h"

#define SIZE 5
void * test_threads();
int main() {
    pthread_t thread1, thread2, thread3, thread4;
    pthread_create(&thread1, NULL, test_threads, NULL);
    pthread_create(&thread2, NULL, test_threads, NULL);
    pthread_create(&thread3, NULL, test_threads, NULL);
    pthread_create(&thread4, NULL, test_threads, NULL);
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);
    pthread_join(thread3, NULL);
    pthread_join(thread4, NULL);
    return 0;
}

void * test_threads() {
    printf("thread id: %d\n", pthread_self());
    printf("Allocating three arrays of 400 bytes\n");
    void *a = a_malloc(100*4);
    int old_a = (int)a;
    void *b = a_malloc(100*4);
    void *c = a_malloc(100*4);
    int x = 1;
    int y, z;
    int i =0, j=0;
    int address_a = 0, address_b = 0;
    int address_c = 0;

    printf("Addresses of the allocations: %x, %x, %x\n", (int)a, (int)b, (int)c);

    printf("Storing integers to generate a SIZExSIZE matrix\n");
    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            address_a = (unsigned int)a + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            address_b = (unsigned int)b + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            put_value((void *)address_a, &x, sizeof(int));
            put_value((void *)address_b, &x, sizeof(int));
        }
    } 

    printf("Fetching matrix elements stored in the arrays\n");

    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            address_a = (unsigned int)a + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            address_b = (unsigned int)b + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            get_value((void *)address_a, &y, sizeof(int));
            get_value( (void *)address_b, &z, sizeof(int));
            printf("%d ", y);
        }
        printf("\n");
    } 

    printf("Performing matrix multiplication with itself!\n");
    mat_mult(a, b, SIZE, c);


    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            address_c = (unsigned int)c + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            get_value((void *)address_c, &y, sizeof(int));
            printf("%d ", y);
        }
        printf("\n");
    }
    printf("Freeing the allocations!\n");
    a_free(a, 100*4);
    a_free(b, 100*4);
    a_free(c, 100*4);
    
    printf("Checking if allocations were freed!\n");
    a = a_malloc(100*4);
    if ((int)a == old_a)
        printf("free function works\n");
    else
        printf("free function does not work\n");
}

// File:	my_pthread_t.h
// Author:	Yujie REN
// Date:	January 2019

// List all group member's name: Brian Schillaci (netid: bcs115), John Strauser (netid: jps313)
// username of iLab: bcs115, jps313
// iLab Server: man.cs.rutgers.edu

#ifndef MY_PTHREAD_T_H
#define MY_PTHREAD_T_H

#define _GNU_SOURCE

/* To use real pthread Library in Benchmark, you have to comment the USE_MY_PTHREAD macro */
#define USE_MY_PTHREAD 1

/* include lib header files that you need here: */
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <ucontext.h>
#include <string.h>
#include <sys/time.h>

#define STACK_SIZE 1024*64
#define RUN_TIME_USEC 200

typedef unsigned int my_pthread_t;

typedef enum {READY, WAITING} t_state;

typedef struct threadControlBlock {
    my_pthread_t tid;
    my_pthread_t join_id;
    unsigned int run_time;
    unsigned int priority;
    ucontext_t * thread_context;
    t_state thread_state;
    struct threadControlBlock * next;
} tcb;

/* mutex struct definition */
typedef struct my_pthread_mutex_t {
    unsigned int lock; 
    unsigned int destroyed; 
    my_pthread_t tid; 
    unsigned int initialized;
} my_pthread_mutex_t;

// priority queue for threads
typedef struct TcbQueue {
    tcb * head;
    tcb * tail;
} TcbQueue;

typedef struct TcbReturn {
	my_pthread_t tid;
	void * returnVal;
	struct TcbReturn * next;
} ret;

typedef struct RetQueue {
    ret * head;
    ret * tail;
} RetQueue;


/* Function Declarations: */
static void schedule();

ret * findRet(my_pthread_t tid, RetQueue * queue);

void removeFromRetQueue(ret *toDequeue, RetQueue * queue);

void removeFromTcbQueue(tcb *toDequeue, TcbQueue * queue);

static void sched_stcf();

static void sched_mlfq();

void init();

tcb * getSJF(TcbQueue * queue);

void printTcbQueue(TcbQueue * queue);

tcb * findTCB(my_pthread_t tid, TcbQueue * queue);

tcb * dequeueTcb(TcbQueue * queue);

ret * dequeueRet(RetQueue * queue);

int enqueueRet(ret * toInsert, RetQueue *queue);

int enqueueTcb(tcb * toInsert, TcbQueue *queue);

/* create a new thread */
int my_pthread_create(my_pthread_t * thread, pthread_attr_t * attr, void *(*function)(void*), void * arg);

/* give CPU pocession to other user level threads voluntarily */
int my_pthread_yield();

/* terminate a thread */
void my_pthread_exit(void *value_ptr);

/* wait for thread termination */
int my_pthread_join(my_pthread_t thread, void **value_ptr);

/* initial the mutex lock */
int my_pthread_mutex_init(my_pthread_mutex_t *mutex, const pthread_mutexattr_t *mutexattr);

/* aquire the mutex lock */
int my_pthread_mutex_lock(my_pthread_mutex_t *mutex);

/* release the mutex lock */
int my_pthread_mutex_unlock(my_pthread_mutex_t *mutex);

/* destroy the mutex */
int my_pthread_mutex_destroy(my_pthread_mutex_t *mutex);

#ifdef USE_MY_PTHREAD
#define pthread_t my_pthread_t
#define pthread_mutex_t my_pthread_mutex_t
#define pthread_create my_pthread_create
#define pthread_exit my_pthread_exit
#define pthread_join my_pthread_join
#define pthread_mutex_init my_pthread_mutex_init
#define pthread_mutex_lock my_pthread_mutex_lock
#define pthread_mutex_unlock my_pthread_mutex_unlock
#define pthread_mutex_destroy my_pthread_mutex_destroy
#endif

#endif

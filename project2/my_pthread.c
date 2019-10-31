// File:	my_pthread.c
// Author:	Yujie REN
// Date:	January 2019

// List all group member's name: Brian Schillaci (netid: bcs115), John Strauser (netid: jps313)
// username of iLab: bcs115, jps313
// iLab Server: man.cs.rutgers.edu

#include "my_pthread_t.h"

unsigned int tids = 0;

// blocks ready to be scheduled
TcbQueue * schedQueue;

//mlfq levels
TcbQueue *queue2;
TcbQueue *queue3;
TcbQueue *queue4;

// blocks waiting on IO or a lock to be scheduled
TcbQueue * waitQueue;

// return values queue
RetQueue * retQueue;

int firstTimeRunning = 0;

// The currently running thread block
tcb * currentThread;

//timer for scheduler
struct itimerval timer;

// The exit context
ucontext_t exitContext;

void init() {
	firstTimeRunning = 1;
   
	//creating exit context
	getcontext(&exitContext);
	exitContext.uc_link = 0;
	exitContext.uc_stack.ss_sp = malloc(STACK_SIZE);
	exitContext.uc_stack.ss_size = STACK_SIZE;
	exitContext.uc_stack.ss_flags = 0;
	void (*exit)(void*) = &my_pthread_exit;  
    makecontext(&exitContext, (void*) exit, 1, NULL);
    
    retQueue = malloc(sizeof(RetQueue));
	schedQueue = malloc(sizeof(TcbQueue)); 
	queue2 = malloc(sizeof(TcbQueue));
	queue2->head = NULL;
	queue3 = malloc(sizeof(TcbQueue)); 
	queue3->head = NULL;
	queue4 = malloc(sizeof(TcbQueue)); 
    queue4->head = NULL;
    waitQueue = malloc(sizeof(TcbQueue)); 
    
    tcb * initialBlock = malloc(sizeof(tcb)); 
    initialBlock->thread_context = malloc(sizeof(ucontext_t)); 
    initialBlock->tid = 0;
    initialBlock->run_time = 0;
    initialBlock->priority = 0; 
    getcontext(initialBlock->thread_context); 
    currentThread = initialBlock;  
}

/* scheduler */
static void schedule() {
	
#ifndef MLFQ
    sched_stcf();
#else
    sched_mlfq();
#endif
}

void reset_timer() { 
    getitimer(ITIMER_REAL, &timer); 
    timer.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL); 
	schedule();
}

/* Preemptive SJF (STCF) scheduling algorithm */
static void sched_stcf() {
    struct sigaction interrupt;
    
    tcb * oldThread = currentThread;
    
	if (oldThread != NULL && oldThread->thread_state != WAITING) {
		oldThread->run_time += 1;
		// put the context back into the queue
		oldThread->thread_state = READY;
		enqueueTcb(oldThread, schedQueue);
	}
    memset(&interrupt, 0, sizeof(interrupt));
    interrupt.sa_handler = &reset_timer; 
    sigaction(SIGALRM, &interrupt, NULL); 

	tcb * to_run = getSJF(schedQueue); 
	
	if ( to_run == NULL ){
		return; 
	}
	removeFromTcbQueue(to_run,schedQueue);
	
	timer.it_value.tv_sec = 0; 
    timer.it_value.tv_usec = RUN_TIME_USEC; 
	setitimer(ITIMER_REAL, &timer, NULL); 
		
    if (oldThread != NULL) {
        currentThread = to_run; 
        swapcontext(oldThread->thread_context, to_run->thread_context); 
    }
    else {
        currentThread = to_run; 
        setcontext(to_run->thread_context); 
    }
}

static void sched_mlfq() {
	struct sigaction interrupt;
    
    tcb * oldThread = currentThread;
    
	if (oldThread != NULL && oldThread->thread_state != WAITING) {
		// put the context back into the queue
		oldThread->thread_state = READY;
		//increment priority if less than 3
		//determine which queue to add to
		if(oldThread->priority == 0){
			oldThread->priority++;
			enqueueTcb(oldThread, queue2);
		}else if(oldThread->priority == 1){
			oldThread->priority++;
			enqueueTcb(oldThread, queue3);
		}else{
			enqueueTcb(oldThread, queue4);
		}
	}
    
    memset(&interrupt, 0, sizeof(interrupt));
    interrupt.sa_handler = &reset_timer; 
    sigaction(SIGALRM, &interrupt, NULL); 
	
	int q=-1;
    tcb * to_run = NULL;  
    if(schedQueue->head != NULL){
		q=0;
		to_run = dequeueTcb(schedQueue);
	}else if(queue2->head != NULL){
		q=1;
		to_run = dequeueTcb(queue2);
	}else if(queue3->head != NULL){
		q=2;
		to_run = dequeueTcb(queue3);
	}else if(queue4->head != NULL){
		q=3;
		to_run = dequeueTcb(queue4);
	}
    
      
    if ( to_run == NULL || q == -1 ){
		return; 
	}
	
	timer.it_value.tv_sec = 0; 
    timer.it_value.tv_usec = RUN_TIME_USEC*(q+1);
	setitimer(ITIMER_REAL, &timer, NULL); 
	
    if (oldThread != NULL) {
        currentThread = to_run; 
        swapcontext(oldThread->thread_context, to_run->thread_context); 
    }
    else {
        currentThread = to_run; 
        setcontext(to_run->thread_context); 
    }
}

/* create a new thread */
int my_pthread_create(my_pthread_t *thread, pthread_attr_t *attr,
                      void *(*function)(void *), void *arg) {
	if (firstTimeRunning == 0) {
		init();
	}
	
	// Make the context for this new thread
    ucontext_t * newThread = (ucontext_t*)malloc(sizeof(ucontext_t));
    getcontext(newThread);  
    newThread->uc_stack.ss_sp = malloc(STACK_SIZE);
    newThread->uc_stack.ss_size = STACK_SIZE; 
    newThread->uc_stack.ss_flags = 0;  
    newThread->uc_link = &exitContext; 
    makecontext(newThread, (void*)function, 1, arg); 

    // Create a thread control block for this new thread
    *thread = ++tids; 
    tcb * newBlock = malloc(sizeof(tcb)); 
    newBlock->thread_context = newThread; 
    newBlock->run_time = 0; 
    newBlock->priority = 0;
    newBlock->join_id = 0; 
    newBlock->thread_state = READY;
    newBlock->tid = *thread;  

    // Add the new block to the scheduling queue
    enqueueTcb(newBlock, schedQueue); 

    // Call my pthread_yield to begin scheduling
    my_pthread_yield(); 
    
    return 0;
};

/* give CPU pocession to other user level threads voluntarily */
int my_pthread_yield() {
    getitimer(ITIMER_REAL, &timer);
    timer.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
    schedule();
    return 0;
};

/* terminate a thread */
void my_pthread_exit(void *value_ptr) {
	// Find any thread that was waiting for this thread to exit
	tcb * ptr = waitQueue->head; 
    while (ptr != NULL) {
        if (ptr->join_id == currentThread->tid )
            break; 
        ptr = ptr->next; 
    }

    // Put this thread onto the scheduling queue
    if (ptr != NULL) {
		ptr->thread_state = READY; 
		#ifndef MLFQ
			//SJF
			enqueueTcb(ptr, schedQueue);
		#else
			//MLFQ
			if(ptr->priority == 0){
				enqueueTcb(ptr, schedQueue);
			}else if(ptr->priority == 1){
				enqueueTcb(ptr, queue2);
			}else if(ptr->priority == 2){
				enqueueTcb(ptr, queue3);
			}else{
				enqueueTcb(ptr, queue4);
			}
		#endif		
    }
    
    // Insert return value into the list of return values
    if (value_ptr != NULL) {
        ret *new_Val = (ret*)malloc(sizeof(ret)); 
        new_Val->returnVal = value_ptr; 
        new_Val->tid = currentThread->tid; 
        enqueueRet(new_Val, retQueue);
    }
        
    free(currentThread->thread_context->uc_stack.ss_sp);
    free(currentThread->thread_context); 
    free(currentThread);
    currentThread = NULL;   
    my_pthread_yield(); 
};

/* wait for thread termination */
int my_pthread_join(my_pthread_t thread, void **value_ptr) {
    tcb * potential_thread_ready = findTCB(thread, schedQueue);
	#ifdef MLFQ
		//mlfq, if potential is NULL, check other queues
		if(potential_thread_ready == NULL){
			potential_thread_ready = findTCB(thread, queue2);
			if(potential_thread_ready == NULL){
				potential_thread_ready = findTCB(thread, queue3);
				if(potential_thread_ready == NULL){
					potential_thread_ready = findTCB(thread, queue4);
				}
			}
		}
	#endif
    if (potential_thread_ready != NULL ) {
        currentThread->thread_state = WAITING; 
        currentThread->join_id = thread; 
        enqueueTcb(currentThread, waitQueue);
        my_pthread_yield(); 
    }
    
    ret * retVal = findRet(thread, retQueue);
    
    if (retVal != NULL) {
		*value_ptr = retVal->returnVal;
		removeFromRetQueue(retVal, retQueue);
	}
	
    return 0;
}

tcb * findTCB(my_pthread_t tid, TcbQueue * queue) {
    tcb *temp = queue->head;
    while (temp != NULL) {
        if (temp->tid == tid) {
            return temp;
        }
        temp = temp->next;
    }
    return NULL;
}

ret * findRet(my_pthread_t tid, RetQueue * queue) {
    ret *temp = queue->head;
    while (temp != NULL) {
        if (temp->tid == tid) {
            return temp;
        }
        temp = temp->next;
    }
    return NULL;
}

/* initialize the mutex lock */
int my_pthread_mutex_init(my_pthread_mutex_t *mutex,
                          const pthread_mutexattr_t *mutexattr) {
    if (mutex->initialized == 1) {
        return -1;
    }
    mutex->lock = 0;
    mutex->initialized = 1;
    mutex->tid = -1;
    mutex->destroyed = 0;
    return 0;
};

/* aquire the mutex lock */
int my_pthread_mutex_lock(my_pthread_mutex_t *mutex) {
	if (mutex->destroyed == 1) {
        return -1; 
    }

    while (__sync_lock_test_and_set(&mutex->lock, 1) == 1){
        my_pthread_yield(); 
    }

    mutex->tid = currentThread->tid; 
    return 0;
};

/* release the mutex lock */
int my_pthread_mutex_unlock(my_pthread_mutex_t *mutex) {	
	if (mutex->destroyed == 1 || mutex->tid != currentThread->tid) {
        return -1; 
    }

    mutex->tid = -1; 
    __sync_lock_release(&mutex->lock); 
    return 0;
};


/* destroy the mutex */
int my_pthread_mutex_destroy(my_pthread_mutex_t *mutex) {
	if (mutex->lock == 1 || mutex->destroyed == 1) {
        return -1; 
    }

    mutex->destroyed = 1; 
    mutex->tid = -1; 
    mutex->initialized = 0;
    return 0; 
};

tcb * getSJF(TcbQueue * queue){
	//return lowest run time from queue
	if(queue->head == NULL){
		return NULL;
	}
	tcb * out = queue->head;
	tcb * temp = out->next;
	while(temp != NULL){
		if(temp->run_time < out->run_time){
			out = temp;
		}
		temp = temp->next;
	}
	return out;
}

/* removes a return value from a return value queue */
void removeFromRetQueue(ret * toDequeue, RetQueue * queue) {
    if (queue->head == NULL)
        return;

    ret * temp = queue->head;

    if (toDequeue == queue->head) {
        queue->head = temp->next;
        free(temp);
        return;
    }
    
    while (temp != NULL && temp->next != toDequeue) {
        temp = temp->next;
    }

    if (temp == NULL || temp->next == NULL) {
        return;
	}
      
    ret *next = temp->next->next;
    free(temp->next);
    temp->next = next;
    if(toDequeue == queue->tail){
		queue->tail = temp;
	}
}

/* removes a thread block from a thread block queue */
void removeFromTcbQueue(tcb *toDequeue, TcbQueue * queue) {
    if (queue->head == NULL) {
        return;
	}

    tcb * ptr = queue->head;

    if (toDequeue == queue->head) {
        queue->head = ptr->next;
        return;
    }
    
    while (ptr != NULL && ptr->next != toDequeue) {
        ptr = ptr->next;
    }

    if (ptr == NULL || ptr->next == NULL) {
        return;
	}
    
    tcb * next = toDequeue->next;
    ptr->next = next;
    if(toDequeue == queue->tail){
		queue->tail = ptr;
	}
}

/* dequeue's the head of a tcb queue */
tcb * dequeueTcb(TcbQueue * queue) {
    if (queue->head != NULL) {
		tcb * b = queue->head;
		queue->head = queue->head->next;
        return b;
	} else {
		return NULL;
	}
}

/* enqueue's a tcb block */
int enqueueTcb(tcb *toInsert, TcbQueue *queue) {
    // Case the queue is empty
    if (queue->head == NULL) {
    	if(toInsert == NULL) {
    		return -1;
    	}
        queue->head = toInsert;
        queue->tail = toInsert;
        queue->tail->next = NULL;
    }
    else {
        queue->tail->next = toInsert;
        queue->tail = toInsert;
        queue->tail->next = NULL;
    }
}

/* dequeue's the head of a ret queue */
ret * dequeueRet(RetQueue * queue) {
    if (queue->head != NULL) {
		ret * b = queue->head;
		queue->head = queue->head->next;
        return b;
	} else {
		return NULL;
	}
}

/* enqueue's a ret queue */
int enqueueRet(ret *toInsert, RetQueue *queue) {
    // Case the queue is empty
    if (queue->head == NULL) {
    	if(toInsert == NULL) {
    		return -1;
    	}
        queue->head = toInsert;
        queue->tail = toInsert;
        queue->tail->next = NULL;
    }
        // Add to the end of the queue
    else {
        queue->tail->next = toInsert;
        queue->tail = toInsert;
        queue->tail->next = NULL;
    }
}

/* Helper function to print out a tcb queue */
void printTcbQueue(TcbQueue * queue) {
    tcb *ptr = queue->head;
    printf("[");
    while (ptr != NULL) {
        printf("%d, ", ptr->tid);
        ptr = ptr->next;
    }
    printf("]\n");
}

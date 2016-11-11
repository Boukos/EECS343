int thread_create(void (*start_routine)(void*), void* arg);
int thread_join(int pid);
void lock_acquire(lock_t* lock);
void lock_release(lock_t* lock);
void lock_init(lock_t* lock);
void cv_wait(cond_t* conditionVariable, lock_t* lock);
void cv_signal(cond_t* conditionVariable);
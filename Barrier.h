#ifndef BARRIER_H_
#define BARRIER_H_

#include <semaphore.h>

class Barrier {
    const unsigned int num_of_threads;
    unsigned int waiting_threads;
    pthread_mutex_t m;
    sem_t sem;
    sem_t sem2;
public:
    explicit Barrier(unsigned int num_of_threads);
    void wait();
    ~Barrier();

};

#endif // BARRIER_H_


#include <pthread.h>
#include "Barrier.h"
#include <semaphore.h>


Barrier::Barrier(unsigned int num_of_threads) : num_of_threads(num_of_threads), waiting_threads(0){
    sem_init(&sem, 0, 0);
    sem_init(&sem2, 0, 1);
    pthread_mutex_init(&m, nullptr);
}
void Barrier::wait() {
    pthread_mutex_lock(&m);
        waiting_threads++;
        if (waiting_threads == num_of_threads){
            sem_post(&sem);
            sem_wait(&sem2);
        }
    pthread_mutex_unlock(&m);

    sem_wait(&sem);
    sem_post(&sem);

    pthread_mutex_lock(&m);
        waiting_threads--;
        if(waiting_threads == 0) {
            sem_wait(&sem);
            sem_post(&sem2);
        }
    pthread_mutex_unlock(&m);

    sem_wait(&sem2);
    sem_post(&sem2);
}

Barrier::~Barrier() {
    sem_destroy(&sem);
    pthread_mutex_destroy(&m);
}
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <assert.h>
#include <errno.h>

#define PC_BUFFER_LEN 10
#define PC_FULL_SPOTS_SEM "/pc_full_spots_sem3"
#define PC_EMPTY_SPOTS_SEM "/pc_empty_spots_sem3"
#define PC_MUTEX_SEM "/pc_mutex_sem3"
#define INSTRUCTIONS "Instructions:\n\
-p: run the producer/consumer problem\n\
  -n {N}: number of producers (required if using -p)\n\
  -c {C}: number of consumers (required if using -p)\n\
-d: run the dining philosopher's problem\n\
-b: run the potion brewers problem\n"

extern int errno;

// Producer Consumer Problem ----------------------------------------------------------------------------------------

// producer consumer buffer and associated values
struct PC_Buffer {
    sem_t *full_spots;  // consumers decrement (wait), producers increment (post)
    sem_t *empty_spots;  // producers decrement (wait), consumers increment (post)
    sem_t *mutex;  // mutex to lock the buffer
    size_t put_index;  // the index where producer will put the next item
    size_t get_index;  // the index where consumer will get the next item
    int count;  // the total number of items produced so far within the array
    int array[PC_BUFFER_LEN];  // the actual buffer
};

struct PC_Buffer pc_buffer;

// given a pc_buffer, initializes its data members
void init_pc(struct PC_Buffer *buff) {
    buff->full_spots = sem_open(PC_FULL_SPOTS_SEM, O_CREAT | O_EXCL, 0770, 0);  // # of full spots = zero
    if(buff->full_spots == SEM_FAILED)
        fprintf(stderr, "Error in sem_open %s: %s\n", PC_FULL_SPOTS_SEM, strerror(errno));
    buff->empty_spots = sem_open(PC_EMPTY_SPOTS_SEM, O_CREAT | O_EXCL, 0770, PC_BUFFER_LEN);  // empty spots = buff len
    if(buff->empty_spots == SEM_FAILED)
        fprintf(stderr, "Error in sem_open %s: %s\n", PC_EMPTY_SPOTS_SEM, strerror(errno));
    buff->mutex = sem_open(PC_MUTEX_SEM, O_CREAT | O_EXCL, 0770, 1);  // binary semaphore (mutex)
    if(buff->mutex == SEM_FAILED)
        fprintf(stderr, "Error in sem_open %s: %s\n", PC_MUTEX_SEM, strerror(errno));
    buff->put_index = 0;
    buff->get_index = 0;
    buff->count = 1;
    memset(buff->array, -1, PC_BUFFER_LEN);
}

void destroy_pc(struct PC_Buffer *buff) {
    if(sem_close(buff->full_spots) == -1)
        fprintf(stderr, "Error in sem_close full_spots: %s\n", strerror(errno));
    if(sem_close(buff->empty_spots) == -1)
        fprintf(stderr, "Error in sem_close empty_spots: %s\n", strerror(errno));
    if(sem_close(buff->mutex) == -1)
        fprintf(stderr, "Error in sem_close mutex: %s\n", strerror(errno));
    if(sem_unlink(PC_FULL_SPOTS_SEM) == -1)
        fprintf(stderr, "Error in sem_unlink %s: %s\n", PC_FULL_SPOTS_SEM, strerror(errno));
    if(sem_unlink(PC_EMPTY_SPOTS_SEM) == -1)
        fprintf(stderr, "Error in sem_unlink %s: %s\n", PC_EMPTY_SPOTS_SEM, strerror(errno));
    if(sem_unlink(PC_MUTEX_SEM) == -1)
        fprintf(stderr, "Error in sem_unlink %s: %s\n", PC_MUTEX_SEM, strerror(errno));
}

// Given an integer and a pc_buffer, puts the int into buffer
void pc_put(int val, struct PC_Buffer *buff) {
    sem_wait(buff->empty_spots);  // decrement # of empty spots, or block until one becomes available
    sem_wait(buff->mutex);  // acquire mutex to modify the buffer
    buff->array[buff->put_index] = val;  // put val at next producer index spot
    buff->count++;  // increment count
    buff->put_index = (buff->put_index + 1) % PC_BUFFER_LEN;  // increment index of next producer item
    sem_post(buff->mutex);  // release the mutex
    sem_post(buff->full_spots);  // increment # of full spots
}

int pc_get(struct PC_Buffer *buff) {
    int val;
    sem_wait(buff->full_spots);  // decrement # of full spots, or block until one becomes available
    sem_wait(buff->mutex);  // acquire mutex to modify the buffer
    val = buff->array[buff->get_index];  // get val from next consumer index spot
    buff->array[buff->get_index] = -1;  // zero out array space
    buff->get_index = (buff->get_index + 1) % PC_BUFFER_LEN;  // increment index of next consumer item
    sem_post(buff->mutex);  // release the mutex
    sem_post(buff->empty_spots);  // increment # of empty spots
    return val;
}

struct pc_thread_args {
    int thread_id;
    int producer_loops;
    int consumer_loops;
};

void *do_producer_work(void *arg) {
    int val;
    struct pc_thread_args *args = (struct pc_thread_args *)arg;
    for (int i = 0; i < args->producer_loops; i++) {
        printf("producer %d sleeping...\n", args->thread_id);
        fflush(stdout);
        sleep(rand() % 3);  // sleep between 0 and 2 seconds
        printf("producer %d awake...\n", args->thread_id);
        fflush(stdout);
        val = (args->thread_id * 1000) + i + 1;
        pc_put(val, &pc_buffer);
        printf("producer %d put:     %d\n", args->thread_id, val);
        fflush(stdout);
    }
    return NULL;
}

void *do_consumer_work(void *arg) {
    struct pc_thread_args *args = (struct pc_thread_args *)arg;
    int val;
    for (int i = 0; i < args->consumer_loops; i++) {
        printf("consumer %d sleeping...\n", args->thread_id);
        fflush(stdout);
        sleep(rand() % 3);  // sleep between 0 and 2 seconds
        printf("consumer %d awake...\n", args->thread_id);
        fflush(stdout);
        val = pc_get(&pc_buffer);
        printf("consumer %d got:           %d\n", args->thread_id, val);
        fflush(stdout);
    }
    return NULL;
}

void init_pc_thread_args(struct pc_thread_args *args, int id, int producer_loops, int consumer_loops) {
    args->thread_id = id;
    args->producer_loops = producer_loops;
    args->consumer_loops = consumer_loops;
}

int run_producer_consumer(int producer_count, int consumer_count) {
    printf("producers: %d consumers: %d\n", producer_count, consumer_count);
    srand(time(NULL));
    init_pc(&pc_buffer);
    pthread_t producer_threads[producer_count];
    pthread_t consumer_threads[consumer_count];
    int total_threads = producer_count > consumer_count ? producer_count : consumer_count;
    struct pc_thread_args all_args[total_threads];
    for (int i = 0; i < total_threads; i++) {
        init_pc_thread_args(&all_args[i], i + 1, consumer_count * 3, producer_count * 3);
        if(i < producer_count) pthread_create(&producer_threads[i], NULL, do_producer_work, &all_args[i]);
        if(i < consumer_count) pthread_create(&consumer_threads[i], NULL, do_consumer_work, &all_args[i]);
    }
    for (int i = 0; i < total_threads; i++) {
        if(i < producer_count) pthread_join(producer_threads[i], NULL);
        if(i < consumer_count) pthread_join(consumer_threads[i], NULL);
    }
    destroy_pc(&pc_buffer);
    return EXIT_SUCCESS;
}


// Dining Philosopher's Problem --------------------------------------------------------------------------------------

// OSTEP pdf page 441
int run_dining_philosophers() {
    printf("Diners!");
    return EXIT_SUCCESS;
}


// Brew Master's Problem --------------------------------------------------------------------------------------------

int run_brew_master() {
    printf("Brew Masters!");
    return EXIT_SUCCESS;
}


// Main function ----------------------------------------------------------------------------------------------------

int exit_and_print_instructions() {
    printf("%s", INSTRUCTIONS);
    return EXIT_FAILURE;
}

int main(int argc, char *argv[]) {
    if (argc < 2)
        return exit_and_print_instructions();
    // consumer producer problem
    if(strncmp(argv[1], "-p", 2) == 0) {
        if(argc < 6)
            return exit_and_print_instructions();
        if(strncmp(argv[2], "-n", 2) != 0 || strncmp(argv[4], "-c", 2) != 0)
            return exit_and_print_instructions();
        return run_producer_consumer(atoi(argv[3]), atoi(argv[5]));
    } else if (strncmp(argv[1], "-d", 2) == 0) {
        return run_dining_philosophers();
    } else if (strncmp(argv[1], "-b", 2) == 0) {
        return run_brew_master();
    } else {
        return exit_and_print_instructions();
    }
}
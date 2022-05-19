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


// --------------------------------------------------------------------------------------------------------------------
// Producer Consumer Problem ----------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------

// SOURCE: Adapted from Operating Systems: Three Easy Pieces pages 400
// DATE: 5/19/2022

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
    if(buff->full_spots == SEM_FAILED) {
        fprintf(stderr, "Error in sem_open %s: %s\n", PC_FULL_SPOTS_SEM, strerror(errno));
        exit(EXIT_FAILURE);
    }
    buff->empty_spots = sem_open(PC_EMPTY_SPOTS_SEM, O_CREAT | O_EXCL, 0770, PC_BUFFER_LEN);  // empty spots = buff len
    if(buff->empty_spots == SEM_FAILED) {
        fprintf(stderr, "Error in sem_open %s: %s\n", PC_EMPTY_SPOTS_SEM, strerror(errno));
        exit(EXIT_FAILURE);
    }
    buff->mutex = sem_open(PC_MUTEX_SEM, O_CREAT | O_EXCL, 0770, 1);  // binary semaphore (mutex)
    if(buff->mutex == SEM_FAILED) {
        fprintf(stderr, "Error in sem_open %s: %s\n", PC_MUTEX_SEM, strerror(errno));
        exit(EXIT_FAILURE);
    }
    buff->put_index = 0;
    buff->get_index = 0;
    buff->count = 1;
    memset(buff->array, -1, PC_BUFFER_LEN);
}

// closes all semaphores
void destroy_pc(struct PC_Buffer *buff) {
    if(sem_close(buff->full_spots) == -1) {
        fprintf(stderr, "Error in sem_close full_spots: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if(sem_close(buff->empty_spots) == -1) {
        fprintf(stderr, "Error in sem_close empty_spots: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if(sem_close(buff->mutex) == -1) {
        fprintf(stderr, "Error in sem_close mutex: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if(sem_unlink(PC_FULL_SPOTS_SEM) == -1) {
        fprintf(stderr, "Error in sem_unlink %s: %s\n", PC_FULL_SPOTS_SEM, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if(sem_unlink(PC_EMPTY_SPOTS_SEM) == -1) {
        fprintf(stderr, "Error in sem_unlink %s: %s\n", PC_EMPTY_SPOTS_SEM, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if(sem_unlink(PC_MUTEX_SEM) == -1) {
        fprintf(stderr, "Error in sem_unlink %s: %s\n", PC_MUTEX_SEM, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

// puts the value onto the queue, in FIFO order
void pc_put(int val, struct PC_Buffer *buff) {
    sem_wait(buff->empty_spots);  // decrement # of empty spots, or block until one becomes available
    sem_wait(buff->mutex);  // acquire mutex to modify the buffer
    buff->array[buff->put_index] = val;  // put val at next producer index spot
    buff->count++;  // increment count
    buff->put_index = (buff->put_index + 1) % PC_BUFFER_LEN;  // increment index of next producer item
    sem_post(buff->mutex);  // release the mutex
    sem_post(buff->full_spots);  // increment # of full spots
}

// gets the value from the queue, in FIFO order
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

// arguments for the thread start functions
struct pc_thread_args {
    int thread_id;
    int producer_loops;
    int consumer_loops;
};

// start function for queue producer threads
void *do_producer_work(void *arg) {
    int val;
    struct pc_thread_args *args = (struct pc_thread_args *)arg;
    for (int i = 0; i < args->producer_loops; i++) {
        sleep(rand() % 3);  // sleep between 0 and 2 seconds
        val = (args->thread_id * 1000) + i + 1;  // ie: 9012 represents 12th item produced by thread #9
        pc_put(val, &pc_buffer);
        printf("producer %d put:     %d\n", args->thread_id, val);
        fflush(stdout);  // immediately flush the output; occasionally there is still a lag though
    }
    return NULL;
}

// start function for queue consumer thread
void *do_consumer_work(void *arg) {
    struct pc_thread_args *args = (struct pc_thread_args *)arg;
    int val;
    for (int i = 0; i < args->consumer_loops; i++) {
        sleep(rand() % 3);  // sleep between 0 and 2 seconds
        val = pc_get(&pc_buffer);
        printf("consumer %d got:           %d\n", args->thread_id, val);
        fflush(stdout);  // immediately flush the output; occasionally there is still a lag though
    }
    return NULL;
}

// initializes the producer consumer thread arguments
void init_pc_thread_args(struct pc_thread_args *args, int id, int producer_loops, int consumer_loops) {
    args->thread_id = id;
    args->producer_loops = producer_loops;
    args->consumer_loops = consumer_loops;
}

// run the producer consumer queue simulation
int run_producer_consumer(int producer_count, int consumer_count) {
    printf("Running Producer/Consumer simulation with %d producers, %d consumers.\n\n", producer_count, consumer_count);
    // initialize the simulation arguments
    srand(time(NULL));
    init_pc(&pc_buffer);
    pthread_t producer_threads[producer_count];
    pthread_t consumer_threads[consumer_count];
    int total_threads = producer_count > consumer_count ? producer_count : consumer_count;
    struct pc_thread_args all_args[total_threads];
    // create all the threads
    for (int i = 0; i < total_threads; i++) {
        init_pc_thread_args(&all_args[i], i + 1, consumer_count * 3, producer_count * 3);
        if(i < producer_count) pthread_create(&producer_threads[i], NULL, do_producer_work, &all_args[i]);
        if(i < consumer_count) pthread_create(&consumer_threads[i], NULL, do_consumer_work, &all_args[i]);
    }
    // join all the threads
    for (int i = 0; i < total_threads; i++) {
        if(i < producer_count) pthread_join(producer_threads[i], NULL);
        if(i < consumer_count) pthread_join(consumer_threads[i], NULL);
    }
    destroy_pc(&pc_buffer);
    return EXIT_SUCCESS;
}



// --------------------------------------------------------------------------------------------------------------------
// Dining Philosopher's Problem --------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------

// SOURCE: Adapted from Operating Systems: Three Easy Pieces pages 403+
// DATE: 5/19/2022

// semaphore values
sem_t *forks[5];  // semaphores representing the five forks
char *fork_sems[5] = {"/fork_sem0", "/fork_sem1", "/fork_sem2", "/fork_sem3", "/fork_sem4"}; // semaphore names

void init_fork_semaphores() {
    for(int i = 0; i < 5; i++) {
        forks[i] = sem_open(fork_sems[i], O_CREAT | O_EXCL, 0770, 1);  // # of full spots = zero
        if(forks[i] == SEM_FAILED) {
            fprintf(stderr, "Error in sem_open %s: %s\n", fork_sems[i], strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
}

void destroy_fork_semaphores() {
    for(int i = 0; i < 5; i++) {
        if(sem_close(forks[i]) == -1) {
            fprintf(stderr, "Error in sem_close %s: %s\n", fork_sems[i], strerror(errno));
            exit(EXIT_FAILURE);
        }
        if(sem_unlink(fork_sems[i]) == -1) {
            fprintf(stderr, "Error in sem_unlink %s: %s\n", fork_sems[i], strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
}

// functions to get the left / right fork from a given philosopher
int left(int p) { return p; }
int right(int p) { return (p + 1) % 5; }

// functions to acquire and put down the fork semaphores on either side of a philosopher
void get_forks(int p) {
    // to prevent deadlock - need at least one philosopher to acquire forks in different order
    printf("Philosopher %d is acquiring forks...\n", p);
    fflush(stdout);
    if(p == 4) {
        sem_wait(forks[right(p)]);
        sem_wait(forks[left(p)]);
    } else {
        sem_wait(forks[left(p)]);
        sem_wait(forks[right(p)]);
    }
}

void put_forks(int p) {
    sem_post(forks[left(p)]);
    sem_post(forks[right(p)]);
}

// think and eat are simulating by sleeping between 0 and 3 seconds
void think(int p) {
    printf("Philosopher %d is thinking...\n", p);
    fflush(stdout);
    sleep(rand() % 5);
}
void eat(int p) {
    printf("Philosopher %d is eating...\n", p);
    fflush(stdout);
    sleep(rand() % 5);
}

// start function for the philosopher threads
void *do_philosopher_work(void *arg) {
    int p = *(int *)arg;
    for(int i = 0; i < 5; i++) {
        think(p);
        get_forks(p);
        eat(p);
        put_forks(p);
    }
    return NULL;
}

// run the dining philosopher simulation
int run_dining_philosophers() {
    printf("Running Dining Philosopher's simulation. Each will eat 5 times before ending.\n\n");
    init_fork_semaphores();
    pthread_t philosophers[5];
    // create all the threads
    int p_index[5] = {0, 1, 2, 3, 4};
    for (int i = 0; i < 5; i++) {
        pthread_create(&philosophers[i], NULL, do_philosopher_work, &p_index[i]);
    }
    // join all the threads
    for (int i = 0; i < 5; i++) {
        pthread_join(philosophers[i], NULL);
    }
    destroy_fork_semaphores();
    return EXIT_SUCCESS;
}



// --------------------------------------------------------------------------------------------------------------------
// Brew Master's Problem --------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------

int run_brew_master() {
    printf("Brew Masters!");
    return EXIT_SUCCESS;
}



// --------------------------------------------------------------------------------------------------------------------
// Main function ----------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------

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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>

#define INSTRUCTIONS "Instructions:\n\
-p: run the producer/consumer problem\n\
  -n {N}: number of producers (required if using -p)\n\
  -c {C}: number of consumers (required if using -p)\n\
-d: run the dining philosopher's problem\n\
-b: run the potion brewers problem\n"

#define PC_BUFFER_LEN 10

#define RESOURCE_COUNT 3
#define RESOURCE_TOTAL ((RESOURCE_COUNT * (RESOURCE_COUNT-1)) / 2)  // 0 + 1 + 2 = 3 total

extern int errno;

// helper functions to initialize and destroy named semaphores
void open_sem(sem_t **semaphore, char *name, int init_val) {
    *semaphore = sem_open(name, O_CREAT | O_EXCL, 0770, init_val);
    if(*semaphore == SEM_FAILED) {
        fprintf(stderr, "Error in sem_open %s: %s\n", name, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void destroy_sem(sem_t *semaphore, char *name) {
    if(sem_close(semaphore) == -1) {
        fprintf(stderr, "Error in sem_close %s: %s\n", name, strerror(errno));
    }
    if(sem_unlink(name) == -1) {
        fprintf(stderr, "Error in sem_unlink %s: %s\n", name, strerror(errno));
    }
}



// --------------------------------------------------------------------------------------------------------------------
// Producer Consumer Problem ------------------------------------------------------------------------------------------
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
char *pc_full_sem_name = "/pc_full_spots_semaphore";
char *pc_empty_sem_name = "/pc_empty_spots_semaphore";
char *pc_mutex_sem_name = "/pc_mutex_semaphore";

// given a pc_buffer, initializes its data members
void init_pc(struct PC_Buffer *buff) {
    open_sem(&buff->full_spots, pc_full_sem_name, 0);  // # of full spots = zero
    open_sem(&buff->empty_spots, pc_empty_sem_name, PC_BUFFER_LEN);  // empty spots = buff len
    open_sem(&buff->mutex, pc_mutex_sem_name, 1);  // binary semaphore (mutex)
    buff->put_index = 0;
    buff->get_index = 0;
    buff->count = 1;
    memset(buff->array, -1, PC_BUFFER_LEN);  // clear the buffer
}

// closes all pc_buffer semaphores
void destroy_pc(struct PC_Buffer *buff) {
    destroy_sem(buff->full_spots, pc_full_sem_name);
    destroy_sem(buff->empty_spots, pc_empty_sem_name);
    destroy_sem(buff->mutex, pc_mutex_sem_name);
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
// Dining Philosopher's Problem ---------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------

// SOURCE: Adapted from Operating Systems: Three Easy Pieces pages 403+
// DATE: 5/19/2022

// semaphore values
sem_t *forks[5];  // semaphores representing the five forks
char *fork_sems[5] = {"/fork_sem0", "/fork_sem1", "/fork_sem2", "/fork_sem3", "/fork_sem4"}; // semaphore names

void init_fork_semaphores() {
    for(int i = 0; i < 5; i++)  open_sem(&forks[i], fork_sems[i], 1);
}

void destroy_fork_semaphores() {
    for(int i = 0; i < 5; i++)  destroy_sem(forks[i], fork_sems[i]);
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
// Brew Master's Problem ----------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------

// necessary semaphores for condition signalling
sem_t *agentSem;
char *agentSemName = "/agent_brewmaster";
sem_t *resourceSems[RESOURCE_COUNT];
char *resourceSemNames[RESOURCE_COUNT] = {"/bezoars", "/unicorn_horns", "/mistletoe_berries"};
sem_t *brewersSems[RESOURCE_COUNT];
char *brewerSemNames[RESOURCE_COUNT] = {"/bezoars_brewer", "/unicorn_horns_brewer", "/mistletoe_berries_brewer"};

// the state shared by the pusher threads
sem_t *resourceStateMutex;
char *resourceStateMutexName = "/pusher_state";
struct ResourceState {
    int count;  // e.g. if count = 1, this means only 1 resource has produced; need (3 - count)
    int total;  // e.g. if total = 2, this means 0 + 2 have produced but (3 - 2) == 1 is missing
};
struct ResourceState resourceState = {0, 0};

// functions to initialize and destroy the semaphores
void init_brewmaster_sems() {
    open_sem(&agentSem, agentSemName, 1);
    open_sem(&resourceStateMutex, resourceStateMutexName, 1);
    for(int i = 0; i < RESOURCE_COUNT; i++) {
        open_sem(&resourceSems[i], resourceSemNames[i], 0);
        open_sem(&brewersSems[i], brewerSemNames[i], 0);
    }
}

void destroy_brewmaster_sems() {
    destroy_sem(agentSem, agentSemName);
    destroy_sem(resourceStateMutex, resourceStateMutexName);
    for(int i = 0; i < RESOURCE_COUNT; i++) {
        destroy_sem(resourceSems[i], resourceSemNames[i]);
        destroy_sem(brewersSems[i], brewerSemNames[i]);
    }
}

// main function for agent thread; produces resources of a given type
// (or rather, produces all resource, except the one which it is explicitly missing)
void *do_agent_work(void *arg) {
    int missing_resource = *(int *)arg;
    for(int i = 0; i < 10; i++) {      // produce 10 times (arbitrarily chosen simulation count)
        sem_wait(agentSem);
        usleep(250000);  // sleep .25 seconds (just to make printing of data slower)
        for(int j = 0; j < RESOURCE_COUNT; j++) {
            // e.g. if this thread is 0 = bezoars, this threads skips 0 but produces 1 and 2
            if(j != missing_resource) {
                printf("Agent produced:          %s\n", resourceSemNames[j]+1);
                fflush(stdout);
                sem_post(resourceSems[j]);
            }
        }
    }
    return NULL;
}

// main function for pusher thread; wakes up the appropriate brewer depending on which resources agent produces
void *do_pusher_work(void *arg) {
    int assigned_resource = *(int *)arg;
    for(int i = 0; i < (10 * (RESOURCE_COUNT - 1)); i++) {
        // wait for this resource to be produced by the agent, the update the state of what is "on the table"
        sem_wait(resourceSems[assigned_resource]);
        sem_wait(resourceStateMutex);
        resourceState.count++;
        resourceState.total += assigned_resource;
        // if this was the last resource from agent, wake up the brewer with the missing resource and reset state to 0
        if(resourceState.count == (RESOURCE_COUNT - 1)) {
            sem_post(brewersSems[RESOURCE_TOTAL - resourceState.total]);
            resourceState.count = 0;
            resourceState.total = 0;
        }
        sem_post(resourceStateMutex);
    }
    return NULL;
}

// main function for brewer thread; woken up by pusher, produces potion, wakes up agent the next batch of resources
void *do_brewer_work(void *arg) {
    int assigned_resource = *(int *)arg;
    for(int i = 0; i < 10; i++) {
        sem_wait(brewersSems[assigned_resource]);
        printf("Potion by brewer with:   %s\n", resourceSemNames[assigned_resource]+1);
        fflush(stdout);
        sem_post(agentSem);
    }
    return NULL;
}

// run the brewmaster simulation
int run_brew_master() {
    printf("Running the brew master simulation. 10 potions of each type will be produced.\n\n");
    init_brewmaster_sems();
    int resource[RESOURCE_COUNT] = {0, 1, 2};  // 0 = bezoars, 1 = unicorn_horns, 2 = mistletoe_berries
    // create all the threads
    pthread_t agents[RESOURCE_COUNT];  // the agent threads to produce resources
    pthread_t pushers[RESOURCE_COUNT];  // the pusher threads to interface between agent and brewers
    pthread_t brewers[RESOURCE_COUNT];  // the brewer threads to produce potions
    for (int i = 0; i < RESOURCE_COUNT; i++) {
        pthread_create(&agents[i], NULL, do_agent_work, &resource[i]);
        pthread_create(&pushers[i], NULL, do_pusher_work, &resource[i]);
        pthread_create(&brewers[i], NULL, do_brewer_work, &resource[i]);
    }
    // join all the threads
    for (int i = 0; i < RESOURCE_COUNT; i++) {
        pthread_join(agents[i], NULL);
        pthread_join(pushers[i], NULL);
        pthread_join(brewers[i], NULL);
    }
    destroy_brewmaster_sems();
    return EXIT_SUCCESS;
}



// --------------------------------------------------------------------------------------------------------------------
// Main function ------------------------------------------------------------------------------------------------------
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
/*
 * INSTRUCTIONS TO GRADER:
 *
 * TO COMPILE:
 * gcc --std=gnu99 -Wall -pthread -o bairdjo_hw4 bairdjo_hw4.c
 *
 * NOTES & OVERVIEW:
 *  I implemented each of the three problems with BOTH processes and threads
 *  The process version of each can be run using a -proc flag; the thread via -thread
 *
 * EXAMPLE INSTRUCTIONS:
 *  ./bairdjo_hw4.c [-proc/-thread] -p -n 8 -c 2    --> runs producer consumer with 8 producers, 2 consumers
 *  ./bairdjo_hw4.c [-proc/-thread] -d              --> runs dining philosophers
 *  ./bairdjo_hw4.c [-proc/-thread] -b              --> runs potion brewers
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <sys/wait.h>

#define INSTRUCTIONS "Instructions:\n\
first argument: \
  -proc or -thread to run process or thread version\
second argument:\
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


// TODO - check if read() and write() to pipes work differently on little endian system

// --------------------------------------------------------------------------------------------------------------------
// Producer Consumer Problem:  Processes ------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------
// arguments for the process start functions
struct pc_process_args {
    int id;
    int producer_loops;
    int consumer_loops;
    int pipeFD;
};

// initializes the producer consumer process arguments
void init_pc_process_args(struct pc_process_args *args, int id, int producer_loops, int consumer_loops, int fd) {
    args->id = id;
    args->producer_loops = producer_loops;
    args->consumer_loops = consumer_loops;
    args->pipeFD = fd;
}

// main function for producer processes
void do_process_producer_work(struct pc_process_args *args) {
    int val;
    for (int i = 0; i < args->producer_loops; i++) {
        sleep(rand() % 3);  // sleep between 0 and 2 seconds
        val = (args->id * 1000) + i + 1;  // ie: 9012 represents 12th item produced by process #9
        write(args->pipeFD, (void *)&val, sizeof(int));  // write val into the pipe
        printf("producer %d put:     %d\n", args->id, val);
        fflush(stdout);  // immediately flush the output; occasionally there is still a lag though
    }
    exit(EXIT_SUCCESS);
}

// main function for queue producer process
void do_process_consumer_work(struct pc_process_args *args) {
    int val;
    for (int i = 0; i < args->consumer_loops; i++) {
        sleep(rand() % 3);  // sleep between 0 and 2 seconds
        read(args->pipeFD, &val, sizeof(int));  // write val into the pipe
        printf("consumer %d got:           %d\n", args->id, val);
        fflush(stdout);  // immediately flush the output; occasionally there is still a lag though
    }
    exit(EXIT_SUCCESS);
}



int run_pc_processes(int producer_count, int consumer_count) {
    printf("[PROCESSES] Running Producer/Consumer simulation with %d producers, %d consumers.\n\n", producer_count, consumer_count);
    // initialize the simulation arguments
    int pipeFDs[2]; // pipeFDs[0] is read, pipeFDs[1] is write
    if (pipe(pipeFDs) == -1) {
        printf("Call to pipe() failed\n");
        exit(EXIT_FAILURE);
    }
    pid_t producers[producer_count];
    pid_t consumers[consumer_count];
    struct pc_process_args args;
    // create all the processes
    for (int i = 0; i < producer_count; i++) {
        init_pc_process_args(&args, i + 1, consumer_count * 3, producer_count * 3, pipeFDs[1]);
        if((producers[i] = fork()) == 0) do_process_producer_work(&args);
    }
    for (int i = 0; i < consumer_count; i++) {
        init_pc_process_args(&args, i + 1, consumer_count * 3, producer_count * 3, pipeFDs[0]);
        if((consumers[i] = fork()) == 0) do_process_consumer_work(&args);
    }
    // wait for all processes
    int wstatus;
    for (int i = 0; i < producer_count; i++)
        waitpid(producers[i], &wstatus, 0);
    for (int i = 0; i < consumer_count; i++)
        waitpid(consumers[i], &wstatus, 0);
    return EXIT_SUCCESS;
}



// --------------------------------------------------------------------------------------------------------------------
// Producer Consumer Problem:  Threads --------------------------------------------------------------------------------
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
    memset(buff->array, -1, PC_BUFFER_LEN * sizeof(int));  // clear the buffer
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
int run_pc_threads(int producer_count, int consumer_count) {
    printf("[THREADS] Running Producer/Consumer simulation with %d producers, %d consumers.\n\n", producer_count, consumer_count);
    // initialize the simulation arguments
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
// Dining Philosopher's Problem:  Threads -----------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------

// SOURCE: Adapted from Operating Systems: Three Easy Pieces pages 403+
// DATE: 5/19/2022

void init_fork_semaphores(sem_t *forks[], char *fork_sems[]) {
    for(int i = 0; i < 5; i++)  open_sem(&forks[i], fork_sems[i], 1);
}

void destroy_fork_semaphores(sem_t *forks[], char *fork_sems[]) {
    for(int i = 0; i < 5; i++)  destroy_sem(forks[i], fork_sems[i]);
}

// functions to acquire and put down the fork semaphores on either side of a philosopher
void get_forks(int p, sem_t *left_fork, sem_t *right_fork) {
    // to prevent deadlock - need at least one philosopher to acquire forks in different order
    printf("Philosopher %d is acquiring forks...\n", p);
    fflush(stdout);
    if(p == 4) {
        sem_wait(right_fork);
        sem_wait(left_fork);
    } else {
        sem_wait(left_fork);
        sem_wait(right_fork);
    }
}

void put_forks(int p, sem_t *left_fork, sem_t *right_fork) {
    sem_post(left_fork);
    sem_post(right_fork);
}

// think and eat are simulating by sleeping
void think(int p) {
    printf("Philosopher %d is thinking...\n", p);
    fflush(stdout);
    sleep((rand() % 20) + 1);  // Thinking takes a random amount of time in the range of 1-20 seconds.
}
void eat(int p) {
    printf("Philosopher %d is eating...\n", p);
    fflush(stdout);
    sleep((rand() % 8) + 2);  // Eating takes a random amount of time in the range of 2-9 seconds.
}

// functions to get the left / right fork from a given philosopher
int left(int p) { return p; }
int right(int p) { return (p + 1) % 5; }

// function to run a philosopher simulation
int do_philosopher_work(int id, sem_t *left_fork, sem_t *right_fork) {
    for(int i = 0; i < 5; i++) {
        think(id);
        get_forks(id, left_fork, right_fork);
        eat(id);
        put_forks(id, left_fork, right_fork);
    }
    return EXIT_SUCCESS;
}

// data members necessary for thread implementation
struct dining_thread_arg {
    int id;
    sem_t *left_fork;
    sem_t *right_fork;
};

// start function for the philosopher threads
void *do_philosopher_work_thread(void *data) {
    struct dining_thread_arg *arg = (struct dining_thread_arg *)data;
    do_philosopher_work(arg->id, arg->left_fork, arg->right_fork);
    return NULL;
}

// run the dining philosopher simulation
int run_dining_philosophers_threads() {
    printf("[THREADS] Running Dining Philosopher's simulation. Each will eat 5 times before ending.\n\n");
    // initialize the arguments
    sem_t *forks[5];  // semaphores representing the five forks
    char *fork_sems[5] = {"/fork_sem0", "/fork_sem1", "/fork_sem2", "/fork_sem3", "/fork_sem4"}; // semaphore names
    init_fork_semaphores(forks, fork_sems);
    // initialize the arguments
    struct dining_thread_arg args[5];
    for(int i = 0; i < 5; i++) {
        args[i].id = i;
        args[i].left_fork = forks[left(i)];
        args[i].right_fork = forks[right(i)];
    }
    // create all the threads
    pthread_t philosophers[5];
    for (int i = 0; i < 5; i++)
        pthread_create(&philosophers[i], NULL, do_philosopher_work_thread, &args[i]);
    // join all the threads
    for (int i = 0; i < 5; i++)
        pthread_join(philosophers[i], NULL);
    destroy_fork_semaphores(forks, fork_sems);
    return EXIT_SUCCESS;
}



// --------------------------------------------------------------------------------------------------------------------
// Dining Philosopher's Problem:  Processes ---------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------
int run_dining_philosophers_processes() {
    printf("[PROCESSES] Running Dining Philosopher's simulation. Each will eat 5 times before ending.\n\n");
    // semaphore values
    sem_t *forks[5];  // semaphores representing the five forks
    char *fork_sems[5] = {"/fork_sem0", "/fork_sem1", "/fork_sem2", "/fork_sem3", "/fork_sem4"}; // semaphore names
    init_fork_semaphores(forks, fork_sems);
    // create all the processes
    pid_t philosophers[5];
    for (int i = 0; i < 5; i++)
        if((philosophers[i] = fork()) == 0)
            return do_philosopher_work(i, forks[left(i)], forks[right(i)]);
    // wait for all the processes
    int wstatus;
    for (int i = 0; i < 5; i++)
        waitpid(philosophers[i], &wstatus, 0);
    destroy_fork_semaphores(forks, fork_sems);
    return EXIT_SUCCESS;
}

// --------------------------------------------------------------------------------------------------------------------
// Brew Master's Problem: Threads -------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------

// ADAPTED FROM: The Little Book of Semaphores  DATE: 5/22/2022
// URL: https://www.greenteapress.com/semaphores/LittleBookOfSemaphores.pdf

// necessary semaphores for condition signalling
struct brewmaster_sems {
    sem_t *agentSem;
    char *agentSemName;
    sem_t *resourceSems[RESOURCE_COUNT];
    char *resourceSemNames[RESOURCE_COUNT];
    sem_t *brewersSems[RESOURCE_COUNT];
    char *brewerSemNames[RESOURCE_COUNT];
    sem_t *resourceStateMutex;
    char *resourceStateMutexName;
} sems = {
        .agentSemName = "/agent_brewmaster",
        .resourceSemNames = {"/bezoars", "/unicorn_horns", "/mistletoe_berries"},
        .brewerSemNames = {"/bezoars_brewer", "/unicorn_horns_brewer", "/mistletoe_berries_brewer"},
        .resourceStateMutexName = "/pusher_state"
};;

struct ResourceState {
    int count;  // e.g. if count = 1, this means only 1 resource has produced; need (3 - count)
    int total;  // e.g. if total = 2, this means 0 + 2 have produced but (3 - 2) == 1 is missing
} resourceState = {0, 0};

// functions to initialize and destroy the semaphores
void init_brewmaster_sems(struct brewmaster_sems *sems) {
    open_sem(&sems->agentSem, sems->agentSemName, 1);
    open_sem(&sems->resourceStateMutex, sems->resourceStateMutexName, 1);
    for(int i = 0; i < RESOURCE_COUNT; i++) {
        open_sem(&sems->resourceSems[i], sems->resourceSemNames[i], 0);
        open_sem(&sems->brewersSems[i], sems->brewerSemNames[i], 0);
    }
}

void destroy_brewmaster_sems(struct brewmaster_sems *sems) {
    destroy_sem(sems->agentSem, sems->agentSemName);
    destroy_sem(sems->resourceStateMutex, sems->resourceStateMutexName);
    for(int i = 0; i < RESOURCE_COUNT; i++) {
        destroy_sem(sems->resourceSems[i], sems->resourceSemNames[i]);
        destroy_sem(sems->brewersSems[i], sems->brewerSemNames[i]);
    }
}

// main function for agent thread; produces resources of a given type
// (or rather, produces all resource, except the one which it is explicitly missing)
int do_agent_work(int missing_resource) {
    for(int i = 0; i < 10; i++) {      // produce 10 times (arbitrarily chosen simulation count)
        sem_wait(sems.agentSem);
        usleep(250000);  // sleep .25 seconds (just to make printing of data slower)
        for(int j = 0; j < RESOURCE_COUNT; j++) {
            // e.g. if this thread is 0 = bezoars, this threads skips 0 but produces 1 and 2
            if(j != missing_resource) {
                printf("Agent produced:          %s\n", sems.resourceSemNames[j]+1);
                fflush(stdout);
                sem_post(sems.resourceSems[j]);
            }
        }
    }
    return EXIT_SUCCESS;
}
void *do_agent_work_threads(void *arg) {
    int missing_resource = *(int *)arg;
    do_agent_work(missing_resource);
    return NULL;
}


// main function for pusher thread; wakes up the appropriate brewer depending on which resources agent produces
int do_pusher_work(int assigned_resource) {
    for(int i = 0; i < (10 * (RESOURCE_COUNT - 1)); i++) {
        // wait for this resource to be produced by the agent, the update the state of what is "on the table"
        sem_wait(sems.resourceSems[assigned_resource]);
        sem_wait(sems.resourceStateMutex);
        resourceState.count++;
        resourceState.total += assigned_resource;
        // if this was the last resource from agent, wake up the brewer with the missing resource and reset state to 0
        if(resourceState.count == (RESOURCE_COUNT - 1)) {
            sem_post(sems.brewersSems[RESOURCE_TOTAL - resourceState.total]);
            resourceState.count = 0;
            resourceState.total = 0;
        }
        sem_post(sems.resourceStateMutex);
    }
    return EXIT_SUCCESS;
}
void *do_pusher_work_threads(void *arg) {
    int assigned_resource = *(int *)arg;
    do_pusher_work(assigned_resource);
    return NULL;
}

// main function for brewer thread; woken up by pusher, produces potion, wakes up agent the next batch of resources
int do_brewer_work(int assigned_resource) {
    for(int i = 0; i < 10; i++) {
        sem_wait(sems.brewersSems[assigned_resource]);
        printf("Potion by brewer with:   %s\n", sems.resourceSemNames[assigned_resource]+1);
        fflush(stdout);
        sem_post(sems.agentSem);
    }
    return EXIT_SUCCESS;
}
void *do_brewer_work_threads(void *arg) {
    int assigned_resource = *(int *)arg;
    do_brewer_work(assigned_resource);
    return NULL;
}

// run the brewmaster simulation
int run_brew_master_threads() {
    printf("[THREADS] Running the brew master simulation. 10 potions of each type will be produced.\n\n");
    init_brewmaster_sems(&sems);
    int resource[RESOURCE_COUNT] = {0, 1, 2};  // 0 = bezoars, 1 = unicorn_horns, 2 = mistletoe_berries
    // create all the threads
    pthread_t agents[RESOURCE_COUNT];  // the agent threads to produce resources
    pthread_t pushers[RESOURCE_COUNT];  // the pusher threads to interface between agent and brewers
    pthread_t brewers[RESOURCE_COUNT];  // the brewer threads to produce potions
    for (int i = 0; i < RESOURCE_COUNT; i++) {
        pthread_create(&agents[i], NULL, do_agent_work_threads, &resource[i]);
        pthread_create(&pushers[i], NULL, do_pusher_work_threads, &resource[i]);
        pthread_create(&brewers[i], NULL, do_brewer_work_threads, &resource[i]);
    }
    // join all the threads
    for (int i = 0; i < RESOURCE_COUNT; i++) {
        pthread_join(agents[i], NULL);
        pthread_join(pushers[i], NULL);
        pthread_join(brewers[i], NULL);
    }
    destroy_brewmaster_sems(&sems);
    return EXIT_SUCCESS;
}



// --------------------------------------------------------------------------------------------------------------------
// Brew Master's Problem: Processes -----------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------------------------
int do_agent_work_processes(int missing_resource) {return do_agent_work(missing_resource);}
int do_brewer_work_processes(int assigned_resource) {return do_brewer_work(assigned_resource);}
int do_pusher_work_processes(int assigned_resource, int pipeFD) {
    // wait for this resource to be produced by the agent, then push the resource into the pipe
    for(int i = 0; i < (10 * (RESOURCE_COUNT - 1)); i++) {
        sem_wait(sems.resourceSems[assigned_resource]);
        write(pipeFD, (void *)&assigned_resource, sizeof(int));
    }
    return EXIT_SUCCESS;
}
int do_aggregator_work_processes(int pipeFD) {
    // read the resources from the pipe, when a pair is found, signal the brewing w/ missing resource
    int count = 0;
    int total = 0;
    int produced_resource = 0;
    for(int i = 0; i < (10 * (RESOURCE_COUNT - 1) * RESOURCE_COUNT); i++) {
        read(pipeFD, &produced_resource, sizeof(int));
        count++;
        total += produced_resource;
        // if this was the 2nd resource, wake up the brewer with the missing resource and reset state to 0
        if(count == (RESOURCE_COUNT - 1)) {
            sem_post(sems.brewersSems[RESOURCE_TOTAL - total]);
            count = 0;
            total = 0;
        }
    }
    return EXIT_SUCCESS;
}

int run_brew_master_processes() {
    printf("[PROCESSES] Running the brew master simulation. 10 potions of each type will be produced.\n\n");
    init_brewmaster_sems(&sems);
    // create pipe which is used to transfer resource from the pushers to the aggregator
    int pipeFDs[2]; // pipeFDs[0] is read, pipeFDs[1] is write
    if (pipe(pipeFDs) == -1) {
        printf("Call to pipe() failed\n");
        exit(EXIT_FAILURE);
    }
    // create all the processes
    pid_t agents[RESOURCE_COUNT];  // the agent to produce resources
    pid_t pushers[RESOURCE_COUNT];  // the pusher to interface between agent and brewers
    pid_t brewers[RESOURCE_COUNT];  // the brewer to produce potions
    pid_t aggregator;
    for (int i = 0; i < RESOURCE_COUNT; i++) {
        if((agents[i] = fork()) == 0) return do_agent_work_processes(i);
        if((pushers[i] = fork()) == 0) return do_pusher_work_processes(i, pipeFDs[1]);
        if((brewers[i] = fork()) == 0) return do_brewer_work_processes(i);
    }
    if((aggregator = fork()) == 0) return do_aggregator_work_processes(pipeFDs[0]);
    // wait for all processes
    int wstatus;
    for (int i = 0; i < RESOURCE_COUNT; i++) {
        waitpid(agents[i], &wstatus, 0);
        waitpid(pushers[i], &wstatus, 0);
        waitpid(brewers[i], &wstatus, 0);
    }
    waitpid(aggregator, &wstatus, 0);
    destroy_brewmaster_sems(&sems);
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
    srand(time(NULL));
    if (argc < 3)
        return exit_and_print_instructions();
    // first flag must be -proc or -thread
    int is_thread;
    if(strncmp(argv[1], "-thread", 7) == 0) {
        is_thread = 1;
    } else if (strncmp(argv[1], "-proc", 5) == 0) {
        is_thread = 0;
    } else {
        return exit_and_print_instructions();
    }
    // next flags are for problem type
    if(strncmp(argv[2], "-p", 2) == 0) {
        // consumer producer problem
        if(argc < 7)
            return exit_and_print_instructions();
        if(strncmp(argv[3], "-n", 2) != 0 || strncmp(argv[5], "-c", 2) != 0)
            return exit_and_print_instructions();
        int producers = atoi(argv[4]);
        int consumers = atoi(argv[6]);
        return is_thread ? run_pc_threads(producers, consumers) : run_pc_processes(producers, consumers);
    } else if (strncmp(argv[2], "-d", 2) == 0) {
        // dining philosophers
        return is_thread ? run_dining_philosophers_threads() : run_dining_philosophers_processes();
    } else if (strncmp(argv[2], "-b", 2) == 0) {
        //
        return is_thread ? run_brew_master_threads() : run_brew_master_processes();
    } else {
        return exit_and_print_instructions();
    }
}
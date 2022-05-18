#include "stdlib.h"
#include "stdio.h"
#include "string.h"

#define INSTRUCTIONS "Instructions:\n\
-p: run the producer/consumer problem\n\
  -n {N}: number of producers (required if using -p)\n\
  -c {C}: number of consumers (required if using -p)\n\
-d: run the dining philosopher's problem\n\
-b: run the potion brewers problem\n"


int runConsumerProducer(int producers, int consumers) {
    printf("Producers %d Consumers %d", producers, consumers);
    return EXIT_SUCCESS;
}

int runDiningPhilosophers() {
    printf("Diners!");
    return EXIT_SUCCESS;
}

int runBrewMaster() {
    printf("Brew Masters!");
    return EXIT_SUCCESS;
}

int exitAndPrintInstructions() {
    printf("%s", INSTRUCTIONS);
    return EXIT_FAILURE;
}

int main(int argc, char *argv[]) {
    if (argc < 2)
        return exitAndPrintInstructions();
    // consumer producer problem
    if(strncmp(argv[1], "-p", 2) == 0) {
        if(argc < 6)
            return exitAndPrintInstructions();
        if(strncmp(argv[2], "-n", 2) != 0 || strncmp(argv[4], "-c", 2) != 0)
            return exitAndPrintInstructions();
        return runConsumerProducer(atoi(argv[3]), atoi(argv[5]));
    } else if (strncmp(argv[1], "-d", 2) == 0) {
        return runDiningPhilosophers();
    } else if (strncmp(argv[1], "-b", 2) == 0) {
        return runBrewMaster();
    } else {
        return exitAndPrintInstructions();
    }
}
CC=gcc

start_p: build
	./build/bairdjo_hw4 -p -n $(N) -c $(C)

start_d: build
	./build/bairdjo_hw4 -d

start_b: build
	./build/bairdjo_hw4 -b

build: bairdjo_hw4.c
	@$(CC) --std=gnu99 -Wall -pthread -o ./build/bairdjo_hw4 bairdjo_hw4.c

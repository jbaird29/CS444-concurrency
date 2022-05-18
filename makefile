CC=gcc

start: build
	@./build/bairdjo_hw4

build: bairdjo_hw4.c
	@$(CC) --std=gnu99 -Wall -pthread -o ./build/bairdjo_hw4 bairdjo_hw4.c

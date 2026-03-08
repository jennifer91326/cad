SHELL := /bin/bash

CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -pthread -ffast-math  
LDFLAGS = -pthread 



TARGET := sta
SRCS := parser.cpp step1.cpp step2.cpp main.cpp


TIME ?= /usr/bin/time -f "real=%E user=%U sys=%S maxrss=%MKB"
ARGS ?=            
R    ?= 5          

.PHONY: all clean run bench

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)


run: $(TARGET)
	$(TIME) ./$(TARGET) $(ARGS)



clean:
	rm -f $(TARGET) *.o *~ *.txt

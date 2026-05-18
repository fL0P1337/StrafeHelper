CXX = g++
CXXFLAGS = -std=c++20 -I../ -I./mock -g -Wall -fpermissive

SRCS = ../SuperglideLogic.cpp ../globals.h
TEST_SRCS = test_SuperglideLogic.cpp stub_utils.cpp stub_config.cpp stub_globals.cpp
OBJS = ../SuperglideLogic.o stub_globals.o test_SuperglideLogic.o stub_utils.o stub_config.o

all: test

test: $(OBJS)
	$(CXX) $(CXXFLAGS) -o test_runner $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) test_runner

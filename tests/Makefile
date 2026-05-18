CXX = g++
CXXFLAGS = -std=c++20 -I../ -I./mock -g -Wall -fpermissive -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE

OBJS = ../SuperglideLogic.o ../Utils.o ../Config.o stub_globals.o stub_logger.o test_SuperglideLogic.o test_Utils.o test_main.o

all: test_runner

test_runner: $(OBJS)
	$(CXX) $(CXXFLAGS) -o test_runner $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) test_runner

test: test_runner
	./test_runner

<<<<<<< HEAD
CXX = g++
<<<<<<< HEAD
CXXFLAGS = -std=c++20 -I../ -I./mock -g -Wall -fpermissive -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE

SRCS = ../SuperglideLogic.cpp ../Utils.cpp ../globals.h ../Config.cpp
TEST_SRCS = test_SuperglideLogic.cpp test_Utils.cpp stub_globals.cpp stub_logger.cpp
OBJS = ../SuperglideLogic.o ../Utils.o ../Config.o stub_globals.o stub_logger.o test_SuperglideLogic.o test_Utils.o test_main.o

all: test_runner

test_runner: $(OBJS)
	$(CXX) $(CXXFLAGS) -o test_runner $(OBJS)

test_main.o: test_main.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) test_runner
=======
CXXFLAGS = -std=c++20 -I. -I./mock -pthread
TARGETS = test_superglide

all: $(TARGETS)

test_superglide: test_superglide.cpp ../SuperglideLogic.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f $(TARGETS)

test: all
	./test_superglide
>>>>>>> origin/jules-13692354564841379828-b7768257
=======
# Mock Windows tests makefile

CXX = g++
CXXFLAGS = -std=c++20 -I.. -I./mock -D_MSC_VER=1900 -Wno-narrowing

TEST_SRCS = ConfigTests.cpp ../Logger.cpp ./mock/UtilsMock.cpp
TEST_BINS = ConfigTests

test: $(TEST_BINS)
	./ConfigTests

ConfigTests: $(TEST_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f $(TEST_BINS)
>>>>>>> origin/add-config-tests-11195440746030750129

<<<<<<< HEAD
<<<<<<< HEAD
<<<<<<< HEAD
<<<<<<< HEAD
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

=======
CXX = g++
CXXFLAGS = -std=c++20 -I../ -I./mock -g -Wall -fpermissive

SRCS = ../SuperglideLogic.cpp ../globals.h
TEST_SRCS = test_SuperglideLogic.cpp stub_utils.cpp stub_config.cpp stub_globals.cpp
OBJS = ../SuperglideLogic.o stub_globals.o test_SuperglideLogic.o stub_utils.o stub_config.o

all: test

test: $(OBJS)
	$(CXX) $(CXXFLAGS) -o test_runner $(OBJS)

>>>>>>> origin/add-superglide-tests-17935704524920767383
=======
CXX = g++
CXXFLAGS = -std=c++20 -I../ -I./mock -Wall -Wextra

SOURCES = test_config.cpp ../Logger.cpp mock/Utils.cpp
OBJECTS = $(SOURCES:.cpp=.o)

test: test_runner
	./test_runner

test_runner: $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

>>>>>>> origin/jules-10012660180567200711-714cdd0a
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
<<<<<<< HEAD
	rm -f $(OBJS) test_runner
<<<<<<< HEAD
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
=======
>>>>>>> origin/add-superglide-tests-17935704524920767383
=======
CXX ?= g++
CXXFLAGS = -std=c++20 -I../ -I./mock -g -O0

SRCS = ConfigTest.cpp ./mock/Utils.cpp ./mock/Logger.cpp
OBJS = $(SRCS:.cpp=.o)

test: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^
	./test

clean:
	rm -f $(OBJS) test

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
>>>>>>> origin/jules-12324778450551743726-20657356
=======
CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -I. -I.. -Imock

SRCS = test_utils.cpp ../Utils.cpp ../Config.cpp mock/windows_mock.cpp ../Logger.cpp
OBJS = $(SRCS:.cpp=.o)

TARGET = test_utils

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
>>>>>>> origin/jules-tests-virtualkeytoscan-7065570391822414374
=======
	rm -f $(OBJECTS) test_runner
>>>>>>> origin/jules-10012660180567200711-714cdd0a

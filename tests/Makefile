CXX = g++
CXXFLAGS = -std=c++20 -I../ -I./mock -g -Wall -fpermissive -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE

OBJS = ../EventDispatcher.o ../KeybindManager.o ../SuperglideLogic.o ../Utils.o ../Config.o ../Logger.o ../PrecisionTimer.o stub_dispatch_dependencies.o stub_globals.o test_superglide.o test_utils.o test_config_validation.o test_input_state.o test_main.o

all: test_runner

test_runner: $(OBJS)
	$(CXX) $(CXXFLAGS) -o test_runner $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) test_runner

test: test_runner
	./test_runner

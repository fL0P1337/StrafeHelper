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

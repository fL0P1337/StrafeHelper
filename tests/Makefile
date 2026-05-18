CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -I.. -I./mock -g
LDFLAGS = -pthread

SRCS = test_main.cpp test_utils.cpp ../Utils.cpp mock/ConfigMock.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = test_runner

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

test: $(TARGET)
	./$(TARGET)

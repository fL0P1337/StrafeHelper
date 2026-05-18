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

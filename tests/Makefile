CXX = g++
CXXFLAGS = -std=c++20 -I../ -I./mock -Wall -Wextra

SOURCES = test_config.cpp ../Logger.cpp mock/Utils.cpp
OBJECTS = $(SOURCES:.cpp=.o)

test: test_runner
	./test_runner

test_runner: $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) test_runner

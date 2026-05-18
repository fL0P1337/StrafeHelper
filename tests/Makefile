CXX = g++
CXXFLAGS = -std=c++20 -I. -I./mock -pthread
TARGETS = test_superglide

all: $(TARGETS)

test_superglide: test_superglide.cpp ../SuperglideLogic.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f $(TARGETS)

test: all
	./test_superglide

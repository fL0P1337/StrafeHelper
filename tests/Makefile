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

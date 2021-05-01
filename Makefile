CXXSOURCES = main.cpp 
CXX = g++
CXXFLAGS = -lstdc++fs

all: serwer

serwer: 
	$(CXX) $(CXXSOURCES) $(CXXFLAGS) -o serwer

.PHONY: clean
clean:
	rm -rf *.o serwer

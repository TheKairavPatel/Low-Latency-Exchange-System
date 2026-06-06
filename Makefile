CXX = g++
FLAGS = -O3 -march=native -fno-exceptions -fno-rtti -std=c++20 -flto -funroll-loops

SRCS = src/engine/engine.cpp \
       src/engine/priceLevel.cpp \
       src/gateway/Gateway.cpp \
       src/gateway/Queues.cpp

linux-multi:
	$(CXX) $(FLAGS) -o build/LinuxMulti $(SRCS) bench/LinuxMulti.cpp -lpthread

linux-single:
	$(CXX) $(FLAGS) -o build/LinuxSingle $(SRCS) bench/LinuxSingleThread.cpp -lpthread

windows-single:
	$(CXX) $(FLAGS) -o build/WindowsSingle $(SRCS) bench/WindowsSingleThread.cpp -lpthread

tests:
	$(CXX) $(FLAGS) -o build/test $(SRCS) tests/test.cpp -lpthread

clean:
	rm -rf build/*
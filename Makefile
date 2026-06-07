CXX = g++
FLAGS = -O3 -march=native -fno-exceptions -fno-rtti -std=c++20 -flto -funroll-loops

SRCS = src/engine/engine.cpp \
       src/engine/priceLevel.cpp \
       src/gateway/Gateway.cpp \
       src/gateway/Queues.cpp

linux-multi:
	$(CXX) $(FLAGS) -o build/LinuxMulti $(SRCS) bench/LinuxMulti.cpp -lpthread

linux-single:
	$(CXX) $(FLAGS) -o build/LinuxSingle $(SRCS) bench/LinuxSingle.cpp -lpthread

windows-single:
	$(CXX) $(FLAGS) -o build/WindowsSingle $(SRCS) bench/WindowsSingle.cpp -lpthread

live-demo:
	$(CXX) $(FLAGS) -o build/LiveDemo src/engine/engine.cpp src/engine/priceLevel.cpp src/gateway/Gateway.cpp src/gateway/Queues.cpp src/snapshot/SnapshotWriter.cpp bench/LiveDemo.cpp -lpthread

clean:
	rm -rf build/*
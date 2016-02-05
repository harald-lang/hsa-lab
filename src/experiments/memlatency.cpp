#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>
#include <sstream>
#include <unistd.h>
static int64_t numProcessors() {
	int64_t n = sysconf(_SC_NPROCESSORS_ONLN);
	if (n < 1) {
		std::cout << "Failed to determine the number of processors." << std::endl;
		n = 1;
	}
	return n;
}

static void setAffinity(const uint32_t coreNum) {
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(coreNum % numProcessors(), &mask);
	int result = sched_setaffinity(0, sizeof(mask), &mask);
	if (result != 0) {
		std::cout << "Failed to set CPU affinity." << std::endl;
	}
}


void pingPong(std::atomic<uint32_t>& mem, const uint32_t waitFor, const uint32_t reply,
		const size_t repeats, const uint32_t threadAffinity) {
	setAffinity(threadAffinity);
	for (size_t r = 0; r < repeats; r++) {
		while (mem != waitFor) { /* busy wait */ }
		mem = reply;
	}
}

void pingPongBenchmark(const uint32_t thread1Affinity, const uint32_t thread2Affinity, const size_t numRepeats) {
	std::cout << "ping pong" << std::endl;
	std::atomic<uint32_t> av = { 0 };
	std::chrono::time_point<std::chrono::high_resolution_clock> start, end;
	start = std::chrono::high_resolution_clock::now();
	std::thread t1(pingPong, std::ref(av), 0, 1, numRepeats, thread1Affinity);
	std::thread t2(pingPong, std::ref(av), 1, 0, numRepeats, thread2Affinity);
	t1.join();
	t2.join();
	end = std::chrono::high_resolution_clock::now();
	auto timeDiff = end - start;
	std::cout << thread1Affinity << ", "  << thread2Affinity << ", " <<
			(std::chrono::duration<double, std::nano>(end-start).count() / numRepeats) << " ns" << std::endl;
}

int main() {
	const size_t numRepeats = 1*1024*1024;
	for (uint32_t thread1Affinity = 0; thread1Affinity < numProcessors(); thread1Affinity++) {
		for (uint32_t thread2Affinity = 0; thread2Affinity < numProcessors(); thread2Affinity++) {
			if (thread1Affinity == thread2Affinity) continue;
			pingPongBenchmark(thread1Affinity, thread2Affinity, numRepeats);
		}
	}
}

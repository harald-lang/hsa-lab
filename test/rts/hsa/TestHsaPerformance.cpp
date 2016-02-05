#include "gtest/gtest.h"
#include <sstream>
#include <rts/hsa/HsaContext.hpp>
#include <rts/hsa/HsaException.hpp>
#include <rts/hsa/HsaRuntime.hpp>
#include <rts/hsa/HsaUtils.hpp>
#include <rts/hsa/KernelArgs.hpp>
#include <fstream>
#include <memory>
#include <atomic>
#include <iostream>
#include <chrono>
#include <thread>
//---------------------------------------------------------------------------
// SIM[DT] Lab
// (c) Harald Lang 2015
//---------------------------------------------------------------------------
namespace {

using namespace std;
using namespace rts::hsa;

/// Cycle counter on x86
static inline uint64_t rdtsc() {
#if defined(__x86_64__) && defined(__GNUC__)
   uint32_t hi,lo;
   __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
   return static_cast<uint64_t>(lo)|(static_cast<uint64_t>(hi)<<32);
#else
   return 0;
#endif
}


/// Returns the CPU cycles spent in execution of the given function
static uint64_t cpuCycles(function<void(void)> fn) {
   uint64_t counterBegin=rdtsc();
   fn();
   return rdtsc()-counterBegin;
}

static double clock(function<void(void)> fn) {
	std::chrono::time_point<std::chrono::high_resolution_clock> start, end;
	start = std::chrono::high_resolution_clock::now();
	fn();
	end = std::chrono::high_resolution_clock::now();
	return std::chrono::duration<double, milli>(end-start).count();
}


static string loadFromFile(const string& filename) {
	ifstream file(filename, ifstream::binary);
	if (file.fail()) {
		throw "couldn't open file: " + filename;
	}
	string contents((istreambuf_iterator<char>(file)), (istreambuf_iterator<char>()));
	if (contents.substr(0, 8) != "HSA BRIG") {
		throw "invalid magic number";
	}
	return contents;
}


TEST(HsaPerformance, DispatchSync) {
	HsaRuntime rt;
	rt.initialize();
	HsaContext ctx(rt);

	const string module1 = loadFromFile("bin/test/rts/hsa/kernel/Nothing.brig");

	ctx.addModule(module1.c_str());
	ctx.finalize();
	ctx.createQueue();

	const size_t n = 128;
	size_t* output = new size_t[n];
	memset(output,0, n * sizeof(size_t));

	const std::string kernelName = "&__OpenCL_nothing_kernel";
	const auto kernelObject = ctx.getKernelObject(kernelName);

	const size_t repeats = 8;
	const double duration = clock([&] {
		for (size_t r = 0; r < repeats; r++) {
			for (size_t i = 0; i < n; i++) {
				ctx.dispatch<size_t*, size_t>(kernelObject, 1, &output[i], i);
			}
		}
	});
	cout << "milliseconds/dispatch = " << (duration/(n*repeats)) << endl;
	cout << "dispatches/seconds = " << ((n*repeats)/duration)*1000 << endl;

	const uint64_t cycles = cpuCycles([&] {
		for (size_t r = 0; r < repeats; r++) {
			for (size_t i = 0; i < n; i++) {
				ctx.dispatch<size_t*, size_t>(kernelObject, 1, &output[i], i);
			}
		}
	});
	cout << "cycles/dispatch = " << (cycles/(n*repeats)) << endl;

	delete [] output;
	rt.shutDown();
}


TEST(HsaPerformance, DispatchAsync) {
	HsaRuntime rt;
	rt.initialize();
	HsaContext ctx(rt);

	const string module1 = loadFromFile("bin/test/rts/hsa/kernel/Nothing.brig");

	ctx.addModule(module1.c_str());
	ctx.finalize();
	ctx.createQueue();

	constexpr size_t n = 128;
	size_t* output = new size_t[n];
	memset(output,0, n * sizeof(size_t));

	const std::string kernelName = "&__OpenCL_nothing_kernel";
	const auto kernelObject = ctx.getKernelObject(kernelName);

	const size_t repeats = 8;
	const double duration = clock([&] {
		for (size_t r = 0; r < repeats; r++) {
			uint64_t handles[n];
			HsaContext::Future* tasks = reinterpret_cast<HsaContext::Future*>(&handles);
			for (size_t i = 0; i < n; i++) {
				tasks[i] = ctx.dispatchAsync<size_t*, size_t>(kernelObject, 1, &output[i], i);
			}
			for (size_t i = 0; i < n; i++) {
				tasks[i].wait();
			}
		}
	});
	cout << "milliseconds/dispatch = " << (duration/(n*repeats)) << endl;
	cout << "dispatches/seconds = " << ((n*repeats)/duration)*1000 << endl;

	delete [] output;
	rt.shutDown();
}


TEST(HsaPerformance, DispatchBatch) {
	HsaRuntime rt;
	rt.initialize();
	HsaContext ctx(rt);

	const string module1 = loadFromFile("bin/test/rts/hsa/kernel/Nothing.brig");

	ctx.addModule(module1.c_str());
	ctx.finalize();
	ctx.createQueue();

	constexpr size_t n = 256;
	size_t* output = new size_t[n];
	memset(output,0, n * sizeof(size_t));

	const std::string kernelName = "&__OpenCL_nothing_kernel";
	const auto kernelObject = ctx.getKernelObject(kernelName);

	const size_t repeats = 8;
	const double duration = clock([&] {
		for (size_t r = 0; r < repeats; r++) {
			auto dispatchCycles = cpuCycles([&] {
				for (size_t i = 0; i < n; i++) {
					ctx.dispatchBatch<size_t*, size_t>(kernelObject, 1, &output[i], i);
				}
			});
			auto waitCycles = cpuCycles([&] {
				ctx.waitForBatchCompletion();
			});
			cout << "dispatch-cycles = " << (dispatchCycles / n) << ", wait-cycles = " << (waitCycles / n) << endl;
		}
	});
	cout << "milliseconds/dispatch = " << (duration/(n*repeats)) << endl;
	cout << "dispatches/seconds = " << ((n*repeats)/duration)*1000 << endl;

	delete [] output;
	rt.shutDown();
}

TEST(HsaPerformance, DispatchBatchDelayedExecution) {
	HsaRuntime rt;
	rt.initialize();
	HsaContext ctx(rt);

	const string module1 = loadFromFile("bin/test/rts/hsa/kernel/Nothing.brig");

	ctx.addModule(module1.c_str());
	ctx.finalize();
	ctx.createQueue();

	constexpr size_t n = 256;
	size_t* output = new size_t[n];
	memset(output,0, n * sizeof(size_t));

	const std::string kernelName = "&__OpenCL_nothing_kernel";
	const auto kernelObject = ctx.getKernelObject(kernelName);

	const size_t repeats = 8;
	const double duration = clock([&] {
		for (size_t r = 0; r < repeats; r++) {
			uint64_t packetId;
			auto enqueueCycles = cpuCycles([&] {
				for (size_t i = 0; i < n; i++) {
					packetId = ctx.enqueueForBatchProcessing<size_t*, size_t>(kernelObject, 1, &output[i], i);
				}
			});
			auto doorbellCycles = cpuCycles([&] {
				ctx.ringDoorbell(packetId);
			});
			auto executionCycles = cpuCycles([&] {
				ctx.waitForBatchCompletion();
			});
			cout << "enqueue-cycles = " << (enqueueCycles / n)
				 << ", execution-cycles = " << (executionCycles / n)
				 << ", doorbell-cycles = " << doorbellCycles
				 << endl;
		}
	});
	cout << "milliseconds/dispatch = " << (duration/(n*repeats)) << endl;
	cout << "dispatches/seconds = " << ((n*repeats)/duration)*1000 << endl;

	delete [] output;
	rt.shutDown();
}


TEST(HsaPerformance, DispatchBusyWait) {
	HsaRuntime rt;
	rt.initialize();
	HsaContext ctx(rt);

	const string module1 = loadFromFile("bin/test/rts/hsa/kernel/NothingBusyWait.brig");

	ctx.addModule(module1.c_str());
	ctx.finalize();
	ctx.createQueue();

	const std::string kernelName = "&__OpenCL_nothing_kernel";
	const auto kernelObject = ctx.getKernelObject(kernelName);

	atomic<int32_t> controlSignal = { 1 };
	atomic<int32_t> completionSignal = { 0 };
	HsaContext::Future kernelTask = ctx.dispatchAsync<atomic<int32_t>*, atomic<int32_t>*>(
			kernelObject, 1, &controlSignal, &completionSignal);
	// wait for the kernel to start
	double durationFirstStart = clock([&] {
		while (controlSignal!=0) { /* busy wait */ };
	});
	cout << "startup time = " << durationFirstStart << " ms" << endl;
	completionSignal = 0;

	const size_t repeats = 1*1024;

	double duration = 0;
	for (size_t i = 0; i < repeats; i++) {
		completionSignal = 0;
		controlSignal = 1;
		duration += clock([&] {
			while (controlSignal!=0) { /* busy wait */ };
		});
	}
	cout << "send termination signal" << endl;
	controlSignal = 255;
	kernelTask.wait();

	cout << "milliseconds/dispatch = " << (duration/(repeats)) << endl;
	cout << "dispatches/seconds = " << ((repeats)/duration)*1000 << endl;

	rt.shutDown();
}

} // namespace

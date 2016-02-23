#include "gtest/gtest.h"
#include <rts/hsa/HsaContext.hpp>
#include <rts/hsa/HsaException.hpp>
#include <rts/hsa/HsaRuntime.hpp>
#include <rts/hsa/HsaUtils.hpp>
#include <rts/hsa/KernelArgs.hpp>
#include <utils/Utils.hpp>
#include <atomic>
#include <fstream>
#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
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
   uint32_t hi, lo;
   __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
   return static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
#else
   return 0;
#endif
}

/// Returns the CPU cycles spent in execution of the given function
static uint64_t cpuCycles(function<void(void)> fn) {
   uint64_t counterBegin = rdtsc();
   fn();
   return rdtsc() - counterBegin;
}

static double clockSec(function<void(void)> fn) {
   std::chrono::time_point<std::chrono::high_resolution_clock> start, end;
   start = std::chrono::high_resolution_clock::now();
   fn();
   end = std::chrono::high_resolution_clock::now();
   return std::chrono::duration<double>(end - start).count();
}

static double clock(function<void(void)> fn) {
   std::chrono::time_point<std::chrono::high_resolution_clock> start, end;
   start = std::chrono::high_resolution_clock::now();
   fn();
   end = std::chrono::high_resolution_clock::now();
   return std::chrono::duration<double, milli>(end - start).count();
}

static double clockMicro(function<void(void)> fn) {
   std::chrono::time_point<std::chrono::high_resolution_clock> start, end;
   start = std::chrono::high_resolution_clock::now();
   fn();
   end = std::chrono::high_resolution_clock::now();
   return std::chrono::duration<double, micro>(end - start).count();
}

static double clockNano(function<void(void)> fn) {
   std::chrono::time_point<std::chrono::high_resolution_clock> start, end;
   start = std::chrono::high_resolution_clock::now();
   fn();
   end = std::chrono::high_resolution_clock::now();
   return std::chrono::duration<double, nano>(end - start).count();
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

template<typename T>
static T* mallocHuge(size_t n) {
   void* p = mmap(NULL, n * sizeof(T), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   madvise(p, n * sizeof(T), MADV_HUGEPAGE);
   return reinterpret_cast<T*>(p);
}

template<typename T>
static void freeHuge(T* ptr,size_t n) {
   munmap(ptr,n * sizeof(T));
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
   memset(output, 0, n * sizeof(size_t));

   const std::string kernelName = "&__OpenCL_nothing_kernel";
   const auto kernelObject = ctx.getKernelObject(kernelName);

   const size_t repeats = 8;
   const double duration = clock([&] {
      for (size_t r = 0; r < repeats; r++) {
         for (size_t i = 0; i < n; i++) {
            ctx.dispatch<size_t*, size_t>(kernelObject, {1, 128}, &output[i], i);
         }
      }
   });
   cout << "milliseconds/dispatch = " << (duration / (n * repeats)) << endl;
   cout << "dispatches/seconds = " << ((n * repeats) / duration) * 1000 << endl;

   const uint64_t cycles = cpuCycles([&] {
      for (size_t r = 0; r < repeats; r++) {
         for (size_t i = 0; i < n; i++) {
            ctx.dispatch<size_t*, size_t>(kernelObject, {1, 128}, &output[i], i);
         }
      }
   });
   cout << "cycles/dispatch = " << (cycles / (n * repeats)) << endl;

   delete[] output;
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
   memset(output, 0, n * sizeof(size_t));

   const std::string kernelName = "&__OpenCL_nothing_kernel";
   const auto kernelObject = ctx.getKernelObject(kernelName);

   const size_t repeats = 8;
   const double duration = clock([&] {
      for (size_t r = 0; r < repeats; r++) {
         uint64_t handles[n];
         HsaContext::Future* tasks = reinterpret_cast<HsaContext::Future*>(&handles);
         for (size_t i = 0; i < n; i++) {
            tasks[i] = ctx.dispatchAsync<size_t*, size_t>(kernelObject, {1, 128}, &output[i], i);
         }
         for (size_t i = 0; i < n; i++) {
            tasks[i].wait();
         }
      }
   });
   cout << "milliseconds/dispatch = " << (duration / (n * repeats)) << endl;
   cout << "dispatches/seconds = " << ((n * repeats) / duration) * 1000 << endl;

   delete[] output;
   rt.shutDown();
}

TEST(HsaPerformance, DISABLED_DispatchBatch) {
   HsaRuntime rt;
   rt.initialize();
   HsaContext ctx(rt);

   const string module1 = loadFromFile("bin/test/rts/hsa/kernel/Nothing.brig");

   ctx.addModule(module1.c_str());
   ctx.finalize();
   ctx.createQueue();

   constexpr size_t n = 256;
   size_t* output = new size_t[n];
   memset(output, 0, n * sizeof(size_t));

   const std::string kernelName = "&__OpenCL_nothing_kernel";
   const auto kernelObject = ctx.getKernelObject(kernelName);

   const size_t repeats = 8;
   const double duration = clock([&] {
      for (size_t r = 0; r < repeats; r++) {
         auto dispatchCycles = cpuCycles([&] {
                  for (size_t i = 0; i < n; i++) {
                     ctx.dispatchBatch<size_t*, size_t>(kernelObject, {1, 1}, &output[i], i);
                  }
               });
         auto waitCycles = cpuCycles([&] {
                  ctx.waitForBatchCompletion();
               });
         cout << "dispatch-cycles = " << (dispatchCycles / n) << ", wait-cycles = " << (waitCycles / n) << endl;
      }
   });
   cout << "milliseconds/dispatch = " << (duration / (n * repeats)) << endl;
   cout << "dispatches/seconds = " << ((n * repeats) / duration) * 1000 << endl;

   delete[] output;
   rt.shutDown();
}

TEST(HsaPerformance, DISABLED_DispatchBatchDelayedExecution) {
   HsaRuntime rt;
   rt.initialize();
   HsaContext ctx(rt);

   const string module1 = loadFromFile("bin/test/rts/hsa/kernel/Nothing.brig");

   ctx.addModule(module1.c_str());
   ctx.finalize();
   ctx.createQueue();

   constexpr size_t n = 256;
   size_t* output = new size_t[n];
   memset(output, 0, n * sizeof(size_t));

   const std::string kernelName = "&__OpenCL_nothing_kernel";
   const auto kernelObject = ctx.getKernelObject(kernelName);

   const size_t repeats = 8;
   const double duration = clock([&] {
      for (size_t r = 0; r < repeats; r++) {
         uint64_t packetId;
         auto enqueueCycles = cpuCycles([&] {
                  for (size_t i = 0; i < n; i++) {
                     packetId = ctx.enqueueForBatchProcessing<size_t*, size_t>(kernelObject, {1, 128}, &output[i], i);
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
   cout << "milliseconds/dispatch = " << (duration / (n * repeats)) << endl;
   cout << "dispatches/seconds = " << ((n * repeats) / duration) * 1000 << endl;

   delete[] output;
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

   atomic<int32_t> controlSignal = {1};
   atomic<int32_t> completionSignal = {0};
   HsaContext::Future kernelTask = ctx.dispatchAsync<atomic<int32_t>*, atomic<int32_t>*>(kernelObject, {1, 128}, &controlSignal, &completionSignal);
   // wait for the kernel to start
   double durationFirstStart = clock([&] {
      while (controlSignal!=0) { /* busy wait */};
   });
   cout << "startup time = " << durationFirstStart << " ms" << endl;
   completionSignal = 0;

   const size_t repeats = 1 * 1024;

   double duration = 0;
   for (size_t i = 0; i < repeats; i++) {
      completionSignal = 0;
      controlSignal = 1;
      duration += clock([&] {
         while (controlSignal!=0) { /* busy wait */};
      });
   }
   cout << "send termination signal" << endl;
   controlSignal = 255;
   kernelTask.wait();

   cout << "milliseconds/dispatch = " << (duration / (repeats)) << endl;
   cout << "dispatches/seconds = " << ((repeats) / duration) * 1000 << endl;

   rt.shutDown();
}

TEST(HsaPerformance, DISABLED_SimtUtilizationWorkitems) {
   HsaRuntime rt;
   rt.initialize();
   HsaContext ctx(rt);

   const string module1 = loadFromFile("bin/test/rts/hsa/kernel/SimtUtil.brig");

   ctx.addModule(module1.c_str());
   ctx.finalize();
   ctx.createQueue();

   const size_t sizeInMiB = 1;
   constexpr size_t n = (sizeInMiB * 1024 * 1024) / sizeof(uint64_t);
   cout << n << endl;
   uint64_t* input = mallocHuge<uint64_t>(n * sizeof(uint64_t));
   uint64_t* output = mallocHuge<uint64_t>(n * sizeof(uint64_t));
   for (size_t i = 0; i < n;  i++) {
      input[i] = i;
   }
   memset(output, 0, n * sizeof(uint64_t));

   const std::string kernelName = "&__OpenCL_simtUtilization_kernel";
   const auto kernelObject = ctx.getKernelObject(kernelName);

   const size_t repeats = 128;
   const size_t numThreads = 1024 * 32;
   for (uint16_t w = 8; w <= 1024; w <<= 1) {
      for (uint32_t active = w; active >  0; active -= 4) {
         cout << w << "|" << active << "|" << flush;
         const double duration = clock([&] {
            for (size_t r = 0; r < repeats; r++) {
               ctx.dispatch<uint64_t*, uint64_t*, uint32_t, uint32_t>(kernelObject, {n, w}, input, output, n, active);
            }
         });
         cout << (((n * repeats) / (duration * 1000) / 1024.0) ) << endl;
      }
   }

   freeHuge(input, n);
   freeHuge(output, n);
   rt.shutDown();
}


TEST(HsaPerformance, DISABLED_SimtUtilizationLoop) {
   HsaRuntime rt;
   rt.initialize();
   HsaContext ctx(rt);

   const string module1 = loadFromFile("bin/test/rts/hsa/kernel/SimtUtil.brig");

   ctx.addModule(module1.c_str());
   ctx.finalize();
   ctx.createQueue();

   const size_t sizeInMiB = 1;
   constexpr size_t n = (sizeInMiB * 1024 * 1024) / sizeof(uint64_t);
   cout << n << endl;
   uint64_t* input = mallocHuge<uint64_t>(n);
   uint64_t* output = mallocHuge<uint64_t>(n);
   for (size_t i = 0; i < n;  i++) {
      input[i] = i;
   }
   memset(output, 0, n * sizeof(uint64_t));

   const std::string kernelName = "&__OpenCL_simtUtilizationLoop_kernel";
   const auto kernelObject = ctx.getKernelObject(kernelName);

   const size_t repeats = 128;
   const size_t numThreads = 1024 * 32;
   for (uint16_t w = 8; w <= 1024; w <<= 1) {
      for (uint32_t active = w; active >  0; active -= 4) {
         cout << w << "|" << active << "|" << flush;
         const double duration = clock([&] {
            for (size_t r = 0; r < repeats; r++) {
               ctx.dispatch<uint64_t*, uint64_t*, uint32_t, uint32_t>(kernelObject, {numThreads, w}, input, output, n, active);
            }
         });
         cout << (((n * repeats) / (duration * 1000) / 1024.0) ) << endl;
      }
   }

   freeHuge(input, n);
   freeHuge(output, n);
   rt.shutDown();
}


TEST(HsaPerformance, SeqRead) {
   HsaRuntime rt;
   rt.initialize();
   HsaContext ctx(rt);

   const string module1 = loadFromFile("bin/test/rts/hsa/kernel/Sum.brig");

   ctx.addModule(module1.c_str());
   ctx.finalize();
   ctx.createQueue();

   const size_t sizeInMiB = 1024;
   const size_t minNumGpuThreads = 512;
   const size_t maxNumGpuThreads = 64*8*1024;
   constexpr size_t n = (sizeInMiB * 1024 * 1024) / sizeof(uint32_t);
   cout << n << endl;
   uint32_t* input = mallocHuge<uint32_t>(n);
   uint64_t* output = mallocHuge<uint64_t>(n);
   uint64_t expectedResult = 0;
   for (size_t i = 0; i < n; i++) {
      input[i] = i;
      expectedResult += input[i];
   }
   for (uint64_t i = 0; i < maxNumGpuThreads; i++) {
      output[i] = 0;
   }

   {
      // CPU performance
      const size_t repeats = 10;
      const double duration = clockSec([&] {
         for (size_t r = 0; r < repeats; r++) {
            output[0] = 0;
            for (size_t i = 0; i < n; i++) {
               output[0] += input[i];
            }
         }
      });
      if (output[0] != expectedResult) {
         cout << "[CPU] validation failed: expected " << expectedResult << ", but got " << output[0] << endl;
      }
      cout << "CPU single-threaded: " << ((sizeInMiB / 1024.0) * repeats) / duration << " [GiB/s]" << endl;
      // clear results
      for (uint64_t i = 0; i < maxNumGpuThreads; i++) {
         output[i] = 0;
      }
   }

   memset(output, 0, maxNumGpuThreads * sizeof(uint64_t));

   const std::string kernelName = "&__OpenCL_sumLoop_kernel";
   const auto kernelObject = ctx.getKernelObject(kernelName);

   cout << "workgroupSize|#threads|elements/thread|throughput[GiB/sec]"<< endl;
   const size_t repeats = 1;
   for (uint16_t w = 32; w <= 1024; w <<= 1) {
      for (uint32_t t = minNumGpuThreads; t <= maxNumGpuThreads; t <<= 1) {
         cout << w << "|" << t << "|" << flush;
         const double duration = clockSec([&] {
            for (size_t r = 0; r < repeats; r++) {
               ctx.dispatch<uint32_t*, uint64_t*, size_t>(kernelObject, {t, w}, input, output, n);
            }
         });
         cout << (n / t) << '|';
         cout << ((sizeInMiB / 1024.0) * repeats) / duration << endl;

         // validate results
         uint64_t val = 0;
         for (uint64_t i = 0; i < maxNumGpuThreads; i++) {
            val += output[i];
         }
         if (val != expectedResult) {
            cout << "validation failed: expected " << expectedResult << ", but got " << val << endl;
         }
         // clear results
         for (uint64_t i = 0; i < maxNumGpuThreads; i++) {
            output[i] = 0;
         }
      }
   }

   freeHuge(input, n);
   freeHuge(output, n);
   rt.shutDown();
}

TEST(HsaPerformance, GroupReduction) {
   HsaRuntime rt;
   rt.initialize();
   HsaContext ctx(rt);

   const string module1 = loadFromFile("bin/test/rts/hsa/kernel/Sum.brig");

   ctx.addModule(module1.c_str());
   ctx.finalize();
   ctx.createQueue();

   const size_t sizeInMiB = 1024;

   constexpr size_t n = (sizeInMiB * 1024 * 1024) / sizeof(uint32_t);
   cout << n << endl;

   uint32_t* input = mallocHuge<uint32_t>(n);
   uint64_t* output = mallocHuge<uint64_t>(n);
   uint64_t expectedResult = 0;
   for (size_t i = 0; i < n; i++) {
      input[i] = i;
      expectedResult += input[i];
   }

   const std::string kernelName = "&__OpenCL_sumGroupReductionHand_kernel";
   const auto kernelObject = ctx.getKernelObject(kernelName);

   cout << "workgroupSize|#groups|throughput[GiB/sec]"<< endl;

   // vary workgroup size
   for (uint16_t w = 32; w <= 128; w <<= 1) {

      // clear output
      memset(output, 0, n * sizeof(uint64_t));

      const size_t repeats = 5;
      cout << w << "|" << n/w << '|' << flush;
      const double duration = clockSec([&] {
         for (size_t r = 0; r < repeats; r++) {
            ctx.dispatch<uint32_t*, uint64_t*>(kernelObject, {n, w}, input, output);
         }
      });
      cout << ((sizeInMiB / 1024.0) * repeats) / duration << endl;

//         for(int i = 0; i < 1024; i++) {
//            cout << output[i] << ", ";
//         }
//         cout << endl;

      // validate results
      uint64_t val = 0;
      for (uint64_t i = 0; i < n; i++) {
         val += output[i];
      }
      if (val != expectedResult) {
         cout << "validation failed: expected " << expectedResult << ", but got " << val << endl;
      }
   }

   freeHuge(input, n);
   freeHuge(output, n);
   rt.shutDown();
}

TEST(HsaPerformance, GroupReductionLoop) {
   HsaRuntime rt;
   rt.initialize();
   HsaContext ctx(rt);

   const string module1 = loadFromFile("bin/test/rts/hsa/kernel/Sum.brig");

   ctx.addModule(module1.c_str());
   ctx.finalize();
   ctx.createQueue();

   const size_t sizeInMiB = 1024;

   constexpr size_t n = (sizeInMiB * 1024 * 1024) / sizeof(uint32_t);
   cout << n << endl;

   uint32_t* input = mallocHuge<uint32_t>(n);
   uint64_t* output = mallocHuge<uint64_t>(n);
   uint64_t expectedResult = 0;
   for (size_t i = 0; i < n; i++) {
      input[i] = i;
      expectedResult += input[i];
   }

   const std::string kernelName = "&__OpenCL_sumGroupReductionHandLoop_kernel";
   const auto kernelObject = ctx.getKernelObject(kernelName);

   cout << "#threads|elements/thread|workgroupSize|#groups|throughput[GiB/sec]"<< endl;

   // vary number of threads
   for (uint32_t t = 512; t <= 512*64; t <<= 1) {

      // vary workgroup size
      for (uint16_t w = 64; w <= 64; w <<= 1) {

         // clear output
         memset(output, 0, n * sizeof(uint64_t));

         const size_t repeats = 1;
         cout << t << "|" << (n/t) << "|" << w << "|" << (w/t) << '|' << flush;
         const double duration = clockSec([&] {
            for (size_t r = 0; r < repeats; r++) {
               ctx.dispatch<uint32_t*, uint64_t*, uint32_t>(kernelObject, {t, w}, input, output, n);
            }
         });
         cout << ((sizeInMiB / 1024.0) * repeats) / duration << endl;

   //         for(int i = 0; i < 1024; i++) {
   //            cout << output[i] << ", ";
   //         }
   //         cout << endl;

         // validate results
         uint64_t val = 0;
         for (uint64_t i = 0; i < n; i++) {
            val += output[i];
         }
         if (val != expectedResult) {
            cout << "validation failed: expected " << expectedResult << ", but got " << val << endl;
         }
      }
   }

   freeHuge(input, n);
   freeHuge(output, n);
   rt.shutDown();
}





} // namespace

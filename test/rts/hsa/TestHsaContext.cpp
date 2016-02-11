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
//---------------------------------------------------------------------------
// SIM[DT] Lab
// (c) Harald Lang 2015
//---------------------------------------------------------------------------
namespace {

using namespace std;
using namespace rts::hsa;

TEST(HsaContext, InitAndShutdown) {
   HsaRuntime rt;
   ASSERT_FALSE(HsaUtils::isInitialized());
}

TEST(HsaContext, DetermineHsaState) {
   HsaRuntime rt;
   ASSERT_FALSE(HsaUtils::isInitialized());
   rt.initialize();
   ASSERT_TRUE(HsaUtils::isInitialized());
   rt.shutDown();
   ASSERT_FALSE(HsaUtils::isInitialized());
}

TEST(HsaContext, HsaException) {
   HsaRuntime rt;
   rt.initialize();
   try {
      throw HsaException("test");
      FAIL();
   }
   catch (const HsaException& e) {
      ASSERT_STREQ("test", e.what());
   }
   rt.shutDown();
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

TEST(HsaContext, Dispatch) {
   HsaRuntime rt;
   rt.initialize();
   HsaContext ctx(rt);

   const string module1 = loadFromFile("bin/test/rts/hsa/kernel/StoreGlobalId.brig");

   ctx.addModule(module1.c_str());
   ctx.finalize();
   ctx.createQueue();

   const size_t n = 1024 * 1024;
   size_t* output = new size_t[n];
   memset(output, 0, n * sizeof(size_t));

   std::string kernelName = "&__OpenCL_storeGlobalId_kernel";
   ctx.dispatch<size_t*, size_t>(kernelName, n, output, n);

   for (size_t i = 0; i < n; i++) {
      ASSERT_EQ(i, output[i]);
   }

   delete[] output;
   rt.shutDown();
}

TEST(HsaContext, MultipleDispatches) {
   HsaRuntime rt;
   rt.initialize();
   HsaContext ctx(rt);

   const string module1 = loadFromFile("bin/test/rts/hsa/kernel/StoreGlobalId.brig");

   ctx.addModule(module1.c_str());
   ctx.finalize();
   ctx.createQueue();

   const size_t n = 1024;
   size_t* output = new size_t[n];
   memset(output, 42, n * sizeof(size_t));

   for (size_t i = 0; i < n; i++) {
      ctx.dispatch<size_t*, size_t>("&__OpenCL_storeGlobalId_kernel", 1, &output[i], 1);
   }

   for (size_t i = 0; i < n; i++) {
      ASSERT_EQ(0u, output[i]);
   }

   delete[] output;
   rt.shutDown();
}

TEST(HsaContext, DISABLED_DispatchBatch) {
   HsaRuntime rt;
   rt.initialize();
   HsaContext ctx(rt);

   const string module1 = loadFromFile("bin/test/rts/hsa/kernel/StoreGlobalId.brig");

   ctx.addModule(module1.c_str());
   ctx.finalize();
   ctx.createQueue();

   constexpr size_t n = 128;
   size_t* output = new size_t[n];
   memset(output, 42, n * sizeof(size_t));

   const std::string kernelName = "&__OpenCL_storeGlobalId_kernel";
   const auto kernelObject = ctx.getKernelObject(kernelName);

   for (size_t i = 0; i < n; i++) {
      ctx.dispatchBatch<size_t*, size_t>(kernelObject, 1, &output[i], 1);
   }
   ctx.waitForBatchCompletion();

   for (size_t i = 0; i < n; i++) {
      ASSERT_EQ(0u, output[i]);
   }

   delete[] output;
   rt.shutDown();
}

TEST(HsaContext, BundleModules) {
   HsaRuntime rt;
   rt.initialize();
   HsaContext ctx(rt);

   const string module1 = loadFromFile("bin/test/rts/hsa/kernel/StoreGlobalId.brig");
   ctx.addModule(module1.c_str());
   const string module2 = loadFromFile("bin/test/rts/hsa/kernel/Add.brig");
   ctx.addModule(module2.c_str());

   ctx.finalize();
   ctx.createQueue();

   const size_t n = 1024 * 1024;
   size_t* output = new size_t[n];
   memset(output, 0, n * sizeof(size_t));

   ctx.dispatch<size_t*, size_t>("&__OpenCL_storeGlobalId_kernel", n, output, n);
   ctx.dispatch<size_t*, size_t, size_t>("&__OpenCL_add_kernel", n, output, 42, n);

   for (size_t i = 0; i < n; i++) {
      ASSERT_EQ(i + 42, output[i]);
   }

   delete[] output;
   rt.shutDown();
}

TEST(HsaContext, GroupLocalMemory) {
   HsaRuntime rt;
   rt.initialize();
   HsaContext ctx(rt);

   const string module1 = loadFromFile("bin/test/rts/hsa/kernel/GroupLocalMemory.brig");
   ctx.addModule(module1.c_str());

   ctx.finalize();
   ctx.createQueue();

   const uint32_t n = 1024 * 1024;
   uint32_t* output = new uint32_t[n];
   memset(output, 0, n * sizeof(uint32_t));

   ctx.dispatch<uint32_t*>("&__OpenCL_groupLocalMemory_kernel", n, output);

   for (uint32_t i = 0; i < n; i++) {
      ASSERT_EQ(i, output[i]);
   }

   delete[] output;
   rt.shutDown();
}

TEST(HsaContext, DISABLED_DeviceEnqueue) {
   HsaRuntime rt;
   rt.initialize();
   HsaContext ctx(rt);

   const string module1 = loadFromFile("bin/test/rts/hsa/kernel/DeviceSideEnqueue.brig");

   ctx.addModule(module1.c_str());
   ctx.finalize();
   ctx.createQueue();

   const size_t n = 1024;
   size_t* output = new size_t[n];
   memset(output, 0, n * sizeof(size_t));

   std::string kernelName = "&__OpenCL_enqueueStoreGlobalId_kernel";
   ctx.dispatch<size_t*, size_t>(kernelName, n, output, n);

   cout << output[0] << endl;
   for (size_t i = 0; i < n; i++) {
      ASSERT_EQ(i, output[i]);
   }

   delete[] output;
   rt.shutDown();
}

} // namespace

#pragma once
//---------------------------------------------------------------------------
#include <chrono>
#include <fstream>
#include <iostream>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
class Utils {
public:

   /// Memory allocation (huge pages)
   static inline void* mallocHuge(size_t size) {
      void* p=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
      madvise(p,size,MADV_HUGEPAGE);
      return p;
   }

   /// Free allocated huge pages
   static inline void freeHuge(void* ptr,size_t size) {
      munmap(ptr,size);
   }

   static inline void* mallocHugeAndSet(size_t size) {
      void* p=mmap(NULL,size,PROT_READ|PROT_WRITE,
      MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
      madvise(p,size,MADV_HUGEPAGE);
      memset(p,0,size);
      return p;
   }

   static inline double getTime(void) {
      struct timeval now_tv;
      gettimeofday(&now_tv,NULL);
      return ((double) now_tv.tv_sec)+((double) now_tv.tv_usec)/1000000.0;
   }

   /// Cycle counter on x86
   static uint64_t rdtsc() {
#if defined(__x86_64__) && defined(__GNUC__)
      uint32_t hi,lo;
      __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
      return static_cast<uint64_t>(lo)|(static_cast<uint64_t>(hi)<<32);
#else
      return 0;
#endif
   }

   /// Determine the number of processors
   static int64_t numProcessors() {
      int64_t n=sysconf(_SC_NPROCESSORS_ONLN);
      if (n<1) {
         cout<<"Failed to determine the number of processors."<<endl;
         n=1;
      }
      return n;
   }

   /// Thread affinitizer
   static void setAffinity(const uint32_t threadId) {
      cpu_set_t mask;
      CPU_ZERO(&mask);
      CPU_SET(threadId%numProcessors(),&mask);
      int result=sched_setaffinity(0,sizeof(mask),&mask);
      if (result!=0) {
         cout<<"Failed to set CPU affinity."<<endl;
      }
   }

   static string stringFromFile(string& fileName) {
//      cout<<"Reading file: "<<fileName<<endl;
      ifstream infile;
      infile.open(fileName.c_str());
      if (!infile) {
         cout<<"Failed to open file "<<fileName<<endl;
         return nullptr;
      }
      infile.seekg(0,ios::end);
      int len=infile.tellg();
      char *str=new char[len+1];
      infile.seekg(0,ios::beg);
      infile.read(str,len);
      int lenRead=infile.gcount();
      str[lenRead]=(char) 0;
      return string(str);
   }

   static inline bool fileExists(const string& fileName) {
      ifstream f(fileName.c_str());
      if (f.good()) {
         f.close();
         return true;
      }
      else {
         f.close();
         return false;
      }
   }

   static inline string selfPath(const string& fileName) {
      char buf[4096];
      ssize_t len=readlink("/proc/self/exe",buf,sizeof(buf)-1);
      if (len!=-1) {
         buf[len]='\0';
         return string(buf);
      }
      else {
         cout<<"Could not determine self-path."<<endl;
         return nullptr;
      }
   }

   static inline string relativeFileName(const string& fileName) {
      char buf[4096];
      ssize_t len=readlink("/proc/self/exe",buf,sizeof(buf)-1);
      if (len!=-1) {
         buf[len]='\0';
         while (len>0 && buf[len-1]!='/') len--;
         buf[len]='\0';
         return string(buf) + fileName;
      }
      else {
         cout<<"Could not determine self-path."<<endl;
         return nullptr;
      }
   }



};
//---------------------------------------------------------------------------

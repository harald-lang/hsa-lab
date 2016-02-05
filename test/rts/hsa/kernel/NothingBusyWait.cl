// Note: only one workitem
__kernel void nothing(__global atomic_int* controlSignal, __global atomic_int* completionSignal) {

   // initialize control flow variable
   int control = 0;

   // the main loop
   while (true) {

      // wait for the start signal (greater than 0)
      while (control == 0) {
         control = atomic_load(controlSignal);
      }
      atomic_store(controlSignal, 0);

      // poison pill
      if (control == 255) {
         atomic_store(completionSignal, 1);
         return;
      }

      // send completion signal
      control = 0;
   }
}

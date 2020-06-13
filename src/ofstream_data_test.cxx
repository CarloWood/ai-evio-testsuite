// Copyright (C) 2004, by
//
// Carlo Wood, Run on IRC <carlo@alinoe.com>
// RSA-1024 0x624ACAD5 1997-01-26                    Sign & Encrypt
// Fingerprint16 = 32 EC A7 B6 AC DB 65 A6  F6 F6 55 DD 1C DC FF 61
//
// This file may be distributed under the terms of the Q Public License
// version 1.0 as appearing in the file LICENSE.QPL included in the
// packaging of this file.
//

#include "sys.h"
#include "debug.h"
#include "evio/EventLoop.h"
#include "evio/PersistentInputFile.h"
#include "utils/AIAlert.h"
#include "utils/debug_ostream_operators.h"

using namespace evio;

class OutputStream128 : public OutputStream
{
 public:
  using OutputStream::OutputStream;

  size_t minimum_block_size_estimate() const override { return 128; }
};

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  // Create the thread pool.
  AIThreadPool thread_pool(1);
  // Create the event loop thread and let it handle new events through the thread pool.
  AIQueueHandle handler = thread_pool.new_queue(32);
  EventLoop event_loop(handler);

  try
  {
    boost::intrusive_ptr<PersistentInputFile> device2;
    {
      // We want to create the following data flow:
      //
      //                   output1 __
      //                             \                                                    .
      // 'data written to an ostream' O-->O file "blah.txt" O-->O file "blah2.txt"
      //                                 /                 /     \                        .
      //                      device1 __/       device2 __/       \__ device3
      //
      // The connection between device2 and device3 is made first, before writing data
      // to "blah.txt". But device2 is a PersistentInputFile: it will keep monitoring
      // the file and read any newly appended data as it appears (like 'tail -f').
      //
      // Aka, we'll do more or less the equivalent of the following:
      //
      // $ echo > "blah.txt"
      // $ nohup tail -f "blah.txt" > "blah2.txt" 2>/dev/null &
      // $ echo "data" >> blah.txt"
      //
      // and observe that "data" ends up in both files.
      //
      // For a clean termination (and flushing) we have the following requirements:
      // 1) As soon are we wrote the last data to output1, call flush_output_device() on device1.
      // 2) Call close() on device2 once we're done with the test: we just sleep 1 second while
      //    the main loop of evio runs and then close() device2.
      // 3) Device3 will automatically be closed once device2 is deleted.

      // Open a buffered output file that uses a buffer with a minimum block size of 96 bytes and that we can write to using an ostream.
      OutputStream128 output1;                                          // The std::ostream sink.
      Dout(dc::notice, "Creating device1:");
      auto device1 = create<File>();                                    // A File device.
      device1->set_source(output1);                                     // Create a buffer that device1 will read from and output1 will write to.
      device1->open("blah.txt", std::ios_base::trunc);                  // Open and truncate the file "blah.txt".

      // Open an input file that reads 'persistent' from "blah.txt" and link it with an output file that writes to "blah2.txt".
      Dout(dc::notice, "Creating device2:");
      device2 = create<PersistentInputFile>();
      Dout(dc::notice, "Creating device3:");
      auto device3 = create<File>();
      device3->set_source(device2, 1024 - evio::block_overhead_c, 4096, 1000000);                // Create a buffer that device3 will read from and device2 will write to.
      device2->open("blah.txt", std::ios_base::in);                     // device2 reads from "blah.txt" (when new data is appended to it).
      device3->open("blah2.txt", std::ios_base::trunc);                 // device3 creates a new file "blah2.txt" and writes to that.

      // Give device2 time to read till EOF. This tests the persistent-ness. If it wasn't
      // persistent it would close itself when reaching EOF and nothing would be written
      // to blah2.txt.
      Dout(dc::notice, "Sleeping 1 second...");
      std::this_thread::sleep_for(std::chrono::seconds(1));

      // Fill buffer of device1 with data.
      Dout(dc::notice|continued_cf, "Fill buffer of device1 with data... ");
      Debug(dc::evio.off());
      for (int i = 1; i <= 200; ++i)
        output1 << "Hello world " << i << '\n';
      Debug(dc::evio.on());
      Dout(dc::finish, "done");
      // Actually (start) writing data to device1.
      output1.flush();  // This is just ostream::flush(). It doesn't actually flush the buffer, it tells
                        // the library that the data that was written to the buffer is now ready to be read.

      // We must mark device1 as done, because below we're going to destroy EventLoop
      // which is not allowed while there are still any devices that are not closed.
      // This causes device1 to be closed as soon as we're done writing all data in its buffer to "blah.txt".
      device1->flush_output_device();
      // Destruct device pointers device1 and device3 - that tests that they aren't deleted before they are finished.
    }

    // Wait with closing device2 so it has time to read whatever is written to blah.txt.
    Dout(dc::notice, "Sleeping 1 second...");
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Actually get rid of device2 (it won't disappear otherwise, since it is persistent).
    device2->close();   // This causes device2 to be closed and deleted (and therefore device3 to also be closed and deleted (as soon as the buffer is empty)).
    // Destruct device pointer device2 too (everything has already completed here though).
  }
  catch (AIAlert::Error const& error)
  {
    Dout(dc::warning, error);
  }

  event_loop.join();    // Cause the event loop thread to finish what it was doing (continue running until m_active == 0) before exiting and joining with main.
  Dout(dc::notice, "Leaving main()...");
}

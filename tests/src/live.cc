#include "helper/common.hpp"

TEST_F(GstTestFixture, Live)
{
  this->SetUpPipeline({ false, "x264enc", 30 });
  this->SetLive(true);

  GstElement* gpaccmafmux =
    gst_element_factory_make_full("gpaccmafmux", "cdur", 1.0, NULL);
  GstAppSink* sink = new GstAppSink(gpaccmafmux, GetEncoder(), pipeline);
  sink->SetSync(true);

  this->StartPipeline();

  // Count the number of buffers
  int buffer_count = 0;
  while (true) {
    GstBufferList* buffer = sink->PopBuffer();
    if (!buffer)
      break;
    buffer_count++;
  }
  EXPECT_EQ(buffer_count, 1);
}

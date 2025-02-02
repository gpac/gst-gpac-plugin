#pragma once

#include <gst/gst.h>
#include <gtest/gtest.h>
#include <thread>

class GstAppSink
{
private:
  GstElement* appsink;

public:
  GstAppSink(GstElement* test_element, GstElement* tee, GstElement* pipeline)
  {
    // Create the elements
    GstElement* queue = gst_element_factory_make("queue", NULL);
    appsink = gst_element_factory_make("appsink", NULL);
    if (!queue || !appsink) {
      g_error("Failed to create elements");
      return;
    }

    // Set the appsink properties
    g_object_set(
      appsink, "emit-signals", TRUE, "sync", FALSE, "buffer-list", TRUE, NULL);

    // Add the appsink to the test element
    gst_bin_add_many(GST_BIN(pipeline), queue, test_element, appsink, NULL);

    // Link the elements
    if (!gst_element_link(tee, queue) ||
        !gst_element_link(queue, test_element) ||
        !gst_element_link(test_element, appsink)) {
      g_error("Failed to link elements");
      return;
    }
  }

  GstBufferList* PopBuffer()
  {
    GstSample* sample;
    g_signal_emit_by_name(appsink, "pull-sample", &sample);
    if (sample == NULL)
      return NULL;

    GstBufferList* buffer = gst_sample_get_buffer_list(sample);
    gst_buffer_list_ref(buffer);
    gst_sample_unref(sample);

    return buffer;
  }

  ~GstAppSink() { gst_object_unref(appsink); }
};

class GstTestFixture : public ::testing::Test
{
protected:
  GstElement* tee;
  GstElement* pipeline;

  static void SetUpTestSuite() { gst_init(NULL, NULL); }
  static void TearDownTestSuite() { gst_deinit(); }

  void SetUp() override
  {
    GstElement* source = gst_element_factory_make_full(
      "videotestsrc", "num-buffers", 60, "do-timestamp", TRUE, NULL);
    GstElement* capsfilter = gst_element_factory_make_full(
      "capsfilter",
      "caps",
      gst_caps_from_string("video/x-raw, framerate=30/1"),
      NULL);
    GstElement* encoder =
      gst_element_factory_make_full("x264enc", "key-int-max", 30, NULL);
    tee = gst_element_factory_make("tee", NULL);

    // Create the pipeline
    pipeline = gst_pipeline_new("test-pipeline");
    if (!pipeline || !source || !capsfilter || !encoder || !tee) {
      g_error("Failed to create elements");
      return;
    }

    // Build the pipeline
    gst_bin_add_many(GST_BIN(pipeline), source, capsfilter, encoder, tee, NULL);
    if (!gst_element_link(source, capsfilter) ||
        !gst_element_link(capsfilter, encoder) ||
        !gst_element_link(encoder, tee)) {
      g_error("Failed to link elements");
      return;
    }
  }

  void TearDown() override
  {
    // Wait until error or EOS
    GstBus* bus = gst_element_get_bus(pipeline);
    GstMessage* msg = gst_bus_timed_pop_filtered(
      bus,
      GST_CLOCK_TIME_NONE,
      (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

    // Parse message
    if (msg != NULL) {
      GError* err;
      gchar* debug_info;

      switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR:
          gst_message_parse_error(msg, &err, &debug_info);
          g_printerr("Error received from element %s: %s\n",
                     GST_OBJECT_NAME(msg->src),
                     err->message);
          g_printerr("Debugging information: %s\n",
                     debug_info ? debug_info : "none");
          g_clear_error(&err);
          g_free(debug_info);
          break;
        case GST_MESSAGE_EOS:
          break;
        default:
          // We should not reach here because we only asked for ERRORs and EOS
          g_printerr("Unexpected message received.\n");
          break;
      }
      gst_message_unref(msg);
    }

    // Free resources
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
  }

  bool StartPipeline()
  {
    GstStateChangeReturn ret =
      gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
      g_error("Failed to start pipeline");
      return false;
    }
    return true;
  }
};

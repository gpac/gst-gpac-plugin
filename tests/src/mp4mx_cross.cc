#include "common.hpp"

typedef std::pair<guint32, guint32> fourcc_t;

bool
extract_box_fourccs(GstBuffer* buffer,
                    std::vector<fourcc_t>& fourccs,
                    guint32* leftover = NULL)
{
  GstBufferMapInfo map_info;
  if (!gst_buffer_map(buffer, &map_info, GST_MAP_READ))
    return false;

  guint8* data = map_info.data;
  guint32 size = map_info.size;
  guint32 offset = 0;

  while (offset < size) {
    guint32 box_size =
      GUINT32_FROM_LE((data[offset] << 24) | (data[offset + 1] << 16) |
                      (data[offset + 2] << 8) | data[offset + 3]);
    guint32 box_type =
      GUINT32_FROM_BE((data[offset + 4] << 24) | (data[offset + 5] << 16) |
                      (data[offset + 6] << 8) | data[offset + 7]);
    fourcc_t fourcc = std::make_pair(box_type, box_size);
    fourccs.push_back(fourcc);
    offset += box_size;
  }

  gst_buffer_unmap(buffer, &map_info);
  if (leftover)
    *leftover = offset - size;
  return true;
}

void
IsSegmentInit(GstBuffer* buffer)
{
  // It must only have the following flags
  EXPECT_TRUE(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_HEADER));
  EXPECT_TRUE(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT));

  // It must contain also contain at least "ftyp" and "moov" boxes
  std::vector<fourcc_t> fourccs;
  extract_box_fourccs(buffer, fourccs);

  bool has_ftyp = false;
  bool has_moov = false;
  for (fourcc_t fourcc : fourccs) {
    guint32 fourcc_type = fourcc.first;
    if (fourcc_type == GST_MAKE_FOURCC('f', 't', 'y', 'p'))
      has_ftyp = true;
    if (fourcc_type == GST_MAKE_FOURCC('m', 'o', 'o', 'v'))
      has_moov = true;
  }

  EXPECT_TRUE(has_ftyp);
  EXPECT_TRUE(has_moov);
}

guint32
IsSegmentHeader(GstBuffer* buffer)
{
  // It must only have the following flags
  if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
    // cmafmux sets this flag on N>0 buffers
    EXPECT_TRUE(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT));
  }
  // gpacmp4mx does not
  EXPECT_TRUE(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_HEADER));

  // It must contain also contain at least "ftyp" and "moov" boxes
  guint32 leftover;
  std::vector<fourcc_t> fourccs;
  extract_box_fourccs(buffer, fourccs, &leftover);

  bool has_moof = false;
  bool has_mdat = false;
  for (fourcc_t fourcc : fourccs) {
    guint32 fourcc_type = fourcc.first;
    guint32 box_size = fourcc.second;
    if (fourcc_type == GST_MAKE_FOURCC('m', 'o', 'o', 'f'))
      has_moof = true;
    if (fourcc_type == GST_MAKE_FOURCC('m', 'd', 'a', 't')) {
      has_mdat = true;

      // only size+fourcc is inside the header
      EXPECT_EQ(box_size - 8, leftover);
    }
  }

  EXPECT_TRUE(has_moof);
  EXPECT_TRUE(has_mdat);
  return leftover;
}

guint32
IsSegmentDelta(GstBuffer* buffer, guint32 expected_segment_size)
{
  // It must not exceed the expected segment size
  guint32 buffer_size = gst_buffer_get_size(buffer);
  EXPECT_LE(buffer_size, expected_segment_size);
  guint32 leftover = expected_segment_size - buffer_size;

  // It must only have the following flags
  if (leftover == 0) {
    EXPECT_TRUE(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_MARKER));
  }
  EXPECT_TRUE(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT));

  // cmafmux sends buffers partially, so we need to check the leftover
  return leftover;
}

TEST_F(GstTestFixture, StructureTest)
{
  // Create test elements
  GstElement* cmafmux = gst_element_factory_make_full(
    "cmafmux", "chunk-duration", GST_SECOND, NULL);
  GstElement* gpacmp4mx =
    gst_element_factory_make_full("gpacmp4mx", "cdur", 1.0, NULL);

  // Create element sinks
  GstAppSink* cmafmux_sink = new GstAppSink(cmafmux, tee, pipeline);
  GstAppSink* gpacmp4mx_sink = new GstAppSink(gpacmp4mx, tee, pipeline);

  // Start the pipeline
  this->StartPipeline();

  // Go through all buffers
  bool first_cmaf_buffer = true;
  bool first_gpacmp4mx_buffer = true;
  while (true) {
    GstBufferList* cmaf_buffer = cmafmux_sink->PopBuffer();
    GstBufferList* gpacmp4mx_buffer = gpacmp4mx_sink->PopBuffer();

    // If neither buffer is available, we are done
    if (!cmaf_buffer && !gpacmp4mx_buffer)
      break;

    // Both buffers should return something
    ASSERT_TRUE(cmaf_buffer && gpacmp4mx_buffer);

    std::vector<GstBufferList*> buffer_lists = { cmaf_buffer,
                                                 gpacmp4mx_buffer };
    for (GstBufferList* buffer_list : buffer_lists) {
      guint idx = 0;
      GstBuffer* buf;
      guint32 buffer_count = gst_buffer_list_length(buffer_list);
      bool is_cmaf = buffer_list == cmaf_buffer;

#define GET_NEXT_BUFFER()                        \
  ASSERT_LT(idx, buffer_count);                  \
  buf = gst_buffer_list_get(buffer_list, idx++);

      if ((is_cmaf && first_cmaf_buffer) ||
          (!is_cmaf && first_gpacmp4mx_buffer)) {
        EXPECT_GE(buffer_count, 3); // init, header, delta

        // Check buffer #0
        GET_NEXT_BUFFER();
        IsSegmentInit(buf);

        if (is_cmaf)
          first_cmaf_buffer = false;
        else
          first_gpacmp4mx_buffer = false;
      } else {
        EXPECT_GE(buffer_count, 2); // header, delta
      }

      // Check buffer #1
      GET_NEXT_BUFFER();
      guint32 data_size = IsSegmentHeader(buf);

      // Check buffer #2...N
      guint32 leftover = data_size;
      while (gst_buffer_list_length(buffer_list) > idx) {
        GET_NEXT_BUFFER();
        leftover = IsSegmentDelta(buf, leftover);
        if (leftover == 0)
          break;
      }
      EXPECT_EQ(leftover, 0);
      EXPECT_EQ(gst_buffer_list_length(buffer_list), idx);

#undef GET_NEXT_BUFFER
    }
  }
}

TEST_F(GstTestFixture, TimingTest)
{
  // Create test elements
  GstElement* cmafmux = gst_element_factory_make_full(
    "cmafmux", "chunk-duration", GST_SECOND, NULL);
  GstElement* gpacmp4mx =
    gst_element_factory_make_full("gpacmp4mx", "cdur", 1.0, NULL);

  // Create element sinks
  GstAppSink* cmafmux_sink = new GstAppSink(cmafmux, tee, pipeline);
  GstAppSink* gpacmp4mx_sink = new GstAppSink(gpacmp4mx, tee, pipeline);

  // Start the pipeline
  this->StartPipeline();

  // Go through all buffers
  while (true) {
    GstBufferList* cmaf_buffer = cmafmux_sink->PopBuffer();
    GstBufferList* gpacmp4mx_buffer = gpacmp4mx_sink->PopBuffer();

    // If neither buffer is available, we are done
    if (!cmaf_buffer && !gpacmp4mx_buffer)
      break;

    // Both buffers should return something
    ASSERT_TRUE(cmaf_buffer && gpacmp4mx_buffer);

#define GET_NEXT_BUFFER()                        \
  ASSERT_LT(idx, buffer_count);                  \
  buf = gst_buffer_list_get(buffer_list, idx++);

    // Zip buffer lists
    guint length = std::min(gst_buffer_list_length(cmaf_buffer),
                            gst_buffer_list_length(gpacmp4mx_buffer));
    for (guint idx = 0; idx < length; idx++) {
      GstBuffer* cmaf_buf = gst_buffer_list_get(cmaf_buffer, idx);
      GstBuffer* gpacmp4mx_buf = gst_buffer_list_get(gpacmp4mx_buffer, idx);

      // Check only the PTS
      EXPECT_EQ(GST_BUFFER_PTS(cmaf_buf), GST_BUFFER_PTS(gpacmp4mx_buf));

      // The duration differs because, compared to cmafmux, we are
      // outputting complete segments rather than dividing the mdat across
      // individual buffers by samples.
      if (!GST_BUFFER_FLAG_IS_SET(cmaf_buf, GST_BUFFER_FLAG_DELTA_UNIT)) {
        EXPECT_EQ(GST_BUFFER_DURATION(cmaf_buf),
                  GST_BUFFER_DURATION(gpacmp4mx_buf));
      }

      // For the same reason, the DTS is also not the same.
    }

#undef GET_NEXT_BUFFER
  }
}

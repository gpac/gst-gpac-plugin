#include <gio/gio.h>
#include <gst/gst.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class SignalMemoryCapture
{
public:
  using Buffer = std::vector<uint8_t>;

  void connect(GstElement* element, const std::string& signal_name)
  {
    auto* callback = new Callback{ .signal = signal_name,
                                   .buffers = &buffers_[signal_name],
                                   .label_buffers = &label_buffers_ };
    callback_ptrs_.push_back(callback);

    gulong signal_id = g_signal_connect_data(
      element,
      signal_name.c_str(),
      G_CALLBACK(&Callback::thunk),
      callback,
      [](gpointer data, GClosure*) { delete static_cast<Callback*>(data); },
      G_CONNECT_AFTER);
    signal_ids_.push_back(signal_id);
  }

  void finish(GstElement* element)
  {
    for (auto& cb_ptr : callback_ptrs_) {
      if (cb_ptr)
        cb_ptr->finalize();
    }
    for (const auto& signal_id : signal_ids_) {
      g_signal_handler_disconnect(element, signal_id);
    }
    signal_ids_.clear();
    callback_ptrs_.clear();
  }

  auto get_all(const std::string& signal_name) const
    -> const std::vector<Buffer>&
  {
    static const std::vector<Buffer> empty;
    auto it = buffers_.find(signal_name);
    return it != buffers_.end() ? it->second : empty;
  }

  auto get_labeled(const std::string& label) const -> const Buffer*
  {
    auto it = label_buffers_.find(label);
    if (it != label_buffers_.end())
      return &it->second;
    return nullptr;
  }

private:
  struct Callback
  {
    std::string signal;
    std::vector<Buffer>* buffers;
    std::unordered_map<std::string, Buffer>* label_buffers;

    static GOutputStream* thunk(GstElement* self,
                                const gchar* label,
                                gpointer user_data)
    {
      auto* cb = static_cast<Callback*>(user_data);
      auto* basename = g_path_get_basename(label);

      auto* mem_stream = g_memory_output_stream_new_resizable();
      cb->stream_labels.emplace_back(
        std::make_pair(basename, G_OUTPUT_STREAM(mem_stream)));

      g_free(basename);
      return G_OUTPUT_STREAM(mem_stream);
    }

    // Keep label with each stream
    std::vector<std::pair<std::string, GOutputStream*>> stream_labels;

    void finalize()
    {
      for (auto& [label, stream] : stream_labels) {
        if (!stream)
          continue;

        auto* mem_stream = G_MEMORY_OUTPUT_STREAM(stream);
        GBytes* bytes = g_memory_output_stream_steal_as_bytes(mem_stream);
        if (!bytes)
          continue;

        gsize size;
        const guint8* data =
          static_cast<const guint8*>(g_bytes_get_data(bytes, &size));
        (*buffers).emplace_back(data, data + size);
        if (label_buffers) {
          (*label_buffers)[label] = (*buffers).back();
        }

        g_bytes_unref(bytes);
        g_object_unref(stream);
        stream = nullptr;
      }
    }

    ~Callback() { finalize(); }
  };

  std::unordered_map<std::string, std::vector<Buffer>> buffers_;
  std::unordered_map<std::string, Buffer> label_buffers_;
  std::vector<gulong> signal_ids_;
  std::vector<Callback*> callback_ptrs_;
};

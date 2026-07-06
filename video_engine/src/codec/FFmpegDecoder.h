#pragma once

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
}

namespace video_engine {

class FFmpegDecoder {
public:
  FFmpegDecoder();
  ~FFmpegDecoder();

  FFmpegDecoder(const FFmpegDecoder&) = delete;
  FFmpegDecoder& operator=(const FFmpegDecoder&) = delete;

  void open(const std::string& input_path);
  void seek(double timestamp_seconds);
  bool read(AVFrame*& frame, double& timestamp_seconds);
  void close();

  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] double fps() const { return fps_; }
  [[nodiscard]] AVBufferRef* hwDeviceContext() const { return hw_device_context_; }
  [[nodiscard]] AVBufferRef* hwFramesContext() const { return decoded_frame_ ? decoded_frame_->hw_frames_ctx : nullptr; }
  [[nodiscard]] AVPixelFormat hardwarePixelFormat() const { return hardware_pixel_format_; }
  [[nodiscard]] AVPixelFormat softwarePixelFormat() const { return software_pixel_format_; }

private:
  static AVPixelFormat selectHardwareFormat(AVCodecContext* codec_context, const AVPixelFormat* pixel_formats);
  void validateHardwareSupport() const;

  AVFormatContext* format_context_ = nullptr;
  AVCodecContext* codec_context_ = nullptr;
  const AVCodec* codec_ = nullptr;
  AVPacket* packet_ = nullptr;
  AVFrame* decoded_frame_ = nullptr;
  AVBufferRef* hw_device_context_ = nullptr;
  int video_stream_index_ = -1;
  int width_ = 0;
  int height_ = 0;
  double fps_ = 0.0;
  bool flushing_ = false;
  AVPixelFormat hardware_pixel_format_ = AV_PIX_FMT_CUDA;
  AVPixelFormat software_pixel_format_ = AV_PIX_FMT_NV12;
};

}  // namespace video_engine

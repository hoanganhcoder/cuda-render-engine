#pragma once

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
}

namespace video_engine {

class FFmpegEncoder {
public:
  FFmpegEncoder();
  ~FFmpegEncoder();

  FFmpegEncoder(const FFmpegEncoder&) = delete;
  FFmpegEncoder& operator=(const FFmpegEncoder&) = delete;

  void open(
      const std::string& output_path,
      int width,
      int height,
      double fps,
      AVBufferRef* hw_device_context,
      AVBufferRef* hw_frames_context,
      const std::string& audio_path = "");
  [[nodiscard]] AVBufferRef* hwFramesContext() const { return hw_frames_context_; }
  void write(AVFrame* frame);
  void close();

private:
  void encodeFrame(AVFrame* frame);
  void openAudioInput(const std::string& audio_path);
  bool readNextAudioPacket();
  void writeAudioUntil(double timestamp_seconds);
  void drainAudio();
  [[nodiscard]] double pendingAudioTimestampSeconds() const;

  AVFormatContext* format_context_ = nullptr;
  AVFormatContext* audio_input_context_ = nullptr;
  AVCodecContext* codec_context_ = nullptr;
  const AVCodec* codec_ = nullptr;
  AVStream* stream_ = nullptr;
  AVStream* audio_input_stream_ = nullptr;
  AVStream* audio_stream_ = nullptr;
  AVPacket* packet_ = nullptr;
  AVPacket* audio_packet_ = nullptr;
  AVBufferRef* hw_device_context_ = nullptr;
  AVBufferRef* hw_frames_context_ = nullptr;
  int audio_stream_index_ = -1;
  bool audio_eof_ = false;
  bool has_pending_audio_packet_ = false;
  int width_ = 0;
  int height_ = 0;
  double fps_ = 0.0;
  int64_t next_pts_ = 0;
};

}  // namespace video_engine

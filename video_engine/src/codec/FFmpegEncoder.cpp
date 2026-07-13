#include "codec/FFmpegEncoder.h"

#include <string>
#include <stdexcept>

extern "C" {
#include <libavutil/rational.h>
}

namespace video_engine {

namespace {

std::string ffmpegError(int error_code) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(error_code, buffer, sizeof(buffer));
  return std::string(buffer);
}

void throwOnError(int error_code, const std::string& context) {
  if (error_code < 0) {
    throw std::runtime_error(context + ": " + ffmpegError(error_code));
  }
}

}  // namespace

FFmpegEncoder::FFmpegEncoder() {
  packet_ = av_packet_alloc();
  audio_packet_ = av_packet_alloc();
  if (!packet_ || !audio_packet_) {
    throw std::runtime_error("Failed to allocate FFmpeg encoder packet.");
  }
}

FFmpegEncoder::~FFmpegEncoder() {
  try {
    close();
  } catch (...) {
  }
  av_packet_free(&packet_);
  av_packet_free(&audio_packet_);
}

void FFmpegEncoder::open(
    const std::string& output_path,
    int width,
    int height,
    double fps,
    AVBufferRef* hw_device_context,
    AVBufferRef* hw_frames_context,
    const std::string& audio_path) {
  close();
  (void)hw_frames_context;

  width_ = width;
  height_ = height;
  fps_ = fps > 0.0 ? fps : 30.0;
  next_pts_ = 0;
  hw_device_context_ = av_buffer_ref(hw_device_context);
  hw_frames_context_ = av_hwframe_ctx_alloc(hw_device_context_);
  if (!hw_frames_context_) {
    throw std::runtime_error("Failed to allocate encoder CUDA hw frames context.");
  }
  auto* frames_context = reinterpret_cast<AVHWFramesContext*>(hw_frames_context_->data);
  frames_context->format = AV_PIX_FMT_CUDA;
  frames_context->sw_format = AV_PIX_FMT_NV12;
  frames_context->width = width_;
  frames_context->height = height_;
  frames_context->initial_pool_size = 8;
  throwOnError(av_hwframe_ctx_init(hw_frames_context_), "Failed to initialize encoder CUDA hw frames context");

  throwOnError(avformat_alloc_output_context2(&format_context_, nullptr, nullptr, output_path.c_str()),
               "Failed to create output format context");
  codec_ = avcodec_find_encoder_by_name("h264_nvenc");
  if (!codec_) {
    throw std::runtime_error("Failed to find NVENC encoder h264_nvenc.");
  }

  stream_ = avformat_new_stream(format_context_, codec_);
  if (!stream_) {
    throw std::runtime_error("Failed to create output stream.");
  }

  codec_context_ = avcodec_alloc_context3(codec_);
  if (!codec_context_) {
    throw std::runtime_error("Failed to allocate encoder context.");
  }

  codec_context_->codec_id = codec_->id;
  codec_context_->codec_type = AVMEDIA_TYPE_VIDEO;
  codec_context_->width = width_;
  codec_context_->height = height_;
  codec_context_->pix_fmt = AV_PIX_FMT_CUDA;
  const AVRational frame_rate = av_d2q(fps_, 1'000'000);
  codec_context_->framerate = frame_rate;
  codec_context_->time_base = av_inv_q(frame_rate);
  codec_context_->gop_size = 60;
  codec_context_->max_b_frames = 0;
  codec_context_->bit_rate = 8'000'000;
  codec_context_->hw_device_ctx = av_buffer_ref(hw_device_context_);
  codec_context_->hw_frames_ctx = av_buffer_ref(hw_frames_context_);
  if (format_context_->oformat->flags & AVFMT_GLOBALHEADER) {
    codec_context_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  AVDictionary* options = nullptr;
  av_dict_set(&options, "preset", "p5", 0);
  av_dict_set(&options, "rc", "vbr", 0);
  av_dict_set(&options, "cq", "19", 0);
  av_dict_set(&options, "tune", "hq", 0);
  throwOnError(avcodec_open2(codec_context_, codec_, &options), "Failed to open NVENC encoder");
  av_dict_free(&options);

  throwOnError(avcodec_parameters_from_context(stream_->codecpar, codec_context_),
               "Failed to copy encoder parameters to stream");
  stream_->time_base = codec_context_->time_base;

  if (!audio_path.empty()) {
    openAudioInput(audio_path);
  }

  if (!(format_context_->oformat->flags & AVFMT_NOFILE)) {
    throwOnError(avio_open(&format_context_->pb, output_path.c_str(), AVIO_FLAG_WRITE),
                 "Failed to open output file");
  }

  throwOnError(avformat_write_header(format_context_, nullptr), "Failed to write container header");
}

void FFmpegEncoder::write(AVFrame* frame) {
  if (!frame || frame->width != width_ || frame->height != height_) {
    const int got_width = frame ? frame->width : -1;
    const int got_height = frame ? frame->height : -1;
    throw std::runtime_error(
        "Encoder received invalid GPU frame dimensions. expected=" + std::to_string(width_) + "x" +
        std::to_string(height_) + " got=" + std::to_string(got_width) + "x" + std::to_string(got_height));
  }

  frame->format = AV_PIX_FMT_CUDA;
  frame->pts = next_pts_++;
  encodeFrame(frame);
}

void FFmpegEncoder::close() {
  if (codec_context_) {
    encodeFrame(nullptr);
  }
  drainAudio();
  if (format_context_) {
    av_write_trailer(format_context_);
  }
  if (codec_context_) {
    avcodec_free_context(&codec_context_);
  }
  if (format_context_) {
    if (!(format_context_->oformat->flags & AVFMT_NOFILE) && format_context_->pb) {
      avio_closep(&format_context_->pb);
    }
    avformat_free_context(format_context_);
    format_context_ = nullptr;
  }
  if (audio_input_context_) {
    avformat_close_input(&audio_input_context_);
  }
  if (hw_frames_context_) {
    av_buffer_unref(&hw_frames_context_);
  }
  if (hw_device_context_) {
    av_buffer_unref(&hw_device_context_);
  }
  stream_ = nullptr;
  audio_input_stream_ = nullptr;
  audio_stream_ = nullptr;
  codec_ = nullptr;
  audio_stream_index_ = -1;
  audio_eof_ = false;
  has_pending_audio_packet_ = false;
  width_ = 0;
  height_ = 0;
  fps_ = 0.0;
  next_pts_ = 0;
}

void FFmpegEncoder::encodeFrame(AVFrame* frame) {
  throwOnError(avcodec_send_frame(codec_context_, frame), "Failed to send frame to NVENC");
  while (true) {
    const int receive_result = avcodec_receive_packet(codec_context_, packet_);
    if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF) {
      return;
    }
    throwOnError(receive_result, "Failed to receive encoded packet");
    av_packet_rescale_ts(packet_, codec_context_->time_base, stream_->time_base);
    packet_->stream_index = stream_->index;
    const double video_timestamp_seconds =
        packet_->dts == AV_NOPTS_VALUE ? 0.0 : static_cast<double>(packet_->dts) * av_q2d(stream_->time_base);
    writeAudioUntil(video_timestamp_seconds + 1.0);
    throwOnError(av_interleaved_write_frame(format_context_, packet_), "Failed to write encoded packet");
    av_packet_unref(packet_);
  }
}

void FFmpegEncoder::openAudioInput(const std::string& audio_path) {
  throwOnError(avformat_open_input(&audio_input_context_, audio_path.c_str(), nullptr, nullptr),
               "Failed to open mux audio input");
  throwOnError(avformat_find_stream_info(audio_input_context_, nullptr), "Failed to read mux audio stream info");
  audio_stream_index_ = av_find_best_stream(audio_input_context_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (audio_stream_index_ < 0) {
    throw std::runtime_error("Failed to find an audio stream in mux audio input.");
  }
  audio_input_stream_ = audio_input_context_->streams[audio_stream_index_];
  audio_stream_ = avformat_new_stream(format_context_, nullptr);
  if (!audio_stream_) {
    throw std::runtime_error("Failed to create output audio stream.");
  }
  throwOnError(avcodec_parameters_copy(audio_stream_->codecpar, audio_input_stream_->codecpar),
               "Failed to copy audio parameters to output stream");
  audio_stream_->codecpar->codec_tag = 0;
  audio_stream_->time_base = audio_input_stream_->time_base;
  audio_eof_ = false;
  has_pending_audio_packet_ = false;
}

bool FFmpegEncoder::readNextAudioPacket() {
  if (audio_eof_ || has_pending_audio_packet_ || audio_input_context_ == nullptr || audio_stream_ == nullptr) {
    return has_pending_audio_packet_;
  }

  while (true) {
    const int result = av_read_frame(audio_input_context_, audio_packet_);
    if (result == AVERROR_EOF) {
      audio_eof_ = true;
      return false;
    }
    throwOnError(result, "Failed to read mux audio packet");
    if (audio_packet_->stream_index != audio_stream_index_) {
      av_packet_unref(audio_packet_);
      continue;
    }
    av_packet_rescale_ts(audio_packet_, audio_input_stream_->time_base, audio_stream_->time_base);
    audio_packet_->stream_index = audio_stream_->index;
    has_pending_audio_packet_ = true;
    return true;
  }
}

double FFmpegEncoder::pendingAudioTimestampSeconds() const {
  if (!has_pending_audio_packet_ || audio_stream_ == nullptr) {
    return 0.0;
  }
  const int64_t timestamp = audio_packet_->dts != AV_NOPTS_VALUE ? audio_packet_->dts : audio_packet_->pts;
  return timestamp == AV_NOPTS_VALUE ? 0.0 : static_cast<double>(timestamp) * av_q2d(audio_stream_->time_base);
}

void FFmpegEncoder::writeAudioUntil(double timestamp_seconds) {
  if (audio_stream_ == nullptr) {
    return;
  }
  while (readNextAudioPacket()) {
    if (pendingAudioTimestampSeconds() > timestamp_seconds) {
      return;
    }
    throwOnError(av_interleaved_write_frame(format_context_, audio_packet_), "Failed to write mux audio packet");
    has_pending_audio_packet_ = false;
    av_packet_unref(audio_packet_);
  }
}

void FFmpegEncoder::drainAudio() {
  if (audio_stream_ == nullptr) {
    return;
  }
  while (readNextAudioPacket()) {
    throwOnError(av_interleaved_write_frame(format_context_, audio_packet_), "Failed to drain mux audio packet");
    has_pending_audio_packet_ = false;
    av_packet_unref(audio_packet_);
  }
}

}  // namespace video_engine

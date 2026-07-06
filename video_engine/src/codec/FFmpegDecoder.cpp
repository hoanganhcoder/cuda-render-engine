#include "codec/FFmpegDecoder.h"

#include <string>
#include <stdexcept>

#include "core/Logger.h"

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

FFmpegDecoder::FFmpegDecoder() {
  packet_ = av_packet_alloc();
  decoded_frame_ = av_frame_alloc();
  if (!packet_ || !decoded_frame_) {
    throw std::runtime_error("Failed to allocate FFmpeg decoder packet/frame.");
  }
}

FFmpegDecoder::~FFmpegDecoder() {
  close();
  av_packet_free(&packet_);
  av_frame_free(&decoded_frame_);
}

void FFmpegDecoder::open(const std::string& input_path) {
  close();

  throwOnError(avformat_open_input(&format_context_, input_path.c_str(), nullptr, nullptr),
               "Failed to open input file");
  throwOnError(avformat_find_stream_info(format_context_, nullptr), "Failed to read stream info");

  video_stream_index_ = av_find_best_stream(format_context_, AVMEDIA_TYPE_VIDEO, -1, -1, &codec_, 0);
  if (video_stream_index_ < 0) {
    throw std::runtime_error("Failed to find a video stream in input.");
  }

  validateHardwareSupport();

  codec_context_ = avcodec_alloc_context3(codec_);
  if (!codec_context_) {
    throw std::runtime_error("Failed to allocate decoder context.");
  }

  AVStream* stream = format_context_->streams[video_stream_index_];
  throwOnError(avcodec_parameters_to_context(codec_context_, stream->codecpar),
               "Failed to copy codec parameters to decoder context");
  throwOnError(av_hwdevice_ctx_create(&hw_device_context_, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0),
               "Failed to create CUDA hardware device context for decoder");

  codec_context_->opaque = this;
  codec_context_->get_format = &FFmpegDecoder::selectHardwareFormat;
  codec_context_->hw_device_ctx = av_buffer_ref(hw_device_context_);
  throwOnError(avcodec_open2(codec_context_, codec_, nullptr), "Failed to open NVDEC decoder");

  width_ = codec_context_->width;
  height_ = codec_context_->height;
  const AVRational guessed_fps = av_guess_frame_rate(format_context_, stream, nullptr);
  fps_ = guessed_fps.num > 0 && guessed_fps.den > 0 ? av_q2d(guessed_fps) : 30.0;

  Logger::info("NVDEC decoder opened.");
}

void FFmpegDecoder::seek(double timestamp_seconds) {
  if (!format_context_ || !codec_context_ || video_stream_index_ < 0) {
    throw std::runtime_error("Decoder must be opened before seeking.");
  }
  const AVStream* stream = format_context_->streams[video_stream_index_];
  const int64_t timestamp = static_cast<int64_t>(timestamp_seconds / av_q2d(stream->time_base));
  avcodec_flush_buffers(codec_context_);
  av_packet_unref(packet_);
  av_frame_unref(decoded_frame_);
  flushing_ = false;
  throwOnError(
      av_seek_frame(format_context_, video_stream_index_, timestamp, AVSEEK_FLAG_BACKWARD),
      "Failed to seek input video");
}

bool FFmpegDecoder::read(AVFrame*& frame, double& timestamp_seconds) {
  auto receive_frame = [&]() -> int {
    const int receive_result = avcodec_receive_frame(codec_context_, decoded_frame_);
    if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF) {
      return receive_result;
    }
    throwOnError(receive_result, "Failed to receive decoded GPU frame");

    if (decoded_frame_->format != hardware_pixel_format_) {
      throw std::runtime_error("Decoder did not return AV_PIX_FMT_CUDA frame.");
    }

    if (decoded_frame_->hw_frames_ctx) {
      const auto* frames_context =
          reinterpret_cast<const AVHWFramesContext*>(decoded_frame_->hw_frames_ctx->data);
      software_pixel_format_ = frames_context->sw_format;
    }

    const AVStream* stream = format_context_->streams[video_stream_index_];
    const int64_t best_pts = decoded_frame_->best_effort_timestamp;
    timestamp_seconds = best_pts == AV_NOPTS_VALUE ? 0.0 : best_pts * av_q2d(stream->time_base);
    frame = decoded_frame_;
    return 0;
  };

  while (true) {
    av_frame_unref(decoded_frame_);
    const int receive_result = receive_frame();
    if (receive_result == 0) {
      return true;
    }
    if (receive_result == AVERROR_EOF) {
      return false;
    }
    if (flushing_) {
      return false;
    }

    const int read_result = av_read_frame(format_context_, packet_);
    if (read_result == AVERROR_EOF) {
      flushing_ = true;
      throwOnError(avcodec_send_packet(codec_context_, nullptr), "Failed to flush decoder");
      continue;
    }
    throwOnError(read_result, "Failed to read packet");

    if (packet_->stream_index != video_stream_index_) {
      av_packet_unref(packet_);
      continue;
    }

    throwOnError(avcodec_send_packet(codec_context_, packet_), "Failed to send packet to NVDEC");
    av_packet_unref(packet_);
  }
}

void FFmpegDecoder::close() {
  if (codec_context_) {
    avcodec_free_context(&codec_context_);
  }
  if (hw_device_context_) {
    av_buffer_unref(&hw_device_context_);
  }
  if (format_context_) {
    avformat_close_input(&format_context_);
  }
  video_stream_index_ = -1;
  width_ = 0;
  height_ = 0;
  fps_ = 0.0;
  flushing_ = false;
  software_pixel_format_ = AV_PIX_FMT_NV12;
}

AVPixelFormat FFmpegDecoder::selectHardwareFormat(AVCodecContext* codec_context, const AVPixelFormat* pixel_formats) {
  auto* self = reinterpret_cast<FFmpegDecoder*>(codec_context->opaque);
  for (const AVPixelFormat* current = pixel_formats; *current != AV_PIX_FMT_NONE; ++current) {
    if (*current == self->hardware_pixel_format_) {
      return *current;
    }
  }
  return AV_PIX_FMT_NONE;
}

void FFmpegDecoder::validateHardwareSupport() const {
  for (int index = 0;; ++index) {
    const AVCodecHWConfig* config = avcodec_get_hw_config(codec_, index);
    if (!config) {
      break;
    }
    if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
        config->device_type == AV_HWDEVICE_TYPE_CUDA &&
        config->pix_fmt == hardware_pixel_format_) {
      return;
    }
  }
  throw std::runtime_error("Selected decoder does not support CUDA/NVDEC hardware frames.");
}

}  // namespace video_engine

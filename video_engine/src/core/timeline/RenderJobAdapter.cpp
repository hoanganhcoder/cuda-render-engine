#include "core/timeline/RenderJobAdapter.h"

namespace video_engine::timeline {

Sequence RenderJobAdapter::toSequence(const video_engine::RenderJob& job) {
  Sequence sequence;
  sequence.input_path = job.input;
  sequence.output_path = job.output;
  sequence.width = job.width;
  sequence.height = job.height;
  sequence.fps = job.fps;
  sequence.video_scale = job.video_scale;
  sequence.flip_horizontal = job.flip_horizontal;

  sequence.subtitle.srt_path = job.subtitle_srt;
  sequence.subtitle.text = job.subtitle_text;
  sequence.subtitle.font_family = job.subtitle_font_family;
  sequence.subtitle.font_path = job.subtitle_font_path;
  sequence.subtitle.text_color = job.subtitle_text_color;
  sequence.subtitle.outline_color = job.subtitle_outline_color;
  sequence.subtitle.back_color = job.subtitle_back_color;
  sequence.subtitle.font_size_percent = job.subtitle_font_size;
  sequence.subtitle.outline = job.subtitle_outline;
  sequence.subtitle.shadow = job.subtitle_shadow;
  sequence.subtitle.bold = job.subtitle_bold;
  sequence.subtitle.italic = job.subtitle_italic;
  sequence.subtitle.uppercase = job.subtitle_uppercase;
  sequence.subtitle.wrap = job.subtitle_wrap;
  sequence.subtitle.clip = job.subtitle_clip;
  sequence.subtitle.auto_fit = job.subtitle_auto_fit;
  sequence.subtitle.padding_x = job.subtitle_padding_x;
  sequence.subtitle.padding_y = job.subtitle_padding_y;
  sequence.subtitle.align_h = job.subtitle_align_h;
  sequence.subtitle.align_v = job.subtitle_align_v;
  sequence.subtitle.opacity = job.subtitle_opacity;
  sequence.subtitle.regions = job.subtitle_regions.empty() ? job.regions : job.subtitle_regions;

  sequence.blur_box.enabled = job.subtitle_gaussian_blur || !job.blur_regions.empty();
  sequence.blur_box.regions = job.blur_regions.empty() ? job.regions : job.blur_regions;

  sequence.watermark.enabled = !job.watermark_text.empty() || !job.logo_path.empty();
  return sequence;
}

}  // namespace video_engine::timeline

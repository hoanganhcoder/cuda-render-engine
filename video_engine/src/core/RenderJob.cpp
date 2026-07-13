#include "core/RenderJob.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <utility>

namespace py = pybind11;

namespace video_engine {

namespace {

template <typename T>
T requiredValue(const py::dict& dict, const char* key) {
  if (!dict.contains(key)) {
    throw std::runtime_error(std::string("Missing required job field: ") + key);
  }
  return py::cast<T>(dict[key]);
}

template <typename T>
T optionalValue(const py::dict& dict, const char* key, T fallback) {
  if (!dict.contains(key) || dict[key].is_none()) {
    return fallback;
  }
  return py::cast<T>(dict[key]);
}

template <typename T>
T optionalAliasValue(const py::dict& dict, const char* primary_key, const char* alias_key, T fallback) {
  if (dict.contains(primary_key) && !dict[primary_key].is_none()) {
    return py::cast<T>(dict[primary_key]);
  }
  if (dict.contains(alias_key) && !dict[alias_key].is_none()) {
    return py::cast<T>(dict[alias_key]);
  }
  return fallback;
}

py::dict optionalDict(const py::dict& dict, const char* key) {
  if (!dict.contains(key) || dict[key].is_none()) {
    return py::dict();
  }
  return py::cast<py::dict>(dict[key]);
}

template <typename T>
T optionalScopedValue(const py::dict& scoped_dict, const py::dict& root_dict, const char* scoped_key, const char* root_key, T fallback) {
  if (!scoped_dict.empty() && scoped_dict.contains(scoped_key) && !scoped_dict[scoped_key].is_none()) {
    return py::cast<T>(scoped_dict[scoped_key]);
  }
  return optionalValue<T>(root_dict, root_key, fallback);
}

template <typename T>
T optionalScopedAliasValue(
    const py::dict& scoped_dict,
    const py::dict& root_dict,
    const char* scoped_key,
    const char* scoped_alias_key,
    const char* root_key,
    const char* root_alias_key,
    T fallback) {
  if (!scoped_dict.empty()) {
    if (scoped_dict.contains(scoped_key) && !scoped_dict[scoped_key].is_none()) {
      return py::cast<T>(scoped_dict[scoped_key]);
    }
    if (scoped_dict.contains(scoped_alias_key) && !scoped_dict[scoped_alias_key].is_none()) {
      return py::cast<T>(scoped_dict[scoped_alias_key]);
    }
  }
  return optionalAliasValue<T>(root_dict, root_key, root_alias_key, fallback);
}

Region parseRegionWithDefaults(const py::handle& handle, double default_start, double default_end) {
  py::dict dict = py::cast<py::dict>(handle);
  Region region;
  region.start = optionalValue<double>(dict, "start", default_start);
  region.end = optionalValue<double>(dict, "end", default_end);
  region.x = requiredValue<int>(dict, "x");
  region.y = requiredValue<int>(dict, "y");
  region.w = requiredValue<int>(dict, "w");
  region.h = requiredValue<int>(dict, "h");
  region.strength = optionalValue<float>(dict, "strength", 0.85f);
  region.feather = optionalValue<float>(dict, "feather", 32.0f);
  region.vertical_stretch = optionalValue<float>(dict, "vertical_stretch", 1.0f);
  region.horizontal_blur = optionalValue<float>(dict, "horizontal_blur", 0.15f);
  region.temporal_blend = optionalValue<float>(dict, "temporal_blend", 0.0f);
  return region;
}

Region parseRegion(const py::handle& handle) {
  return parseRegionWithDefaults(handle, 0.0, 1.0e12);
}

void applyBlurDefaults(Region& region, const py::dict& blur_dict) {
  if (blur_dict.empty()) {
    return;
  }
  region.strength = optionalValue<float>(blur_dict, "strength", region.strength);
  region.feather = optionalValue<float>(blur_dict, "feather", region.feather);
  region.vertical_stretch = optionalValue<float>(blur_dict, "vertical_stretch", region.vertical_stretch);
  region.horizontal_blur = optionalValue<float>(blur_dict, "horizontal_blur", region.horizontal_blur);
  region.temporal_blend = optionalValue<float>(blur_dict, "temporal_blend", region.temporal_blend);
}

std::vector<Region> parseRegionList(const py::dict& dict, const char* key, const py::dict& blur_defaults) {
  std::vector<Region> regions;
  if (!dict.contains(key) || dict[key].is_none()) {
    return regions;
  }
  py::handle region_value = dict[key];
  if (py::isinstance<py::dict>(region_value)) {
    Region region = parseRegion(region_value);
    applyBlurDefaults(region, blur_defaults);
    regions.push_back(region);
    return regions;
  }
  py::list region_list = py::cast<py::list>(region_value);
  regions.reserve(region_list.size());
  for (const py::handle& item : region_list) {
    Region region = parseRegion(item);
    applyBlurDefaults(region, blur_defaults);
    regions.push_back(region);
  }
  return regions;
}

bool looksLikeFontPath(const std::string& value) {
  if (value.empty()) {
    return false;
  }
  const std::string lower = [&]() {
    std::string output = value;
    for (char& ch : output) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return output;
  }();
  const auto has_suffix = [&](const char* suffix) {
    const std::string suffix_string(suffix);
    return lower.size() >= suffix_string.size() &&
           lower.compare(lower.size() - suffix_string.size(), suffix_string.size(), suffix_string) == 0;
  };
  return has_suffix(".ttf") || has_suffix(".otf") || has_suffix(".ttc");
}

bool hasExtension(const std::string& value, const char* extension) {
  std::string lower = value;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  const std::string suffix(extension);
  return lower.size() >= suffix.size() && lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string lowerString(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

void assignSubtitlePath(RenderJob& job, const std::string& path) {
  if (path.empty()) {
    return;
  }
  if (hasExtension(path, ".ass") || hasExtension(path, ".ssa")) {
    job.subtitle_ass = path;
  } else {
    job.subtitle_srt = path;
  }
}

}  // namespace

RenderJob RenderJob::fromPythonDict(const py::dict& job_dict) {
  RenderJob job;
  const bool has_tracks = job_dict.contains("tracks") && !job_dict["tracks"].is_none();
  const py::dict subtitle_dict = optionalDict(job_dict, "subtitle");
  const py::dict watermark_dict = optionalDict(job_dict, "watermark");
  const py::dict logo_dict = optionalDict(job_dict, "logo");

  job.input = has_tracks ? "" : requiredValue<std::string>(job_dict, "input");
  job.output = optionalAliasValue<std::string>(job_dict, "output", "output_path", "");
  job.audio_path = optionalAliasValue<std::string>(job_dict, "audio_path", "audio", "");
  if (job.output.empty() && !has_tracks) {
    throw std::runtime_error("Missing required job field: output");
  }
  job.width = optionalValue<int>(job_dict, "width", 0);
  job.height = optionalValue<int>(job_dict, "height", 0);
  job.fps = optionalValue<double>(job_dict, "fps", 0.0);
  job.video_aspect_ratio = optionalValue<std::string>(job_dict, "video_aspect_ratio", job.video_aspect_ratio);
  job.bg_color = optionalValue<std::string>(job_dict, "bg_color", job.bg_color);
  job.video_scale = optionalValue<float>(job_dict, "video_scale", 1.0f);
  job.video_time_scale = optionalAliasValue<float>(job_dict, "video_time_scale", "video_stretch", 1.0f);
  job.flip_horizontal = optionalValue<bool>(job_dict, "flip_horizontal", false);
  job.subtitle_gaussian_blur = optionalScopedValue<bool>(subtitle_dict, job_dict, "gaussian_blur", "subtitle_gaussian_blur", true);
  job.subtitle_srt = optionalScopedAliasValue<std::string>(subtitle_dict, job_dict, "srt", "subtitle_srt", "subtitle_srt", "subtitle_srt", "");
  job.subtitle_ass = optionalScopedAliasValue<std::string>(subtitle_dict, job_dict, "ass", "subtitle_ass", "subtitle_ass", "subtitle_ass", "");
  assignSubtitlePath(job, optionalScopedAliasValue<std::string>(subtitle_dict, job_dict, "path", "subtitle_path", "subtitle_path", "subtitle_path", ""));
  job.subtitle_renderer =
      optionalScopedAliasValue<std::string>(subtitle_dict, job_dict, "renderer", "subtitle_renderer", "subtitle_renderer", "subtitle_renderer", "auto");
  job.subtitle_text = optionalScopedAliasValue<std::string>(subtitle_dict, job_dict, "text", "subtitle_text", "subtitle_text", "subtitle_text", "");
  job.subtitle_font_family = optionalScopedAliasValue<std::string>(subtitle_dict, job_dict, "font", "font_family", "subtitle_font_family", "subtitle_font_family", "Noto Sans");
  job.subtitle_font_path = optionalScopedAliasValue<std::string>(subtitle_dict, job_dict, "font_ttf", "font_path", "subtitle_font_path", "subtitle_font_path", "");
  job.subtitle_text_color = optionalScopedAliasValue<std::string>(subtitle_dict, job_dict, "color", "text_color", "subtitle_text_color", "subtitle_text_color", "#FFF200");
  job.subtitle_outline_color = optionalScopedAliasValue<std::string>(subtitle_dict, job_dict, "outline_color", "stroke_color", "subtitle_outline_color", "subtitle_outline_color", "#101010");
  job.subtitle_back_color = optionalScopedAliasValue<std::string>(subtitle_dict, job_dict, "back_color", "background_color", "subtitle_back_color", "subtitle_back_color", "#00000000");
  job.subtitle_font_scale = optionalScopedValue<int>(subtitle_dict, job_dict, "font_scale", "subtitle_font_scale", 4);
  job.subtitle_font_size = optionalScopedAliasValue<float>(subtitle_dict, job_dict, "size", "font_size", "subtitle_font_size", "subtitle_font_size", 1.5f);
  job.subtitle_margin = optionalScopedValue<int>(subtitle_dict, job_dict, "margin", "subtitle_margin", 8);
  job.subtitle_outline = optionalScopedAliasValue<int>(subtitle_dict, job_dict, "outline", "stroke", "subtitle_outline", "subtitle_outline", 3);
  job.subtitle_shadow = optionalScopedValue<int>(subtitle_dict, job_dict, "shadow", "subtitle_shadow", 0);
  job.subtitle_bold = optionalScopedValue<bool>(subtitle_dict, job_dict, "bold", "subtitle_bold", true);
  job.subtitle_italic = optionalScopedAliasValue<bool>(subtitle_dict, job_dict, "italic", "i", "subtitle_italic", "subtitle_italic", true);
  job.subtitle_uppercase = optionalScopedAliasValue<bool>(subtitle_dict, job_dict, "upper", "uppercase", "subtitle_uppercase", "subtitle_uppercase", false);
  job.subtitle_opacity = optionalScopedValue<float>(subtitle_dict, job_dict, "opacity", "subtitle_opacity", 1.0f);
  job.subtitle_wrap = optionalScopedValue<bool>(subtitle_dict, job_dict, "wrap", "subtitle_wrap", true);
  job.subtitle_clip = optionalScopedValue<bool>(subtitle_dict, job_dict, "clip", "subtitle_clip", false);
  job.subtitle_auto_fit = optionalScopedValue<bool>(subtitle_dict, job_dict, "auto_fit", "subtitle_auto_fit", true);
  job.subtitle_padding_x = optionalScopedValue<int>(subtitle_dict, job_dict, "padding_x", "subtitle_padding_x", 0);
  job.subtitle_padding_y = optionalScopedValue<int>(subtitle_dict, job_dict, "padding_y", "subtitle_padding_y", 0);
  job.subtitle_align_h = optionalScopedAliasValue<std::string>(subtitle_dict, job_dict, "align_h", "subtitle_align_h", "subtitle_align_h", "subtitle_align_h", "center");
  job.subtitle_align_v = optionalScopedAliasValue<std::string>(subtitle_dict, job_dict, "align_v", "subtitle_align_v", "subtitle_align_v", "subtitle_align_v", "middle");

  job.logo_path = optionalScopedAliasValue<std::string>(logo_dict, job_dict, "path", "logo_path", "logo_path", "logo_path", "");
  job.logo_scale = optionalScopedValue<float>(logo_dict, job_dict, "scale", "logo_scale", 0.18f);
  job.logo_opacity = optionalScopedValue<float>(logo_dict, job_dict, "opacity", "logo_opacity", 0.22f);
  job.logo_margin = optionalScopedValue<int>(logo_dict, job_dict, "margin", "logo_margin", 24);
  job.logo_bounce = optionalScopedValue<bool>(logo_dict, job_dict, "bounce", "logo_bounce", false);
  job.logo_speed_x = optionalScopedValue<float>(logo_dict, job_dict, "speed_x", "logo_speed_x", 72.0f);
  job.logo_speed_y = optionalScopedValue<float>(logo_dict, job_dict, "speed_y", "logo_speed_y", 48.0f);
  if (!logo_dict.empty() && logo_dict.contains("position") && !logo_dict["position"].is_none()) {
    py::dict position_dict = py::cast<py::dict>(logo_dict["position"]);
    job.logo_position_x = optionalValue<float>(position_dict, "x", job.logo_position_x);
    job.logo_position_y = optionalValue<float>(position_dict, "y", job.logo_position_y);
  }

  job.watermark_text = optionalScopedAliasValue<std::string>(watermark_dict, job_dict, "text", "text_logo", "watermark_text", "text_logo", "");
  job.watermark_font_family = optionalScopedAliasValue<std::string>(watermark_dict, job_dict, "font", "font_family", "watermark_font_family", "watermark_font_family", "Noto Sans");
  job.watermark_font_path = optionalScopedAliasValue<std::string>(watermark_dict, job_dict, "font_ttf", "font_path", "watermark_font_path", "watermark_font_path", "");
  job.watermark_text_color = optionalScopedAliasValue<std::string>(watermark_dict, job_dict, "color", "text_color", "watermark_text_color", "watermark_text_color", "#FFFFFF");
  job.watermark_outline_color = optionalScopedAliasValue<std::string>(watermark_dict, job_dict, "outline_color", "stroke_color", "watermark_outline_color", "watermark_outline_color", "#000000");
  job.watermark_back_color = optionalScopedAliasValue<std::string>(watermark_dict, job_dict, "back_color", "background_color", "watermark_back_color", "watermark_back_color", "#00000000");
  job.watermark_font_size = optionalScopedAliasValue<float>(watermark_dict, job_dict, "size", "font_size", "watermark_font_size", "watermark_font_size", 4.0f);
  job.watermark_outline = optionalScopedAliasValue<int>(watermark_dict, job_dict, "outline", "stroke", "watermark_outline", "watermark_outline", 1);
  job.watermark_shadow = optionalScopedValue<int>(watermark_dict, job_dict, "shadow", "watermark_shadow", 0);
  job.watermark_margin = optionalScopedValue<int>(watermark_dict, job_dict, "margin", "watermark_margin", 24);
  job.watermark_bold = optionalScopedValue<bool>(watermark_dict, job_dict, "bold", "watermark_bold", true);
  job.watermark_italic = optionalScopedAliasValue<bool>(watermark_dict, job_dict, "italic", "i", "watermark_italic", "watermark_italic", true);
  job.watermark_uppercase = optionalScopedAliasValue<bool>(watermark_dict, job_dict, "upper", "uppercase", "watermark_uppercase", "watermark_uppercase", false);
  job.watermark_bounce = optionalScopedAliasValue<bool>(watermark_dict, job_dict, "bounce", "text_logo_bounce", "watermark_bounce", "text_logo_bounce", false);
  job.watermark_speed_x = optionalScopedValue<float>(watermark_dict, job_dict, "speed_x", "watermark_speed_x", 96.0f);
  job.watermark_speed_y = optionalScopedValue<float>(watermark_dict, job_dict, "speed_y", "watermark_speed_y", 64.0f);
  job.watermark_opacity = optionalScopedAliasValue<float>(watermark_dict, job_dict, "opacity", "text_logo_opacity", "watermark_opacity", "text_logo_opacity", 0.28f);

  if (job.subtitle_font_path.empty() && looksLikeFontPath(job.subtitle_font_family)) {
    job.subtitle_font_path = job.subtitle_font_family;
    job.subtitle_font_family = std::filesystem::path(job.subtitle_font_path).stem().string();
  }
  if (job.watermark_font_path.empty() && looksLikeFontPath(job.watermark_font_family)) {
    job.watermark_font_path = job.watermark_font_family;
    job.watermark_font_family = std::filesystem::path(job.watermark_font_path).stem().string();
  }

  if (has_tracks) {
    py::list tracks = py::cast<py::list>(job_dict["tracks"]);
    for (const py::handle& track_handle : tracks) {
      py::dict track = py::cast<py::dict>(track_handle);
      const std::string type = optionalValue<std::string>(track, "type", "");
      if (type == "video") {
        job.input = requiredValue<std::string>(track, "path");
        job.video_scale = optionalValue<float>(track, "video_scale", job.video_scale);
        job.video_time_scale = optionalAliasValue<float>(track, "video_time_scale", "video_stretch", job.video_time_scale);
        job.flip_horizontal = optionalValue<bool>(track, "flip_horizontal", job.flip_horizontal);
        job.video_align_h = optionalAliasValue<std::string>(track, "align_h", "h", job.video_align_h);
        job.video_align_v = optionalAliasValue<std::string>(track, "align_v", "v", job.video_align_v);
        job.resize_mode = optionalValue<std::string>(track, "resize_mode", job.resize_mode);
      } else if (type == "audio") {
        AudioTrackSpec audio;
        audio.path = requiredValue<std::string>(track, "path");
        job.audio_tracks.push_back(audio);
        if (job.audio_path.empty()) {
          job.audio_path = audio.path;
        }
      } else if (type == "subtitle") {
        const bool subtitle_track_blur = optionalValue<bool>(track, "gaussian_blur", false);
        job.subtitle_srt = optionalAliasValue<std::string>(track, "srt", "subtitle_srt", job.subtitle_srt);
        job.subtitle_ass = optionalAliasValue<std::string>(track, "ass", "subtitle_ass", job.subtitle_ass);
        assignSubtitlePath(job, optionalAliasValue<std::string>(track, "path", "subtitle_path", ""));
        job.subtitle_renderer = optionalAliasValue<std::string>(track, "renderer", "subtitle_renderer", job.subtitle_renderer);
        job.subtitle_text = optionalAliasValue<std::string>(track, "text", "subtitle_text", job.subtitle_text);
        job.subtitle_font_family = optionalAliasValue<std::string>(track, "font", "font_family", job.subtitle_font_family);
        job.subtitle_font_path = optionalAliasValue<std::string>(track, "font_ttf", "font_path", job.subtitle_font_path);
        job.subtitle_font_size = optionalAliasValue<float>(track, "size", "font_size", job.subtitle_font_size);
        job.subtitle_bold = optionalValue<bool>(track, "bold", job.subtitle_bold);
        job.subtitle_italic = optionalAliasValue<bool>(track, "italic", "i", job.subtitle_italic);
        job.subtitle_uppercase = optionalAliasValue<bool>(track, "upper", "uppercase", job.subtitle_uppercase);
        job.subtitle_text_color = optionalAliasValue<std::string>(track, "color", "text_color", job.subtitle_text_color);
        job.subtitle_outline_color =
            optionalAliasValue<std::string>(track, "outline_color", "stroke_color", job.subtitle_outline_color);
        job.subtitle_back_color =
            optionalAliasValue<std::string>(track, "back_color", "background_color", job.subtitle_back_color);
        job.subtitle_outline = optionalAliasValue<int>(track, "outline", "stroke", job.subtitle_outline);
        job.subtitle_shadow = optionalValue<int>(track, "shadow", job.subtitle_shadow);
        job.subtitle_margin = optionalValue<int>(track, "margin", job.subtitle_margin);
        job.subtitle_opacity = optionalValue<float>(track, "opacity", job.subtitle_opacity);
        job.subtitle_wrap = optionalValue<bool>(track, "wrap", job.subtitle_wrap);
        job.subtitle_clip = optionalValue<bool>(track, "clip", false);
        job.subtitle_auto_fit = optionalValue<bool>(track, "auto_fit", job.subtitle_auto_fit);
        job.subtitle_padding_x = optionalValue<int>(track, "padding_x", job.subtitle_padding_x);
        job.subtitle_padding_y = optionalValue<int>(track, "padding_y", job.subtitle_padding_y);
        job.subtitle_align_h = optionalAliasValue<std::string>(track, "align_h", "h", job.subtitle_align_h);
        job.subtitle_align_v = optionalAliasValue<std::string>(track, "align_v", "v", job.subtitle_align_v);
        const py::dict blur_dict = optionalDict(track, "blur");
        std::vector<Region> regions = parseRegionList(track, "regions", blur_dict);
        job.subtitle_regions.insert(job.subtitle_regions.end(), regions.begin(), regions.end());
        if (subtitle_track_blur) {
          job.subtitle_gaussian_blur = true;
          job.blur_regions.insert(job.blur_regions.end(), regions.begin(), regions.end());
        }
      } else if (type == "gaussian_blur") {
        std::vector<Region> regions = parseRegionList(track, "regions", track);
        job.blur_regions.insert(job.blur_regions.end(), regions.begin(), regions.end());
      } else if (type == "watermark") {
        job.watermark_text = optionalAliasValue<std::string>(track, "text", "text_logo", job.watermark_text);
        job.watermark_font_family = optionalAliasValue<std::string>(track, "font", "font_family", job.watermark_font_family);
        job.watermark_font_path = optionalAliasValue<std::string>(track, "font_ttf", "font_path", job.watermark_font_path);
        job.watermark_font_size = optionalAliasValue<float>(track, "size", "font_size", job.watermark_font_size);
        job.watermark_bold = optionalValue<bool>(track, "bold", job.watermark_bold);
        job.watermark_italic = optionalAliasValue<bool>(track, "italic", "i", job.watermark_italic);
        job.watermark_uppercase = optionalAliasValue<bool>(track, "upper", "uppercase", job.watermark_uppercase);
        job.watermark_text_color = optionalAliasValue<std::string>(track, "color", "text_color", job.watermark_text_color);
        job.watermark_outline_color =
            optionalAliasValue<std::string>(track, "outline_color", "stroke_color", job.watermark_outline_color);
        job.watermark_bounce = optionalAliasValue<bool>(track, "bounce", "text_logo_bounce", job.watermark_bounce);
        job.watermark_opacity = optionalAliasValue<float>(track, "opacity", "text_logo_opacity", job.watermark_opacity);
        job.watermark_speed_x = optionalValue<float>(track, "speed_x", job.watermark_speed_x);
        job.watermark_speed_y = optionalValue<float>(track, "speed_y", job.watermark_speed_y);
      } else if (type == "logo") {
        job.logo_path = optionalAliasValue<std::string>(track, "path", "logo_path", job.logo_path);
        job.logo_scale = optionalValue<float>(track, "scale", job.logo_scale);
        job.logo_opacity = optionalValue<float>(track, "opacity", job.logo_opacity);
        job.logo_bounce = optionalValue<bool>(track, "bounce", job.logo_bounce);
        job.logo_speed_x = optionalValue<float>(track, "speed_x", job.logo_speed_x);
        job.logo_speed_y = optionalValue<float>(track, "speed_y", job.logo_speed_y);
        if (track.contains("position") && !track["position"].is_none()) {
          py::dict position_dict = py::cast<py::dict>(track["position"]);
          job.logo_position_x = optionalValue<float>(position_dict, "x", job.logo_position_x);
          job.logo_position_y = optionalValue<float>(position_dict, "y", job.logo_position_y);
        }
      } else if (type == "image") {
        ImageOverlaySpec image;
        image.path = requiredValue<std::string>(track, "path");
        image.width = optionalValue<std::string>(track, "w", image.width);
        image.height = optionalValue<std::string>(track, "h", image.height);
        image.resize_mode = optionalValue<std::string>(track, "resize_mode", image.resize_mode);
        image.opacity = optionalValue<float>(track, "opacity", image.opacity);
        if (track.contains("position") && !track["position"].is_none()) {
          py::dict position_dict = py::cast<py::dict>(track["position"]);
          image.position_x = optionalValue<float>(position_dict, "x", image.position_x);
          image.position_y = optionalValue<float>(position_dict, "y", image.position_y);
        }
        job.image_overlays.push_back(std::move(image));
      }
    }
  } else if (!subtitle_dict.empty() && subtitle_dict.contains("regions") && !subtitle_dict["regions"].is_none()) {
    job.regions = parseRegionList(subtitle_dict, "regions", py::dict());
    job.subtitle_regions = job.regions;
    job.blur_regions = job.regions;
  } else if (job_dict.contains("regions") && !job_dict["regions"].is_none()) {
    job.regions = parseRegionList(job_dict, "regions", py::dict());
    job.subtitle_regions = job.regions;
    job.blur_regions = job.regions;
  }

  if (job.subtitle_font_path.empty() && looksLikeFontPath(job.subtitle_font_family)) {
    job.subtitle_font_path = job.subtitle_font_family;
    job.subtitle_font_family = std::filesystem::path(job.subtitle_font_path).stem().string();
  }
  if (job.watermark_font_path.empty() && looksLikeFontPath(job.watermark_font_family)) {
    job.watermark_font_path = job.watermark_font_family;
    job.watermark_font_family = std::filesystem::path(job.watermark_font_path).stem().string();
  }
  job.subtitle_renderer = lowerString(job.subtitle_renderer);

  job.validate();
  return job;
}

void RenderJob::validate() const {
  auto isValidHexColor = [](const std::string& value) {
    if (value.empty() || value[0] != '#') {
      return false;
    }
    const size_t hex_digits = value.size() - 1;
    if (hex_digits != 6 && hex_digits != 8) {
      return false;
    }
    for (size_t index = 1; index < value.size(); ++index) {
      if (!std::isxdigit(static_cast<unsigned char>(value[index]))) {
        return false;
      }
    }
    return true;
  };

  if (input.empty()) {
    throw std::runtime_error("Input path must not be empty.");
  }
  if (output.empty()) {
    throw std::runtime_error("Output path must not be empty.");
  }
  if (width < 0 || height < 0) {
    throw std::runtime_error("Job width/height must be >= 0.");
  }
  if (fps < 0.0) {
    throw std::runtime_error("Job fps must be >= 0.");
  }
  if (video_scale < 1.0f) {
    throw std::runtime_error("video_scale must be >= 1.0.");
  }
  if (video_time_scale <= 0.0f) {
    throw std::runtime_error("video_time_scale must be > 0.");
  }
  if (resize_mode != "fit" && resize_mode != "fill" && resize_mode != "stretch") {
    throw std::runtime_error("resize_mode must be one of: fit, fill, stretch.");
  }
  if (subtitle_renderer != "auto" && subtitle_renderer != "textbox" && subtitle_renderer != "libass" &&
      subtitle_renderer != "ass") {
    throw std::runtime_error("subtitle_renderer must be one of: auto, textbox, libass, ass.");
  }
  if (video_align_h != "left" && video_align_h != "center" && video_align_h != "right") {
    throw std::runtime_error("video horizontal alignment must be left, center, or right.");
  }
  if (video_align_v != "top" && video_align_v != "center" && video_align_v != "middle" && video_align_v != "bottom") {
    throw std::runtime_error("video vertical alignment must be top, center/middle, or bottom.");
  }
  if (subtitle_font_scale < 1) {
    throw std::runtime_error("subtitle_font_scale must be >= 1.");
  }
  if (subtitle_margin < 0) {
    throw std::runtime_error("subtitle_margin must be >= 0.");
  }
  if (subtitle_font_size <= 0.0f) {
    throw std::runtime_error("subtitle_font_size must be > 0 and expressed as % of video height.");
  }
  if (subtitle_padding_x < 0 || subtitle_padding_y < 0) {
    throw std::runtime_error("subtitle_padding_x/y must be >= 0.");
  }
  if (subtitle_outline < 0) {
    throw std::runtime_error("subtitle_outline must be >= 0.");
  }
  if (subtitle_shadow < 0) {
    throw std::runtime_error("subtitle_shadow must be >= 0.");
  }
  if (logo_scale <= 0.0f) {
    throw std::runtime_error("logo_scale must be > 0.");
  }
  if (logo_margin < 0) {
    throw std::runtime_error("logo_margin must be >= 0.");
  }
  if (logo_opacity < 0.0f || logo_opacity > 1.0f) {
    throw std::runtime_error("logo_opacity must be within [0, 1].");
  }
  if (watermark_font_size <= 0.0f) {
    throw std::runtime_error("watermark_font_size must be > 0 and expressed as % of video height.");
  }
  if (watermark_outline < 0) {
    throw std::runtime_error("watermark_outline must be >= 0.");
  }
  if (watermark_shadow < 0) {
    throw std::runtime_error("watermark_shadow must be >= 0.");
  }
  if (watermark_margin < 0) {
    throw std::runtime_error("watermark_margin must be >= 0.");
  }
  if (!isValidHexColor(subtitle_text_color)) {
    throw std::runtime_error("subtitle_text_color must be in #RRGGBB or #RRGGBBAA format.");
  }
  if (!isValidHexColor(subtitle_outline_color)) {
    throw std::runtime_error("subtitle_outline_color must be in #RRGGBB or #RRGGBBAA format.");
  }
  if (!isValidHexColor(subtitle_back_color)) {
    throw std::runtime_error("subtitle_back_color must be in #RRGGBB or #RRGGBBAA format.");
  }
  if (!isValidHexColor(watermark_text_color)) {
    throw std::runtime_error("watermark_text_color must be in #RRGGBB or #RRGGBBAA format.");
  }
  if (!isValidHexColor(watermark_outline_color)) {
    throw std::runtime_error("watermark_outline_color must be in #RRGGBB or #RRGGBBAA format.");
  }
  if (!isValidHexColor(watermark_back_color)) {
    throw std::runtime_error("watermark_back_color must be in #RRGGBB or #RRGGBBAA format.");
  }
  if (subtitle_opacity < 0.0f || subtitle_opacity > 1.0f) {
    throw std::runtime_error("subtitle_opacity must be within [0, 1].");
  }
  const auto subtitle_align_h_lower = [&]() {
    std::string value = subtitle_align_h;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
  }();
  const auto subtitle_align_v_lower = [&]() {
    std::string value = subtitle_align_v;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
  }();
  if (subtitle_align_h_lower != "left" && subtitle_align_h_lower != "center" && subtitle_align_h_lower != "right") {
    throw std::runtime_error("subtitle_align_h must be left, center, or right.");
  }
  if (subtitle_align_v_lower != "top" && subtitle_align_v_lower != "middle" && subtitle_align_v_lower != "bottom") {
    throw std::runtime_error("subtitle_align_v must be top, middle, or bottom.");
  }
  if (watermark_opacity < 0.0f || watermark_opacity > 1.0f) {
    throw std::runtime_error("watermark_opacity must be within [0, 1].");
  }
  for (const AudioTrackSpec& audio : audio_tracks) {
    if (audio.path.empty()) {
      throw std::runtime_error("audio track path must not be empty.");
    }
  }
  for (const ImageOverlaySpec& image : image_overlays) {
    if (image.path.empty()) {
      throw std::runtime_error("image track path must not be empty.");
    }
    if (image.resize_mode != "fit" && image.resize_mode != "fill" && image.resize_mode != "stretch") {
      throw std::runtime_error("image resize_mode must be one of: fit, fill, stretch.");
    }
    if (image.opacity < 0.0f || image.opacity > 1.0f) {
      throw std::runtime_error("image opacity must be within [0, 1].");
    }
  }

  for (const Region& region : regions) {
    if (region.w < 0 || region.h < 0) {
      throw std::runtime_error("Region width/height must be >= 0.");
    }
  }
}

}  // namespace video_engine

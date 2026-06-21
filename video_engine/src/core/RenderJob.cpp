#include "core/RenderJob.h"

#include <cctype>
#include <stdexcept>

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

Region parseRegion(const py::handle& handle) {
  py::dict dict = py::cast<py::dict>(handle);
  Region region;
  region.start = requiredValue<double>(dict, "start");
  region.end = requiredValue<double>(dict, "end");
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

}  // namespace

RenderJob RenderJob::fromPythonDict(const py::dict& job_dict) {
  RenderJob job;
  job.input = requiredValue<std::string>(job_dict, "input");
  job.output = requiredValue<std::string>(job_dict, "output");
  job.width = optionalValue<int>(job_dict, "width", 0);
  job.height = optionalValue<int>(job_dict, "height", 0);
  job.fps = optionalValue<double>(job_dict, "fps", 0.0);
  job.subtitle_srt = optionalValue<std::string>(job_dict, "subtitle_srt", "");
  job.subtitle_text = optionalValue<std::string>(job_dict, "subtitle_text", "");
  job.subtitle_font_family = optionalValue<std::string>(job_dict, "subtitle_font_family", "Noto Sans");
  job.subtitle_font_path = optionalValue<std::string>(job_dict, "subtitle_font_path", "");
  job.subtitle_text_color = optionalValue<std::string>(job_dict, "subtitle_text_color", "#FFF200");
  job.subtitle_outline_color = optionalValue<std::string>(job_dict, "subtitle_outline_color", "#101010");
  job.subtitle_back_color = optionalValue<std::string>(job_dict, "subtitle_back_color", "#00000000");
  job.subtitle_font_scale = optionalValue<int>(job_dict, "subtitle_font_scale", 4);
  job.subtitle_font_size = optionalValue<int>(job_dict, "subtitle_font_size", 36);
  job.subtitle_margin = optionalValue<int>(job_dict, "subtitle_margin", 8);
  job.subtitle_outline = optionalValue<int>(job_dict, "subtitle_outline", 3);
  job.subtitle_shadow = optionalValue<int>(job_dict, "subtitle_shadow", 0);
  job.subtitle_bold = optionalValue<bool>(job_dict, "subtitle_bold", true);
  job.subtitle_italic = optionalValue<bool>(job_dict, "subtitle_italic", false);
  job.subtitle_opacity = optionalValue<float>(job_dict, "subtitle_opacity", 1.0f);
  job.logo_path = optionalValue<std::string>(job_dict, "logo_path", "");
  job.logo_scale = optionalValue<float>(job_dict, "logo_scale", 0.18f);
  job.logo_opacity = optionalValue<float>(job_dict, "logo_opacity", 0.22f);
  job.logo_margin = optionalValue<int>(job_dict, "logo_margin", 24);
  job.logo_bounce = optionalValue<bool>(job_dict, "logo_bounce", false);
  job.logo_speed_x = optionalValue<float>(job_dict, "logo_speed_x", 72.0f);
  job.logo_speed_y = optionalValue<float>(job_dict, "logo_speed_y", 48.0f);
  job.watermark_text = optionalAliasValue<std::string>(job_dict, "watermark_text", "text_logo", "");
  job.watermark_font_family = optionalValue<std::string>(job_dict, "watermark_font_family", "Noto Sans");
  job.watermark_font_path = optionalValue<std::string>(job_dict, "watermark_font_path", "");
  job.watermark_text_color = optionalValue<std::string>(job_dict, "watermark_text_color", "#FFFFFF80");
  job.watermark_outline_color = optionalValue<std::string>(job_dict, "watermark_outline_color", "#00000020");
  job.watermark_back_color = optionalValue<std::string>(job_dict, "watermark_back_color", "#00000000");
  job.watermark_font_size = optionalValue<int>(job_dict, "watermark_font_size", 28);
  job.watermark_outline = optionalValue<int>(job_dict, "watermark_outline", 1);
  job.watermark_shadow = optionalValue<int>(job_dict, "watermark_shadow", 0);
  job.watermark_margin = optionalValue<int>(job_dict, "watermark_margin", 24);
  job.watermark_bold = optionalValue<bool>(job_dict, "watermark_bold", true);
  job.watermark_italic = optionalValue<bool>(job_dict, "watermark_italic", false);
  job.watermark_bounce = optionalAliasValue<bool>(job_dict, "watermark_bounce", "text_logo_bounce", false);
  job.watermark_speed_x = optionalValue<float>(job_dict, "watermark_speed_x", 96.0f);
  job.watermark_speed_y = optionalValue<float>(job_dict, "watermark_speed_y", 64.0f);
  job.watermark_opacity = optionalAliasValue<float>(job_dict, "watermark_opacity", "text_logo_opacity", 0.18f);

  if (job_dict.contains("regions") && !job_dict["regions"].is_none()) {
    py::list region_list = py::cast<py::list>(job_dict["regions"]);
    job.regions.reserve(region_list.size());
    for (const py::handle& item : region_list) {
      job.regions.push_back(parseRegion(item));
    }
  }

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
  if (subtitle_font_scale < 1) {
    throw std::runtime_error("subtitle_font_scale must be >= 1.");
  }
  if (subtitle_margin < 0) {
    throw std::runtime_error("subtitle_margin must be >= 0.");
  }
  if (subtitle_font_size < 1) {
    throw std::runtime_error("subtitle_font_size must be >= 1.");
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
  if (watermark_font_size < 1) {
    throw std::runtime_error("watermark_font_size must be >= 1.");
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
  if (watermark_opacity < 0.0f || watermark_opacity > 1.0f) {
    throw std::runtime_error("watermark_opacity must be within [0, 1].");
  }

  for (const Region& region : regions) {
    if (region.w < 0 || region.h < 0) {
      throw std::runtime_error("Region width/height must be >= 0.");
    }
  }
}

}  // namespace video_engine

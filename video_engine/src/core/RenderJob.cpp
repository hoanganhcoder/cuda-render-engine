#include "core/RenderJob.h"

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
  job.subtitle_font_scale = optionalValue<int>(job_dict, "subtitle_font_scale", 4);
  job.subtitle_font_size = optionalValue<int>(job_dict, "subtitle_font_size", 36);
  job.subtitle_margin = optionalValue<int>(job_dict, "subtitle_margin", 8);
  job.subtitle_outline = optionalValue<int>(job_dict, "subtitle_outline", 2);
  job.subtitle_shadow = optionalValue<int>(job_dict, "subtitle_shadow", 1);
  job.subtitle_bold = optionalValue<bool>(job_dict, "subtitle_bold", true);
  job.subtitle_italic = optionalValue<bool>(job_dict, "subtitle_italic", false);
  job.subtitle_opacity = optionalValue<float>(job_dict, "subtitle_opacity", 1.0f);

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
  if (subtitle_opacity < 0.0f || subtitle_opacity > 1.0f) {
    throw std::runtime_error("subtitle_opacity must be within [0, 1].");
  }

  for (const Region& region : regions) {
    if (region.w < 0 || region.h < 0) {
      throw std::runtime_error("Region width/height must be >= 0.");
    }
  }
}

}  // namespace video_engine

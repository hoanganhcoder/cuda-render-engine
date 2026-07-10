#include <pybind11/pybind11.h>
#include <pybind11/iostream.h>
#include <pybind11/numpy.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

#include "core/Logger.h"
#include "core/RenderEngine.h"

namespace py = pybind11;

namespace video_engine {

constexpr const char* kVersion = "0.3.2";

class ScopedPythonLogSink {
public:
  ScopedPythonLogSink() {
    previous_sink_ = Logger::setSink([](const std::string& line) {
      py::gil_scoped_acquire acquire;
      py::print(line, py::arg("flush") = true);
    });
  }

  ~ScopedPythonLogSink() {
    Logger::setSink(std::move(previous_sink_));
  }

  ScopedPythonLogSink(const ScopedPythonLogSink&) = delete;
  ScopedPythonLogSink& operator=(const ScopedPythonLogSink&) = delete;

private:
  Logger::Sink previous_sink_;
};

bool renderFromPython(const py::dict& job_dict) {
  RenderJob job = RenderJob::fromPythonDict(job_dict);
  RenderEngine engine;
  ScopedPythonLogSink python_log_sink;
  py::scoped_ostream_redirect stdout_redirect(
      std::cout,
      py::module_::import("sys").attr("stdout"));
  py::scoped_ostream_redirect stderr_redirect(
      std::cerr,
      py::module_::import("sys").attr("stderr"));
  return engine.render(job);
}

py::dict renderFrameFromPython(const py::dict& job_dict, double time_seconds) {
  py::dict preview_job;
  for (const auto& item : job_dict) {
    preview_job[item.first] = item.second;
  }
  if ((!preview_job.contains("output") || preview_job["output"].is_none()) &&
      (!preview_job.contains("output_path") || preview_job["output_path"].is_none())) {
    preview_job["output_path"] = "__video_engine_preview__.mp4";
  }
  RenderJob job = RenderJob::fromPythonDict(preview_job);
  RenderEngine engine;
  ScopedPythonLogSink python_log_sink;
  py::scoped_ostream_redirect stdout_redirect(
      std::cout,
      py::module_::import("sys").attr("stdout"));
  py::scoped_ostream_redirect stderr_redirect(
      std::cerr,
      py::module_::import("sys").attr("stderr"));

  RenderedFrame frame = engine.renderFrame(job, time_seconds);
  if (frame.width <= 0 || frame.height <= 0 || frame.rgba.empty()) {
    throw std::runtime_error("Failed to render preview frame.");
  }
  auto data = std::make_unique<std::vector<uint8_t>>(std::move(frame.rgba));
  py::capsule capsule(data.get(), [](void* pointer) {
    delete reinterpret_cast<std::vector<uint8_t>*>(pointer);
  });
  py::array_t<uint8_t> rgba(
      {frame.height, frame.width, 4},
      {frame.width * 4, 4, 1},
      data->data(),
      capsule);
  data.release();

  py::dict result;
  result["rgba"] = rgba;
  result["width"] = frame.width;
  result["height"] = frame.height;
  result["timestamp"] = frame.timestamp_seconds;
  return result;
}

}  // namespace video_engine

PYBIND11_MODULE(_video_engine_native, module) {
  module.doc() = "Headless CUDA/FFmpeg video render engine";
  module.def("version", []() { return std::string(video_engine::kVersion); });
  module.def("render", &video_engine::renderFromPython, py::arg("job"));
  module.def("render_frame", &video_engine::renderFrameFromPython, py::arg("job"), py::arg("time_seconds"));
}

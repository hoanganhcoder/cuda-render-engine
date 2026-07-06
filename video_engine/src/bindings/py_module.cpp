#include <pybind11/pybind11.h>
#include <pybind11/iostream.h>

#include <iostream>
#include <utility>

#include "core/Logger.h"
#include "core/RenderEngine.h"

namespace py = pybind11;

namespace video_engine {

constexpr const char* kVersion = "0.2.5";

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

}  // namespace video_engine

PYBIND11_MODULE(_video_engine_native, module) {
  module.doc() = "Headless CUDA/FFmpeg video render engine";
  module.def("version", []() { return std::string(video_engine::kVersion); });
  module.def("render", &video_engine::renderFromPython, py::arg("job"));
}

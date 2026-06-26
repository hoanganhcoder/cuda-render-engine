#include <pybind11/pybind11.h>
#include <pybind11/iostream.h>

#include <iostream>

#include "core/RenderEngine.h"

namespace py = pybind11;

namespace video_engine {

constexpr const char* kVersion = "0.1.2";

bool renderFromPython(const py::dict& job_dict) {
  RenderJob job = RenderJob::fromPythonDict(job_dict);
  RenderEngine engine;
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

#include <pybind11/pybind11.h>

#include "core/RenderEngine.h"

namespace py = pybind11;

namespace video_engine {

constexpr const char* kVersion = "0.1.0";

bool renderFromPython(const py::dict& job_dict) {
  RenderJob job = RenderJob::fromPythonDict(job_dict);
  RenderEngine engine;
  return engine.render(job);
}

}  // namespace video_engine

PYBIND11_MODULE(video_engine, module) {
  module.doc() = "Headless CUDA/FFmpeg video render engine";
  module.def("version", []() { return std::string(video_engine::kVersion); });
  module.def("render", &video_engine::renderFromPython, py::arg("job"));
}

#include "gft/log.hpp"
#include "gft/args.hpp"
#include "gft/glslang.hpp"
#include "gft/vk.hpp"
#include "gft/assert.hpp"
#include "gft/platform/windows.hpp"
#include "taichi/taichi_core.h"
#include "taichi/taichi_vulkan.h"

using namespace liong;
using namespace vk;


static const char* APP_NAME = "GraphiT-Template";
static const char* APP_DESC = "GraphiT project template.";


template<typename T>
TiNdArray allocate_ndarray(TiRuntime runtime, const std::vector<uint32_t>& shape) {
  size_t size = sizeof(T);
  TiNdArray out {};
  out.shape.dim_count = shape.size();
  for (size_t i = 0; i < shape.size(); ++i) {
    size *= out.shape.dims[i] = shape.at(i);
  }

  TiMemoryAllocateInfo mai {};
  mai.usage = TI_MEMORY_USAGE_STORAGE_BIT;
  mai.size = size;
  out.memory = ti_allocate_memory(runtime, &mai);
  return out;
}


struct AppConfig {
  bool verbose = false;
  bool kernel = false;
  std::string module_dir = "";
} CFG;

void initialize(int argc, const char** argv) {
  args::init_arg_parse(APP_NAME, APP_DESC);
  args::reg_arg<args::SwitchParser>("-v", "--verbose", CFG.verbose,
    "Produce extra amount of logs for debugging.");
  args::reg_arg<args::SwitchParser>("-k", "--kernel", CFG.kernel,
    "Run kernel instead of compute graph.");
  args::reg_arg<args::StringParser>("-m", "--module-dir", CFG.module_dir,
    "Precompiled AOT module root directory.");

  args::parse_args(argc, argv);

  extern void log_cb(log::LogLevel lv, const std::string & msg);
  log::set_log_callback(log_cb);
  log::LogLevel level = CFG.verbose ?
    log::LogLevel::L_LOG_LEVEL_DEBUG : log::LogLevel::L_LOG_LEVEL_INFO;
  log::set_log_filter_level(level);

  vk::initialize();
}

struct Module_fractal {
  virtual ~Module_fractal() { }
  virtual void fractal(float t, const TiNdArray& canvas) = 0;
};

struct Module_fractal_kernel : public Module_fractal {
  TiRuntime runtime_;
  TiAotModule aot_module_;
  TiKernel kernel_fractal_;

  Module_fractal_kernel(TiRuntime context, const char* module_path) :
    runtime_(context),
    aot_module_(ti_load_aot_module(context, module_path)),
    kernel_fractal_(ti_get_aot_module_kernel(aot_module_, "fractal")) {}
  virtual ~Module_fractal_kernel() override final {
    ti_destroy_aot_module(aot_module_);
  }

  virtual void fractal(float t, const TiNdArray& canvas) override final {
    std::array<TiArgument, 2> args {};
    args[0].type = TI_ARGUMENT_TYPE_F32;
    args[0].value.f32 = t;
    args[1].type = TI_ARGUMENT_TYPE_NDARRAY;
    args[1].value.ndarray = canvas;

    ti_launch_kernel(runtime_, kernel_fractal_, args.size(), args.data());
  }
};

struct Module_fractal_cgraph : public Module_fractal {
  TiRuntime runtime_;
  TiAotModule aot_module_;
  TiComputeGraph compute_graph_fractal_;

  Module_fractal_cgraph(TiRuntime context, const char* module_path) :
    runtime_(context),
    aot_module_(ti_load_aot_module(context, module_path)),
    compute_graph_fractal_(ti_get_aot_module_compute_graph(aot_module_, "fractal")) {}
  virtual ~Module_fractal_cgraph() override final {
    ti_destroy_aot_module(aot_module_);
  }

  virtual void fractal(float t, const TiNdArray& canvas) override final {
    std::array<TiNamedArgument, 2> args {};
    args[0].name = "t";
    args[0].arg.type = TI_ARGUMENT_TYPE_F32;
    args[0].arg.value.f32 = t;
    args[1].name = "canvas";
    args[1].arg.type = TI_ARGUMENT_TYPE_NDARRAY;
    args[1].arg.value.ndarray = canvas;

    ti_launch_compute_graph(runtime_, compute_graph_fractal_, args.size(), args.data());
  }
};

struct FractalApp {
  TiRuntime runtime_;

  std::unique_ptr<Module_fractal> module_fractal_;

  TiNdArray ndarray_canvas;

  scoped::Task copy_task;

  TiRuntime create_taichi_device(const scoped::Context& ctxt) {
    TiVulkanRuntimeInteropInfo vdii {};
    vdii.api_version = vk::get_inst().api_ver;
    vdii.instance = vk::get_inst().inst;
    vdii.physical_device = ctxt.inner->physdev();
    vdii.device = ctxt.inner->dev;
    const auto& comp_submit_detail = ctxt.inner->submit_details.at(L_SUBMIT_TYPE_COMPUTE);
    vdii.compute_queue = comp_submit_detail.queue;
    vdii.compute_queue_family_index = comp_submit_detail.qfam_idx;
    const auto& graph_submit_detail = ctxt.inner->submit_details.at(L_SUBMIT_TYPE_GRAPHICS);
    vdii.graphics_queue = graph_submit_detail.queue;
    vdii.graphics_queue_family_index = graph_submit_detail.qfam_idx;

    TiRuntime out = ti_import_vulkan_runtime(&vdii);
    return out;
  }

  scoped::Task create_copy_task(const scoped::Context& ctxt) {
    const char* comp_src = R"(
      #version 460 core
      layout(local_size_x_id=0, local_size_y_id=1, local_size_z_id=2) in;
      layout(binding=0) uniform Uniform {
        vec4 color;
        ivec2 size;
      };
      layout(binding=1) readonly buffer Src {
        float src[];
      };
      layout(binding=2, rgba8) writeonly uniform image2D dst;

      void main() {
        uvec2 global_id = gl_GlobalInvocationID.xy;
        int x = int(global_id.x);
        int y = int(global_id.y);
        if (x > size.x || y > size.y) { return; }

        imageStore(dst, ivec2(x, y), color * src[y * size.x + x]);
      }
    )";
    auto art = glslang::compile_comp(comp_src, "main");
    scoped::Task copy_task = ctxt.build_comp_task("copy")
      .comp(art.comp_spv)
      .rsc(L_RESOURCE_TYPE_UNIFORM_BUFFER)
      .rsc(L_RESOURCE_TYPE_STORAGE_BUFFER)
      .rsc(L_RESOURCE_TYPE_STORAGE_IMAGE)
      .workgrp_size(8, 8, 1)
      .build();
    return copy_task;
  }

  std::unique_ptr<Module_fractal> create_module_fractal(TiRuntime runtime, const std::string& module_path) {
    if (CFG.kernel) {
      log::info("kernel module is loaded");
      return std::unique_ptr<Module_fractal>(new Module_fractal_kernel(runtime_, (module_path + "/fractal").c_str()));
    } else {
      log::info("compute graph module is loaded");
      return std::unique_ptr<Module_fractal>(new Module_fractal_cgraph(runtime_, (module_path + "/fractal.cgraph").c_str()));
    }
  }

  FractalApp(const scoped::Context& ctxt, const std::string& module_path) :
    runtime_(create_taichi_device(ctxt)),
    module_fractal_(create_module_fractal(runtime_, module_path)),
    ndarray_canvas(allocate_ndarray<float>(runtime_, { 640, 320 })),
    copy_task(create_copy_task(ctxt)) {}
  ~FractalApp() {
    ti_destroy_runtime(runtime_);
  }

  void run(
    const scoped::Context& ctxt,
    const scoped::Swapchain& swapchain,
    uint32_t iframe
  ) {
    module_fractal_->fractal(iframe * 0.03f, ndarray_canvas);
    ti_submit(runtime_);

    TiVulkanMemoryInteropInfo vdmii;
    ti_export_vulkan_memory(runtime_, ndarray_canvas.memory, &vdmii);

    scoped::Image render_target_img = swapchain.get_img();
    uint32_t width = render_target_img.cfg().width;
    uint32_t height = render_target_img.cfg().height;

    struct Uniform {
      glm::vec4 color;
      glm::ivec2 size;
    } uniform;


    float pos = iframe % 100;
    float alpha = (pos < 50) ? (pos / 50.0f) : (2.0f - pos / 50.0f);

    uniform.color.r = alpha * 0.75f;
    uniform.color.g = 0.75f;
    uniform.color.b = (1.0f - alpha) * 0.75f;
    uniform.color.a = 1.0f;
    uniform.size = glm::ivec2(width, height);

    scoped::Buffer uniform_buf = ctxt.build_buf()
      .uniform()
      .streaming_with(uniform)
      .build();

    Buffer src_buf {};
    src_buf.buf = vdmii.buffer;
    src_buf.buf_cfg.align = 1;
    src_buf.buf_cfg.size = vdmii.size;
    L_ASSERT(vdmii.usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT != 0 &&
      vdmii.usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT != 0);
    src_buf.buf_cfg.usage =
      L_BUFFER_USAGE_STORAGE_BIT | L_BUFFER_USAGE_TRANSFER_SRC_BIT;

    ti_wait(runtime_);

    scoped::Invocation copy_invoke = copy_task.build_comp_invoke()
      .rsc(uniform_buf.view())
      .rsc(make_buf_view(src_buf))
      .rsc(render_target_img.view())
      .workgrp_count(util::align_up(width, 8), util::align_up(height, 8), 1)
      .build();

    ctxt.build_composite_invoke()
      .invoke(copy_invoke)
      .invoke(swapchain.create_present_invoke())
      .build()
      .submit().wait();
  }
};



void guarded_main() {
  windows::Window wnd = windows::create_window(640, 320);

  scoped::Context ctxt;
  {
    ContextWindowsConfig cfg {};
    cfg.label = "context";
    cfg.dev_idx = 0;
    cfg.hinst = wnd.hinst;
    cfg.hwnd = wnd.hwnd;
    ctxt = scoped::Context::own_by_raii(create_ctxt_windows(cfg));
  }

  scoped::Swapchain swapchain = ctxt.build_swapchain("swapchain")
    .build();

  FractalApp app(ctxt, CFG.module_dir);

  for (uint32_t i = 0;; ++i) {
    app.run(ctxt, swapchain, i);
  }

}



// -----------------------------------------------------------------------------
// Usually you don't need to change things below.

int main(int argc, const char** argv) {
  initialize(argc, argv);
  try {
    guarded_main();
  } catch (const std::exception& e) {
    liong::log::error("application threw an exception");
    liong::log::error(e.what());
    liong::log::error("application cannot continue");
  } catch (...) {
    liong::log::error("application threw an illiterate exception");
  }

  return 0;
}

void log_cb(log::LogLevel lv, const std::string& msg) {
  using log::LogLevel;
  switch (lv) {
  case LogLevel::L_LOG_LEVEL_DEBUG:
    printf("[\x1b[90mDEBUG\x1B[0m] %s\n", msg.c_str());
    break;
  case LogLevel::L_LOG_LEVEL_INFO:
    printf("[\x1B[32mINFO\x1B[0m] %s\n", msg.c_str());
    break;
  case LogLevel::L_LOG_LEVEL_WARNING:
    printf("[\x1B[33mWARN\x1B[0m] %s\n", msg.c_str());
    break;
  case LogLevel::L_LOG_LEVEL_ERROR:
    printf("[\x1B[31mERROR\x1B[0m] %s\n", msg.c_str());
    break;
  }
}

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
TiNdArray allocate_ndarray(TiDevice device, const std::vector<uint32_t>& shape) {
  size_t size = sizeof(T);
  TiNdArray out {};
  out.shape.dim_count = shape.size();
  for (size_t i = 0; i < shape.size(); ++i) {
    size *= out.shape.dims[i] = shape.at(i);
  }

  TiMemoryAllocateInfo mai {};
  mai.usage = TI_MEMORY_USAGE_STORAGE_BIT;
  mai.size = size;
  out.devmem = ti_allocate_device_memory(device, &mai);
  return out;
}


struct AppConfig {
  bool verbose = false;
  std::string module_dir = "";
} CFG;

void initialize(int argc, const char** argv) {
  args::init_arg_parse(APP_NAME, APP_DESC);
  args::reg_arg<args::SwitchParser>("-v", "--verbose", CFG.verbose,
    "Produce extra amount of logs for debugging.");
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
  TiContext context_;
  TiAotModule aot_module_;
  TiKernel kernel_fractal_;

  Module_fractal(TiContext context, const char* module_path) :
    context_(context),
    aot_module_(ti_load_vulkan_aot_module(context, module_path)),
    kernel_fractal_(ti_get_aot_module_kernel(aot_module_, "fractal")) {}
  ~Module_fractal() {
    ti_destroy_aot_module(aot_module_);
  }

  void fractal(float t, const TiNdArray& canvas) {
    ti_set_context_arg_f32(context_, 0, t);
    ti_set_context_arg_ndarray(context_, 1, &canvas);
    ti_launch_kernel(context_, kernel_fractal_);
  }
};

struct FractalApp {
  TiDevice device_;
  TiContext context_;

  Module_fractal module_fractal_;

  TiNdArray ndarray_canvas;

  scoped::Task copy_task;

  TiDevice create_taichi_device(const scoped::Context& ctxt) {
    TiVulkanDeviceInteropInfo vdii {};
    vdii.instance = vk::get_inst().inst;
    vdii.physical_device = ctxt.inner->physdev();
    vdii.device = ctxt.inner->dev;
    const auto& comp_submit_detail = ctxt.inner->submit_details.at(L_SUBMIT_TYPE_COMPUTE);
    vdii.compute_queue = comp_submit_detail.queue;
    vdii.compute_queue_family_index = comp_submit_detail.qfam_idx;
    const auto& graph_submit_detail = ctxt.inner->submit_details.at(L_SUBMIT_TYPE_GRAPHICS);
    vdii.graphics_queue = graph_submit_detail.queue;
    vdii.graphics_queue_family_index = graph_submit_detail.qfam_idx;

    TiDevice out = ti_import_vulkan_device(&vdii);
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

  FractalApp(const scoped::Context& ctxt, const std::string& module_path) :
    device_(create_taichi_device(ctxt)),
    context_(ti_create_context(device_)),
    module_fractal_(context_, module_path.c_str()),
    ndarray_canvas(allocate_ndarray<float>(device_, { 640, 320 })),
    copy_task(create_copy_task(ctxt)) {}
  ~FractalApp() {
    ti_destroy_context(context_);
    ti_destroy_device(device_);
  }

  void run(
    const scoped::Context& ctxt,
    const scoped::Swapchain& swapchain,
    uint32_t iframe
  ) {
    module_fractal_.fractal(iframe * 0.03f, ndarray_canvas);
    ti_submit(device_);

    TiVulkanDeviceMemoryInteropInfo vdmii;
    ti_export_vulkan_device_memory(device_, ndarray_canvas.devmem, &vdmii);

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

    ti_wait(device_);

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

  FractalApp app(ctxt, CFG.module_dir + "/fractal");

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

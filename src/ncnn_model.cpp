#include "ncnn_model.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <stdexcept>

#include <net.h>
#if NCNN_VULKAN
#include <gpu.h>
#endif

#include "platform/process.h"

namespace hailstone::texpack {

namespace {

std::mutex g_instance_mutex;
bool g_instance = false;

void ensure_instance() {
#if NCNN_VULKAN
    std::lock_guard lock(g_instance_mutex);
    if (!g_instance) {
        ncnn::create_gpu_instance();
        g_instance = true;
    }
#endif
}

} // namespace

std::vector<GpuDevice> list_devices() {
    std::vector<GpuDevice> out;
#if NCNN_VULKAN
    ensure_instance();
    const int n = ncnn::get_gpu_count();
    for (int i = 0; i < n; ++i) {
        const ncnn::GpuInfo& info = ncnn::get_gpu_info(i);
        GpuDevice d;
        d.index = i;
        d.name = info.device_name();
        switch (info.type()) {
        case 0: d.type = "discrete"; break;
        case 1: d.type = "integrated"; break;
        case 2: d.type = "virtual"; break;
        default: d.type = "cpu"; break; // (a software Vulkan driver: llvmpipe, SwiftShader)
        }
        if (ncnn::VulkanDevice* dev = ncnn::get_gpu_device(i)) d.memory_mb = dev->get_heap_budget();
        d.fp16 = info.support_fp16_storage();
        out.push_back(std::move(d));
    }
    auto rank = [](const GpuDevice& d) { return d.type == "discrete" ? 0 : d.type == "integrated" ? 1 : d.type == "virtual" ? 2 : 3; };
    std::stable_sort(out.begin(), out.end(), [&](const GpuDevice& a, const GpuDevice& b) { return rank(a) < rank(b); });
#endif
    GpuDevice cpu;
    cpu.name = "CPU";
    cpu.type = "cpu";
    out.push_back(std::move(cpu));
    return out;
}

void shutdown_devices() {
#if NCNN_VULKAN
    std::lock_guard lock(g_instance_mutex);
    if (g_instance) {
        ncnn::destroy_gpu_instance();
        g_instance = false;
    }
#endif
}

struct NcnnModel::Impl {
    ncnn::Net net;
    std::string input = "in0", output = "out0";
};

NcnnModel::NcnnModel() : impl_(std::make_unique<Impl>()) {}
NcnnModel::~NcnnModel() = default;

std::unique_ptr<NcnnModel> NcnnModel::load(const ModelEntry& entry, int gpu, unsigned threads, std::string* error) {
    auto fail = [&](std::string why) -> std::unique_ptr<NcnnModel> {
        if (error) *error = entry.id + ": " + why;
        return nullptr;
    };
    std::unique_ptr<NcnnModel> m(new NcnnModel);
    ncnn::Option& opt = m->impl_->net.opt;
    opt.num_threads = static_cast<int>(std::max(1u, threads));
    opt.use_bf16_storage = false;
    // fp16 arithmetic loses detail in the 1x maps; fp16 storage does not (README.md "Accuracy").
    opt.use_fp16_arithmetic = false;
#if NCNN_VULKAN
    if (gpu >= 0) {
        ensure_instance();
        if (gpu >= ncnn::get_gpu_count()) return fail("no such GPU");
        const bool fp16 = ncnn::get_gpu_info(gpu).support_fp16_storage();
        opt.use_vulkan_compute = true;
        opt.use_fp16_packed = fp16;
        opt.use_fp16_storage = fp16;
        m->impl_->net.set_vulkan_device(gpu);
    } else {
        opt.use_vulkan_compute = false;
    }
#else
    if (gpu >= 0) return fail("this build has no GPU support");
#endif
    {
        std::FILE* f = platform::open_c_file(entry.param, "rb");
        if (!f) return fail("cannot open " + entry.param.string());
        int r = m->impl_->net.load_param(f);
        std::fclose(f);
        if (r != 0) return fail("its .param could not be read");
    }
    {
        std::FILE* f = platform::open_c_file(entry.bin, "rb");
        if (!f) return fail("cannot open " + entry.bin.string());
        int r = m->impl_->net.load_model(f);
        std::fclose(f);
        if (r != 0) return fail("its .bin could not be read");
    }
    const auto& ins = m->impl_->net.input_names();
    const auto& outs = m->impl_->net.output_names();
    if (ins.empty() || outs.empty()) return fail("it has no input or output");
    m->impl_->input = ins.front();
    m->impl_->output = outs.front();
    // The scale it works at: from its .ini, else what a small image comes out as.
    if (entry.scale) {
        m->scale_ = entry.scale;
    } else {
        try {
            Planes probe(24, 24, 3, 0.5f);
            Planes r = m->run(probe);
            if (r.width % 24 != 0 || r.width == 0) return fail("its output is not a whole multiple of its input");
            m->scale_ = r.width / 24;
        } catch (const std::exception& e) {
            return fail(e.what());
        }
    }
    return m;
}

Planes NcnnModel::run(const Planes& rgb) {
    const int w = static_cast<int>(rgb.width), h = static_cast<int>(rgb.height);
    ncnn::Mat in(w, h, 3);
    for (int c = 0; c < 3; ++c) {
        ncnn::Mat ch = in.channel(c);
        for (int y = 0; y < h; ++y) {
            float* row = ch.row(y);
            for (int x = 0; x < w; ++x)
                row[x] = rgb.px[(std::size_t(y) * rgb.width + static_cast<std::size_t>(x)) * rgb.channels +
                                static_cast<std::size_t>(std::min(c, static_cast<int>(rgb.channels) - 1))];
        }
    }
    ncnn::Extractor ex = impl_->net.create_extractor();
    if (ex.input(impl_->input.c_str(), in) != 0) throw std::runtime_error("the model took no input");
    ncnn::Mat out;
    if (ex.extract(impl_->output.c_str(), out) != 0 || out.empty())
        throw std::runtime_error("the model failed (out of video memory?)");
    if (out.c < 3 || out.dims != 3) throw std::runtime_error("the model's output is not an RGB image");
    Planes r(static_cast<std::uint32_t>(out.w), static_cast<std::uint32_t>(out.h), 3);
    for (int c = 0; c < 3; ++c) {
        const ncnn::Mat ch = out.channel(c);
        for (int y = 0; y < out.h; ++y) {
            const float* row = ch.row(y);
            for (int x = 0; x < out.w; ++x)
                r.px[(std::size_t(y) * r.width + static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(c)] = row[x];
        }
    }
    return r;
}

namespace {

class NcnnModelSet final : public ModelSet {
public:
    std::unique_ptr<NcnnModel> color, normal, rough;
    Models models() override { return {color.get(), normal.get(), rough.get()}; }
};

} // namespace

ModelSetFactory ncnn_model_set(ModelEntry color, std::optional<ModelEntry> normal, std::optional<ModelEntry> rough,
                               int gpu, unsigned cpu_threads) {
    return [=](std::string* error) -> std::unique_ptr<ModelSet> {
        auto set = std::make_unique<NcnnModelSet>();
        set->color = NcnnModel::load(color, gpu, cpu_threads, error);
        if (!set->color) return nullptr;
        if (normal && !(set->normal = NcnnModel::load(*normal, gpu, cpu_threads, error))) return nullptr;
        if (rough && !(set->rough = NcnnModel::load(*rough, gpu, cpu_threads, error))) return nullptr;
        return set;
    };
}

} // namespace hailstone::texpack

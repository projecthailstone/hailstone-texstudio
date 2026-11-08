#pragma once

// The texture studio's models on ncnn (README.md): Vulkan compute on any GPU with a Vulkan driver (ncnn's
// own loader, so nothing else need be installed), or the CPU.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "texpack/model_catalog.h"
#include "texpack/pack_job.h"
#include "texpack/upscale.h"

namespace hailstone::texpack {

struct GpuDevice {
    int index = -1;     // ncnn's device index; -1 the CPU
    std::string name;
    std::string type;   // "discrete", "integrated", "virtual", "cpu"
    std::uint32_t memory_mb = 0; // ncnn's heap budget (0: not known)
    bool fp16 = false;
};

// Starts ncnn's Vulkan instance (once) and lists the GPUs, best first (discrete before integrated), then the CPU.
std::vector<GpuDevice> list_devices();
// Ends ncnn's Vulkan instance; at exit, after every model is gone.
void shutdown_devices();

class NcnnModel final : public Model {
public:
    // Loads <param> and <bin> for device `gpu` (-1: the CPU, on `threads` threads). Null with *error set when it
    // cannot. `scale` 0: found by running a small image.
    static std::unique_ptr<NcnnModel> load(const ModelEntry& entry, int gpu, unsigned threads, std::string* error);
    ~NcnnModel() override;
    std::uint32_t scale() const override { return scale_; }
    Planes run(const Planes& rgb) override;

private:
    NcnnModel();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint32_t scale_ = 1;
};

// The colour model and the maps' for a ModelSetFactory (GpuWorker): `normal` and `rough` may be null.
ModelSetFactory ncnn_model_set(ModelEntry color, std::optional<ModelEntry> normal, std::optional<ModelEntry> rough,
                               int gpu, unsigned cpu_threads);

} // namespace hailstone::texpack

#ifndef NEUROSUITE_GPU_METALCONTEXT_H
#define NEUROSUITE_GPU_METALCONTEXT_H

// The Apple GPU (Metal) device shared by every Metal backend in neurosuite-3.
// Design: doc/design/metal-backend.md.  Built only on macOS with USE_METAL
// (target Neurosuite::metal, which defines NS_HAVE_METAL for its users).
//
// Use:
//   MetalContext* ctx = MetalContext::instance();   // nullptr: no Metal GPU
//   if (!ctx) { ...CPU path... }
//   extern const char my_kernels[];                 // from ns_metal_embed()
//   MTL::ComputePipelineState* p = ctx->pipeline("my_kernels", my_kernels, "myKernel", &err);
//   MetalRef<MTL::Buffer> buf(ctx->newSharedBuffer(bytes));
//   {
//       AutoreleaseScope pool;          // command buffers/encoders are autoreleased
//       MTL::CommandBuffer* cmd = ctx->queue()->commandBuffer();
//       ... encode, cmd->commit(); cmd->waitUntilCompleted();
//       if (!MetalContext::succeeded(cmd, &err)) ...
//   }
//
// Kernels are compiled at run time from the embedded source (by the OS, no
// Xcode needed) the first time a pipeline is asked for, and cached for the
// life of the process.  Buffers use shared storage: on Apple silicon CPU and
// GPU see the same memory, so contents() is written and read directly, with
// no uploads or downloads.

#include <Metal/Metal.hpp>

#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace neurosuite {
namespace gpu {

// Owns one reference to a metal-cpp object (one returned by a new...(),
// alloc() or Create...() call) and releases it.
template <class T>
class MetalRef {
public:
    MetalRef() = default;
    explicit MetalRef(T* object) : ptr(object) {}
    ~MetalRef() { reset(); }
    MetalRef(const MetalRef&) = delete;
    MetalRef& operator=(const MetalRef&) = delete;
    MetalRef(MetalRef&& other) noexcept : ptr(std::exchange(other.ptr, nullptr)) {}
    MetalRef& operator=(MetalRef&& other) noexcept
    {
        if (this != &other) {
            reset();
            ptr = std::exchange(other.ptr, nullptr);
        }
        return *this;
    }

    T* get() const { return ptr; }
    T* operator->() const { return ptr; }
    explicit operator bool() const { return ptr != nullptr; }
    void reset(T* object = nullptr)
    {
        if (ptr) ptr->release();
        ptr = object;
    }

private:
    T* ptr = nullptr;
};

// Drains the objects metal-cpp autoreleases (command buffers, encoders,
// NS::String temporaries) at the end of a scope.  Command-line programs have
// no run loop that would do it.
class AutoreleaseScope {
public:
    AutoreleaseScope() : pool(NS::AutoreleasePool::alloc()->init()) {}
    ~AutoreleaseScope() { pool->release(); }
    AutoreleaseScope(const AutoreleaseScope&) = delete;
    AutoreleaseScope& operator=(const AutoreleaseScope&) = delete;

private:
    NS::AutoreleasePool* pool;
};

class MetalContext {
public:
    // The process-wide context, created on first use; nullptr when the
    // machine has no Metal device, or when the environment sets
    // NS_DISABLE_METAL (to anything but "0"), so every Metal path falls back
    // to the CPU.  Thread-safe.
    static MetalContext* instance();

    MTL::Device* device() const { return dev.get(); }
    MTL::CommandQueue* queue() const { return cmdQueue.get(); }
    std::string deviceName() const;

    // The compute pipeline for kernel function `function` in the Metal source
    // `source` (registered under `library`, which must name one source).  The
    // library is compiled on first use and both are cached.  nullptr on a
    // compile error, with the compiler's message in *error.  Thread-safe.
    MTL::ComputePipelineState* pipeline(const char* library, const char* source,
                                        const char* function, std::string* error = nullptr);

    // A new shared-storage buffer of `bytes` bytes (CPU and GPU see the same
    // memory), owned by the caller; nullptr if it cannot be allocated.
    MTL::Buffer* newSharedBuffer(std::size_t bytes) const;

    // True if a committed, waited-for command buffer completed; otherwise
    // false with the reason in *error.
    static bool succeeded(MTL::CommandBuffer* cmd, std::string* error = nullptr);

    // Threads per threadgroup for a 1-D dispatch of `pipeline`: its
    // maximum, capped at `cap`.
    static std::size_t threadgroupWidth(const MTL::ComputePipelineState* pipeline,
                                        std::size_t cap = 256);

    MetalContext(const MetalContext&) = delete;
    MetalContext& operator=(const MetalContext&) = delete;
    ~MetalContext();

private:
    MetalContext(MTL::Device* device, MTL::CommandQueue* queue);

    MetalRef<MTL::Device> dev;
    MetalRef<MTL::CommandQueue> cmdQueue;
    std::mutex cacheMutex;
    std::map<std::string, MetalRef<MTL::Library>> libraries;
    std::map<std::pair<std::string, std::string>, MetalRef<MTL::ComputePipelineState>> pipelines;
};

} // namespace gpu
} // namespace neurosuite

#endif // NEUROSUITE_GPU_METALCONTEXT_H

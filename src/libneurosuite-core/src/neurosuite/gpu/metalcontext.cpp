// The one translation unit that instantiates metal-cpp's implementation
// (Apple's rule: define the *_PRIVATE_IMPLEMENTATION macros exactly once per
// program).  Every Metal user links Neurosuite::metal and so gets it here.
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#include "metalcontext.h"

#include <algorithm>
#include <memory>

namespace neurosuite {
namespace gpu {

namespace {

std::string describe(NS::Error* error, const char* fallback)
{
    if (error && error->localizedDescription())
        return error->localizedDescription()->utf8String();
    return fallback;
}

NS::String* nsString(const char* text)
{
    return NS::String::string(text, NS::UTF8StringEncoding);   // autoreleased
}

} // namespace

MetalContext* MetalContext::instance()
{
    // Created once, never destroyed: Metal objects outlive static destruction
    // order safely only if nobody releases them during exit.
    static MetalContext* const shared = [] () -> MetalContext* {
        AutoreleaseScope pool;
        MTL::Device* device = MTL::CreateSystemDefaultDevice();
        if (!device) return nullptr;
        MTL::CommandQueue* queue = device->newCommandQueue();
        if (!queue) {
            device->release();
            return nullptr;
        }
        return new MetalContext(device, queue);
    }();
    return shared;
}

MetalContext::MetalContext(MTL::Device* device, MTL::CommandQueue* queue)
    : dev(device), cmdQueue(queue)
{
}

MetalContext::~MetalContext() = default;

std::string MetalContext::deviceName() const
{
    AutoreleaseScope pool;
    NS::String* name = dev->name();
    return name ? name->utf8String() : "unknown Metal device";
}

MTL::ComputePipelineState* MetalContext::pipeline(const char* library, const char* source,
                                                  const char* function, std::string* error)
{
    std::lock_guard<std::mutex> lock(cacheMutex);
    const auto key = std::make_pair(std::string(library), std::string(function));
    auto found = pipelines.find(key);
    if (found != pipelines.end()) return found->second.get();

    AutoreleaseScope pool;
    auto lib = libraries.find(library);
    if (lib == libraries.end()) {
        MetalRef<MTL::CompileOptions> options(MTL::CompileOptions::alloc()->init());
        // IEEE-conforming float arithmetic, as on the CPU: the GPU result is
        // checked against the CPU's.
        options->setFastMathEnabled(false);
        NS::Error* err = nullptr;
        MTL::Library* compiled = dev->newLibrary(nsString(source), options.get(), &err);
        if (!compiled) {
            if (error) *error = std::string("compiling ") + library + ": " +
                                describe(err, "unknown compiler error");
            return nullptr;
        }
        lib = libraries.emplace(library, MetalRef<MTL::Library>(compiled)).first;
    }

    MetalRef<MTL::Function> fn(lib->second->newFunction(nsString(function)));
    if (!fn) {
        if (error) *error = std::string(library) + " has no kernel " + function;
        return nullptr;
    }
    NS::Error* err = nullptr;
    MTL::ComputePipelineState* state = dev->newComputePipelineState(fn.get(), &err);
    if (!state) {
        if (error) *error = std::string("pipeline ") + function + ": " +
                            describe(err, "unknown error");
        return nullptr;
    }
    pipelines.emplace(key, MetalRef<MTL::ComputePipelineState>(state));
    return state;
}

MTL::Buffer* MetalContext::newSharedBuffer(std::size_t bytes) const
{
    if (bytes == 0 || bytes > dev->maxBufferLength()) return nullptr;
    return dev->newBuffer(static_cast<NS::UInteger>(bytes), MTL::ResourceStorageModeShared);
}

bool MetalContext::succeeded(MTL::CommandBuffer* cmd, std::string* error)
{
    if (cmd->status() == MTL::CommandBufferStatusCompleted) return true;
    if (error) *error = describe(cmd->error(), "command buffer did not complete");
    return false;
}

std::size_t MetalContext::threadgroupWidth(const MTL::ComputePipelineState* pipeline,
                                           std::size_t cap)
{
    return std::max<std::size_t>(1, std::min<std::size_t>(cap, pipeline->maxTotalThreadsPerThreadgroup()));
}

} // namespace gpu
} // namespace neurosuite

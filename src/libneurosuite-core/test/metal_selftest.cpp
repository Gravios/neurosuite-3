// The Apple GPU (Metal) context, Neurosuite::metal (macOS with USE_METAL):
// device and queue, run-time compilation of embedded kernels and the pipeline
// cache, shared buffers, dispatch, threadgroup memory, atomics, and compile
// errors reported rather than fatal.  Prints SKIPPED (exit 0) on a machine
// with no Metal device, e.g. a CI virtual machine without a GPU.
#include <neurosuite/gpu/metalcontext.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using neurosuite::gpu::AutoreleaseScope;
using neurosuite::gpu::MetalContext;
using neurosuite::gpu::MetalRef;

extern const char metal_selftest_kernels[];   // metal_selftest.metal, by ns_metal_embed

static int failures = 0;
static void check(bool ok, const char* name, double got = 0, double want = 0)
{
    if (ok) std::printf("  PASS  %s\n", name);
    else { std::printf("  FAIL  %s   (got %.6g, want %.6g)\n", name, got, want); ++failures; }
}

int main()
{
    MetalContext* ctx = MetalContext::instance();
    if (!ctx) {
        std::printf("SKIPPED: no Metal device on this machine\n");
        return 0;
    }
    std::printf("  ....  Metal device: %s\n", ctx->deviceName().c_str());
    check(ctx == MetalContext::instance(), "instance() returns one shared context");

    std::string err;
    MTL::ComputePipelineState* affine =
        ctx->pipeline("metal_selftest", metal_selftest_kernels, "affineU32", &err);
    check(affine != nullptr, "embedded kernel source compiles at run time");
    if (!affine) {
        std::printf("        %s\n", err.c_str());
        std::printf("\nFAILED (%d failure%s)\n", failures, failures == 1 ? "" : "s");
        return 1;
    }
    check(ctx->pipeline("metal_selftest", metal_selftest_kernels, "affineU32") == affine,
          "a second request returns the cached pipeline");

    // Concurrent first requests for another kernel all get one pipeline.
    {
        std::vector<MTL::ComputePipelineState*> got(8, nullptr);
        std::vector<std::thread> pool;
        for (int t = 0; t < 8; ++t)
            pool.emplace_back([&, t] {
                got[t] = ctx->pipeline("metal_selftest", metal_selftest_kernels, "groupSum");
            });
        for (std::thread& th : pool) th.join();
        bool same = got[0] != nullptr;
        for (MTL::ComputePipelineState* p : got) same = same && p == got[0];
        check(same, "concurrent requests share one pipeline");
    }

    // affineU32 over a size that is not a multiple of the threadgroup width.
    {
        const std::uint32_t n = 1000003;
        MetalRef<MTL::Buffer> in(ctx->newSharedBuffer(n * sizeof(std::uint32_t)));
        MetalRef<MTL::Buffer> out(ctx->newSharedBuffer(n * sizeof(std::uint32_t)));
        check(in && out, "shared buffers allocate");
        auto* src = static_cast<std::uint32_t*>(in->contents());
        for (std::uint32_t i = 0; i < n; ++i) src[i] = i * 2654435761u;
        std::memset(out->contents(), 0, n * sizeof(std::uint32_t));
        {
            AutoreleaseScope pool;
            MTL::CommandBuffer* cmd = ctx->queue()->commandBuffer();
            MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
            enc->setComputePipelineState(affine);
            enc->setBuffer(in.get(), 0, 0);
            enc->setBuffer(out.get(), 0, 1);
            enc->setBytes(&n, sizeof n, 2);
            enc->dispatchThreads(MTL::Size(n, 1, 1),
                                 MTL::Size(MetalContext::threadgroupWidth(affine), 1, 1));
            enc->endEncoding();
            cmd->commit();
            cmd->waitUntilCompleted();
            check(MetalContext::succeeded(cmd, &err), "affineU32 command buffer completes");
        }
        const auto* res = static_cast<const std::uint32_t*>(out->contents());
        std::uint32_t bad = 0;
        for (std::uint32_t i = 0; i < n; ++i) bad += res[i] != src[i] * 3u + 7u;
        check(bad == 0, "affineU32 matches the CPU on every element", bad, 0);
    }

    // groupSum: threadgroup memory, barriers and a global atomic.
    {
        MTL::ComputePipelineState* sum = ctx->pipeline("metal_selftest", metal_selftest_kernels, "groupSum");
        const std::uint32_t n = 777777;
        MetalRef<MTL::Buffer> in(ctx->newSharedBuffer(n * sizeof(std::uint32_t)));
        MetalRef<MTL::Buffer> total(ctx->newSharedBuffer(sizeof(std::uint32_t)));
        auto* src = static_cast<std::uint32_t*>(in->contents());
        std::uint64_t want = 0;
        for (std::uint32_t i = 0; i < n; ++i) { src[i] = i % 1000; want += src[i]; }
        *static_cast<std::uint32_t*>(total->contents()) = 0;
        const std::size_t width = 256;   // a power of two, as the tree reduction needs
        const bool fits = sum && sum->maxTotalThreadsPerThreadgroup() >= width;
        check(fits, "groupSum runs 256 threads per threadgroup");
        if (fits) {
            AutoreleaseScope pool;
            MTL::CommandBuffer* cmd = ctx->queue()->commandBuffer();
            MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
            enc->setComputePipelineState(sum);
            enc->setBuffer(in.get(), 0, 0);
            enc->setBuffer(total.get(), 0, 1);
            enc->setBytes(&n, sizeof n, 2);
            enc->setThreadgroupMemoryLength(width * sizeof(std::uint32_t), 0);
            const std::size_t groups = (n + width - 1) / width;
            enc->dispatchThreadgroups(MTL::Size(groups, 1, 1), MTL::Size(width, 1, 1));
            enc->endEncoding();
            cmd->commit();
            cmd->waitUntilCompleted();
            check(MetalContext::succeeded(cmd, &err), "groupSum command buffer completes");
            const std::uint32_t got = *static_cast<const std::uint32_t*>(total->contents());
            check(got == want, "groupSum matches the CPU sum", got, double(want));
        }
    }

    // Errors are reported, not fatal.
    {
        std::string message;
        check(ctx->pipeline("metal_selftest_broken", "kernel void k( { }", "k", &message) == nullptr
                  && !message.empty(),
              "a source that does not compile gives nullptr and the compiler's message");
        message.clear();
        check(ctx->pipeline("metal_selftest", metal_selftest_kernels, "noSuchKernel", &message) == nullptr
                  && !message.empty(),
              "a missing kernel gives nullptr and a message");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

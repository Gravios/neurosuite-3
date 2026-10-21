// Every Metal kernel source in the repository, compiled the way the programs
// compile it at run time: each .metal is a library, built with fast math off
// (as MetalContext builds it), and every kernel function in it gets a compute
// pipeline.  The numerical tests only reach the kernels of the programs they
// exercise; this one fails on any kernel Apple's compiler refuses, wherever it
// lives (doc/design/metal-backend.md, Phase 5).  The sources are the ones the
// programs embed (ns_metal_embed); tests/CMakeLists.txt lists them.
// SKIPPED (exit 0) without a Metal device.
#include <neurosuite/gpu/metalcontext.h>

#include <cstdio>
#include <string>

struct MetalSourceEntry {
    const char* path;
    const char* source;
};
extern const MetalSourceEntry kMetalSources[];
extern const int kMetalSourceCount;

using neurosuite::gpu::AutoreleaseScope;
using neurosuite::gpu::MetalContext;
using neurosuite::gpu::MetalRef;

int main()
{
    MetalContext* ctx = MetalContext::instance();
    if (!ctx) {
        std::printf("SKIPPED: no Metal device\n");
        return 0;
    }
    std::printf("Metal device: %s\n", ctx->deviceName().c_str());
    int failures = 0, nKernels = 0;
    for (int i = 0; i < kMetalSourceCount; ++i) {
        const MetalSourceEntry& e = kMetalSources[i];
        AutoreleaseScope pool;
        MetalRef<MTL::CompileOptions> options(MTL::CompileOptions::alloc()->init());
        options->setFastMathEnabled(false);
        NS::Error* err = nullptr;
        MetalRef<MTL::Library> lib(ctx->device()->newLibrary(
            NS::String::string(e.source, NS::UTF8StringEncoding), options.get(), &err));
        if (!lib) {
            std::printf("  FAIL  %s does not compile:\n%s\n", e.path,
                        err && err->localizedDescription()
                            ? err->localizedDescription()->utf8String() : "(no message)");
            ++failures;
            continue;
        }
        NS::Array* names = lib->functionNames();
        const NS::UInteger n = names ? names->count() : 0;
        int nHere = 0;
        for (NS::UInteger k = 0; k < n; ++k) {
            NS::String* name = names->object<NS::String>(k);
            MetalRef<MTL::Function> fn(lib->newFunction(name));
            if (!fn || fn->functionType() != MTL::FunctionTypeKernel) continue;
            NS::Error* perr = nullptr;
            MetalRef<MTL::ComputePipelineState> ps(ctx->device()->newComputePipelineState(fn.get(), &perr));
            if (!ps) {
                std::printf("  FAIL  %s: kernel %s has no pipeline: %s\n", e.path, name->utf8String(),
                            perr && perr->localizedDescription()
                                ? perr->localizedDescription()->utf8String() : "(no message)");
                ++failures;
                continue;
            }
            ++nHere;
        }
        if (nHere == 0) {
            std::printf("  FAIL  %s: no kernel function\n", e.path);
            ++failures;
        } else {
            std::printf("  PASS  %s: %d kernel(s)\n", e.path, nHere);
        }
        nKernels += nHere;
    }
    std::printf("\n%d source(s), %d kernel(s): %s (%d failure%s)\n", kMetalSourceCount, nKernels,
                failures ? "FAILED" : "ALL PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

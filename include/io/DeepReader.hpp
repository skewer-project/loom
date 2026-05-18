#pragma once

#include <future>
#include <memory>
#include <string>

#include "core/DeepLayout.hpp"
#include "io/ParsedDeepImage.hpp"

namespace loom::io {

// Result type for `IDeepReader::readFrame`. Bundles the parsed CPU-side
// payload with the interned `core::DeepLayout*` schema that describes its
// channels. The layout pointer is non-owning — owned by the process-wide
// registry (see `core::getDeepLayout`).
//
// `isValid()` returns false on read failure (file not found, parse error,
// non-deep EXR, etc.). The future itself never throws; the reader catches
// every exception path and returns an invalid `DeepFrame`. This keeps
// `IDeepReader::readFrame(...).get()` total-functional and lets the
// caller decide whether to log / fall back / retry.
struct DeepFrame {
    ParsedDeepImage image;
    const core::DeepLayout* layout = nullptr;

    [[nodiscard]] bool isValid() const noexcept { return layout != nullptr; }
};

// Reader interface. v1 ships a sync implementation (`SyncDeepReader`) that
// returns a ready future from a blocking OpenEXR Deep read. Phase C.1 swaps
// in a worker-thread impl behind the same interface — the async-shaped
// signature is in place from day one so call sites never change.
class IDeepReader {
  public:
    virtual ~IDeepReader() = default;

    // Read the deep image at `path`. `frameIndex` is reserved for the
    // animation playback path in Phase C — v1 ignores it for single-file
    // inputs. The convention for animation will be `frame_####.exr`
    // substitution applied to `path`; the worker-thread impl honours it.
    [[nodiscard]] virtual std::future<DeepFrame> readFrame(const std::string& path,
                                                           int frameIndex = 0) = 0;
};

// Synchronous implementation. `readFrame` blocks the calling thread until
// the EXR has parsed, then returns a ready future. Use only from a thread
// where blocking is acceptable (the v1 production caller is `DeepEXRReadNode`
// in Phase B.2, which is invoked from `Graph::execute` on the main thread
// — acceptable for single-file inspection but not for animation playback).
class SyncDeepReader : public IDeepReader {
  public:
    [[nodiscard]] std::future<DeepFrame> readFrame(const std::string& path,
                                                   int frameIndex = 0) override;
};

}  // namespace loom::io

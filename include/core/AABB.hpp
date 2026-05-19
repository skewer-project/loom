#pragma once

#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace loom::core {

// Axis-aligned bounding box in world space. Right-handed / Y-up / meters
// per docs/CONVENTIONS.md §19.
//
// `valid` starts false; `expand(p)` initialises the box on the first point
// and grows it monotonically thereafter. An unmodified `AABB{}` is valid
// metadata for "no data was reduced into me" rather than a degenerate
// zero-extent box at the origin — consumers must check `valid` before
// trusting `center()` / `radius()`.
//
// The struct intentionally has no method that mutates the box from a
// half-open container or that prunes outliers — those concerns live with
// the caller. The "Z = 1e+10 background sentinel" filter referenced in
// docs/CONVENTIONS.md §19 is applied by `gpu::uploadDeepImage` before it
// touches `expand`, so this type stays a dumb reducer.
struct AABB {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    bool valid = false;

    void expand(glm::vec3 p) noexcept {
        if (!valid) {
            min = p;
            max = p;
            valid = true;
        } else {
            min = glm::min(min, p);
            max = glm::max(max, p);
        }
    }

    [[nodiscard]] glm::vec3 center() const noexcept { return 0.5f * (min + max); }
    [[nodiscard]] glm::vec3 extent() const noexcept { return max - min; }

    // Half-diagonal length — a single scalar "scene size" hint used by
    // camera auto-framing (Phase B.8.2). Returns 0 for an empty AABB.
    [[nodiscard]] float radius() const noexcept {
        if (!valid) return 0.0f;
        return 0.5f * glm::length(extent());
    }
};

}  // namespace loom::core

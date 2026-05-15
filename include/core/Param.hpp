#pragma once

#include <crude_json.h>

#include <glm/vec3.hpp>
#include <string>
#include <variant>

namespace loom::core {

// Optional metadata describing the editable range of a numeric Param. Applies
// to `float` / `int` only — bool / vec3 / string Params ignore these fields.
// Stored regardless so the JSON round-trip is total (no type-conditional
// serialisation in the writer).
//
// `hasBounds = false` means "no enforced range" — the UI widget falls back to
// a free-form input box. `step = 0.0f` means "continuous" — the slider /
// drag widget uses its default granularity.
struct ParamRange {
    float min = 0.0f;
    float max = 0.0f;
    float step = 0.0f;
    bool hasBounds = false;
};

// Tag-union parameter carried on every node. The variant alternatives cover
// the five types we need today; adding a new type is a single line in the
// variant + a branch in `toJson` / `fromJson`. JSON serialisation handles
// round-trip; unknown types in the stored JSON (future-tolerance) round-trip
// as the closest representable value or are dropped at load.
//
// Params are value-typed. Mutation does not auto-flip the owning node's
// `isDirty` flag — that's `Node::setParam`'s job (the panel and tests both
// route mutations through it).
class Param {
  public:
    using Value = std::variant<float, int, bool, glm::vec3, std::string>;

    Param() = default;
    Param(std::string name, Value v, ParamRange range = {})
        : m_name(std::move(name)), m_value(std::move(v)), m_range(range) {}

    [[nodiscard]] const std::string& name() const noexcept { return m_name; }
    [[nodiscard]] const Value& value() const noexcept { return m_value; }
    [[nodiscard]] Value& value() noexcept { return m_value; }
    [[nodiscard]] const ParamRange& range() const noexcept { return m_range; }
    [[nodiscard]] ParamRange& range() noexcept { return m_range; }

    void setValue(Value v) { m_value = std::move(v); }

    // JSON round-trip. The encoded form is an object:
    //   { "name": ..., "type": ..., "value": ..., "range": {...} }
    // where `type` is one of "float" / "int" / "bool" / "vec3" / "string".
    // `range` is omitted when `range.hasBounds` is false.
    [[nodiscard]] crude_json::value toJson() const;

    // Parse a Param from the JSON shape above. A malformed or
    // unrecognised-type entry produces a default-constructed Param (empty
    // name, float value 0) and is logged via `loom::log::warn`. This
    // matches the broader project posture: don't silently swallow user
    // data, but never refuse to load a session.
    [[nodiscard]] static Param fromJson(const crude_json::value& j);

  private:
    std::string m_name;
    Value m_value;
    ParamRange m_range;
};

}  // namespace loom::core

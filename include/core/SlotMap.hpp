#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace loom::core {

template <typename T>
struct Slot {
    uint32_t generation{1};
    std::optional<T> data;
    uint32_t nextFree{std::numeric_limits<uint32_t>::max()};
    bool isActive{false};
};

template <typename T, typename HandleType>
class SlotMap {
  public:
    SlotMap() = default;

    HandleType insert(const T& item) { return emplace(item); }

    HandleType insert(T&& item) { return emplace(std::move(item)); }

    template <typename... Args>
    HandleType emplace(Args&&... args) {
        if (freeListHead != std::numeric_limits<uint32_t>::max()) {
            uint32_t index = freeListHead;
            Slot<T>& slot = slots[index];
            freeListHead = slot.nextFree;

            slot.data.emplace(std::forward<Args>(args)...);
            slot.isActive = true;
            activeCount++;

            return HandleType(index, slot.generation);
        }

        uint32_t index = static_cast<uint32_t>(slots.size());
        slots.push_back({});  // Default constructed Slot
        Slot<T>& slot = slots.back();
        slot.generation = 1;
        slot.data.emplace(std::forward<Args>(args)...);
        slot.isActive = true;
        activeCount++;

        return HandleType(index, 1);
    }

    bool isValid(HandleType handle) const {
        if (!handle.isValid() || handle.index >= slots.size()) return false;
        const Slot<T>& slot = slots[handle.index];
        return slot.isActive && slot.generation == handle.generation;
    }

    T* get(HandleType handle) {
        if (isValid(handle)) {
            return &slots[handle.index].data.value();
        }
        return nullptr;
    }

    const T* get(HandleType handle) const {
        if (isValid(handle)) {
            return &slots[handle.index].data.value();
        }
        return nullptr;
    }

    bool remove(HandleType handle) {
        if (!isValid(handle)) return false;

        Slot<T>& slot = slots[handle.index];
        slot.data.reset();  // Destroy the object immediately
        slot.isActive = false;

        // Increment generation, handle wrap around by skipping 0
        slot.generation++;
        if (slot.generation == 0) {
            slot.generation = 1;
        }

        slot.nextFree = freeListHead;
        freeListHead = handle.index;
        activeCount--;

        return true;
    }

    void forEach(std::function<void(HandleType, T&)> callback) {
        for (uint32_t i = 0; i < slots.size(); ++i) {
            if (slots[i].isActive) {
                callback(HandleType(i, slots[i].generation), slots[i].data.value());
            }
        }
    }

    void forEach(std::function<void(HandleType, const T&)> callback) const {
        for (uint32_t i = 0; i < slots.size(); ++i) {
            if (slots[i].isActive) {
                callback(HandleType(i, slots[i].generation), slots[i].data.value());
            }
        }
    }

    uint32_t size() const { return activeCount; }
    uint32_t capacity() const { return static_cast<uint32_t>(slots.size()); }

    void clear() {
        for (uint32_t i = 0; i < slots.size(); ++i) {
            if (slots[i].isActive) {
                remove(HandleType(i, slots[i].generation));
            }
        }
    }

  private:
    std::vector<Slot<T>> slots;
    uint32_t freeListHead{std::numeric_limits<uint32_t>::max()};
    uint32_t activeCount{0};
};

}  // namespace loom::core

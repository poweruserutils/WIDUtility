#pragma once

#include "core/PendingChange.h"

#include <functional>
#include <mutex>
#include <vector>

namespace wid::core {

class ChangeQueue {
public:
    using Listener = std::function<void()>;

    std::uint64_t enqueue(PendingChange c);
    bool          remove(std::uint64_t id);
    bool          moveUp(std::uint64_t id);
    bool          moveDown(std::uint64_t id);
    void          clear();

    std::vector<PendingChange> snapshot() const;
    std::size_t                size() const;

    void onChanged(Listener l) { listener_ = std::move(l); }

    // Profile/preset persistence (plain text format for now; JSON later).
    bool saveToFile(const std::wstring& path) const;
    bool loadFromFile(const std::wstring& path);

private:
    void notify_();

    mutable std::mutex         mtx_;
    std::vector<PendingChange> items_;
    std::uint64_t              nextId_ = 1;
    Listener                   listener_;
};

} // namespace wid::core

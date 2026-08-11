#pragma once

#include <atomic>
#include <memory>

namespace xscope::network {

/// Cooperative cancellation for in-flight network work (HTTP / XAIOP streams).
class CancelToken {
public:
    CancelToken() : state_(std::make_shared<std::atomic<bool>>(false)) {}

    void cancel() noexcept { state_->store(true, std::memory_order_release); }
    bool is_cancelled() const noexcept { return state_->load(std::memory_order_acquire); }

    std::shared_ptr<std::atomic<bool>> shared_state() const { return state_; }

private:
    std::shared_ptr<std::atomic<bool>> state_;
};

} // namespace xscope::network

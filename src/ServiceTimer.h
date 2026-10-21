#pragma once

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

// Message-thread service timer that is safe to create, start and destroy from any thread
// (docs/specs/W3-TIMER-EVOLUTION.md section 2).
//
//  - Nothing starts at construction. ensureStarted() (any thread, idempotent) starts the timer on the message
//    thread: directly when called there, otherwise through MessageManager::callAsync. With no MessageManager
//    it does nothing and may be called again later.
//  - detach() (any thread, idempotent) waits for a tick that is in flight, guarantees that no tick runs
//    afterwards, and stops the timer on the message thread. The shared core outlives queued messages, so
//    nothing refers to the owner after detach() returns.
//  - The tick runs on the message thread with the core mutex held: detach() from another thread therefore
//    blocks for at most one tick. Do not call detach() while holding a lock the tick needs.
class ServiceTimer final
{
public:
    explicit ServiceTimer(std::function<void()> tickFunction, int frequencyHz = 20)
        : core(std::make_shared<Core>(std::move(tickFunction), frequencyHz))
    {
    }

    ~ServiceTimer() { detach(); }

    ServiceTimer(const ServiceTimer&) = delete;
    ServiceTimer& operator=(const ServiceTimer&) = delete;

    // True once the timer has been started on the message thread.
    bool isRunning() const noexcept { return core->running.load(std::memory_order_acquire); }

    void ensureStarted()
    {
        if (core->running.load(std::memory_order_acquire))
            return;
        auto* manager = juce::MessageManager::getInstanceWithoutCreating();
        if (manager == nullptr)
            return;
        if (manager->isThisTheMessageThread())
        {
            core->startOnMessageThread();
            return;
        }
        if (core->startPosted.exchange(true, std::memory_order_acq_rel))
            return; // a start request is already queued
        auto shared = core;
        if (!juce::MessageManager::callAsync([shared] { shared->startOnMessageThread(); }))
            core->startPosted.store(false, std::memory_order_release); // could not queue: retry on the next call
    }

    void detach()
    {
        bool wasRunning = false;
        {
            const std::lock_guard<std::mutex> lock(core->mutex);
            core->detached = true;
            core->tick = nullptr;
            wasRunning = core->running.load(std::memory_order_acquire);
        }
        if (!wasRunning)
            return;
        auto* manager = juce::MessageManager::getInstanceWithoutCreating();
        if (manager != nullptr && manager->isThisTheMessageThread())
        {
            core->stopTimer();
            core->running.store(false, std::memory_order_release);
            return;
        }
        auto shared = core;
        if (manager == nullptr || !juce::MessageManager::callAsync([shared] { shared->stopOnMessageThread(); }))
        {
            // No message loop left: nothing can fire the timer any more. Leave the core to its destructor.
            core->running.store(false, std::memory_order_release);
        }
    }

private:
    struct Core final : private juce::Timer
    {
        Core(std::function<void()> f, int hzIn) : tick(std::move(f)), hz(hzIn) {}
        ~Core() override = default;

        void startOnMessageThread()
        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (detached || running.load(std::memory_order_acquire))
                return;
            startTimerHz(hz);
            running.store(true, std::memory_order_release);
        }

        void stopOnMessageThread()
        {
            stopTimer();
            running.store(false, std::memory_order_release);
        }

        using juce::Timer::stopTimer;

        void timerCallback() override
        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (!detached && tick)
                tick();
        }

        std::mutex mutex;
        std::function<void()> tick;
        bool detached = false;
        std::atomic<bool> running{false};
        std::atomic<bool> startPosted{false};
        int hz;
    };

    std::shared_ptr<Core> core;
};

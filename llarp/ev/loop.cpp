#include "loop.hpp"

#include <llarp/vpn/platform.hpp>

namespace llarp
{
    static auto logcat = log::Cat("EventLoop");

    std::shared_ptr<EventLoop> EventLoop::make()
    {
        return std::shared_ptr<EventLoop>{new EventLoop{}};
    }

    EventLoop::EventLoop() : _loop{std::make_shared<oxen::quic::Loop>()} {}

    EventLoop::~EventLoop()
    {
        // _loop->shutdown(_close_immediately);
        log::info(logcat, "lokinet loop shut down {}", _close_immediately ? "immediately" : "gracefully");
    }

    std::shared_ptr<EventWatcher> EventLoop::make_poll_watcher(std::function<void()> func)
    {
        return _loop->template make_shared<EventWatcher>(_loop->loop(), std::move(func));
    }

    void EventLoop::stop(bool)
    {
        // _loop->shutdown(immediate);
    }

}  //  namespace llarp

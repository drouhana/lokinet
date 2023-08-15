#include "libuv.hpp"
#include <memory>
#include <thread>
#include <type_traits>
#include <cstring>

#include <llarp/util/exceptions.hpp>
#include <llarp/util/thread/queue.hpp>
#include <llarp/vpn/platform.hpp>

#include <uvw.hpp>

namespace llarp::uv
{
  std::shared_ptr<uvw::loop>
  loop::MaybeGetUVWLoop()
  {
    return m_Impl;
  }

  class UVWakeup final : public EventLoopWakeup
  {
    std::shared_ptr<uvw::async_handle> async;

   public:
    UVWakeup(uvw::loop& loop, std::function<void()> callback)
        : async{loop.resource<uvw::async_handle>()}
    {
      async->on<uvw::async_event>([f = std::move(callback)](auto&, auto&) { f(); });
    }

    void
    Trigger() override
    {
      async->send();
    }

    ~UVWakeup() override
    {
      async->close();
    }
  };

  class UVRepeater final : public EventLoopRepeater
  {
    std::shared_ptr<uvw::timer_handle> timer;

   public:
    UVRepeater(uvw::loop& loop) : timer{loop.resource<uvw::timer_handle>()}
    {}

    void
    start(llarp_time_t every, std::function<void()> task) override
    {
      timer->start(every, every);
      timer->on<uvw::timer_event>([task = std::move(task)](auto&, auto&) { task(); });
    }

    ~UVRepeater() override
    {
      timer->stop();
    }
  };

  struct UDPHandle final : llarp::UDPHandle
  {
    UDPHandle(uvw::loop& loop, ReceiveFunc rf);

    bool
    listen(const SockAddr& addr) override;

    bool
    send(const SockAddr& dest, const llarp_buffer_t& buf) override;

    std::optional<SockAddr>
    LocalAddr() const override
    {
      struct addrinfo hints, *res = nullptr;
      hints.ai_family = AF_UNSPEC;
      hints.ai_flags = AI_NUMERICHOST;

      auto addr = handle->sock();
      int r = getaddrinfo(addr.ip.c_str(), nullptr, &hints, &res);

      if (r)
      {
        // should this throw?
        llarp::LogWarn("getaddrinfo failed to determine ipv4 vs ipv6 for ", addr.ip);
        return std::nullopt;
      }

      if (res->ai_family == AF_INET)
        return SockAddr{addr.ip, huint16_t{static_cast<uint16_t>(addr.port)}};
      if (res->ai_family == AF_INET6)
        return SockAddr{addr.ip, huint16_t{static_cast<uint16_t>(addr.port)}};
      return std::nullopt;
    }

    std::optional<int>
    file_descriptor() override
    {
#ifndef _WIN32
      if (int fd = handle->fd(); fd >= 0)
        return fd;
#endif
      return std::nullopt;
    }

    void
    close() override;

    ~UDPHandle() override;

   private:
    std::shared_ptr<uvw::udp_handle> handle;

    void
    reset_handle(uvw::loop& loop);
  };

  void
  loop::FlushLogic()
  {
    llarp::LogTrace("Loop::FlushLogic() start");
    while (not m_LogicCalls.empty())
    {
      auto f = m_LogicCalls.popFront();
      f();
    }
    llarp::LogTrace("Loop::FlushLogic() end");
  }

  void
  loop::tick_event_loop()
  {
    llarp::LogTrace("ticking event loop.");
    FlushLogic();
  }

  loop::loop(size_t queue_size) : llarp::EventLoop{}, m_LogicCalls{queue_size}
  {
    m_Impl = uvw::loop::create();
    if (!m_Impl)
      throw std::runtime_error{"Failed to construct libuv loop"};

#ifdef LOKINET_DEBUG
    last_time = 0;
    loop_run_count = 0;
#endif

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    m_Run.store(true);
    m_nextID.store(0);
    m_WakeUp = m_Impl->resource<uvw::async_handle>();

    if (!m_WakeUp)
      throw std::runtime_error{"Failed to create libuv async"};
    m_WakeUp->on<uvw::async_event>([this](const auto&, auto&) { tick_event_loop(); });
  }

  bool
  loop::running() const
  {
    return m_Run.load();
  }

  void
  loop::run()
  {
    llarp::LogTrace("Loop::run_loop()");
    m_EventLoopThreadID = std::this_thread::get_id();
    m_Impl->run();
    m_Impl->close();
    m_Impl.reset();
    llarp::LogInfo("we have stopped");
  }

  void
  loop::wakeup()
  {
    m_WakeUp->send();
  }

  std::shared_ptr<llarp::UDPHandle>
  loop::make_udp(UDPReceiveFunc on_recv)
  {
    return std::static_pointer_cast<llarp::UDPHandle>(
        std::make_shared<llarp::uv::UDPHandle>(*m_Impl, std::move(on_recv)));
  }

  static void
  setup_oneshot_timer(uvw::loop& loop, llarp_time_t delay, std::function<void()> callback)
  {
    auto timer = loop.resource<uvw::timer_handle>();
    timer->on<uvw::timer_event>([f = std::move(callback)](const auto&, auto& timer) {
      f();
      timer.stop();
      timer.close();
    });
    timer->start(delay, 0ms);
  }

  void
  loop::call_later(llarp_time_t delay_ms, std::function<void(void)> callback)
  {
    llarp::LogTrace("Loop::call_after_delay()");
#ifdef TESTNET_SPEED
    delay_ms *= TESTNET_SPEED;
#endif

    if (inEventLoop())
      setup_oneshot_timer(*m_Impl, delay_ms, std::move(callback));
    else
    {
      call_soon([this, f = std::move(callback), target_time = time_now() + delay_ms] {
        // Recalculate delay because it may have taken some time to get ourselves into the logic
        // thread
        auto updated_delay = target_time - time_now();
        if (updated_delay <= 0ms)
          f();  // Timer already expired!
        else
          setup_oneshot_timer(*m_Impl, updated_delay, std::move(f));
      });
    }
  }

  void
  loop::stop()
  {
    if (m_Run)
    {
      if (not inEventLoop())
        return call_soon([this] { stop(); });

      llarp::LogInfo("stopping event loop");
      m_Impl->walk([](auto&& handle) {
        if constexpr (!std::is_pointer_v<std::remove_reference_t<decltype(handle)>>)
          handle.close();
      });
      llarp::LogDebug("Closed all handles, stopping the loop");
      m_Impl->stop();

      m_Run.store(false);
    }
  }

  bool
  loop::add_ticker(std::function<void(void)> func)
  {
    auto check = m_Impl->resource<uvw::check_handle>();
    check->on<uvw::check_event>([f = std::move(func)](auto&, auto&) { f(); });
    check->start();
    return true;
  }

  bool
  loop::add_network_interface(
      std::shared_ptr<llarp::vpn::NetworkInterface> netif,
      std::function<void(llarp::net::IPPacket)> handler)
  {
#ifdef __linux__
    using event_t = uvw::poll_event;
    auto handle = m_Impl->resource<uvw::poll_handle>(netif->PollFD());
#else
    // we use a uv_prepare_t because it fires before blocking for new io events unconditionally
    // we want to match what linux does, using a uv_check_t does not suffice as the order of
    // operations is not what we need.
    using event_t = uvw::prepare_event;
    auto handle = m_Impl->resource<uvw::prepare_handle>();
#endif

    if (!handle)
      return false;

    handle->on<event_t>(
        [netif = std::move(netif), handler_func = std::move(handler)](const auto&, auto&) {
          for (auto pkt = netif->ReadNextPacket(); true; pkt = netif->ReadNextPacket())
          {
            if (pkt.empty())
              return;
            if (handler_func)
              handler_func(std::move(pkt));
            // on windows/apple, vpn packet io does not happen as an io action that wakes up the
            // event loop thus, we must manually wake up the event loop when we get a packet on our
            // interface. on linux/android this is a nop
            netif->MaybeWakeUpperLayers();
          }
        });

#ifdef __linux__
    handle->start(uvw::poll_handle::poll_event::READABLE);
#else
    handle->start();
#endif

    return true;
  }

  void
  loop::call_soon(std::function<void(void)> f)
  {
    if (not m_EventLoopThreadID.has_value())
    {
      m_LogicCalls.tryPushBack(f);
      m_WakeUp->send();
      return;
    }

    if (inEventLoop() and m_LogicCalls.full())
    {
      FlushLogic();
    }
    m_LogicCalls.pushBack(f);
    m_WakeUp->send();
  }

  // Sets `handle` to a new uvw UDP handle, first initiating a close and then disowning the handle
  // if already set, allocating the resource, and setting the receive event on it.
  void
  UDPHandle::reset_handle(uvw::loop& loop)
  {
    if (handle)
      handle->close();
    handle = loop.resource<uvw::udp_handle>();
    handle->on<uvw::udp_data_event>([this](auto& event, auto& /*handle*/) {
      on_recv(
          *this,
          SockAddr{event.sender.ip, huint16_t{static_cast<uint16_t>(event.sender.port)}},
          OwnedBuffer{std::move(event.data), event.length});
    });
  }

  llarp::uv::UDPHandle::UDPHandle(uvw::loop& loop, ReceiveFunc rf) : llarp::UDPHandle{std::move(rf)}
  {
    reset_handle(loop);
  }

  bool
  UDPHandle::listen(const SockAddr& addr)
  {
    if (handle->active())
      reset_handle(handle->parent());

    handle->on<uvw::error_event>([addr](auto& event, auto&) {
      throw llarp::util::bind_socket_error{
          fmt::format("failed to bind udp socket on {}: {}", addr, event.what())};
    });
    handle->bind(*static_cast<const sockaddr*>(addr));
    handle->recv();
    return true;
  }

  bool
  UDPHandle::send(const SockAddr& to, const llarp_buffer_t& buf)
  {
    return handle->try_send(
               *static_cast<const sockaddr*>(to),
               const_cast<char*>(reinterpret_cast<const char*>(buf.base)),
               buf.sz)
        >= 0;
  }

  void
  UDPHandle::close()
  {
    handle->close();
    handle.reset();
  }

  UDPHandle::~UDPHandle()
  {
    close();
  }

  std::shared_ptr<llarp::EventLoopWakeup>
  loop::make_waker(std::function<void()> callback)
  {
    return std::static_pointer_cast<llarp::EventLoopWakeup>(
        std::make_shared<UVWakeup>(*m_Impl, std::move(callback)));
  }

  std::shared_ptr<EventLoopRepeater>
  loop::make_repeater()
  {
    return std::static_pointer_cast<EventLoopRepeater>(std::make_shared<UVRepeater>(*m_Impl));
  }

  bool
  loop::inEventLoop() const
  {
    if (m_EventLoopThreadID)
      return *m_EventLoopThreadID == std::this_thread::get_id();
    // assume we are in it because we haven't started up yet
    return true;
  }

}  // namespace llarp::uv

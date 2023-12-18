#pragma once

#include "connection.hpp"

#include <llarp/constants/path.hpp>
#include <llarp/crypto/crypto.hpp>
#include <llarp/messages/common.hpp>
#include <llarp/path/transit_hop.hpp>
#include <llarp/router_contact.hpp>
#include <llarp/util/compare_ptr.hpp>
#include <llarp/util/decaying_hashset.hpp>
#include <llarp/util/logging.hpp>
#include <llarp/util/priority_queue.hpp>

#include <quic.hpp>

#include <atomic>
#include <set>
#include <unordered_map>

namespace
{
  static auto quic_cat = llarp::log::Cat("lokinet.quic");
}  // namespace

namespace llarp
{
  struct LinkManager;
  class NodeDB;

  using conn_open_hook = oxen::quic::connection_established_callback;
  using conn_closed_hook = oxen::quic::connection_closed_callback;
  using stream_open_hook = oxen::quic::stream_open_callback;
  using stream_closed_hook = oxen::quic::stream_close_callback;

  using keep_alive = oxen::quic::opt::keep_alive;
  using inbound_alpns = oxen::quic::opt::inbound_alpns;
  using outbound_alpns = oxen::quic::opt::outbound_alpns;

  inline const keep_alive ROUTER_KEEP_ALIVE{10s};
  inline const keep_alive CLIENT_KEEP_ALIVE{0s};

  namespace alpns
  {
    inline const auto SN_ALPNS = "SERVICE_NODE"_us;
    inline const auto C_ALPNS = "CLIENT"_us;

    inline const inbound_alpns SERVICE_INBOUND{{SN_ALPNS, C_ALPNS}};
    inline const outbound_alpns SERVICE_OUTBOUND{{SN_ALPNS}};

    inline const inbound_alpns CLIENT_INBOUND{};
    inline const outbound_alpns CLIENT_OUTBOUND{{C_ALPNS}};
  }  // namespace alpns

  namespace link
  {
    struct Connection;

    struct Endpoint
    {
      Endpoint(std::shared_ptr<oxen::quic::Endpoint> ep, LinkManager& lm);

      std::shared_ptr<oxen::quic::Endpoint> endpoint;
      LinkManager& link_manager;

      // for outgoing packets, we route via RouterID; map RouterID->Connection
      // for incoming packets, we get a ConnectionID; map ConnectionID->RouterID
      std::unordered_map<RouterID, std::shared_ptr<link::Connection>> service_conns;
      std::unordered_map<oxen::quic::ConnectionID, RouterID> service_connid_map;

      std::unordered_map<RouterID, std::shared_ptr<link::Connection>> client_conns;
      std::unordered_map<oxen::quic::ConnectionID, RouterID> client_connid_map;

      // for pending connections, cleared in LinkManager::on_conn_open
      std::unordered_map<RouterID, std::shared_ptr<link::Connection>> pending_conns;

      // TODO: see which of these is actually useful and delete the other
      std::shared_ptr<link::Connection>
      get_conn(const RemoteRC&) const;

      std::shared_ptr<link::Connection>
      get_conn(const RouterID&) const;

      bool
      have_client_conn(const RouterID& remote) const;

      bool
      have_conn(const RouterID& remote) const;

      std::pair<size_t, size_t>
      num_in_out() const;

      size_t
      num_connected(bool clients_only) const;

      bool
      get_random_connection(RemoteRC& router) const;

      template <typename... Opt>
      bool
      establish_connection(
          const oxen::quic::RemoteAddress& remote, const RemoteRC& rc, Opt&&... opts);

      void
      for_each_connection(std::function<void(link::Connection&)> func);

      void
      close_connection(RouterID rid);

     private:
      const bool _is_service_node;
    };
  }  // namespace link

  enum class SessionResult
  {
    Establish,
    Timeout,
    RouterNotFound,
    InvalidRouter,
    NoLink,
    EstablishFail
  };

  constexpr std::string_view
  ToString(SessionResult sr)
  {
    return sr == llarp::SessionResult::Establish     ? "success"sv
        : sr == llarp::SessionResult::Timeout        ? "timeout"sv
        : sr == llarp::SessionResult::NoLink         ? "no link"sv
        : sr == llarp::SessionResult::InvalidRouter  ? "invalid router"sv
        : sr == llarp::SessionResult::RouterNotFound ? "not found"sv
        : sr == llarp::SessionResult::EstablishFail  ? "establish failed"sv
                                                     : "???"sv;
  }
  template <>
  constexpr inline bool IsToStringFormattable<SessionResult> = true;

  struct PendingMessage
  {
    std::string body;
    std::optional<std::string> endpoint = std::nullopt;
    std::function<void(oxen::quic::message)> func = nullptr;

    RouterID rid;
    bool is_control = false;

    PendingMessage(std::string b) : body{std::move(b)}
    {}

    PendingMessage(
        std::string b, std::string ep, std::function<void(oxen::quic::message)> f = nullptr)
        : body{std::move(b)}, endpoint{std::move(ep)}, func{std::move(f)}, is_control{true}
    {}
  };

  using MessageQueue = std::deque<PendingMessage>;

  struct Router;

  struct LinkManager
  {
   public:
    static std::unique_ptr<LinkManager>
    make(Router& r);

    bool
    send_control_message(
        const RouterID& remote,
        std::string endpoint,
        std::string body,
        std::function<void(oxen::quic::message)> = nullptr);

    bool
    send_data_message(const RouterID& remote, std::string data);

    Router&
    router() const
    {
      return _router;
    }

   private:
    explicit LinkManager(Router& r);

    bool
    send_control_message_impl(
        const RouterID& remote,
        std::string endpoint,
        std::string body,
        std::function<void(oxen::quic::message)> = nullptr);

    friend struct link::Endpoint;

    std::atomic<bool> is_stopping;

    // sessions to persist -> timestamp to end persist at
    std::unordered_map<RouterID, llarp_time_t> persisting_conns;

    // holds any messages we attempt to send while connections are establishing
    std::unordered_map<RouterID, MessageQueue> pending_conn_msg_queue;
    // when establishing a connection, the rid of the remote is placed here to be cross-
    // checked by the tls verification callback
    std::map<RouterID, RemoteRC> rids_pending_verification;
    // in the interim of verifying an outbound connection and the creation of its link::Connection
    // object, we store the rid and rc here
    std::map<RouterID, RemoteRC> verified_rids;

    util::DecayingHashSet<RouterID> clients{path::DEFAULT_LIFETIME};

    std::shared_ptr<NodeDB> node_db;

    oxen::quic::Address addr;

    Router& _router;

    const bool _is_service_node;

    // FIXME: Lokinet currently expects to be able to kill all network functionality before
    // finishing other shutdown things, including destroying this class, and that is all in
    // Network's destructor, so we need to be able to destroy it before this class.
    std::unique_ptr<oxen::quic::Network> quic;
    std::shared_ptr<oxen::quic::GNUTLSCreds> tls_creds;
    link::Endpoint ep;

    void
    recv_data_message(oxen::quic::dgram_interface& dgi, bstring dgram);

    void
    recv_control_message(oxen::quic::message msg);

    void
    on_inbound_conn(oxen::quic::connection_interface& ci);

    void
    on_conn_open(oxen::quic::connection_interface& ci);

    void
    on_conn_closed(oxen::quic::connection_interface& ci, uint64_t ec);

    std::shared_ptr<oxen::quic::Endpoint>
    startup_endpoint();

    void
    register_commands(std::shared_ptr<oxen::quic::BTRequestStream>& s, const RouterID& rid);

   public:
    const link::Endpoint&
    endpoint() const
    {
      return ep;
    }

    const oxen::quic::Address&
    local()
    {
      return addr;
    }

    void
    gossip_rc(const RouterID& last_sender, const RemoteRC& rc);

    void
    handle_gossip_rc(oxen::quic::message m);

    void
    fetch_rcs(
        const RouterID& source,
        std::string payload,
        std::function<void(oxen::quic::message m)> func);

    void
    handle_fetch_rcs(oxen::quic::message m);

    void
    fetch_router_ids(
        const RouterID& via, std::string payload, std::function<void(oxen::quic::message m)> func);

    void
    handle_fetch_router_ids(oxen::quic::message m);

    void
    fetch_bootstrap_rcs(
        const RemoteRC& source,
        std::string payload,
        std::function<void(oxen::quic::message m)> func);

    void
    handle_fetch_bootstrap_rcs(oxen::quic::message m);

    bool
    have_connection_to(const RouterID& remote) const;

    bool
    have_client_connection_to(const RouterID& remote) const;

    void
    test_reachability(const RouterID& rid, conn_open_hook, conn_closed_hook);

    void
    connect_to(const RouterID& router, conn_open_hook = nullptr);

    void
    connect_to(const RemoteRC& rc, conn_open_hook = nullptr, conn_closed_hook = nullptr);

    void
    close_connection(RouterID rid);

    void
    stop();

    void
    set_conn_persist(const RouterID& remote, llarp_time_t until);

    std::pair<size_t, size_t>
    num_in_out() const;

    size_t
    get_num_connected(bool clients_only = false) const;

    size_t
    get_num_connected_clients() const;

    bool
    get_random_connected(RemoteRC& router) const;

    bool
    is_service_node() const;

    void
    check_persisting_conns(llarp_time_t now);

    util::StatusObject
    extract_status() const;

    void
    init();

    void
    for_each_connection(std::function<void(link::Connection&)> func);

    // Attempts to connect to a number of random routers.
    //
    // This will try to connect to *up to* num_conns routers, but will not
    // check if we already have a connection to any of the random set, as making
    // that thread safe would be slow...I think.
    void
    connect_to_random(int num_conns, bool client_only = false);

    /// always maintain this many client connections to other routers
    int client_router_connections = 4;

   private:
    // DHT messages
    void
    handle_find_name(std::string_view body, std::function<void(std::string)> respond);  // relay
    void
    handle_find_intro(std::string_view body, std::function<void(std::string)> respond);  // relay
    void
    handle_publish_intro(std::string_view body, std::function<void(std::string)> respond);  // relay

    // Path messages
    void
    handle_path_build(oxen::quic::message, const RouterID& from);  // relay
    void handle_path_latency(oxen::quic::message);                 // relay
    void handle_path_transfer(oxen::quic::message);                // relay

    // Exit messages
    void handle_obtain_exit(oxen::quic::message);  // relay
    void handle_update_exit(oxen::quic::message);  // relay
    void handle_close_exit(oxen::quic::message);   // relay

    // Misc
    void handle_convo_intro(oxen::quic::message);

    // These requests come over a path (as a "path_control" request),
    // may or may not need to make a request to another relay,
    // then respond (onioned) back along the path.
    std::unordered_map<
        std::string_view,
        void (LinkManager::*)(std::string_view body, std::function<void(std::string)> respond)>
        path_requests = {
            {"find_name"sv, &LinkManager::handle_find_name},
            {"publish_intro"sv, &LinkManager::handle_publish_intro},
            {"find_intro"sv, &LinkManager::handle_find_intro}};

    // these requests are direct, i.e. not over a path;
    // the rest are relay->relay
    std::unordered_map<
        std::string_view,
        void (LinkManager::*)(std::string_view body, std::function<void(std::string)> respond)>
        direct_requests = {
            {"publish_intro"sv, &LinkManager::handle_publish_intro},
            {"find_intro"sv, &LinkManager::handle_find_intro}};

    // Path relaying
    void
    handle_path_control(oxen::quic::message, const RouterID& from);

    void
    handle_inner_request(
        oxen::quic::message m, std::string payload, std::shared_ptr<path::TransitHop> hop);

    // DHT responses
    void handle_find_name_response(oxen::quic::message);
    void handle_find_intro_response(oxen::quic::message);
    void handle_publish_intro_response(oxen::quic::message);

    // Path responses
    void handle_path_latency_response(oxen::quic::message);
    void handle_path_transfer_response(oxen::quic::message);

    // Exit responses
    void handle_obtain_exit_response(oxen::quic::message);
    void handle_update_exit_response(oxen::quic::message);
    void handle_close_exit_response(oxen::quic::message);

    std::unordered_map<std::string, void (LinkManager::*)(oxen::quic::message)> rpc_responses = {
        {"find_name", &LinkManager::handle_find_name_response},
        {"publish_intro", &LinkManager::handle_publish_intro_response},
        {"find_intro", &LinkManager::handle_find_intro_response},
        {"update_exit", &LinkManager::handle_update_exit_response},
        {"obtain_exit", &LinkManager::handle_obtain_exit_response},
        {"close_exit", &LinkManager::handle_close_exit_response}};
  };

  namespace link
  {
    template <typename... Opt>
    bool
    Endpoint::establish_connection(
        const oxen::quic::RemoteAddress& remote, const RemoteRC& rc, Opt&&... opts)
    {
      try
      {
        const auto& rid = rc.router_id();
        log::critical(logcat, "Establishing connection to RID:{}", rid);

        bool is_snode = link_manager.is_service_node();

        auto conn_interface = endpoint->connect(
            remote,
            link_manager.tls_creds,
            is_snode ? ROUTER_KEEP_ALIVE : CLIENT_KEEP_ALIVE,
            std::forward<Opt>(opts)...);

        // add to pending conns
        auto [itr, b] = pending_conns.emplace(rid, nullptr);

        auto control_stream = conn_interface->template open_stream<oxen::quic::BTRequestStream>(
            [this, rid = rid](oxen::quic::Stream&, uint64_t error_code) {
              log::warning(
                  logcat,
                  "BTRequestStream closed unexpectedly (ec:{}); closing connection...",
                  error_code);
              close_connection(rid);
            });

        link_manager.register_commands(control_stream, rid);
        itr->second = std::make_shared<link::Connection>(conn_interface, control_stream);

        log::critical(logcat, "Connection to RID:{} added to pending connections...", rid);
        return true;
      }
      catch (...)
      {
        log::error(quic_cat, "Error: failed to establish connection to {}", remote);
        return false;
      }
    }
  }  // namespace link

}  // namespace llarp

/*
- Refactor RouterID to use gnutls info and maybe ConnectionID
- Combine routerID and connectionID to simplify mapping in llarp/link/endpoint.hpp
- Combine llarp/link/session.hpp into llarp/link/connection.hpp::Connection

- Combine llarp/link/server.hpp::ILinkLayer into llarp/link/endpoint.hpp::Endpoint
  - must maintain metadata storage, callbacks, etc

- If: one endpoint for ipv4 and ipv6
  - Then: can potentially combine:
    - llarp/link/endpoint.hpp
    - llarp/link/link_manager.hpp
    - llarp/link/outbound_message_handler.hpp
    - llarp/link/outbound_session_maker.hpp

  -> Yields mega-combo endpoint managing object?
    - Can avoid "kitchen sink" by greatly reducing complexity of implementation

  llarp/router/outbound_message_handler.hpp
    - pendingsessionmessagequeue
      - establish queue of messages to be sent on a connection we are creating
      - upon creation, send these messages in the connection established callback
    - if connection times out, flush queue
    - TOCHECK: is priority used at all??


std::unordered_map<std::string, void (llarp::link::LinkManager::*)(oxen::quic::message)>
rpc_commands = {
    {"find_name", &handle_find_name},
    // ...
};

for (const auto& [name, mfn] : rpc_commands)
    bparser.add_command(name, [this, mfn] (oxen::quic::message m) {
        router->call([this, mfn, m=std::move(m)] mutable {
            try {
                std::invoke(mfn, this, std::move(m));
            } catch (const std::exception& e) {
                m.respond("Error: "s + e.what(), true);
            }
        });
    });

*/

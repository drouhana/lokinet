#ifndef LLARP_ROUTER_HPP
#define LLARP_ROUTER_HPP

#include <config.h>
#include <constants/link_layer.hpp>
#include <crypto/types.hpp>
#include <ev/ev.h>
#include <exit/context.hpp>
#include <handlers/tun.hpp>
#include <messages/link_message_parser.hpp>
#include <nodedb.hpp>
#include <path/path.hpp>
#include <profiling.hpp>
#include <router_contact.hpp>
#include <routing/handler.hpp>
#include <routing/message_parser.hpp>
#include <rpc/rpc.hpp>
#include <service/context.hpp>
#include <util/buffer.hpp>
#include <util/fs.hpp>
#include <util/logic.hpp>
#include <util/mem.hpp>
#include <util/str.hpp>
#include <util/threadpool.hpp>

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

namespace llarp
{
  struct Crypto;
}

bool
llarp_findOrCreateEncryption(llarp::Crypto *crypto, const fs::path &fpath,
                             llarp::SecretKey &encryption);

bool
llarp_findOrCreateIdentity(llarp::Crypto *crypto, const fs::path &path,
                           llarp::SecretKey &secretkey);

bool
llarp_loadServiceNodeIdentityKey(llarp::Crypto *crypto, const fs::path &fpath,
                                 llarp::SecretKey &secretkey);

struct TryConnectJob;

namespace llarp
{
  template < typename T >
  struct CompareLinks
  {
    bool
    operator()(const std::unique_ptr< T > &left,
               const std::unique_ptr< T > &right) const
    {
      const std::string leftName  = left->Name();
      const std::string rightName = right->Name();
      return left->Rank() < right->Rank() || leftName < rightName;
    }
  };

  struct AbstractRouter
  {
    virtual ~AbstractRouter() = 0;

    virtual Logic *
    logic() const = 0;

    virtual llarp_dht_context *
    dht() const = 0;

    virtual Crypto *
    crypto() const = 0;

    virtual llarp_nodedb *
    nodedb() const = 0;

    virtual const path::PathContext &
    pathContext() const = 0;

    virtual path::PathContext &
    pathContext() = 0;

    virtual const llarp::RouterContact &
    rc() const = 0;

    virtual const byte_t *
    pubkey() const = 0;

    virtual llarp_time_t
    Now() const = 0;

    virtual bool
    SendToOrQueue(const llarp::RouterID &remote,
                  const llarp::ILinkMessage *msg) = 0;

    virtual void
    PersistSessionUntil(const llarp::RouterID &remote, llarp_time_t until) = 0;

    virtual bool
    ParseRoutingMessageBuffer(const llarp_buffer_t &buf,
                              routing::IMessageHandler *h, PathID_t rxid) = 0;

    virtual void
    HandleDHTLookupForExplore(
        llarp::RouterID remote,
        const std::vector< llarp::RouterContact > &results) = 0;
  };

  struct Router final : public AbstractRouter
  {
    bool ready;
    // transient iwp encryption key
    fs::path transport_keyfile = "transport.key";

    // nodes to connect to on startup
    // DEPRECATED
    // std::map< std::string, fs::path > connect;

    // long term identity key
    fs::path ident_keyfile = "identity.key";

    fs::path encryption_keyfile = "encryption.key";

    // path to write our self signed rc to
    fs::path our_rc_file = "rc.signed";

    // our router contact
    llarp::RouterContact _rc;

    /// are we using the lokid service node seed ?
    bool usingSNSeed = false;

    /// should we obey the service node whitelist?
    bool whitelistRouters = false;

    Logic *
    logic() const override
    {
      return _logic;
    }

    llarp_dht_context *
    dht() const override
    {
      return _dht;
    }

    Crypto *
    crypto() const override
    {
      return _crypto.get();
    }

    llarp_nodedb *
    nodedb() const override
    {
      return _nodedb;
    }

    const path::PathContext &
    pathContext() const override
    {
      return paths;
    }

    path::PathContext &
    pathContext() override
    {
      return paths;
    }

    const llarp::RouterContact &
    rc() const override
    {
      return _rc;
    }

    // our ipv4 public setting
    bool publicOverride = false;
    struct sockaddr_in ip4addr;
    llarp::AddressInfo addrInfo;

    llarp_ev_loop *netloop;
    llarp_threadpool *tp;
    Logic *_logic;
    std::unique_ptr< Crypto > _crypto;
    path::PathContext paths;
    exit::Context exitContext;
    SecretKey identity;
    SecretKey encryption;
    llarp_threadpool *disk;
    llarp_dht_context *_dht = nullptr;
    llarp_nodedb *_nodedb;

    bool
    Sign(Signature &sig, const llarp_buffer_t &buf) const;

    // buffer for serializing link messages
    std::array< byte_t, MAX_LINK_MSG_SIZE > linkmsg_buffer;

    /// always maintain this many connections to other routers
    size_t minConnectedRouters = 1;
    /// hard upperbound limit on the number of router to router connections
    size_t maxConnectedRouters = 2000;

    size_t minRequiredRouters = 4;

    // should we be sending padded messages every interval?
    bool sendPadding = false;

    uint32_t ticker_job_id = 0;

    llarp::InboundMessageParser inbound_link_msg_parser;
    llarp::routing::InboundMessageParser inbound_routing_msg_parser;

    llarp::service::Context hiddenServiceContext;

    using NetConfig_t = std::unordered_multimap< std::string, std::string >;

    /// default network config for default network interface
    NetConfig_t netConfig;

    /// identity keys whitelist of routers we will connect to directly (not for
    /// service nodes)
    std::set< llarp::RouterID > strictConnectPubkeys;

    /// bootstrap RCs
    std::list< llarp::RouterContact > bootstrapRCList;

    bool
    ExitEnabled() const
    {
      // TODO: use equal_range ?
      auto itr = netConfig.find("exit");
      if(itr == netConfig.end())
        return false;
      return llarp::IsTrueValue(itr->second.c_str());
    }

    bool
    CreateDefaultHiddenService();

    bool
    ShouldCreateDefaultHiddenService();

    const std::string DefaultRPCBindAddr = "127.0.0.1:1190";
    bool enableRPCServer                 = true;
    std::unique_ptr< llarp::rpc::Server > rpcServer;
    std::string rpcBindAddr = DefaultRPCBindAddr;

    /// lokid caller
    const std::string DefaultLokidRPCAddr = "127.0.0.1:22023";
    std::unique_ptr< llarp::rpc::Caller > rpcCaller;
    std::string lokidRPCAddr     = DefaultLokidRPCAddr;
    std::string lokidRPCUser     = "";
    std::string lokidRPCPassword = "";

    std::set< std::unique_ptr< llarp::ILinkLayer >,
              CompareLinks< llarp::ILinkLayer > >
        outboundLinks;
    std::set< std::unique_ptr< llarp::ILinkLayer >,
              CompareLinks< llarp::ILinkLayer > >
        inboundLinks;

    llarp::Profiling routerProfiling;
    std::string routerProfilesFile = "profiles.dat";

    using MessageQueue = std::queue< std::vector< byte_t > >;

    /// outbound message queue
    std::unordered_map< llarp::RouterID, MessageQueue, llarp::RouterID::Hash >
        outboundMessageQueue;

    /// loki verified routers
    std::unordered_map< llarp::RouterID, llarp::RouterContact,
                        llarp::RouterID::Hash >
        validRouters;

    // pending establishing session with routers
    std::unordered_map< llarp::RouterID, std::unique_ptr< TryConnectJob >,
                        llarp::RouterID::Hash >
        pendingEstablishJobs;

    // pending RCs to be verified by pubkey
    std::unordered_map< llarp::RouterID, llarp_async_verify_rc,
                        llarp::RouterID::Hash >
        pendingVerifyRC;

    // sessions to persist -> timestamp to end persist at
    std::unordered_map< llarp::RouterID, llarp_time_t, llarp::RouterID::Hash >
        m_PersistingSessions;

    // lokinet routers from lokid, maps pubkey to when we think it will expire,
    // set to max value right now
    std::unordered_map< llarp::RouterID, llarp_time_t, llarp::PubKey::Hash >
        lokinetRouters;

    Router(struct llarp_threadpool *tp, struct llarp_ev_loop *netloop,
           llarp::Logic *logic);

    ~Router();

    void
    OnSessionEstablished(llarp::RouterContact rc);

    bool
    HandleRecvLinkMessageBuffer(llarp::ILinkSession *from,
                                const llarp_buffer_t &msg);

    void
    AddInboundLink(std::unique_ptr< llarp::ILinkLayer > &link);

    bool
    InitOutboundLinks();

    bool
    GetRandomGoodRouter(RouterID &r);

    /// initialize us as a service node
    /// return true on success
    bool
    InitServiceNode();

    /// return true if we are running in service node mode
    bool
    IsServiceNode() const;

    void
    Close();

    bool
    LoadHiddenServiceConfig(const char *fname);

    bool
    AddHiddenService(const llarp::service::Config::section_t &config);

    bool
    Configure(struct llarp_config *conf);

    bool
    Ready();

    bool
    Run(struct llarp_nodedb *nodedb);

    /// stop running the router logic gracefully
    void
    Stop();

    /// close all sessions and shutdown all links
    void
    StopLinks();

    void
    PersistSessionUntil(const llarp::RouterID &remote,
                        llarp_time_t until) override;

    bool
    EnsureIdentity();

    bool
    EnsureEncryptionKey();

    bool
    ConnectionToRouterAllowed(const llarp::RouterID &router) const;

    bool
    SaveRC();

    const byte_t *
    pubkey() const override
    {
      return llarp::seckey_topublic(identity);
    }

    void
    OnConnectTimeout(ILinkSession *session);

    bool
    HasPendingConnectJob(const llarp::RouterID &remote);

    void
    try_connect(fs::path rcfile);

    /// inject configuration and reconfigure router
    bool
    Reconfigure(llarp_config *conf);

    /// validate new configuration against old one
    /// return true on 100% valid
    /// return false if not 100% valid
    bool
    ValidateConfig(llarp_config *conf) const;

    /// send to remote router or queue for sending
    /// returns false on overflow
    /// returns true on successful queue
    /// NOT threadsafe
    /// MUST be called in the logic thread
    bool
    SendToOrQueue(const llarp::RouterID &remote,
                  const llarp::ILinkMessage *msg) override;

    /// sendto or drop
    void
    SendTo(llarp::RouterID remote, const llarp::ILinkMessage *msg,
           llarp::ILinkLayer *chosen);

    /// manually flush outbound message queue for just 1 router
    void
    FlushOutboundFor(llarp::RouterID remote,
                     llarp::ILinkLayer *chosen = nullptr);

    /// manually discard all pending messages to remote router
    void
    DiscardOutboundFor(const llarp::RouterID &remote);

    /// try establishing a session to a remote router
    void
    TryEstablishTo(const llarp::RouterID &remote);

    /// lookup a router by pubkey when it expires when we are a service node
    void
    ServiceNodeLookupRouterWhenExpired(llarp::RouterID remote);

    void
    HandleDHTLookupForExplore(
        llarp::RouterID remote,
        const std::vector< llarp::RouterContact > &results) override;

    void
    ForEachPeer(
        std::function< void(const llarp::ILinkSession *, bool) > visit) const;

    void
    ForEachPeer(std::function< void(llarp::ILinkSession *) > visit);

    /// check if newRc matches oldRC and update local rc for this remote contact
    /// if valid
    /// returns true on valid and updated
    /// returns false otherwise
    bool
    CheckRenegotiateValid(RouterContact newRc, RouterContact oldRC);

    /// flush outbound message queue
    void
    FlushOutbound();

    /// called by link when a remote session has no more sessions open
    void
    SessionClosed(RouterID remote);

    /// call internal router ticker
    void
    Tick();

    /// get time from event loop
    llarp_time_t
    Now() const override
    {
      return llarp_ev_loop_time_now_ms(netloop);
    }

    /// schedule ticker to call i ms from now
    void
    ScheduleTicker(uint64_t i = 1000);

    llarp::ILinkLayer *
    GetLinkWithSessionByPubkey(const llarp::RouterID &remote);

    /// parse a routing message in a buffer and handle it with a handler if
    /// successful parsing return true on parse and handle success otherwise
    /// return false
    bool
    ParseRoutingMessageBuffer(const llarp_buffer_t &buf,
                              routing::IMessageHandler *h,
                              PathID_t rxid) override;

    void
    ConnectToRandomRouters(int N);

    size_t
    NumberOfConnectedRouters() const;

    bool
    TryConnectAsync(llarp::RouterContact rc, uint16_t tries);

    bool
    GetRandomConnectedRouter(llarp::RouterContact &result) const;

    void
    async_verify_RC(const llarp::RouterContact &rc, llarp::ILinkLayer *link);

    void
    HandleDHTLookupForSendTo(
        llarp::RouterID remote,
        const std::vector< llarp::RouterContact > &results);

    bool
    HasSessionTo(const llarp::RouterID &remote) const;

    void
    HandleDHTLookupForTryEstablishTo(
        llarp::RouterID remote,
        const std::vector< llarp::RouterContact > &results);

    static void
    on_verify_client_rc(llarp_async_verify_rc *context);

    static void
    on_verify_server_rc(llarp_async_verify_rc *context);

    static void
    handle_router_ticker(void *user, uint64_t orig, uint64_t left);

    static void
    HandleAsyncLoadRCForSendTo(llarp_async_load_rc *async);

   private:
    std::atomic< bool > _stopping;
    std::atomic< bool > _running;

    bool
    UpdateOurRC(bool rotateKeys = false);

    template < typename Config >
    void
    mergeHiddenServiceConfig(const Config &in, Config &out)
    {
      for(const auto &item : netConfig)
        out.push_back({item.first, item.second});
      for(const auto &item : in)
        out.push_back({item.first, item.second});
    }
  };

}  // namespace llarp

#endif
#include "gotrouter.hpp"

#include <memory>
#include <llarp/path/path_context.hpp>
#include <llarp/router/router.hpp>
#include <llarp/router/rc_lookup_handler.hpp>
#include <llarp/tooling/rc_event.hpp>

namespace llarp::dht
{
  GotRouterMessage::~GotRouterMessage() = default;

  void
  GotRouterMessage::bt_encode(oxenc::bt_dict_producer& btdp) const
  {
    try
    {
      btdp.append("A", "S");

      if (closerTarget)
        btdp.append("K", closerTarget->ToView());

      {
        auto sublist = btdp.append_list("N");

        for (auto& k : nearKeys)
          sublist.append(k.ToView());
      }

      {
        auto sublist = btdp.append_list("R");

        for (auto& r : foundRCs)
          sublist.append(r.ToString());
      }

      btdp.append("T", txid);
      btdp.append("V", version);
    }
    catch (...)
    {
      log::error(dht_cat, "Error: GotRouterMessage failed to bt encode contents!");
    }
  }

  bool
  GotRouterMessage::decode_key(const llarp_buffer_t& key, llarp_buffer_t* val)
  {
    if (key.startswith("K"))
    {
      if (closerTarget)  // duplicate key?
        return false;
      closerTarget = std::make_unique<dht::Key_t>();
      return closerTarget->BDecode(val);
    }
    if (key.startswith("N"))
    {
      return BEncodeReadList(nearKeys, val);
    }
    if (key.startswith("R"))
    {
      return BEncodeReadList(foundRCs, val);
    }
    if (key.startswith("T"))
    {
      return bencode_read_integer(val, &txid);
    }
    bool read = false;
    if (!BEncodeMaybeVerifyVersion("V", version, llarp::constants::proto_version, read, key, val))
      return false;

    return read;
  }

  bool
  GotRouterMessage::handle_message(
      AbstractDHTMessageHandler& dht,
      [[maybe_unused]] std::vector<std::unique_ptr<AbstractDHTMessage>>& replies) const
  {
    if (relayed)
    {
      auto pathset = dht.GetRouter()->path_context().GetLocalPathSet(pathID);
      auto copy = std::make_shared<const GotRouterMessage>(*this);
      return pathset && pathset->HandleGotRouterMessage(copy);
    }
    // not relayed
    const TXOwner owner(From, txid);

    if (dht.pendingExploreLookups().HasPendingLookupFrom(owner))
    {
      LogDebug("got ", nearKeys.size(), " results in GRM for explore");
      if (nearKeys.empty())
        dht.pendingExploreLookups().NotFound(owner, closerTarget);
      else
      {
        dht.pendingExploreLookups().Found(owner, From.as_array(), nearKeys);
      }
      return true;
    }
    // not explore lookup
    if (dht.pendingRouterLookups().HasPendingLookupFrom(owner))
    {
      LogDebug("got ", foundRCs.size(), " results in GRM for lookup");
      if (foundRCs.empty())
        dht.pendingRouterLookups().NotFound(owner, closerTarget);
      else if (foundRCs[0].pubkey.IsZero())
        return false;
      else
        dht.pendingRouterLookups().Found(owner, foundRCs[0].pubkey, foundRCs);
      return true;
    }
    // store if valid
    for (const auto& rc : foundRCs)
    {
      if (not dht.GetRouter()->rc_lookup_handler().check_rc(rc))
        return false;
      if (txid == 0)  // txid == 0 on gossip
      {
        auto* router = dht.GetRouter();
        router->notify_router_event<tooling::RCGossipReceivedEvent>(router->pubkey(), rc);
        router->GossipRCIfNeeded(rc);

        auto peerDb = router->peer_db();
        if (peerDb)
          peerDb->handleGossipedRC(rc);
      }
    }
    return true;
  }
}  // namespace llarp::dht

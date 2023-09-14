#pragma once

#include <llarp/util/buffer.hpp>
#include <oxenc/bt.h>

#include <llarp/util/logging.hpp>

namespace
{
  static auto link_cat = llarp::log::Cat("lokinet.link");
}  // namespace

namespace llarp
{
  /// abstract base class for serialized messages
  struct AbstractSerializable
  {
    virtual std::string
    bt_encode() const = 0;
    virtual void
    bt_encode(llarp_buffer& b) const = 0;
    virtual void
    bt_encode(oxenc::bt_dict_producer& btdp) const = 0;
    virtual void
    bt_encode(oxenc::bt_list_producer& btlp) const = 0;
  };

  struct AbstractMessageHandler
  {
    virtual bool
    handle_message(AbstractSerializable&) = 0;
  };
}  // namespace llarp

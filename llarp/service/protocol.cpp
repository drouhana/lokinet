#include "protocol.hpp"
#include <llarp/path/path.hpp>
#include <llarp/routing/handler.hpp>
#include <llarp/util/buffer.hpp>
#include <llarp/util/mem.hpp>
#include <llarp/util/meta/memfn.hpp>
#include "endpoint.hpp"
#include <llarp/router/router.hpp>
#include <utility>

namespace llarp::service
{
  ProtocolMessage::ProtocolMessage()
  {
    tag.Zero();
  }

  ProtocolMessage::ProtocolMessage(const ConvoTag& t) : tag(t)
  {}

  ProtocolMessage::~ProtocolMessage() = default;

  void
  ProtocolMessage::PutBuffer(const llarp_buffer_t& buf)
  {
    payload.resize(buf.sz);
    memcpy(payload.data(), buf.base, buf.sz);
  }

  void
  ProtocolMessage::ProcessAsync(
      path::Path_ptr path, PathID_t from, std::shared_ptr<ProtocolMessage> self)
  {
    if (!self->handler->HandleDataMessage(path, from, self))
      LogWarn("failed to handle data message from ", path->Name());
  }

  bool
  ProtocolMessage::decode_key(const llarp_buffer_t& k, llarp_buffer_t* buf)
  {
    bool read = false;
    if (!BEncodeMaybeReadDictInt("a", proto, read, k, buf))
      return false;
    if (k.startswith("d"))
    {
      llarp_buffer_t strbuf;
      if (!bencode_read_string(buf, &strbuf))
        return false;
      PutBuffer(strbuf);
      return true;
    }
    if (!BEncodeMaybeReadDictEntry("i", introReply, read, k, buf))
      return false;
    if (!BEncodeMaybeReadDictInt("n", seqno, read, k, buf))
      return false;
    if (!BEncodeMaybeReadDictEntry("s", sender, read, k, buf))
      return false;
    if (!BEncodeMaybeReadDictEntry("t", tag, read, k, buf))
      return false;
    if (!BEncodeMaybeReadDictInt("v", version, read, k, buf))
      return false;
    return read;
  }

  std::string
  ProtocolMessage::bt_encode() const
  {
    oxenc::bt_dict_producer btdp;

    try
    {
      btdp.append("a", static_cast<uint64_t>(proto));

      if (not payload.empty())
        btdp.append(
            "d", std::string_view{reinterpret_cast<const char*>(payload.data()), payload.size()});

      {
        auto subdict = btdp.append_dict("i");
        introReply.bt_encode(subdict);
      }

      btdp.append("n", seqno);

      {
        auto subdict = btdp.append_dict("s");
        sender.bt_encode(subdict);
      }

      btdp.append("t", tag.ToView());
      btdp.append("v", version);
    }
    catch (...)
    {
      log::critical(route_cat, "Error: ProtocolMessage failed to bt encode contents!");
    }

    return std::move(btdp).str();
  }

  std::vector<char>
  ProtocolMessage::EncodeAuthInfo() const
  {
    oxenc::bt_dict_producer btdp;

    try
    {
      btdp.append("a", static_cast<uint64_t>(proto));

      {
        auto subdict = btdp.append_dict("s");
        sender.bt_encode(subdict);
      }

      btdp.append("t", tag.ToView());
      btdp.append("v", version);
    }
    catch (...)
    {
      log::critical(route_cat, "Error: ProtocolMessage failed to bt encode auth info");
    }

    auto view = btdp.view();
    std::vector<char> data;
    data.resize(view.size());

    std::copy_n(view.data(), view.size(), data.data());
    return data;
  }

  ProtocolFrameMessage::~ProtocolFrameMessage() = default;

  std::string
  ProtocolFrameMessage::bt_encode() const
  {
    oxenc::bt_dict_producer btdp;

    try
    {
      btdp.append("A", "H");
      btdp.append("C", cipher.ToView());
      btdp.append("D", std::string_view{reinterpret_cast<const char*>(enc.data()), enc.size()});
      btdp.append("F", path_id.ToView());
      btdp.append("N", nonce.ToView());
      btdp.append("R", flag);
      btdp.append("T", convo_tag.ToView());
      btdp.append("V", version);
      btdp.append("Z", sig.ToView());
    }
    catch (...)
    {
      log::critical(route_cat, "Error: ProtocolFrameMessage failed to bt encode contents!");
    }

    return std::move(btdp).str();
  }

  bool
  ProtocolFrameMessage::decode_key(const llarp_buffer_t& key, llarp_buffer_t* val)
  {
    bool read = false;
    if (key.startswith("A"))
    {
      llarp_buffer_t strbuf;
      if (!bencode_read_string(val, &strbuf))
        return false;
      if (strbuf.sz != 1)
        return false;
      return *strbuf.cur == 'H';
    }
    if (!BEncodeMaybeReadDictEntry("D", enc, read, key, val))
      return false;
    if (!BEncodeMaybeReadDictEntry("F", path_id, read, key, val))
      return false;
    if (!BEncodeMaybeReadDictEntry("C", cipher, read, key, val))
      return false;
    if (!BEncodeMaybeReadDictEntry("N", nonce, read, key, val))
      return false;
    if (!BEncodeMaybeReadDictInt("S", sequence_number, read, key, val))
      return false;
    if (!BEncodeMaybeReadDictInt("R", flag, read, key, val))
      return false;
    if (!BEncodeMaybeReadDictEntry("T", convo_tag, read, key, val))
      return false;
    if (!BEncodeMaybeVerifyVersion("V", version, llarp::constants::proto_version, read, key, val))
      return false;
    if (!BEncodeMaybeReadDictEntry("Z", sig, read, key, val))
      return false;
    return read;
  }

  bool
  ProtocolFrameMessage::DecryptPayloadInto(
      const SharedSecret& sharedkey, ProtocolMessage& msg) const
  {
    Encrypted<2048> tmp = enc;
    CryptoManager::instance()->xchacha20(tmp.data(), tmp.size(), sharedkey, nonce);

    return bencode_decode_dict(msg, tmp.Buffer());
  }

  bool
  ProtocolFrameMessage::Sign(const Identity& localIdent)
  {
    sig.Zero();
    std::array<byte_t, MAX_PROTOCOL_MESSAGE_SIZE> tmp;
    llarp_buffer_t buf(tmp);
    // encode
    auto bte = bt_encode();
    buf.write(bte.begin(), bte.end());

    // rewind
    buf.sz = buf.cur - buf.base;
    buf.cur = buf.base;
    // sign
    return localIdent.Sign(sig, reinterpret_cast<uint8_t*>(bte.data()), bte.size());
  }

  bool
  ProtocolFrameMessage::EncryptAndSign(
      const ProtocolMessage& msg, const SharedSecret& sessionKey, const Identity& localIdent)
  {
    // encode message
    auto bte1 = msg.bt_encode();
    // encrypt
    CryptoManager::instance()->xchacha20(
        reinterpret_cast<uint8_t*>(bte1.data()), bte1.size(), sessionKey, nonce);
    // put encrypted buffer
    std::memcpy(enc.data(), bte1.data(), bte1.size());
    // zero out signature
    sig.Zero();

    auto bte2 = bt_encode();
    // sign
    if (!localIdent.Sign(sig, reinterpret_cast<uint8_t*>(bte2.data()), bte2.size()))
    {
      LogError("failed to sign? wtf?!");
      return false;
    }
    return true;
  }

  struct AsyncFrameDecrypt
  {
    path::Path_ptr path;
    EventLoop_ptr loop;
    std::shared_ptr<ProtocolMessage> msg;
    const Identity& m_LocalIdentity;
    Endpoint* handler;
    const ProtocolFrameMessage frame;
    const Introduction fromIntro;

    AsyncFrameDecrypt(
        EventLoop_ptr l,
        const Identity& localIdent,
        Endpoint* h,
        std::shared_ptr<ProtocolMessage> m,
        const ProtocolFrameMessage& f,
        const Introduction& recvIntro)
        : loop(std::move(l))
        , msg(std::move(m))
        , m_LocalIdentity(localIdent)
        , handler(h)
        , frame(f)
        , fromIntro(recvIntro)
    {}

    static void
    Work(std::shared_ptr<AsyncFrameDecrypt> self)
    {
      auto crypto = CryptoManager::instance();
      SharedSecret K;
      SharedSecret shared_key;
      // copy
      ProtocolFrameMessage frame(self->frame);
      if (!crypto->pqe_decrypt(
              self->frame.cipher, K, pq_keypair_to_secret(self->m_LocalIdentity.pq)))
      {
        LogError("pqke failed C=", self->frame.cipher);
        self->msg.reset();
        return;
      }
      // decrypt
      // auto buf = frame.enc.Buffer();
      uint8_t* buf = frame.enc.data();
      size_t sz = frame.enc.size();
      crypto->xchacha20(buf, sz, K, self->frame.nonce);

      auto bte = self->msg->bt_encode();

      if (bte.empty())
      {
        log::error(logcat, "Failed to decode inner protocol message");
        DumpBuffer(*buf);
        self->msg.reset();
        return;
      }

      // verify signature of outer message after we parsed the inner message
      if (!self->frame.Verify(self->msg->sender))
      {
        LogError(
            "intro frame has invalid signature Z=",
            self->frame.sig,
            " from ",
            self->msg->sender.Addr());
        Dump<MAX_PROTOCOL_MESSAGE_SIZE>(self->frame);
        Dump<MAX_PROTOCOL_MESSAGE_SIZE>(*self->msg);
        self->msg.reset();
        return;
      }

      if (self->handler->HasConvoTag(self->msg->tag))
      {
        LogError("dropping duplicate convo tag T=", self->msg->tag);
        // TODO: send convotag reset
        self->msg.reset();
        return;
      }

      // PKE (A, B, N)
      SharedSecret shared_secret;
      path_dh_func dh_server = util::memFn(&Crypto::dh_server, CryptoManager::instance());

      if (!self->m_LocalIdentity.KeyExchange(
              dh_server, shared_secret, self->msg->sender, self->frame.nonce))
      {
        LogError("x25519 key exchange failed");
        Dump<MAX_PROTOCOL_MESSAGE_SIZE>(self->frame);
        self->msg.reset();
        return;
      }
      std::array<uint8_t, 64> tmp;
      // K
      std::memcpy(tmp.begin(), K.begin(), K.size());
      // S = HS( K + PKE( A, B, N))
      std::memcpy(tmp.begin() + 32, shared_secret.begin(), shared_secret.size());

      crypto->shorthash(shared_key, tmp.data(), tmp.size());

      std::shared_ptr<ProtocolMessage> msg = std::move(self->msg);
      path::Path_ptr path = std::move(self->path);
      const PathID_t from = self->frame.path_id;
      msg->handler = self->handler;
      self->handler->AsyncProcessAuthMessage(
          msg,
          [path, msg, from, handler = self->handler, fromIntro = self->fromIntro, shared_key](
              AuthResult result) {
            if (result.code == AuthResultCode::eAuthAccepted)
            {
              if (handler->WantsOutboundSession(msg->sender.Addr()))
              {
                handler->PutSenderFor(msg->tag, msg->sender, false);
              }
              else
              {
                handler->PutSenderFor(msg->tag, msg->sender, true);
              }
              handler->PutReplyIntroFor(msg->tag, msg->introReply);
              handler->PutCachedSessionKeyFor(msg->tag, shared_key);
              handler->SendAuthResult(path, from, msg->tag, result);
              LogInfo("auth okay for T=", msg->tag, " from ", msg->sender.Addr());
              ProtocolMessage::ProcessAsync(path, from, msg);
            }
            else
            {
              LogWarn("auth not okay for T=", msg->tag, ": ", result.reason);
            }
            handler->Pump(time_now_ms());
          });
    }
  };

  ProtocolFrameMessage&
  ProtocolFrameMessage::operator=(const ProtocolFrameMessage& other)
  {
    cipher = other.cipher;
    enc = other.enc;
    path_id = other.path_id;
    nonce = other.nonce;
    sig = other.sig;
    convo_tag = other.convo_tag;
    flag = other.flag;
    sequence_number = other.sequence_number;
    version = other.version;
    return *this;
  }

  struct AsyncDecrypt
  {
    ServiceInfo si;
    SharedSecret shared;
    ProtocolFrameMessage frame;
  };

  bool
  ProtocolFrameMessage::AsyncDecryptAndVerify(
      EventLoop_ptr loop,
      path::Path_ptr recvPath,
      const Identity& localIdent,
      Endpoint* handler,
      std::function<void(std::shared_ptr<ProtocolMessage>)> hook) const
  {
    auto msg = std::make_shared<ProtocolMessage>();
    msg->handler = handler;
    if (convo_tag.IsZero())
    {
      // we need to dh
      auto dh = std::make_shared<AsyncFrameDecrypt>(
          loop, localIdent, handler, msg, *this, recvPath->intro);
      dh->path = recvPath;
      handler->router()->queue_work([dh = std::move(dh)] { return AsyncFrameDecrypt::Work(dh); });
      return true;
    }

    auto v = std::make_shared<AsyncDecrypt>();

    if (!handler->GetCachedSessionKeyFor(convo_tag, v->shared))
    {
      LogError("No cached session for T=", convo_tag);
      return false;
    }
    if (v->shared.IsZero())
    {
      LogError("bad cached session key for T=", convo_tag);
      return false;
    }

    if (!handler->GetSenderFor(convo_tag, v->si))
    {
      LogError("No sender for T=", convo_tag);
      return false;
    }
    if (v->si.Addr().IsZero())
    {
      LogError("Bad sender for T=", convo_tag);
      return false;
    }

    v->frame = *this;
    auto callback = [loop, hook](std::shared_ptr<ProtocolMessage> msg) {
      if (hook)
      {
        loop->call([msg, hook]() { hook(msg); });
      }
    };
    handler->router()->queue_work(
        [v, msg = std::move(msg), recvPath = std::move(recvPath), callback, handler]() {
          auto resetTag =
              [handler, tag = v->frame.convo_tag, from = v->frame.path_id, path = recvPath]() {
                handler->ResetConvoTag(tag, path, from);
              };

          if (not v->frame.Verify(v->si))
          {
            LogError("Signature failure from ", v->si.Addr());
            handler->Loop()->call_soon(resetTag);
            return;
          }
          if (not v->frame.DecryptPayloadInto(v->shared, *msg))
          {
            LogError("failed to decrypt message from ", v->si.Addr());
            handler->Loop()->call_soon(resetTag);
            return;
          }
          callback(msg);
          RecvDataEvent ev;
          ev.fromPath = std::move(recvPath);
          ev.pathid = v->frame.path_id;
          auto* handler = msg->handler;
          ev.msg = std::move(msg);
          handler->QueueRecvData(std::move(ev));
        });
    return true;
  }

  bool
  ProtocolFrameMessage::operator==(const ProtocolFrameMessage& other) const
  {
    return cipher == other.cipher && enc == other.enc && nonce == other.nonce && sig == other.sig
        && convo_tag == other.convo_tag && sequence_number == other.sequence_number
        && version == other.version;
  }

  bool
  ProtocolFrameMessage::Verify(const ServiceInfo& svc) const
  {
    ProtocolFrameMessage copy(*this);
    copy.sig.Zero();

    auto bte = copy.bt_encode();
    return svc.verify(reinterpret_cast<uint8_t*>(bte.data()), bte.size(), sig);
  }

  bool
  ProtocolFrameMessage::handle_message(
      routing::AbstractRoutingMessageHandler* h, Router* /*r*/) const
  {
    return h->HandleHiddenServiceFrame(*this);
  }

}  // namespace llarp::service

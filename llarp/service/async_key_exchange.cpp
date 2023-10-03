#include "async_key_exchange.hpp"
#include "endpoint.hpp"

#include <llarp/crypto/crypto.hpp>
#include <llarp/crypto/types.hpp>
#include <llarp/util/meta/memfn.hpp>
#include <utility>

namespace llarp::service
{
  AsyncKeyExchange::AsyncKeyExchange(
      EventLoop_ptr l,
      ServiceInfo r,
      const Identity& localident,
      const PQPubKey& introsetPubKey,
      const Introduction& remote,
      Endpoint* h,
      const ConvoTag& t,
      ProtocolType proto)
      : loop(std::move(l))
      , m_remote(std::move(r))
      , m_LocalIdentity(localident)
      , introPubKey(introsetPubKey)
      , remoteIntro(remote)
      , handler(h)
      , tag(t)
  {
    msg.proto = proto;
  }

  void
  AsyncKeyExchange::Result(
      std::shared_ptr<AsyncKeyExchange> self, std::shared_ptr<ProtocolFrameMessage> frame)
  {
    // put values
    self->handler->PutSenderFor(self->msg.tag, self->m_remote, false);
    self->handler->PutCachedSessionKeyFor(self->msg.tag, self->sharedKey);
    self->handler->PutIntroFor(self->msg.tag, self->remoteIntro);
    self->handler->PutReplyIntroFor(self->msg.tag, self->msg.introReply);
    self->hook(frame);
  }

  void
  AsyncKeyExchange::Encrypt(
      std::shared_ptr<AsyncKeyExchange> self, std::shared_ptr<ProtocolFrameMessage> frame)
  {
    // derive ntru session key component
    SharedSecret secret;
    auto crypto = CryptoManager::instance();
    crypto->pqe_encrypt(frame->cipher, secret, self->introPubKey);
    // randomize Nonce
    frame->nonce.Randomize();
    // compure post handshake session key
    // PKE (A, B, N)
    SharedSecret sharedSecret;
    path_dh_func dh_client = util::memFn(&Crypto::dh_client, crypto);
    if (!self->m_LocalIdentity.KeyExchange(dh_client, sharedSecret, self->m_remote, frame->nonce))
    {
      LogError("failed to derive x25519 shared key component");
    }

    auto buf = secret.bt_encode() + sharedSecret.bt_encode();
    // H (K + PKE(A, B, N))
    crypto->shorthash(self->sharedKey, reinterpret_cast<uint8_t*>(buf.data()), buf.size());

    // set tag
    self->msg.tag = self->tag;
    // set sender
    self->msg.sender = self->m_LocalIdentity.pub;
    // set version
    self->msg.version = llarp::constants::proto_version;
    // encrypt and sign
    if (frame->EncryptAndSign(self->msg, secret, self->m_LocalIdentity))
      self->loop->call([self, frame] { AsyncKeyExchange::Result(self, frame); });
    else
    {
      LogError("failed to encrypt and sign");
    }
  }
}  // namespace llarp::service

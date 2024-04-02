#pragma once

#include "common.hpp"

#include <llarp/link/tunnel.hpp>
#include <llarp/router/router.hpp>
#include <llarp/vpn/egres_packet_router.hpp>

namespace llarp::handlers
{
    inline const auto EMBEDDED = "embedded"s;

    struct EmbeddedEndpoint final : public dns::Resolver_Base,
                                    public BaseHandler,
                                    public std::enable_shared_from_this<EmbeddedEndpoint>
    {
        EmbeddedEndpoint(Router& r);

        vpn::NetworkInterface* get_vpn_interface() override
        {
            return nullptr;
        }

        int rank() const override
        {
            return 0;
        }

        std::string name() const override
        {
            return EMBEDDED;
        }

        std::string_view resolver_name() const override
        {
            return LOKI_RESOLVER;
        }

        bool maybe_hook_dns(
            std::shared_ptr<dns::PacketSource_Base> /* source */,
            const dns::Message& /* query */,
            const oxen::quic::Address& /* to */,
            const oxen::quic::Address& /* from */) override
        {
            return false;
        }

        bool setup_networking() override
        {
            return true;
        }

        // TODO: this
        bool configure(const NetworkConfig& conf, const DnsConfig& dnsConf) override
        {
            (void)conf;
            (void)dnsConf;
            return true;
        }

        bool handle_inbound_packet(
            const service::SessionTag tag, const llarp_buffer_t& buf, service::ProtocolType t, uint64_t) override;

        std::string get_if_name() const override
        {
            return "";
        }

        bool supports_ipv6() const override
        {
            return false;
        }

        vpn::EgresPacketRouter* egres_packet_router() override
        {
            return _packet_router.get();
        }

      private:
        std::unique_ptr<vpn::EgresPacketRouter> _packet_router;
    };
}  // namespace llarp::handlers
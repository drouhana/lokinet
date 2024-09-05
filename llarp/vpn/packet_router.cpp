#include "packet_router.hpp"

#include <llarp/net/traffic_policy.hpp>

namespace llarp::vpn
{
    constexpr uint8_t udp_proto = 0x11;

    struct UDPPacketHandler : public Layer4Handler
    {
        ip_pkt_hook _base_handler;
        // Ports held in HOST order
        std::unordered_map<uint16_t, ip_pkt_hook> _port_mapped_handlers;

        explicit UDPPacketHandler(ip_pkt_hook baseHandler) : _base_handler{std::move(baseHandler)}
        {
            // map the base handler to port 0, so we dont need to check if dest_port() returns 0
            _port_mapped_handlers[0] = _base_handler;
        }

        void add_sub_handler(uint16_t localport, ip_pkt_hook handler) override
        {
            _port_mapped_handlers.emplace(localport, std::move(handler));
        }

        void handle_ip_packet(IPPacket pkt) override
        {
            if (auto itr = _port_mapped_handlers.find(pkt.dest_port()); itr != _port_mapped_handlers.end())
                itr->second(std::move(pkt));
            else
                _base_handler(std::move(pkt));
        }
    };

    struct GenericLayer4Handler : public Layer4Handler
    {
        ip_pkt_hook _base_handler;

        explicit GenericLayer4Handler(ip_pkt_hook baseHandler) : _base_handler{std::move(baseHandler)} {}

        void handle_ip_packet(IPPacket pkt) override { return _base_handler(std::move(pkt)); }
    };

    PacketRouter::PacketRouter(ip_pkt_hook baseHandler) : _handler{std::move(baseHandler)} {}

    void PacketRouter::handle_ip_packet(IPPacket pkt)
    {
        if (auto it = _ip_proto_handler.find(*pkt.protocol()); it != _ip_proto_handler.end())
            it->second->handle_ip_packet(std::move(pkt));
        else
            _handler(std::move(pkt));
    }

    void PacketRouter::add_udp_handler(uint16_t localport, ip_pkt_hook func)
    {
        if (_ip_proto_handler.find(IPProto::UDP) == _ip_proto_handler.end())
        {
            _ip_proto_handler.emplace(udp_proto, std::make_unique<UDPPacketHandler>(_handler));
        }
        _ip_proto_handler[udp_proto]->add_sub_handler(localport, std::move(func));
    }

    void PacketRouter::add_ip_proto_handler(uint8_t proto, ip_pkt_hook func)
    {
        _ip_proto_handler[proto] = std::make_unique<GenericLayer4Handler>(std::move(func));
    }

}  // namespace llarp::vpn

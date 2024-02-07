#include "ip_range.hpp"

#include "utils.hpp"

namespace llarp
{
    ip_net IPRange::init_ip()
    {
        if (_is_ipv4)
            return ipv4_net{ipv4{oxenc::big_to_host<uint32_t>(_addr.in4().sin_addr.s_addr)}, _mask};
        return ipv6_net{ipv6{_addr.in6().sin6_addr.s6_addr}, _mask};
    }

    std::optional<IPRange> IPRange::from_string(std::string arg)
    {
        std::optional<IPRange> range = std::nullopt;
        oxen::quic::Address _addr;
        uint8_t _mask;

        if (auto pos = arg.find_first_of('/'); pos != std::string::npos)
        {
            try
            {
                auto [host, p] = parse_addr(arg.substr(0, pos), 0);
                assert(p == 0);
                _addr = oxen::quic::Address{host, p};

                if (parse_int(arg.substr(pos), _mask))
                    range = IPRange{std::move(_addr), std::move(_mask)};
                else
                    log::warning(logcat, "Failed to construct IPRange from string input:{}", arg);
            }
            catch (const std::exception& e)
            {
                log::error(logcat, "Exception caught parsing IPRange:{}", e.what());
            }
        }

        return range;
    }

    std::optional<ipv4_net> IPRange::get_ipv4_net() const
    {
        std::optional<ipv4_net> ret = std::nullopt;

        if (auto* maybe = std::get_if<ipv4_net>(&_ip))
            ret = *maybe;

        return ret;
    }

    std::optional<ipv6_net> IPRange::get_ipv6_net() const
    {
        std::optional<ipv6_net> ret = std::nullopt;

        if (auto* maybe = std::get_if<ipv6_net>(&_ip))
            ret = *maybe;

        return ret;
    }

    std::optional<ipv4> IPRange::get_ipv4() const
    {
        std::optional<ipv4> ret = std::nullopt;

        if (auto ipv4 = get_ipv4_net())
            ret = ipv4->base;

        return ret;
    }

    std::optional<ipv6> IPRange::get_ipv6() const
    {
        std::optional<ipv6> ret = std::nullopt;

        if (auto ipv6 = get_ipv6_net())
            ret = ipv6->base;

        return ret;
    }

    bool IPRange::contains(const IPRange& other) const
    {
        if (is_ipv4() ^ other.is_ipv4())
            return false;

        if (is_ipv4())
            return get_ipv4_net()->contains(*other.get_ipv4());

        return get_ipv6_net()->contains(*other.get_ipv6());
    }

    std::optional<IPRange> IPRange::find_private_range(const std::list<IPRange>& excluding)
    {
        auto filter = [&excluding](const IPRange& range) -> bool {
            for (const auto& e : excluding)
                if (e == range)
                    return false;
            return true;
        };

        (void)filter;

        return std::nullopt;
    }
}  //  namespace llarp
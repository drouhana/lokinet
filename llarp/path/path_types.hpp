#pragma once

#include <llarp/constants/path.hpp>
#include <llarp/contact/relay_contact.hpp>
#include <llarp/crypto/constants.hpp>
#include <llarp/crypto/types.hpp>
#include <llarp/util/aligned.hpp>

namespace llarp
{
    struct HopID final : public AlignedBuffer<PATHIDSIZE>
    {
        using AlignedBuffer<PATHIDSIZE>::AlignedBuffer;

        static HopID make_random()
        {
            HopID h;
            h.Randomize();
            return h;
        }
    };

    namespace path
    {
        /// configuration for a single hop when building a path
        struct PathHopConfig
        {
            /// path id
            HopID txID, rxID;

            // TESTNET: do not need the full RC, just the RouterID
            // router contact of router
            RemoteRC rc;

            // TESTNET: do not need to hold on to this nonce after path-building
            /// nonce for key exchange
            SymmNonce nonce;

            /// shared secret at this hop
            SharedSecret shared;
            /// hash of shared secret used for nonce mutation
            SymmNonce nonceXOR;
            /// next hop's router id
            RouterID upstream;
            // lifetime
            std::chrono::milliseconds lifetime{DEFAULT_LIFETIME};

            nlohmann::json ExtractStatus() const;

            bool operator<(const PathHopConfig& other) const
            {
                return std::tie(txID, rxID, rc, upstream, lifetime)
                    < std::tie(other.txID, other.rxID, other.rc, other.upstream, other.lifetime);
            }

            bool operator==(const PathHopConfig& other) const
            {
                return std::tie(txID, rxID, rc, upstream, lifetime)
                    == std::tie(other.txID, other.rxID, other.rc, other.upstream, other.lifetime);
            }

            bool operator!=(const PathHopConfig& other) const { return not(*this == other); }
        };

        // milliseconds waiting between builds on a path per router
        static constexpr auto MIN_PATH_BUILD_INTERVAL{500ms};
        static constexpr auto PATH_BUILD_RATE{100ms};
    }  // namespace path
}  // namespace llarp

namespace std
{
    template <>
    struct hash<llarp::HopID> : hash<llarp::AlignedBuffer<llarp::HopID::SIZE>>
    {};
}  // namespace std

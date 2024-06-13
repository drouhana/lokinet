#pragma once

#include <llarp/router_contact.hpp>
#include <llarp/router_id.hpp>

#include <oxen/quic.hpp>

namespace llarp::link
{
    struct Connection
    {
        std::shared_ptr<oxen::quic::connection_interface> conn;
        std::shared_ptr<oxen::quic::BTRequestStream> control_stream;

        bool remote_is_relay{true};

        bool is_inbound() const { return conn->is_inbound(); }

        Connection(
            std::shared_ptr<oxen::quic::connection_interface> c,
            std::shared_ptr<oxen::quic::BTRequestStream> s,
            bool is_relay = true);
    };
}  // namespace llarp::link

/**
    TODO:
        - add a boolean in this connection object
        - do not continue to try to send things to the bootstarp until the connection
            is actually established!

 */

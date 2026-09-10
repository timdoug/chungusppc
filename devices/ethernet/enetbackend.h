/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team
          (See CREDITS.MD for more details)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/** @file Host side of an emulated Ethernet controller.

    A backend carries complete Ethernet frames between a MAC emulation and
    whatever stands in for the wire: nothing at all, the guest itself, or a
    real host network. MAC emulations talk only to this interface so they
    don't care which one is in use.
 */

#ifndef ENET_BACKEND_H
#define ENET_BACKEND_H

#include <cinttypes>
#include <functional>
#include <memory>
#include <string>

/** Longest frame a backend will carry: 1500 byte payload plus the header,
    the FCS and room for an 802.1Q tag. */
constexpr int ENET_MAX_FRAME_SIZE = 1522;

class EthernetBackend {
public:
    typedef std::function<void(const uint8_t *frame, int len)> RcvCallback;

    EthernetBackend() = default;
    virtual ~EthernetBackend() = default;

    /** Bring the backend up for a MAC with the given hardware address. */
    virtual bool start(const uint8_t mac_addr[6]) = 0;
    virtual void stop() = 0;

    /** Hand a frame from the guest to the host side. */
    virtual void send_frame(const uint8_t *frame, int len) = 0;

    /** Let the backend deliver anything it has. Called periodically because
        no backend is allowed to push frames from another thread. */
    virtual void poll() {}

    /** Register where received frames should go. */
    void set_rcv_callback(RcvCallback cb) { this->rcv_cb = cb; }

protected:
    /** Backends call this to hand a frame to the MAC. */
    void deliver_frame(const uint8_t *frame, int len) {
        if (this->rcv_cb)
            this->rcv_cb(frame, len);
    }

private:
    RcvCallback rcv_cb = nullptr;
};

/** Construct a backend by name, or nullptr if the name isn't known or the
    backend it asks for wasn't compiled in. */
std::unique_ptr<EthernetBackend> create_enet_backend(const std::string &name);

/** Built only when libslirp was found; returns nullptr otherwise. */
std::unique_ptr<EthernetBackend> create_slirp_backend();

#endif // ENET_BACKEND_H

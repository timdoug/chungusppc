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

/** @file Ethernet backends that need no host networking. */

#include <devices/ethernet/enetbackend.h>
#include <loguru.hpp>

#include <cstring>
#include <deque>
#include <vector>

/** Backend that drops everything. The guest sees a working controller with
    nothing plugged into it, which is what an unconfigured machine expects. */
class NullBackend : public EthernetBackend {
public:
    bool start(const uint8_t mac_addr[6]) override {
        LOG_F(INFO, "Ethernet: null backend, MAC %02x:%02x:%02x:%02x:%02x:%02x",
              mac_addr[0], mac_addr[1], mac_addr[2],
              mac_addr[3], mac_addr[4], mac_addr[5]);
        return true;
    }

    void stop() override {}

    void send_frame(const uint8_t *frame, int len) override {
        LOG_F(9, "Ethernet: dropping %d byte frame", len);
    }
};

/** Backend that returns every transmitted frame to the guest. Nothing on a
    real network behaves this way, but it exercises the receive path, the
    address filter and the receive interrupt without a host connection. */
class LoopbackBackend : public EthernetBackend {
public:
    bool start(const uint8_t mac_addr[6]) override {
        LOG_F(INFO, "Ethernet: loopback backend, MAC %02x:%02x:%02x:%02x:%02x:%02x",
              mac_addr[0], mac_addr[1], mac_addr[2],
              mac_addr[3], mac_addr[4], mac_addr[5]);
        return true;
    }

    void stop() override { this->queue.clear(); }

    void send_frame(const uint8_t *frame, int len) override {
        if (len <= 0 || len > ENET_MAX_FRAME_SIZE)
            return;
        // Deliver from poll() instead of here: send_frame() runs inside the
        // guest's transmit DMA, and reentering the receive path from there
        // would drive two transfers on one channel at once.
        this->queue.emplace_back(frame, frame + len);
    }

    void poll() override {
        while (!this->queue.empty()) {
            std::vector<uint8_t> frame = std::move(this->queue.front());
            this->queue.pop_front();
            this->deliver_frame(frame.data(), (int)frame.size());
        }
    }

private:
    std::deque<std::vector<uint8_t>> queue;
};

std::unique_ptr<EthernetBackend> create_enet_backend(const std::string &name) {
    if (name.empty() || name == "null")
        return std::unique_ptr<EthernetBackend>(new NullBackend);

    if (name == "loopback")
        return std::unique_ptr<EthernetBackend>(new LoopbackBackend);

    if (name == "slirp") {
        std::unique_ptr<EthernetBackend> be = create_slirp_backend();
        if (!be)
            LOG_F(ERROR, "Ethernet: this build has no slirp support");
        return be;
    }

    LOG_F(ERROR, "Ethernet: unknown backend \"%s\"", name.c_str());
    return nullptr;
}

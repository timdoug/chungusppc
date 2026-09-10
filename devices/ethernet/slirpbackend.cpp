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

/** @file User mode networking backend built on libslirp.

    Gives the guest a private network with NAT to whatever the host can reach,
    plus the DHCP and DNS servers libslirp provides, without needing any
    privileges. The guest ends up on 10.0.2.0/24 with the host reachable at
    10.0.2.2, matching what other emulators using libslirp present.
 */

#include <devices/ethernet/enetbackend.h>

#ifdef DPPC_ENABLE_SLIRP

#include <loguru.hpp>
#include <libslirp.h>

#include <chrono>
#include <deque>
#include <poll.h>
#include <vector>

/** Guest side addressing. */
constexpr uint32_t SLIRP_NETWORK    = 0x0A000200; // 10.0.2.0
constexpr uint32_t SLIRP_NETMASK    = 0xFFFFFF00; // /24
constexpr uint32_t SLIRP_HOST       = 0x0A000202; // 10.0.2.2
constexpr uint32_t SLIRP_DHCP_START = 0x0A00020F; // 10.0.2.15
constexpr uint32_t SLIRP_NAMESERVER = 0x0A000203; // 10.0.2.3

/** Frames held for the guest before they start being dropped. */
constexpr size_t SLIRP_MAX_QUEUE = 64;

class SlirpBackend : public EthernetBackend {
public:
    ~SlirpBackend() override { this->stop(); }

    bool start(const uint8_t mac_addr[6]) override;
    void stop() override;
    void send_frame(const uint8_t *frame, int len) override;
    void poll() override;

    // Called from the libslirp callbacks below. libslirp answers some frames
    // from inside slirp_input(), which runs under the guest's transmit DMA, so
    // handing one to the MAC here would drive the receive path while the
    // transmit path is still on the stack. Queue it for the next poll instead.
    void frame_from_host(const void *buf, size_t len) {
        if (len == 0 || len > ENET_MAX_FRAME_SIZE)
            return;
        if (this->rcv_queue.size() >= SLIRP_MAX_QUEUE)
            return; // guest isn't draining; drop like a real overrun
        const uint8_t *p = (const uint8_t *)buf;
        this->rcv_queue.emplace_back(p, p + len);
    }
    int add_poll(slirp_os_socket fd, int events);
    int get_revents(int idx);

private:
    Slirp*                            slirp = nullptr;
    std::vector<pollfd>               pollfds;
    std::deque<std::vector<uint8_t>>  rcv_queue;
};

static in_addr make_addr(uint32_t host_order) {
    in_addr addr;
    addr.s_addr = htonl(host_order);
    return addr;
}

// ------------------------- libslirp callbacks -------------------------

static ssize_t cb_send_packet(const void *buf, size_t len, void *opaque) {
    ((SlirpBackend *)opaque)->frame_from_host(buf, len);
    return (ssize_t)len;
}

static void cb_guest_error(const char *msg, void *opaque) {
    LOG_F(WARNING, "slirp: %s", msg);
}

static int64_t cb_clock_get_ns(void *opaque) {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

/** libslirp's timers. Its own poll() call runs them, so they only need to
    remember when they are due. */
struct SlirpTimer {
    SlirpTimerCb cb;
    void*        cb_opaque;
    int64_t      expire_ns;
};

static void *cb_timer_new(SlirpTimerCb cb, void *cb_opaque, void *opaque) {
    SlirpTimer *timer = new SlirpTimer{cb, cb_opaque, 0};
    return timer;
}

static void cb_timer_free(void *timer, void *opaque) {
    delete (SlirpTimer *)timer;
}

static void cb_timer_mod(void *timer, int64_t expire_time, void *opaque) {
    ((SlirpTimer *)timer)->expire_ns = expire_time * 1000000; // ms -> ns
}

static void cb_notify(void *opaque) {
    // Nothing to wake: poll() is driven from the MAC's timer.
}

// libslirp calls these when it opens or closes a socket. Nothing to record
// here because slirp_pollfds_fill() re-enumerates every socket on each poll,
// but they must exist: libslirp calls them without checking for NULL, so
// leaving them unset crashes the first time it forwards a packet off box.
static void cb_register_poll_fd(int fd, void *opaque) {}
static void cb_unregister_poll_fd(int fd, void *opaque) {}
static void cb_register_poll_socket(slirp_os_socket fd, void *opaque) {}
static void cb_unregister_poll_socket(slirp_os_socket fd, void *opaque) {}

static const SlirpCb slirp_callbacks = {
    .send_packet        = cb_send_packet,
    .guest_error        = cb_guest_error,
    .clock_get_ns       = cb_clock_get_ns,
    .timer_new          = cb_timer_new,
    .timer_free         = cb_timer_free,
    .timer_mod          = cb_timer_mod,
    .register_poll_fd   = cb_register_poll_fd,
    .unregister_poll_fd = cb_unregister_poll_fd,
    .notify             = cb_notify,
    .init_completed     = nullptr,
    .timer_new_opaque   = nullptr,
    .register_poll_socket   = cb_register_poll_socket,
    .unregister_poll_socket = cb_unregister_poll_socket,
};

static int cb_add_poll(slirp_os_socket fd, int events, void *opaque) {
    return ((SlirpBackend *)opaque)->add_poll(fd, events);
}

static int cb_get_revents(int idx, void *opaque) {
    return ((SlirpBackend *)opaque)->get_revents(idx);
}

// ------------------------------ backend ------------------------------

bool SlirpBackend::start(const uint8_t mac_addr[6]) {
    SlirpConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.version     = 1; // only version 1 fields are used here
    cfg.restricted  = 0;
    cfg.in_enabled  = true;
    cfg.vnetwork    = make_addr(SLIRP_NETWORK);
    cfg.vnetmask    = make_addr(SLIRP_NETMASK);
    cfg.vhost       = make_addr(SLIRP_HOST);
    cfg.vdhcp_start = make_addr(SLIRP_DHCP_START);
    cfg.vnameserver = make_addr(SLIRP_NAMESERVER);

    this->slirp = slirp_new(&cfg, &slirp_callbacks, this);
    if (this->slirp == nullptr) {
        LOG_F(ERROR, "Ethernet: slirp backend failed to start");
        return false;
    }

    LOG_F(INFO, "Ethernet: slirp backend, guest 10.0.2.0/24, host 10.0.2.2, "
                "DNS 10.0.2.3, MAC %02x:%02x:%02x:%02x:%02x:%02x",
          mac_addr[0], mac_addr[1], mac_addr[2],
          mac_addr[3], mac_addr[4], mac_addr[5]);
    return true;
}

void SlirpBackend::stop() {
    this->rcv_queue.clear();
    if (this->slirp) {
        slirp_cleanup(this->slirp);
        this->slirp = nullptr;
    }
}

void SlirpBackend::send_frame(const uint8_t *frame, int len) {
    if (this->slirp && len > 0)
        slirp_input(this->slirp, frame, len);
}

int SlirpBackend::add_poll(slirp_os_socket fd, int events) {
    short pev = 0;
    if (events & SLIRP_POLL_IN)  pev |= POLLIN;
    if (events & SLIRP_POLL_OUT) pev |= POLLOUT;
    if (events & SLIRP_POLL_PRI) pev |= POLLPRI;

    this->pollfds.push_back(pollfd{(int)fd, pev, 0});
    return (int)this->pollfds.size() - 1;
}

int SlirpBackend::get_revents(int idx) {
    short rev = this->pollfds[idx].revents;
    int   ev  = 0;
    if (rev & POLLIN)  ev |= SLIRP_POLL_IN;
    if (rev & POLLOUT) ev |= SLIRP_POLL_OUT;
    if (rev & POLLPRI) ev |= SLIRP_POLL_PRI;
    if (rev & POLLERR) ev |= SLIRP_POLL_ERR;
    if (rev & POLLHUP) ev |= SLIRP_POLL_HUP;
    return ev;
}

void SlirpBackend::poll() {
    if (this->slirp == nullptr)
        return;

    this->pollfds.clear();

    // Use the socket flavour: the older slirp_pollfds_fill() is deprecated and
    // doesn't report every socket, which leaves replies sitting unnoticed until
    // some other event happens to run the state machine.
    uint32_t timeout = 0; // never block: the guest is waiting on us
    slirp_pollfds_fill_socket(this->slirp, &timeout, cb_add_poll, this);

    int res = this->pollfds.empty()
            ? 0 : ::poll(this->pollfds.data(), (nfds_t)this->pollfds.size(), 0);

    slirp_pollfds_poll(this->slirp, res < 0, cb_get_revents, this);

    // Now that we're outside slirp_input(), it is safe to drive the receive path.
    while (!this->rcv_queue.empty()) {
        std::vector<uint8_t> frame = std::move(this->rcv_queue.front());
        this->rcv_queue.pop_front();
        this->deliver_frame(frame.data(), (int)frame.size());
    }
}

std::unique_ptr<EthernetBackend> create_slirp_backend() {
    return std::unique_ptr<EthernetBackend>(new SlirpBackend);
}

#else // !DPPC_ENABLE_SLIRP

std::unique_ptr<EthernetBackend> create_slirp_backend() {
    return nullptr;
}

#endif // DPPC_ENABLE_SLIRP

/*
DingusPPC - The Experimental PowerPC Macintosh emulator
Copyright (C) 2018-26 The DingusPPC Development Team
          (See CREDITS.MD for more details)

(You may also contact divingkxt or powermax2286 on Discord)

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

/** Character I/O backend implementations. */

#include <devices/serial/chario.h>
#include <loguru.hpp>

#include <cinttypes>
#include <cstring>
#include <memory>

bool CharIoNull::rcv_char_available()
{
    return false;
}

bool CharIoNull::rcv_char_available_now()
{
    return false;
}

int CharIoNull::xmit_char(uint8_t c)
{
    return 0;
}

int CharIoNull::rcv_char(uint8_t *c)
{
    *c = 0xFF;
    return 0;
}

//======================== STDIO character I/O backend ========================
#ifdef _WIN32

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <windows.h>

HANDLE hInput  = GetStdHandle(STD_INPUT_HANDLE);
HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
DWORD old_in_mode, old_out_mode;
int old_stdin_trans_mode;

void CharIoStdin::mysig_handler(int signum) {
    SetStdHandle(signum, hInput);
    SetStdHandle(signum, hOutput);
}

int CharIoStdin::rcv_enable() {
    if (this->stdio_inited)
        return 0;

    GetConsoleMode(hInput, &old_in_mode);
    GetConsoleMode(hOutput, &old_out_mode);

    DWORD new_in_mode = old_in_mode;
    new_in_mode &= ~ENABLE_ECHO_INPUT;
    new_in_mode &= ~ENABLE_LINE_INPUT;
    new_in_mode &= ~ENABLE_PROCESSED_INPUT;

    new_in_mode |= ENABLE_EXTENDED_FLAGS;
    new_in_mode |= ENABLE_INSERT_MODE;
    new_in_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;

    SetConsoleMode(hInput, new_in_mode);

    SetConsoleMode(hOutput, old_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    // disable automatic CRLF translation
    old_stdin_trans_mode = _setmode(_fileno(stdin), _O_BINARY);

    this->stdio_inited = true;

    LOG_F(INFO, "Winterm: receiver initialized");

    return 0;
}

void CharIoStdin::rcv_disable() {
    if (!this->stdio_inited)
        return;

    // restore original console mode
    SetConsoleMode(hInput, old_in_mode);
    SetConsoleMode(hOutput, old_out_mode);

    // restore original translation mode
    _setmode(_fileno(stdin), old_stdin_trans_mode);

    this->stdio_inited = false;

    LOG_F(INFO, "Winterm: receiver disabled");
}

bool CharIoStdin::rcv_char_available()
{
    return this->rcv_char_available_now();
}

bool CharIoStdin::rcv_char_available_now() {
    DWORD events;
    INPUT_RECORD buffer;

    PeekConsoleInput(hInput, &buffer, 1, &events);
    return !!(events > 0);
}

int CharIoStdin::xmit_char(uint8_t c) {
    _write(_fileno(stdout), &c, 1);
    return 0;
}

int CharIoStdin::rcv_char(uint8_t* c) {
    _read(_fileno(stdin), c, 1);
    return 0;
}

#else // non-Windows OS (Linux, mac OS etc.)

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>

static struct sigaction    old_act_sigint, new_act_sigint;
static struct sigaction    old_act_sigterm, new_act_sigterm;
static struct termios      orig_termios;

void CharIoStdin::mysig_handler(int signum)
{
    // restore original terminal state
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);

    // restore original signal handler for SIGINT
    signal(SIGINT, old_act_sigint.sa_handler);
    signal(SIGTERM, old_act_sigterm.sa_handler);

    LOG_F(INFO, "Old terminal state restored, SIG#=%d", signum);

    // re-post signal
    raise(signum);
}

int CharIoStdin::rcv_enable()
{
    if (this->stdio_inited)
        return 0;

    // save original terminal state
    tcgetattr(STDIN_FILENO, &orig_termios);

    struct termios new_termios = orig_termios;

    new_termios.c_cflag &= ~(CSIZE | PARENB);
    new_termios.c_cflag |= CS8;
    new_termios.c_lflag &= ~(ECHO | ICANON | ISIG);
    new_termios.c_iflag &= ~(ICRNL);

    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

    // save original signal handler for SIGINT
    // then redirect SIGINT to new handler
    memset(&new_act_sigint, 0, sizeof(new_act_sigint));
    new_act_sigint.sa_handler = mysig_handler;
    sigaction(SIGINT, &new_act_sigint, &old_act_sigint);

    // save original signal handler for SIGTERM
    // then redirect SIGTERM to new handler
    memset(&new_act_sigterm, 0, sizeof(new_act_sigterm));
    new_act_sigterm.sa_handler = mysig_handler;
    sigaction(SIGTERM, &new_act_sigterm, &old_act_sigterm);

    this->stdio_inited = true;

    return 0;
}

void CharIoStdin::rcv_disable()
{
    if (!this->stdio_inited)
        return;

    // restore original terminal state
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);

    // restore original signal handler for SIGINT
    signal(SIGINT, old_act_sigint.sa_handler);

    // restore original signal handler for SIGTERM
    signal(SIGTERM, old_act_sigterm.sa_handler);

    this->stdio_inited = false;
}

bool CharIoStdin::rcv_char_available()
{
    if (consecutivechars >= 15) {
        consecutivechars++;
        if (consecutivechars >= 400)
            consecutivechars = 0;
        return 0;
    }
    return this->rcv_char_available_now();
}

bool CharIoStdin::rcv_char_available_now()
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    int sel_rv = select(1, &readfds, NULL, NULL, &timeout);
    if (sel_rv > 0)
        consecutivechars++;
    else
        consecutivechars = 0;
    return sel_rv > 0;
}

int CharIoStdin::xmit_char(uint8_t c)
{
    write(STDOUT_FILENO, &c, 1);
    return 0;
}

int CharIoStdin::rcv_char(uint8_t *c)
{
    read(STDIN_FILENO, c, 1);
    return 0;
}

#endif

//======================== SOCKET character I/O backend ========================
#ifdef _WIN32

#else // non-Windows OS (Linux, mac OS etc.)

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/errno.h>
#include <fcntl.h>


CharIoSocket::CharIoSocket(std::string path) : path(std::move(path))
{
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (this->path.size() >= sizeof(address.sun_path)) return;
    std::strcpy(address.sun_path, this->path.c_str());
    unlink(this->path.c_str());
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) return;
    if (bind(sockfd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        listen(sockfd, 1) < 0 || fcntl(sockfd, F_SETFL, O_NONBLOCK) < 0) {
        LOG_F(ERROR, "Serial socket %s: %s", this->path.c_str(), strerror(errno));
        close(sockfd);
        sockfd = -1;
        return;
    }
    LOG_F(INFO, "Serial socket listening at %s", this->path.c_str());
}

CharIoSocket::~CharIoSocket()
{
    disconnect();
    if (sockfd >= 0) {
        close(sockfd);
        unlink(path.c_str());
    }
}

void CharIoSocket::disconnect()
{
    if (acceptfd >= 0) close(acceptfd);
    acceptfd = -1;
    output.clear();
}

void CharIoSocket::poll_connection()
{
    if (acceptfd < 0 && sockfd >= 0) {
        acceptfd = accept(sockfd, nullptr, nullptr);
        if (acceptfd >= 0) {
            fcntl(acceptfd, F_SETFL, O_NONBLOCK);
#ifdef SO_NOSIGPIPE
            int enabled = 1;
            setsockopt(acceptfd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
        }
    }
    flush_output();
}

void CharIoSocket::flush_output()
{
    while (acceptfd >= 0 && !output.empty()) {
        uint8_t byte = output.front();
#ifdef MSG_NOSIGNAL
        int flags = MSG_NOSIGNAL;
#else
        int flags = 0;
#endif
        int sent = send(acceptfd, &byte, 1, flags);
        if (sent == 1) output.pop_front();
        else if (sent < 0 && errno == EINTR) continue;
        else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        else { disconnect(); break; }
    }
}

int CharIoSocket::rcv_enable() { socket_inited = true; return 0; }
void CharIoSocket::rcv_disable() { socket_inited = false; }
bool CharIoSocket::rcv_char_available() { return rcv_char_available_now(); }

bool CharIoSocket::rcv_char_available_now()
{
    poll_connection();
    if (acceptfd < 0) return false;
    uint8_t byte;
    int count = recv(acceptfd, &byte, 1, MSG_PEEK);
    if (count == 1) return socket_inited;
    if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
        disconnect();
    return false;
}

int CharIoSocket::xmit_char(uint8_t c)
{
    poll_connection();
    if (acceptfd >= 0) {
        output.push_back(c);
        flush_output();
    }
    return 0;
}

int CharIoSocket::rcv_char(uint8_t *c)
{
    *c = 0;
    if (!rcv_char_available_now()) return -1;
    return recv(acceptfd, c, 1, 0) == 1 ? 0 : -1;
}

#endif

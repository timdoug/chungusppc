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

/** Character I/O definitions. */

#ifndef CHAR_IO_H
#define CHAR_IO_H

#include <cinttypes>
#include <deque>
#include <string>

#ifdef _WIN32
#else
#include <termios.h>
#include <signal.h>
#endif

enum {
    CHARIO_BE_NULL  = 0, // NULL backend: swallows everything, receives nothing
    CHARIO_BE_STDIO = 1, // STDIO backend: uses STDIN for input and STDOUT for output
    CHARIO_BE_SOCKET = 2, // socket backend: uses a socket for input and output
};

/** Interface for character I/O backends. */
class CharIoBackEnd {
public:
    CharIoBackEnd() = default;
    virtual ~CharIoBackEnd() = default;

    virtual int rcv_enable() { return 0; }
    virtual void rcv_disable() {}
    virtual bool connected() const { return true; }
    virtual bool rcv_char_available() = 0;
    virtual bool rcv_char_available_now() = 0;
    virtual int xmit_char(uint8_t c) = 0;
    virtual int rcv_char(uint8_t *c) = 0;
};

/** Null character I/O backend. */
class CharIoNull : public CharIoBackEnd {
public:
    bool connected() const override { return false; }
    CharIoNull()  = default;
    ~CharIoNull() = default;

    bool rcv_char_available() override;
    bool rcv_char_available_now() override;
    int xmit_char(uint8_t c) override;
    int rcv_char(uint8_t *c) override;
};

/** Stdin character I/O backend. */
class CharIoStdin : public CharIoBackEnd  {
public:
    CharIoStdin() { this->stdio_inited = false; }
    ~CharIoStdin() = default;

    int rcv_enable() override;
    void rcv_disable() override;
    bool rcv_char_available() override;
    bool rcv_char_available_now() override;
    int xmit_char(uint8_t c) override;
    int rcv_char(uint8_t *c) override;

private:
    static void mysig_handler(int signum);
    bool    stdio_inited;
    int     consecutivechars = 0;
};

/** Socket character I/O backend. */
class CharIoSocket : public CharIoBackEnd  {
public:
    explicit CharIoSocket(std::string path = "chungussocket");
    ~CharIoSocket();
    bool connected() const override { return acceptfd >= 0; }

    int rcv_enable() override;
    void rcv_disable() override;
    bool rcv_char_available() override;
    bool rcv_char_available_now() override;
    int xmit_char(uint8_t c) override;
    int rcv_char(uint8_t *c) override;

private:
    bool    socket_inited = false;
    int     sockfd = -1;
    int     acceptfd = -1;
    std::string path;
    std::deque<uint8_t> output;
    void poll_connection();
    void flush_output();
    void disconnect();
};

#endif // CHAR_IO_H

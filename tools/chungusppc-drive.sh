#!/bin/sh
# Type into a running emulator and capture what the guest shows for it.
#
#     chungusppc-drive.sh 'text root' 'key RETURN'
#     chungusppc-drive.sh                          # just capture the screen
#
# Arguments are lines of the input script described in zdocs/users/manual.md.
# They are written to chungusppc-input.txt and handed over with SIGUSR2, then
# SIGUSR1 leaves the guest's screen in chungusppc-screen.bmp. Both files land in
# the emulator's working directory, so run this from there.

set -e

pid=$(pgrep -n -x chungusppc) || {
    echo "chungusppc-drive: no running chungusppc" >&2
    exit 1
}

if [ $# -gt 0 ]; then
    printf '%s\n' "$@" > chungusppc-input.txt
    kill -USR2 "$pid"
    # A key is up to four transitions and only one goes per event poll, so give
    # the queue time to drain before asking what the guest did with it. Mouse
    # moves are far longer than that, so allow the wait to be set outright.
    sleep "${CHUNGUSPPC_WAIT:-$(( 2 + $# ))}"
fi

kill -USR1 "$pid"
sleep 2

# BMP is what SDL writes. Convert it if the host has something that can, since
# most image viewers would rather have a PNG.
if command -v sips > /dev/null 2>&1; then
    sips -s format png chungusppc-screen.bmp --out chungusppc-screen.png > /dev/null
elif command -v convert > /dev/null 2>&1; then
    convert chungusppc-screen.bmp chungusppc-screen.png
fi

ls chungusppc-screen.png 2> /dev/null || ls chungusppc-screen.bmp

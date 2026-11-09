#!/bin/sh
# Gives the session test a private config directory, a session bus, and an X server.
set -eu

if [ -z "${XDG_CONFIG_HOME:-}" ]; then
  LCOS_TEST_HOME=$(mktemp -d)
  export XDG_CONFIG_HOME="$LCOS_TEST_HOME/config"
  export XDG_CACHE_HOME="$LCOS_TEST_HOME/cache"
  mkdir -p "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME"
fi
if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
  XDG_RUNTIME_DIR=$(mktemp -d)
  export XDG_RUNTIME_DIR
  chmod 700 "$XDG_RUNTIME_DIR"
fi
if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
  exec dbus-run-session -- "$0" "$@"
fi
# Always use a private X server. The ambient DISPLAY may be a real session.
if [ -z "${LCOS_SESSION_DISPLAY:-}" ]; then
  export LCOS_SESSION_DISPLAY=1
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
exec "$@"

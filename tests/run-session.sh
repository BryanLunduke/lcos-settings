#!/bin/sh
# Gives the session test a private config directory, a session bus, and an X server.
# Always isolates, even when the build is started from inside a desktop session.
set -eu

if [ -z "${LCOS_SESSION_ISOLATED:-}" ]; then
  export LCOS_SESSION_ISOLATED=1
  LCOS_TEST_HOME=$(mktemp -d)
  export XDG_CONFIG_HOME="$LCOS_TEST_HOME/config"
  export XDG_CACHE_HOME="$LCOS_TEST_HOME/cache"
  export XDG_RUNTIME_DIR="$LCOS_TEST_HOME/run"
  mkdir -p "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR"
  chmod 700 "$XDG_RUNTIME_DIR"
  unset DBUS_SESSION_BUS_ADDRESS
  exec dbus-run-session -- "$0" "$@"
fi
# Always use a private X server. The ambient DISPLAY may be a real session.
if [ -z "${LCOS_SESSION_DISPLAY:-}" ]; then
  export LCOS_SESSION_DISPLAY=1
  exec xvfb-run -a -s "-screen 0 1280x800x24" "$0" "$@"
fi
exec "$@"

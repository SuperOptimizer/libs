#!/bin/sh
ulimit -v 262144 2>/dev/null; exec "$@"

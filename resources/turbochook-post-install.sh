#!/bin/sh
set -eu
# Refresh an older per-user MIME record after a package upgrade.
TC_EXECUTABLE=$(findpaths -p "$0" B_FIND_PATH_APPS_DIRECTORY TurboChook)
mimeset -a -f "$TC_EXECUTABLE"

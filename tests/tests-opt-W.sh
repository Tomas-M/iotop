#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

IOTOP="${IOTOP:-./iotop}"

# Run with an empty config directory and without NO_COLOR, so that neither the config of the user
# (~/.config/iotop/iotoprc) nor the environment changes what is written.
CFG="$(mktemp -d)" || exit 1
trap 'rm -rf "$CFG"' EXIT

XDG_CONFIG_HOME="$CFG" NO_COLOR= "$IOTOP" -l --debug-write | grep -q '^--no-color$'

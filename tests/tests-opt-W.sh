#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

IOTOP="${IOTOP:-./iotop}"

# Pass --color before -l to clear nocolor in case an existing user config
# (~/.config/iotop/iotoprc) already has --no-color enabled.
"$IOTOP" --color -l --debug-write | grep -q '^--no-color$'

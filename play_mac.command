#!/bin/bash
# Builds Kubostrel and starts it in a window on this Mac. No iPhone or Apple ID is needed.
# Run it in Terminal: bash play_mac.command (README.md, "Поиграть на Mac").
exec bash "$(dirname "$0")/install_iphone.command" --mac "$@"

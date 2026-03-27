#!/bin/bash
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

FRAMEWORK_DIR="$SCRIPT_DIR/.pio/libdeps/default/STM32 Framework/"

if [ ! -d "$FRAMEWORK_DIR" ]; then
    echo "STM32 Framework directory not found"
    exit 1
else
    cd "$FRAMEWORK_DIR"
fi

python Update.py "$SCRIPT_DIR"

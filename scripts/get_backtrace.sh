#!/bin/bash
# Run this to get a backtrace of the ndmanager crash.
# Requires: gdb (sudo apt install gdb)
# Usage: ./get_backtrace.sh path/to/jg05-20120316.xml

XML="${1:-}"
if [ -z "$XML" ]; then
    echo "Usage: $0 path/to/session.xml"
    exit 1
fi

# Build with debug symbols if not already done
# (run this from your ndmanager-qt6-fixed build directory)
echo "Running ndmanager under gdb..."
echo "When it crashes, gdb will stop. Type: bt"
echo "Copy and paste the full backtrace output."
echo ""

LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe QT_XCB_GL_INTEGRATION=none \
gdb -ex run -ex "bt full" -ex quit --args ndmanager "$XML" 2>&1 | tee /tmp/ndmanager_crash.txt

echo ""
echo "Backtrace saved to /tmp/ndmanager_crash.txt"

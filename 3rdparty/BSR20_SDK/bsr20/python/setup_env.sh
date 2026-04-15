#!/bin/bash

echo "========================================"
echo " BSR20 Python Sample - Environment Setup"
echo "========================================"
echo

# ========================================================================
# Check Python 3
# ========================================================================
if ! command -v python3 &> /dev/null; then
    echo "[ERROR] python3 not found."
    echo "Please install Python 3.x (e.g. sudo apt install python3 python3-pip)"
    exit 1
fi

echo "[OK] Found Python:"
python3 --version
echo

# ========================================================================
# Check required libraries
# ========================================================================
echo "Checking required libraries..."
echo

MISSING=0

if python3 -c "import PySide6" &> /dev/null; then
    echo "  [O] PySide6       - OK"
else
    echo "  [X] PySide6       - NOT installed"
    MISSING=1
fi

echo

if [ "$MISSING" -eq 0 ]; then
    echo "[OK] All libraries are installed!"
    echo
    echo "You can run the samples:"
    echo "  python3 byda_radar.py     (console)"
    echo "  python3 sample_gui.py     (GUI)"
    exit 0
fi

echo "----------------------------------------"
echo "Some libraries are missing."
echo
read -p "Install missing libraries now? (Y/N): " INSTALL

if [ "$INSTALL" != "Y" ] && [ "$INSTALL" != "y" ]; then
    echo
    echo "Skipped. You can install manually:"
    echo "  python3 -m pip install -r requirements.txt"
    exit 0
fi

echo
echo "Installing from requirements.txt..."
echo

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
python3 -m pip install -r "$SCRIPT_DIR/requirements.txt"

if [ $? -ne 0 ]; then
    echo
    echo "[ERROR] Installation failed."
    exit 1
fi

echo
echo "[OK] All libraries installed successfully!"
echo
echo "You can now run:"
echo "  python3 byda_radar.py     (console)"
echo "  python3 sample_gui.py     (GUI)"

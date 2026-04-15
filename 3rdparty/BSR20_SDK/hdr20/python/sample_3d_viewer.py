"""
HDR20 (4D Radar) - 3D Point Cloud Viewer
==========================================
Real-time 3D visualization of 4D radar point cloud data.

Libraries:
    - pyqtgraph (MIT license)
    - PyOpenGL  (BSD license)
    - PySide6   (LGPL license)

Usage:
    pip install -r requirements.txt
    python sample_3d_viewer.py [IP] [PORT]
"""

import sys
import os
import threading
import numpy as np

from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QLineEdit, QPushButton, QGroupBox, QStatusBar, QCheckBox,
    QComboBox,
)
from PySide6.QtCore import Qt, Signal, QObject, QTimer
from PySide6.QtGui import QFont

import pyqtgraph.opengl as gl
import pyqtgraph as pg

# hdr20_radar.py in same folder
script_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, script_dir)

# DLL path
sample_dir = os.path.normpath(os.path.join(script_dir, "..", ".."))
if sys.platform == "win32":
    import struct
    bits = struct.calcsize("P") * 8
    bit_dir = "32bit" if bits == 32 else "64bit"
    dll_dir = os.path.join(sample_dir, "window", "sdk", bit_dir, "bin")
    if os.path.isdir(dll_dir):
        os.environ["PATH"] = dll_dir + os.pathsep + os.environ.get("PATH", "")
        if hasattr(os, "add_dll_directory"):
            os.add_dll_directory(dll_dir)
else:
    import struct
    bits = struct.calcsize("P") * 8
    if bits != 64:
        print("[ERROR] BSR20_SDK Python sample requires 64-bit Python.")
        print("        32-bit Python is not supported on Linux.")
        sys.exit(1)
    dll_dir = os.path.join(sample_dir, "linux", "sdk", "64bit", "lib")
    if os.path.isdir(dll_dir):
        os.environ["LD_LIBRARY_PATH"] = dll_dir + os.pathsep + os.environ.get("LD_LIBRARY_PATH", "")

from hdr20_radar import Hdr20Radar, FrameInfo, BydaError


# ------------------------------------------------------------------ #
# Signal bridge: worker thread -> GUI
# ------------------------------------------------------------------ #
class SignalBridge(QObject):
    frame_received = Signal(object)  # FrameInfo
    status_msg = Signal(str)
    error_msg = Signal(str)
    disconnected = Signal()


# ------------------------------------------------------------------ #
# Worker thread
# ------------------------------------------------------------------ #
class RadarWorker(threading.Thread):
    def __init__(self, radar: Hdr20Radar, bridge: SignalBridge):
        super().__init__(daemon=True)
        self.radar = radar
        self.bridge = bridge
        self._stop_event = threading.Event()

    def stop(self):
        self._stop_event.set()

    def run(self):
        while not self._stop_event.is_set():
            try:
                ret = self.radar.receive_once()
                if ret == 0:
                    self.bridge.status_msg.emit("Connection closed by radar")
                    break
                if ret < 0:
                    if not self._stop_event.is_set():
                        self.bridge.error_msg.emit(f"Receive error: {ret}")
                    break
            except Exception as e:
                self.bridge.error_msg.emit(f"Exception: {e}")
                break
        self.bridge.disconnected.emit()


# ------------------------------------------------------------------ #
# Main Window
# ------------------------------------------------------------------ #
class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("HDR20 (4D Radar) - 3D Point Cloud Viewer")
        self.setMinimumSize(1024, 768)

        self.radar = None
        self.worker = None
        self.bridge = SignalBridge()
        self.latest_frame = None

        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)

        # -- Connection bar --
        conn_group = QGroupBox("Connection")
        conn_layout = QHBoxLayout(conn_group)

        conn_layout.addWidget(QLabel("IP:"))
        self.ip_edit = QLineEdit("192.168.172.128")
        self.ip_edit.setFixedWidth(150)
        conn_layout.addWidget(self.ip_edit)

        conn_layout.addWidget(QLabel("Port:"))
        self.port_edit = QLineEdit("7")
        self.port_edit.setFixedWidth(50)
        conn_layout.addWidget(self.port_edit)

        self.connect_btn = QPushButton("Connect")
        self.connect_btn.setFixedWidth(100)
        self.connect_btn.setStyleSheet(
            "QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 5px; }"
        )
        conn_layout.addWidget(self.connect_btn)

        self.disconnect_btn = QPushButton("Disconnect")
        self.disconnect_btn.setFixedWidth(100)
        self.disconnect_btn.setEnabled(False)
        self.disconnect_btn.setStyleSheet(
            "QPushButton { background-color: #f44336; color: white; font-weight: bold; padding: 5px; }"
            "QPushButton:disabled { background-color: #aaa; }"
        )
        conn_layout.addWidget(self.disconnect_btn)

        conn_layout.addStretch()

        # Display options
        self.chk_dyn_high = QCheckBox("DynHigh")
        self.chk_dyn_high.setChecked(True)
        conn_layout.addWidget(self.chk_dyn_high)

        self.chk_tracks = QCheckBox("Tracks")
        self.chk_tracks.setChecked(True)
        conn_layout.addWidget(self.chk_tracks)

        layout.addWidget(conn_group)

        # -- Info bar --
        info_layout = QHBoxLayout()
        self.frame_label = QLabel("Frame: -  |  DynHigh: -  |  Tracks: -")
        self.frame_label.setStyleSheet("font-size: 13px; color: #ccc; padding: 4px;")
        info_layout.addWidget(self.frame_label)
        info_layout.addStretch()
        layout.addLayout(info_layout)

        # -- 3D View --
        self.view3d = gl.GLViewWidget()
        self.view3d.setBackgroundColor(20, 20, 30)
        self.view3d.setCameraPosition(distance=80, elevation=30, azimuth=45)

        # Grid
        grid = gl.GLGridItem()
        grid.setSize(200, 200)
        grid.setSpacing(10, 10)
        grid.setColor((80, 80, 80, 120))
        self.view3d.addItem(grid)

        # Axis
        axis = gl.GLAxisItem()
        axis.setSize(20, 20, 20)
        self.view3d.addItem(axis)

        # Scatter plots
        self.dyn_high_scatter = gl.GLScatterPlotItem(
            pos=np.zeros((1, 3), dtype=np.float32),
            color=(0.0, 1.0, 0.3, 0.8),
            size=3,
            pxMode=True,
        )
        self.view3d.addItem(self.dyn_high_scatter)

        self.track_scatter = gl.GLScatterPlotItem(
            pos=np.zeros((1, 3), dtype=np.float32),
            color=(1.0, 0.3, 0.0, 1.0),
            size=6,
            pxMode=True,
        )
        self.view3d.addItem(self.track_scatter)

        layout.addWidget(self.view3d, stretch=1)

        # -- Status bar --
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        self.status_bar.showMessage("Ready")

    def _connect_signals(self):
        self.connect_btn.clicked.connect(self._on_connect)
        self.disconnect_btn.clicked.connect(self._on_disconnect)

        self.bridge.frame_received.connect(self._on_frame)
        self.bridge.status_msg.connect(lambda m: self.status_bar.showMessage(m))
        self.bridge.error_msg.connect(lambda m: self.status_bar.showMessage(m))
        self.bridge.disconnected.connect(self._on_disconnected)

    # -- Actions ---------------------------------------------------- #

    def _on_connect(self):
        ip = self.ip_edit.text().strip()
        port_str = self.port_edit.text().strip()
        if not ip or not port_str:
            self.status_bar.showMessage("IP and Port are required")
            return
        try:
            port = int(port_str)
        except ValueError:
            self.status_bar.showMessage("Port must be a number")
            return

        self.connect_btn.setEnabled(False)
        self.status_bar.showMessage(f"Connecting to {ip}:{port}...")

        try:
            self.radar = Hdr20Radar(ip, port)

            def on_frame(info: FrameInfo):
                self.bridge.frame_received.emit(info)

            self.radar.set_frame_callback(on_frame)
            self.radar.connect()
        except (OSError, BydaError) as e:
            self.status_bar.showMessage(f"Failed: {e}")
            if self.radar:
                self.radar.close()
                self.radar = None
            self.connect_btn.setEnabled(True)
            return

        self.status_bar.showMessage(f"Connected to {ip}:{port}")
        self.disconnect_btn.setEnabled(True)
        self.ip_edit.setEnabled(False)
        self.port_edit.setEnabled(False)

        self.worker = RadarWorker(self.radar, self.bridge)
        self.worker.start()

    def _on_disconnect(self):
        if self.worker:
            self.worker.stop()
            self.worker.join(timeout=3)
            self.worker = None
        if self.radar:
            self.radar.close()
            self.radar = None
        self._on_disconnected()

    def _on_disconnected(self):
        self.connect_btn.setEnabled(True)
        self.disconnect_btn.setEnabled(False)
        self.ip_edit.setEnabled(True)
        self.port_edit.setEnabled(True)
        self.status_bar.showMessage("Disconnected")

    # -- 3D Update -------------------------------------------------- #

    def _on_frame(self, info: FrameInfo):
        self.frame_label.setText(
            f"Frame: {info.frame_num}  |  "
            f"DynHigh: {info.dyn_high_count}  |  "
            f"DynLow: {info.dyn_low_count}  |  "
            f"MmLongH: {info.mm_long_high_count}  |  "
            f"MmShortH: {info.mm_short_high_count}  |  "
            f"Tracks: {info.track_count}"
        )

        # Update dyn_high point cloud
        if self.chk_dyn_high.isChecked() and info.dyn_high_points:
            pts = np.array(
                [[p.x, p.y, p.z] for p in info.dyn_high_points],
                dtype=np.float32,
            )
            # Color by velocity
            vels = np.array([p.vel for p in info.dyn_high_points], dtype=np.float32)
            v_abs = np.abs(vels)
            v_max = max(v_abs.max(), 1.0)
            v_norm = v_abs / v_max
            colors = np.zeros((len(pts), 4), dtype=np.float32)
            colors[:, 0] = v_norm         # red = fast
            colors[:, 1] = 1.0 - v_norm   # green = slow
            colors[:, 2] = 0.2
            colors[:, 3] = 0.8
            self.dyn_high_scatter.setData(pos=pts, color=colors)
        elif not info.dyn_high_points:
            self.dyn_high_scatter.setData(pos=np.zeros((1, 3), dtype=np.float32))

        # Update track points
        if self.chk_tracks.isChecked() and info.track_points:
            pts = np.array(
                [[p.x, p.y, p.z] for p in info.track_points],
                dtype=np.float32,
            )
            self.track_scatter.setData(pos=pts)
        elif not info.track_points:
            self.track_scatter.setData(pos=np.zeros((1, 3), dtype=np.float32))

    def closeEvent(self, event):
        self._on_disconnect()
        event.accept()


# ------------------------------------------------------------------ #
# Entry point
# ------------------------------------------------------------------ #
if __name__ == "__main__":
    app = QApplication(sys.argv)

    # Dark theme
    app.setStyleSheet("""
        QMainWindow, QWidget { background-color: #2b2b2b; color: #ddd; }
        QGroupBox {
            border: 1px solid #555; border-radius: 4px;
            margin-top: 8px; padding-top: 14px;
            font-weight: bold; color: #aaa;
        }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
        QLineEdit {
            background-color: #3c3c3c; border: 1px solid #555;
            border-radius: 3px; padding: 4px; color: #eee;
        }
        QCheckBox { color: #ccc; spacing: 4px; }
        QStatusBar { background-color: #333; color: #aaa; }
    """)

    window = MainWindow()
    window.show()
    sys.exit(app.exec())

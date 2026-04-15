"""
BSR20 SDK - PySide6 Sample GUI
================================
DLL 동작 확인용 간단한 GUI 샘플.
Connect 버튼으로 레이더 연결 후 Track/VDS 데이터 실시간 표출.

실행:
    pip install PySide6
    python sample_gui.py
"""

import sys
import os
from threading import Thread, Event

from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QLineEdit, QPushButton, QTextEdit, QGroupBox, QTableWidget,
    QTableWidgetItem, QHeaderView, QStatusBar,
)
from PySide6.QtCore import Qt, QTimer, Signal, QObject
from PySide6.QtGui import QFont, QColor

# byda_radar.py 가 같은 폴더에 있어야 함
script_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, script_dir)

# DLL path: sample/window/sdk/{32bit,64bit}/bin/ or sample/linux/sdk/64bit/lib/
sample_dir = os.path.normpath(os.path.join(script_dir, "..", ".."))
if sys.platform == "win32":
    import struct
    bits = struct.calcsize("P") * 8
    bit_dir = "32bit" if bits == 32 else "64bit"
    dll_dir = os.path.join(sample_dir, "window", "sdk", bit_dir, "bin")
else:
    import struct
    bits = struct.calcsize("P") * 8
    if bits != 64:
        print("[ERROR] BSR20_SDK Python sample requires 64-bit Python.")
        print("        32-bit Python is not supported on Linux.")
        sys.exit(1)
    dll_dir = os.path.join(sample_dir, "linux", "sdk", "64bit", "lib")
if os.path.isdir(dll_dir):
    os.environ["PATH"] = dll_dir + os.pathsep + os.environ.get("PATH", "")
    if hasattr(os, "add_dll_directory"):  # Python 3.8+
        os.add_dll_directory(dll_dir)

from byda_radar import BydaRadar, BydaError, BYDA_OK


# ------------------------------------------------------------------ #
# Signal bridge: worker thread → GUI update
# ------------------------------------------------------------------ #
class SignalBridge(QObject):
    tracks_updated = Signal(list)       # List[Track]
    vds_updated = Signal(list)          # List[VdsEvent]
    scan_updated = Signal(object)       # ScanInfo
    status_msg = Signal(str)
    error_msg = Signal(str)
    connected = Signal()
    disconnected = Signal()


# ------------------------------------------------------------------ #
# Worker thread: receive + process loop
# ------------------------------------------------------------------ #
class RadarWorker(Thread):
    def __init__(self, radar: BydaRadar, bridge: SignalBridge):
        super().__init__(daemon=True)
        self.radar = radar
        self.bridge = bridge
        self._stop_event = Event()

    def stop(self):
        self._stop_event.set()

    def run(self):
        while not self._stop_event.is_set():
            try:
                n = self.radar.receive()
                if n <= 0:
                    continue

                track_count = self.radar.process()

                # Scan info
                try:
                    scan = self.radar.get_scan_info()
                    self.bridge.scan_updated.emit(scan)
                except BydaError:
                    pass

                # Tracks
                if track_count > 0:
                    tracks = self.radar.get_tracks()
                    self.bridge.tracks_updated.emit(tracks)
                else:
                    self.bridge.tracks_updated.emit([])

                # VDS events
                vds = self.radar.get_vds_events()
                if vds:
                    self.bridge.vds_updated.emit(vds)

            except BydaError as e:
                self.bridge.error_msg.emit(f"Error: {e}")
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
        self.setWindowTitle("BSR20 SDK - Sample GUI")
        self.setMinimumSize(800, 600)

        self.radar = None
        self.worker = None
        self.bridge = SignalBridge()

        self._setup_ui()
        self._connect_signals()

    # ---- UI Setup ------------------------------------------------- #
    def _setup_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)

        # -- Connection group --
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
        self.connect_btn.setFixedWidth(120)
        self.connect_btn.setStyleSheet(
            "QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 5px; }"
            "QPushButton:hover { background-color: #45a049; }"
        )
        conn_layout.addWidget(self.connect_btn)

        self.disconnect_btn = QPushButton("Disconnect")
        self.disconnect_btn.setFixedWidth(120)
        self.disconnect_btn.setEnabled(False)
        self.disconnect_btn.setStyleSheet(
            "QPushButton { background-color: #f44336; color: white; font-weight: bold; padding: 5px; }"
            "QPushButton:hover { background-color: #da190b; }"
            "QPushButton:disabled { background-color: #aaa; }"
        )
        conn_layout.addWidget(self.disconnect_btn)

        conn_layout.addStretch()
        layout.addWidget(conn_group)

        # -- Scan Info --
        info_layout = QHBoxLayout()
        self.scan_label = QLabel("Scan: -  |  Tracks: -  |  Timestamp: -")
        self.scan_label.setStyleSheet("font-size: 13px; color: #ccc; padding: 4px;")
        info_layout.addWidget(self.scan_label)
        info_layout.addStretch()
        layout.addLayout(info_layout)

        # -- Track table --
        track_group = QGroupBox("Tracks")
        track_layout = QVBoxLayout(track_group)

        self.track_table = QTableWidget(0, 7)
        self.track_table.setHorizontalHeaderLabels(
            ["ID", "X (m)", "Y (m)", "Vx (m/s)", "Vy (m/s)", "Type", "Status"]
        )
        header = self.track_table.horizontalHeader()
        for i in range(7):
            header.setSectionResizeMode(i, QHeaderView.Stretch)
        self.track_table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.track_table.setAlternatingRowColors(True)
        track_layout.addWidget(self.track_table)
        layout.addWidget(track_group, stretch=3)

        # -- VDS Log --
        vds_group = QGroupBox("VDS Events")
        vds_layout = QVBoxLayout(vds_group)
        self.vds_log = QTextEdit()
        self.vds_log.setReadOnly(True)
        self.vds_log.setMaximumHeight(150)
        self.vds_log.setStyleSheet("font-family: Consolas, monospace; font-size: 12px;")
        vds_layout.addWidget(self.vds_log)
        layout.addWidget(vds_group, stretch=1)

        # -- Status bar --
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        self.status_bar.showMessage("Ready - DLL not loaded")

    # ---- Signal connections --------------------------------------- #
    def _connect_signals(self):
        self.connect_btn.clicked.connect(self._on_connect)
        self.disconnect_btn.clicked.connect(self._on_disconnect)

        self.bridge.tracks_updated.connect(self._update_tracks)
        self.bridge.vds_updated.connect(self._update_vds)
        self.bridge.scan_updated.connect(self._update_scan)
        self.bridge.status_msg.connect(self._on_status)
        self.bridge.error_msg.connect(self._on_error)
        self.bridge.disconnected.connect(self._on_disconnected)

    # ---- Actions -------------------------------------------------- #
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
        self.status_bar.showMessage(f"Loading DLL & connecting to {ip}:{port}...")

        try:
            self.radar = BydaRadar(ip, port)
            self.radar.connect()
        except OSError as e:
            self.status_bar.showMessage(f"DLL load failed: {e}")
            self.connect_btn.setEnabled(True)
            return
        except BydaError as e:
            self.status_bar.showMessage(f"Connect failed: {e}")
            if self.radar:
                self.radar.close()
                self.radar = None
            self.connect_btn.setEnabled(True)
            return
        except Exception as e:
            self.status_bar.showMessage(f"Error: {e}")
            self.connect_btn.setEnabled(True)
            return

        self.status_bar.showMessage(f"Connected to {ip}:{port}  |  Comm v{self.radar.comm_version}")
        self.disconnect_btn.setEnabled(True)
        self.ip_edit.setEnabled(False)
        self.port_edit.setEnabled(False)

        # Start worker thread
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

    # ---- UI Updates ----------------------------------------------- #
    def _update_tracks(self, tracks):
        self.track_table.setRowCount(len(tracks))
        for row, t in enumerate(tracks):
            items = [
                f"{t.id}",
                f"{t.x_pos:.1f}",
                f"{t.y_pos:.1f}",
                f"{t.x_vel:.1f}",
                f"{t.y_vel:.1f}",
                f"{t.type}",
                f"{t.status}",
            ]
            for col, text in enumerate(items):
                item = QTableWidgetItem(text)
                item.setTextAlignment(Qt.AlignCenter)
                self.track_table.setItem(row, col, item)

    def _update_vds(self, events):
        for ev in events:
            line = (
                f"[VDS] Lane={ev.lane_num}  ID={ev.track_id:3d}  "
                f"Speed={ev.speed:5.1f} kph  Type={ev.track_type}  "
                f"Y={ev.y_pos:.1f}m  Scan={ev.scan_index}"
            )
            self.vds_log.append(line)

        # 로그 최대 500줄 유지
        doc = self.vds_log.document()
        if doc.blockCount() > 500:
            cursor = self.vds_log.textCursor()
            cursor.movePosition(cursor.Start)
            for _ in range(doc.blockCount() - 500):
                cursor.movePosition(cursor.Down, cursor.KeepAnchor)
            cursor.removeSelectedText()
            cursor.deleteChar()  # remove leftover newline

    def _update_scan(self, scan):
        self.scan_label.setText(
            f"Scan: {scan.scan_index}  |  Tracks: {scan.track_count}  |  "
            f"Timestamp: {scan.timestamp}  |  Error: {scan.error_flag}"
        )

    def _on_status(self, msg):
        self.status_bar.showMessage(msg)

    def _on_error(self, msg):
        self.status_bar.showMessage(msg)
        self.vds_log.append(f"<span style='color:red;'>{msg}</span>")

    # ---- Cleanup -------------------------------------------------- #
    def closeEvent(self, event):
        self._on_disconnect()
        event.accept()


# ------------------------------------------------------------------ #
# Entry point
# ------------------------------------------------------------------ #
if __name__ == "__main__":
    app = QApplication(sys.argv)

    # 다크 테마 (간단 버전)
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
        QTableWidget {
            background-color: #2b2b2b; alternate-background-color: #323232;
            gridline-color: #444; color: #ddd; border: 1px solid #555;
        }
        QHeaderView::section {
            background-color: #3c3c3c; color: #ccc;
            padding: 4px; border: 1px solid #555;
        }
        QTextEdit { background-color: #1e1e1e; border: 1px solid #555; color: #ddd; }
        QStatusBar { background-color: #333; color: #aaa; }
    """)

    window = MainWindow()
    window.show()
    sys.exit(app.exec())

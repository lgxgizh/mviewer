#!/usr/bin/env python3
"""MViewer - 图像查看分析软件原型 (PyQt6)"""

import sys
from pathlib import Path

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QSplitter, QTreeView, QListWidget,
    QListWidgetItem, QVBoxLayout, QWidget, QStatusBar, QMenuBar,
    QToolBar, QMessageBox, QInputDialog, QFileDialog,
    QAbstractItemView, QCheckBox, QFrame,
    QSizePolicy, QMenu, QStandardItemModel, QStandardItem
)
from PyQt6.QtCore import (
    Qt, QDir, QModelIndex, QRect, QPoint, QSize, pyqtSignal, QObject
)
from PyQt6.QtGui import (
    QPixmap, QImage, QPainter, QColor, QPen, QBrush, QFont,
    QIcon, QKeySequence, QTransform, QCursor, QPalette,
    QAction
)


class ThumbnailPanel(QListWidget):
    """缩略图面板"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setViewMode(QListWidget.ViewMode.IconMode)
        self.setFlow(QListWidget.Flow.LeftToRight)
        self.setWrapping(True)
        self.setResizeMode(QListWidget.ResizeMode.Adjust)
        self.setSpacing(4)
        self.setIconSize(QSize(120, 120))
        self.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        self.file_paths = []
        self.current_dir = ""

        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.customContextMenuRequested.connect(self.on_context_menu)

    def set_directory(self, dir_path: str):
        self.clear()
        self.file_paths.clear()
        self.current_dir = dir_path

        path = Path(dir_path)
        if not path.exists() or not path.is_dir():
            return

        extensions = {'.jpg', '.jpeg', '.bmp', '.png'}
        files = sorted([
            f for f in path.iterdir()
            if f.is_file() and f.suffix.lower() in extensions
        ])

        for f in files[:1000]:
            pixmap = QPixmap(str(f))
            if pixmap.isNull():
                continue

            thumb = pixmap.scaled(120, 120, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)
            icon = QIcon(thumb)

            item = QListWidgetItem(icon, f.name)
            item.setData(Qt.ItemDataRole.UserRole, str(f.absolute()))
            item.setSizeHint(QSize(130, 140))
            self.addItem(item)
            self.file_paths.append(str(f.absolute()))

    def on_context_menu(self, pos):
        item = self.itemAt(pos)
        if not item:
            return

        menu = QMenu(self)
        rename_action = menu.addAction("重命名")
        delete_action = menu.addAction("删除")

        action = menu.exec(self.mapToGlobal(pos))

        if action == rename_action:
            self.rename_item(item)
        elif action == delete_action:
            self.delete_item(item)

    def rename_item(self, item):
        old_path = item.data(Qt.ItemDataRole.UserRole)
        old_name = Path(old_path).name

        new_name, ok = QInputDialog.getText(self, "重命名", "新名称：", text=old_name)

        if ok:
            old = Path(old_path)
            new = old.parent / new_name
            try:
                old.rename(new)
                item.setText(new_name)
                item.setData(Qt.ItemDataRole.UserRole, str(new))
                idx = self.file_paths.index(old_path)
                self.file_paths[idx] = str(new)
            except Exception as e:
                QMessageBox.warning(self, "错误", f"重命名失败：{e}")

    def delete_item(self, item):
        path = item.data(Qt.ItemDataRole.UserRole)
        reply = QMessageBox.question(self, "确认", f"确定要删除 {Path(path).name} 吗？",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)

        if reply == QMessageBox.StandardButton.Yes:
            try:
                import ctypes
                from ctypes import wintypes
                fileop = wintypes.SHFILEOPSTRUCTW()
                fileop.hwnd = None
                fileop.wFunc = 3
                fileop.pFrom = path + "\0"
                fileop.pTo = None
                fileop.fFlags = 0x0430
                SHFileOperationW = ctypes.windll.shell32.SHFileOperationW
                result = SHFileOperationW(ctypes.byref(fileop))

                if result == 0:
                    row = self.row(item)
                    self.takeItem(row)
                    del self.file_paths[row]
                else:
                    Path(path).unlink()
                    row = self.row(item)
                    self.takeItem(row)
                    del self.file_paths[row]
            except Exception as e:
                QMessageBox.warning(self, "错误", f"删除失败：{e}")


class ImageViewerWidget(QWidget):
    """图片查看组件"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.pixmap = QPixmap()
        self._scale = 1.0
        self._offset = QPoint(0, 0)
        self._dragging = False
        self._last_pos = QPoint()
        self.show_histogram = True
        self.selection = QRect()
        self._selecting = False
        self._selection_start = QPoint()

        self.setMinimumSize(400, 300)
        self.setStyleSheet("background-color: #1e1e1e;")

    def set_image(self, path: str):
        self.pixmap = QPixmap(path)
        self.selection = QRect()

        if not self.pixmap.isNull():
            self.fit_to_widget()

        self.update()

    def fit_to_widget(self):
        if self.pixmap.isNull():
            return
        w, h = self.width(), self.height()
        sx = w / self.pixmap.width() if self.pixmap.width() > 0 else 1
        sy = h / self.pixmap.height() if self.pixmap.height() > 0 else 1
        self._scale = min(sx, sy) * 0.95
        self._scale = max(self._scale, 0.01)
        self._offset = QPoint(
            int((w - self.pixmap.width() * self._scale) / 2),
            int((h - self.pixmap.height() * self._scale) / 2)
        )
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor(30, 30, 30))

        if not self.pixmap.isNull():
            painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
            target = QRect(
                self._offset,
                QSize(
                    int(self.pixmap.width() * self._scale),
                    int(self.pixmap.height() * self._scale)
                )
            )
            painter.drawPixmap(target, self.pixmap, QRect(QPoint(0, 0), self.pixmap.size()))

            if not self.selection.isNull():
                pen = QPen(QColor(255, 255, 0), 2, Qt.PenStyle.DashLine)
                painter.setPen(pen)
                painter.setBrush(QBrush(QColor(255, 255, 0, 60)))
                painter.drawRect(self.selection)

        if self.show_histogram and not self.pixmap.isNull():
            self._draw_histogram(painter)

    def _draw_histogram(self, painter):
        histogram = [0] * 256
        image = self.pixmap.toImage()

        for y in range(image.height()):
            for x in range(image.width()):
                color = image.pixel(x, y)
                lum = int(0.299 * ((color >> 16) & 0xFF) +
                          0.587 * ((color >> 8) & 0xFF) +
                          0.114 * (color & 0xFF))
                histogram[lum] += 1

        painter.save()
        painter.setBrush(QBrush(QColor(0, 0, 0, 160)))
        painter.setPen(Qt.PenStyle.NoPen)
        painter.drawRoundedRect(8, 8, 180, 100, 6, 6)

        max_val = max(histogram) if histogram else 1
        if max_val == 0:
            max_val = 1

        painter.setPen(QPen(QColor(255, 255, 255, 200), 1))
        for i in range(256):
            x = 12 + i * 172 / 256.0
            y = 104 - histogram[i] * 84 / max_val
            painter.drawLine(int(x), 104, int(x), int(y))

        painter.restore()

    def wheelEvent(self, event):
        if self.pixmap.isNull():
            return

        angle = event.angleDelta().y()
        factor = 1.15 if angle > 0 else 1 / 1.15

        mouse_pos = event.position().toPoint()
        img_pos = (mouse_pos - self._offset) / self._scale

        self._scale *= factor
        self._scale = max(0.05, min(50.0, self._scale))

        self._offset = mouse_pos - (img_pos * self._scale).toPoint()
        self.update()

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            if self._selecting:
                self._selection_start = event.position().toPoint()
                self.selection = QRect()
            else:
                self._dragging = True
                self._last_pos = event.position().toPoint()
                self.setCursor(QCursor(Qt.CursorShape.ClosedHandCursor))

    def mouseMoveEvent(self, event):
        if self._dragging:
            delta = event.position().toPoint() - self._last_pos
            self._offset += delta
            self._last_pos = event.position().toPoint()
            self.update()
        elif self._selecting:
            sel_end = event.position().toPoint()
            self.selection = QRect(self._selection_start, sel_end).normalized()
            self.update()

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self._dragging = False
            self.setCursor(QCursor(Qt.CursorShape.ArrowCursor))

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if not self.pixmap.isNull():
            self.fit_to_widget()

    def compute_selection_stats(self):
        if self.pixmap.isNull() or self.selection.isNull():
            return None

        image = self.pixmap.toImage()

        total_r, total_g, total_b, count = 0, 0, 0, 0

        sel = self.selection
        for y in range(sel.y(), min(sel.y() + sel.height(), self.height())):
            for x in range(sel.x(), min(sel.x() + sel.width(), self.width())):
                if x < 0 or y < 0:
                    continue
                ix = int((x - self._offset.x()) / self._scale)
                iy = int((y - self._offset.y()) / self._scale)
                if 0 <= ix < image.width() and 0 <= iy < image.height():
                    c = image.pixel(ix, iy)
                    total_r += (c >> 16) & 0xFF
                    total_g += (c >> 8) & 0xFF
                    total_b += c & 0xFF
                    count += 1

        if count == 0:
            return None

        avg_r = total_r / count
        avg_g = total_g / count
        avg_b = total_b / count
        avg_lum = 0.299 * avg_r + 0.587 * avg_g + 0.114 * avg_b

        return {
            "luminance": avg_lum,
            "r": avg_r,
            "g": avg_g,
            "b": avg_b,
        }


class CompareImageWidget(ImageViewerWidget):
    pass


class CompareWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.image_widgets = []
        self.compare_mode = False
        self.sync_zoom = True
        self.sync_pan = True
        self.setup_ui()

    def setup_ui(self):
        self.layout = QVBoxLayout(self)
        self.layout.setContentsMargins(0, 0, 0, 0)
        self.layout.setSpacing(0)

        toolbar = QToolBar()
        self.layout.addWidget(toolbar)

        self.zoom_sync_cb = QCheckBox("同步缩放")
        self.zoom_sync_cb.setChecked(True)
        self.zoom_sync_cb.toggled.connect(self._on_zoom_sync_changed)
        toolbar.addWidget(self.zoom_sync_cb)

        self.pan_sync_cb = QCheckBox("同步拖动")
        self.pan_sync_cb.setChecked(True)
        self.pan_sync_cb.toggled.connect(self._on_pan_sync_changed)
        toolbar.addWidget(self.pan_sync_cb)

        toolbar.addSeparator()

        select_action = QAction("框选模式", self)
        select_action.setCheckable(True)
        select_action.toggled.connect(self._on_select_mode_changed)
        toolbar.addAction(select_action)

        self.image_container = QWidget()
        self.image_container.setStyleSheet("background-color: #1e1e1e;")
        self.image_layout = QGridLayout(self.image_container)
        self.image_layout.setSpacing(2)
        self.layout.addWidget(self.image_container, stretch=1)

        self.stats_panel = QFrame()
        self.stats_panel.setStyleSheet(
            "QFrame { background-color: #2d2d2d; color: #ffffff; padding: 8px; }"
        )
        stats_layout = QHBoxLayout(self.stats_panel)
        self.stats_label = QLabel("框选区域统计：无")
        self.stats_label.setFont(QFont("Microsoft YaHei", 9))
        stats_layout.addWidget(self.stats_label)
        self.stats_panel.setVisible(False)
        self.stats_panel.setFixedHeight(40)
        self.layout.addWidget(self.stats_panel)

    def set_images(self, file_paths):
        for w in self.image_widgets:
            w.deleteLater()
        self.image_widgets.clear()

        n = len(file_paths)
        if n == 0:
            return

        rows, cols = self.get_layout(n)

        for i, path in enumerate(file_paths):
            row = i // cols
            col = i % cols

            widget = CompareImageWidget()
            widget.setMinimumSize(200, 150)
            widget.set_image(path)

            self.image_layout.addWidget(widget, row, col)
            self.image_widgets.append(widget)

        self.compare_mode = True

    def get_layout(self, n):
        if n == 2:
            return (1, 2)
        elif n == 3:
            return (1, 3)
        elif n == 4:
            return (2, 2)
        else:
            return (2, 4)

    def _on_zoom_sync_changed(self, checked):
        self.sync_zoom = checked

    def _on_pan_sync_changed(self, checked):
        self.sync_pan = checked

    def _on_select_mode_changed(self, checked):
        for w in self.image_widgets:
            w._selecting = checked
            if checked:
                w.setCursor(QCursor(Qt.CursorShape.CrossCursor))
            else:
                w.setCursor(QCursor(Qt.CursorShape.ArrowCursor))

    def exit_compare(self):
        for w in self.image_widgets:
            w.deleteLater()
        self.image_widgets.clear()
        self.compare_mode = False
        self.stats_panel.setVisible(False)

    def compute_selection_stats(self):
        if not self.image_widgets:
            return

        ref_widget = self.image_widgets[0]
        if ref_widget.selection.isNull():
            self.stats_label.setText("框无")
            self.stats_panel.setVisible(False)
            return

        results = []
        for i, widget in enumerate(self.image_widgets):
            stats = widget.compute_selection_stats()
            if stats:
                results.append(
                    "图{}: Lum={:.1f} R={:.1f} G={:.1f} B={:.1f}".format(
                    i+1, stats['luminance'], stats['r'], stats['g'], stats['b'])
                )
            else:
                results.append("图{}: N/A".format(i+1))

        self.stats_label.setText("框选统计：" + " | ".join(results))
        self.stats_panel.setVisible(True)


class MainWindow(QMainWindow):

    def __init__(self):
        super().__init__()
        self.setWindowTitle("MViewer")
        self.resize(1280, 800)
        self.setup_ui()
        self.setup_menu()

        status = QStatusBar()
        self.setStatusBar(status)
        status.showMessage("就绪")

    def setup_ui(self):
        splitter = QSplitter(Qt.Orientation.Horizontal)
        self.setCentralWidget(splitter)

        left_widget = QWidget()
        left_layout = QVBoxLayout(left_widget)
        left_layout.setContentsMargins(0, 0, 0, 0)

        self.dir_tree = QTreeView()
        self.dir_model = QStandardItemModel()
        self.dir_tree.setModel(self.dir_model)
        self.dir_tree.setAnimated(False)
        self.dir_tree.setIndentation(12)
        self.dir_tree.setSortingEnabled(True)
        self.dir_tree.sortByColumn(0, Qt.SortOrder.AscendingOrder)
        self.dir_tree.clicked.connect(self._on_dir_clicked)
        self._populate_initial_dirs()

        self.thumbnail_panel = ThumbnailPanel()
        self.thumbnail_panel.currentItemChanged.connect(self._on_thumbnail_changed)

        left_splitter = QSplitter(Qt.Orientation.Vertical)
        left_splitter.addWidget(self.dir_tree)
        left_splitter.addWidget(self.thumbnail_panel)
        left_splitter.setStretchFactor(1, 1)
        left_splitter.setSizes([400, 200])

        left_layout.addWidget(left_splitter)

        self.compare_widget = CompareWidget()
        self.compare_widget.setVisible(False)

        self.image_viewer = ImageViewerWidget()

        self.right_stack = QWidget()
        right_layout = QVBoxLayout(self.right_stack)
        right_layout.setContentsMargins(0, 0, 0, 0)
        right_layout.addWidget(self.image_viewer)
        right_layout.addWidget(self.compare_widget)

        splitter.addWidget(left_widget)
        splitter.addWidget(self.right_stack)

        splitter.setStretchFactor(0, 0)
        splitter.setStretchFactor(1, 1)
        splitter.setSizes([350, 900])

    def _populate_initial_dirs(self):
        """填充初始目录"""
        self.dir_model.setHorizontalHeaderLabels(['目录'])
        home = QDir.homePath()
        home_item = self._create_dir_item(home, Path(home).name)
        self.dir_model.appendRow(home_item)

        drives = QDir.drives()
        for drive in drives:
            drive_item = self._create_dir_item(str(drive.absolutePath()), str(drive.absolutePath()))
            self.dir_model.appendRow(drive_item)

        self.dir_tree.expand(home_item.index())

    def _create_dir_item(self, path, name):
        item = QStandardItem(name)
        item.setData(path, Qt.ItemDataRole.UserRole)
        item.setIcon(QApplication.style().standardIcon(
            QStyle.StandardPixmap.SP_DirIcon))
        return item

    def _on_dir_clicked(self, index: QModelIndex):
        path = self.dir_model.data(index, Qt.ItemDataRole.UserRole)
        if path:
            self.thumbnail_panel.set_directory(path)

    def _on_thumbnail_changed(self, current, previous):
        if current and not self.compare_widget.compare_mode:
            path = current.data(Qt.ItemDataRole.UserRole)
            if path:
                self.image_viewer.set_image(path)

    def _on_open_directory(self):
        dir_path = QFileDialog.getExistingDirectory(self, "选择目录")
        if dir_path:
            self.thumbnail_panel.set_directory(dir_path)

    def _on_compare(self):
        selected_items = self.thumbnail_panel.selectedItems()
        if len(selected_items) < 2:
            QMessageBox.information(self, "提示", "请至少选择 2 张图片进行比较")
            return

        if len(selected_items) > 8:
            QMessageBox.information(self, "提示", "最多支持 8 张图片比较")
            return

        paths = [item.data(Qt.ItemDataRole.UserRole) for item in selected_items]

        self.image_viewer.setVisible(False)
        self.compare_widget.setVisible(True)
        self.compare_widget.set_images(paths)

    def _on_exit_compare(self):
        self.compare_widget.exit_compare()
        self.compare_widget.setVisible(False)
        self.image_viewer.setVisible(True)

    def _on_fullscreen(self):
        if self.isFullScreen():
            self.showNormal()
        else:
            self.showFullScreen()

    def _on_histogram_toggled(self, checked):
        self.image_viewer.show_histogram = checked
        self.image_viewer.update()

        for w in self.compare_widget.image_widgets:
            w.show_histogram = checked
            w.update()

    def _on_about(self):
        QMessageBox.about(
            self, "关于 MViewer",
            "MViewer v0.1.0\n\n图像查看与分析工具\n"
            "支持图片浏览、多图比较、直方图显示、框选统计"
        )

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_Escape:
            if self.isFullScreen():
                self.showNormal()
            elif self.compare_widget.compare_mode:
                self._on_exit_compare()
        super().keyPressEvent(event)

    def setup_menu(self):
        menubar = self.menuBar()

        file_menu = menubar.addMenu("文件")

        open_action = QAction("打开目录...", self)
        open_action.setShortcut(QKeySequence("Ctrl+O"))
        open_action.triggered.connect(self._on_open_directory)
        file_menu.addAction(open_action)

        file_menu.addSeparator()

        exit_action = QAction("退出", self)
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)

        view_menu = menubar.addMenu("视图")

        self.histogram_action = QAction("显示直方图", self)
        self.histogram_action.setCheckable(True)
        self.histogram_action.setChecked(True)
        self.histogram_action.toggled.connect(self._on_histogram_toggled)
        view_menu.addAction(self.histogram_action)

        view_menu.addSeparator()

        compare_action = QAction("比较...", self)
        compare_action.setShortcut(QKeySequence("Ctrl+Shift+C"))
        compare_action.triggered.connect(self._on_compare)
        view_menu.addAction(compare_action)

        exit_compare_action = QAction("退出比较", self)
        exit_compare_action.triggered.connect(self._on_exit_compare)
        view_menu.addAction(exit_compare_action)

        view_menu.addSeparator()

        fullscreen_action = QAction("全屏", self)
        fullscreen_action.setShortcut(QKeySequence("F11"))
        fullscreen_action.triggered.connect(self._on_fullscreen)
        view_menu.addAction(fullscreen_action)

        help_menu = menubar.addMenu("帮助")

        about_action = QAction("关于", self)
        about_action.triggered.connect(self._on_about)
        help_menu.addAction(about_action)


def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    palette = QPalette()
    palette.setColor(QPalette.ColorRole.Window, QColor(45, 45, 45))
    palette.setColor(QPalette.ColorRole.WindowText, QColor(220, 220, 220))
    palette.setColor(QPalette.ColorRole.Base, QColor(30, 30, 30))
    palette.setColor(QPalette.ColorRole.AlternateBase, QColor(40, 40, 40))
    palette.setColor(QPalette.ColorRole.Text, QColor(220, 220, 220))
    palette.setColor(QPalette.ColorRole.Button, QColor(50, 50, 50))
    palette.setColor(QPalette.ColorRole.ButtonText, QColor(220, 220, 220))
    palette.setColor(QPalette.ColorRole.Highlight, QColor(0, 120, 215))
    palette.setColor(QPalette.ColorRole.HighlightedText, QColor(255, 255, 255))
    app.setPalette(palette)

    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())

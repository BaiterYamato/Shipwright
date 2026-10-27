"""Captura Win32 sem Pillow; o codificador PNG também funciona fora do Windows."""
import ctypes
from ctypes import wintypes as wt
import os
from pathlib import Path
import struct
import zlib


def png_rgb(width, height, pixels):
    if width <= 0 or height <= 0 or len(pixels) != width * height * 3:
        raise ValueError("dimensões ou pixels inválidos")
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    stride = width * 3
    raw = b"".join(b"\0" + pixels[y * stride:(y + 1) * stride] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def _apis():
    if os.name != "nt":
        raise OSError("captura e janelas exigem Windows")
    user = ctypes.WinDLL("user32", use_last_error=True)
    gdi = ctypes.WinDLL("gdi32", use_last_error=True)
    callback = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    definitions = (
        (user, "EnumWindows", [callback, wt.LPARAM], wt.BOOL),
        (user, "GetWindowThreadProcessId", [wt.HWND, ctypes.POINTER(wt.DWORD)], wt.DWORD),
        (user, "IsWindowVisible", [wt.HWND], wt.BOOL),
        (user, "GetWindowRect", [wt.HWND, ctypes.POINTER(wt.RECT)], wt.BOOL),
        (user, "GetDC", [wt.HWND], wt.HDC),
        (user, "ReleaseDC", [wt.HWND, wt.HDC], ctypes.c_int),
        (user, "PrintWindow", [wt.HWND, wt.HDC, wt.UINT], wt.BOOL),
        (user, "GetForegroundWindow", [], wt.HWND),
        (user, "SetForegroundWindow", [wt.HWND], wt.BOOL),
        (user, "PostMessageW", [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM], wt.BOOL),
        (gdi, "CreateCompatibleDC", [wt.HDC], wt.HDC),
        (gdi, "CreateDIBSection", [wt.HDC, ctypes.c_void_p, wt.UINT, ctypes.POINTER(ctypes.c_void_p), wt.HANDLE, wt.DWORD], wt.HBITMAP),
        (gdi, "SelectObject", [wt.HDC, wt.HANDLE], wt.HANDLE),
        (gdi, "DeleteObject", [wt.HANDLE], wt.BOOL),
        (gdi, "DeleteDC", [wt.HDC], wt.BOOL),
        (gdi, "BitBlt", [wt.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                         wt.HDC, ctypes.c_int, ctypes.c_int, wt.DWORD], wt.BOOL),
    )
    for library, name, args, result in definitions:
        function = getattr(library, name)
        function.argtypes, function.restype = args, result
    return user, gdi, callback


def window_for_pid(pid):
    user, _, callback = _apis()
    windows = []
    @callback
    def visit(hwnd, _):
        process = wt.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(process))
        rect = wt.RECT()
        if process.value == pid and user.IsWindowVisible(hwnd) and user.GetWindowRect(hwnd, ctypes.byref(rect)):
            area = (rect.right - rect.left) * (rect.bottom - rect.top)
            if area > 0:
                windows.append((area, hwnd))
        return True
    user.EnumWindows(visit, 0)
    if not windows:
        raise RuntimeError("janela do jogo não encontrada")
    return max(windows, key=lambda item: item[0])[1]


def close_window(pid):
    user, _, _ = _apis()
    if not user.PostMessageW(window_for_pid(pid), 0x0010, 0, 0):
        raise ctypes.WinError(ctypes.get_last_error())


def capture_window(pid, path):
    user, gdi, _ = _apis()
    hwnd = window_for_pid(pid)
    rect = wt.RECT()
    if not user.GetWindowRect(hwnd, ctypes.byref(rect)):
        raise ctypes.WinError(ctypes.get_last_error())
    width, height = rect.right - rect.left, rect.bottom - rect.top
    if width <= 0 or height <= 0 or width * height > 40_000_000:
        raise RuntimeError("dimensões da janela inválidas")
    screen = user.GetDC(None)
    dc = gdi.CreateCompatibleDC(screen) if screen else None
    bitmap = previous = None
    focus_used = False
    foreground = user.GetForegroundWindow()
    try:
        if not screen or not dc:
            raise RuntimeError("não foi possível criar o contexto de captura")
        # BITMAPINFOHEADER fixo, DIB BGRA com linhas de cima para baixo.
        info = ctypes.create_string_buffer(struct.pack("<IiiHHIIiiII", 40, width, -height, 1, 32, 0, width * height * 4, 0, 0, 0, 0))
        bits = ctypes.c_void_p()
        bitmap = gdi.CreateDIBSection(screen, info, 0, ctypes.byref(bits), None, 0)
        if not bitmap or not bits.value:
            raise RuntimeError("não foi possível criar o bitmap")
        previous = gdi.SelectObject(dc, bitmap)
        if not previous or previous == ctypes.c_void_p(-1).value:
            raise RuntimeError("não foi possível selecionar o bitmap")
        def rgb():
            bgra = ctypes.string_at(bits, width * height * 4)
            data = bytearray(width * height * 3)
            data[0::3], data[1::3], data[2::3] = bgra[2::4], bgra[1::4], bgra[0::4]
            return bytes(data)
        printed = user.PrintWindow(hwnd, dc, 2)
        pixels = rgb()
        if not printed or not any(pixels):
            focus_used = True
            if not user.SetForegroundWindow(hwnd):
                raise RuntimeError("PrintWindow falhou e o Windows recusou o foco para a captura")
            # O retângulo pode mudar quando a janela recebe foco.
            user.GetWindowRect(hwnd, ctypes.byref(rect))
            if not gdi.BitBlt(dc, 0, 0, width, height, screen, rect.left, rect.top, 0x00CC0020 | 0x40000000):
                raise RuntimeError("BitBlt falhou")
            pixels = rgb()
            if not any(pixels):
                raise RuntimeError("a captura continuou preta")
        data = png_rgb(width, height, pixels)
        Path(path).write_bytes(data)
        return {"path": str(path), "focus_used": focus_used, "width": width, "height": height, "png": data}
    finally:
        if focus_used and foreground:
            user.SetForegroundWindow(foreground)
        if previous and previous != ctypes.c_void_p(-1).value:
            gdi.SelectObject(dc, previous)
        if bitmap:
            gdi.DeleteObject(bitmap)
        if dc:
            gdi.DeleteDC(dc)
        if screen:
            user.ReleaseDC(None, screen)

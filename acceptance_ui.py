"""Exercise independent rates and saved window placement in an isolated copy."""

import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import shutil
import subprocess
import time


ROOT = Path(__file__).resolve().parent
TEST_DIR = ROOT / "build" / "acceptance-ui"
TEST_DIR.mkdir(parents=True, exist_ok=True)
EXE = TEST_DIR / "videotime.exe"
shutil.copy2(ROOT / "build" / "portable" / "videotime.exe", EXE)
INI = TEST_DIR / "videotime.ini"
if INI.exists():
    INI.unlink()

user32 = C.WinDLL("user32", use_last_error=True)
kernel32 = C.WinDLL("kernel32", use_last_error=True)
WNDENUMPROC = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)


class RECT(C.Structure):
    _fields_ = [("left", W.LONG), ("top", W.LONG), ("right", W.LONG), ("bottom", W.LONG)]


user32.EnumWindows.argtypes = [WNDENUMPROC, W.LPARAM]
user32.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
user32.GetWindowTextLengthW.argtypes = [W.HWND]
user32.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
user32.GetDlgItem.argtypes = [W.HWND, C.c_int]
user32.GetDlgItem.restype = W.HWND
user32.SetWindowTextW.argtypes = [W.HWND, W.LPCWSTR]
user32.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user32.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user32.IsWindowEnabled.argtypes = [W.HWND]
user32.IsDlgButtonChecked.argtypes = [W.HWND, C.c_int]
user32.GetWindowRect.argtypes = [W.HWND, C.POINTER(RECT)]
user32.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
kernel32.GetPrivateProfileIntW.argtypes = [W.LPCWSTR, W.LPCWSTR, W.UINT, W.LPCWSTR]


def title(hwnd):
    length = user32.GetWindowTextLengthW(hwnd)
    text = C.create_unicode_buffer(length + 1)
    user32.GetWindowTextW(hwnd, text, length + 1)
    return text.value


def windows_for(pid):
    found = []

    @WNDENUMPROC
    def visit(hwnd, _):
        actual = W.DWORD()
        user32.GetWindowThreadProcessId(hwnd, C.byref(actual))
        if actual.value == pid:
            found.append((hwnd, title(hwnd)))
        return True

    user32.EnumWindows(visit, 0)
    return found


def wait_for(predicate, label, seconds=10):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        result = predicate()
        if result:
            return result
        time.sleep(0.1)
    raise RuntimeError(f"Timed out waiting for {label}")


def child_text(hwnd, control_id):
    control = user32.GetDlgItem(hwnd, control_id)
    if not control:
        raise RuntimeError(f"Missing control {control_id}")
    return title(control)


def bounds(hwnd):
    rect = RECT()
    if not user32.GetWindowRect(hwnd, C.byref(rect)):
        raise RuntimeError("GetWindowRect failed")
    return (rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top)


def launch():
    process = subprocess.Popen([str(EXE)])
    hwnd = wait_for(lambda: next((hwnd for hwnd, text in windows_for(process.pid)
                                  if text == "VideoTime 1.2.0"), None), "main window")
    return process, hwnd


def open_settings(pid, hwnd):
    user32.PostMessageW(hwnd, 0x0111, 109, 0)  # WM_COMMAND / ID_SETTINGS
    return wait_for(lambda: next((dialog for dialog, text in windows_for(pid)
                                  if text == "VideoTime Settings"), None), "Settings dialog")


def save_settings(dialog, pid):
    user32.PostMessageW(dialog, 0x0111, 1, 0)  # WM_COMMAND / IDOK
    wait_for(lambda: not any(hwnd == dialog for hwnd, _ in windows_for(pid)), "Settings close")


def close(process, hwnd):
    user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
    process.wait(timeout=10)


process = None
try:
    process, hwnd = launch()
    video = user32.GetDlgItem(hwnd, 108)
    audio = user32.GetDlgItem(hwnd, 115)
    assert child_text(hwnd, 108) == "" and child_text(hwnd, 115) == ""
    assert user32.IsWindowEnabled(video) and user32.IsWindowEnabled(audio)
    user32.SetWindowTextW(video, "40")
    assert child_text(hwnd, 115) == "", "Video rate changed audio rate"
    user32.SetWindowTextW(audio, "80")
    assert child_text(hwnd, 108) == "40" and child_text(hwnd, 115) == "80"

    dialog = open_settings(process.pid, hwnd)
    assert child_text(dialog, 208) == "Remember window size and position"
    assert user32.IsDlgButtonChecked(dialog, 208) == 0
    assert user32.IsWindowEnabled(user32.GetDlgItem(dialog, 207))
    user32.SendMessageW(user32.GetDlgItem(dialog, 208), 0x00F5, 0, 0)  # BM_CLICK
    save_settings(dialog, process.pid)
    assert kernel32.GetPrivateProfileIntW("Settings", "RememberWindow", 0, str(INI)) == 1

    expected = (105, 115, 1300, 700)
    assert user32.SetWindowPos(hwnd, None, *expected, 0x0004)  # SWP_NOZORDER
    actual_before = bounds(hwnd)
    close(process, hwnd)
    process = None

    process, hwnd = launch()
    actual_after = bounds(hwnd)
    assert actual_after == actual_before, (actual_before, actual_after)
    dialog = open_settings(process.pid, hwnd)
    assert user32.IsDlgButtonChecked(dialog, 208) == 1
    user32.SendMessageW(user32.GetDlgItem(dialog, 208), 0x00F5, 0, 0)
    save_settings(dialog, process.pid)
    assert kernel32.GetPrivateProfileIntW("Settings", "RememberWindow", 1, str(INI)) == 0
    close(process, hwnd)
    process = None

    print(json.dumps({"independent_rates": ["40", "80"], "saved_bounds": actual_after,
                      "remember_opt_in_and_out": "passed"}))
finally:
    if process is not None and process.poll() is None:
        process.terminate()
        process.wait(timeout=5)

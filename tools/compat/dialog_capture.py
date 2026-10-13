"""Observe worker-owned Windows dialogs and perform only scripted responses.

No renderer hooks or suppression: title, controls and chosen button become part
of the differential result. Unrecognized dialogs remain open and fail by timeout.
"""
import ctypes as C
import json
import os
import threading


def validate_actions(actions):
    if not isinstance(actions, list) or len(actions) > 32:
        raise ValueError("dialog_actions must be a list of at most 32 actions")
    for action in actions:
        if (not isinstance(action, dict) or set(action) != {"title", "text_prefix", "button"}
                or not isinstance(action["title"], str) or not action["title"]
                or not isinstance(action["text_prefix"], str) or not action["text_prefix"]
                or action["button"] not in ("ok", "cancel")):
            raise ValueError("dialog action needs title, nonempty text_prefix and ok/cancel button")


class DialogCapture:
    def __init__(self, actions, destination):
        validate_actions(actions)
        self.actions, self.destination = actions, destination
        self.events, self.error = [], None
        self.stop = threading.Event()
        self.thread = None
        self.consumed = 0

    def __enter__(self):
        if os.name != "nt":
            if self.actions:
                raise RuntimeError("scripted native dialogs require Windows")
            return self
        self.api = C.WinDLL("user32", use_last_error=True)
        self.callback = C.WINFUNCTYPE(C.c_int, C.c_void_p, C.c_ssize_t)
        for name, args, result in (
            ("EnumThreadWindows", [C.c_uint32, self.callback, C.c_ssize_t], C.c_int),
            ("EnumChildWindows", [C.c_void_p, self.callback, C.c_ssize_t], C.c_int),
            ("GetWindowTextLengthW", [C.c_void_p], C.c_int),
            ("GetWindowTextW", [C.c_void_p, C.c_wchar_p, C.c_int], C.c_int),
            ("GetClassNameW", [C.c_void_p, C.c_wchar_p, C.c_int], C.c_int),
            ("GetDlgCtrlID", [C.c_void_p], C.c_int),
            ("GetWindowLongW", [C.c_void_p, C.c_int], C.c_int32),
            ("IsWindowVisible", [C.c_void_p], C.c_int),
            ("GetDlgItem", [C.c_void_p, C.c_int], C.c_void_p),
            ("PostMessageW", [C.c_void_p, C.c_uint, C.c_size_t, C.c_ssize_t], C.c_int),
        ):
            function = getattr(self.api, name)
            function.argtypes, function.restype = args, result
        self.thread_id = C.windll.kernel32.GetCurrentThreadId()
        self.thread = threading.Thread(target=self.watch, daemon=True)
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.stop.set()
        if self.thread:
            self.thread.join(timeout=2)
            if self.thread.is_alive():
                self.error = "dialog observer did not stop"
        if self.consumed != len(self.actions):
            self.error = self.error or "scripted dialog was not observed"

    def text(self, window):
        length = self.api.GetWindowTextLengthW(window)
        if not 0 <= length <= 65535:
            raise RuntimeError("dialog text exceeds capture limit")
        buffer = C.create_unicode_buffer(length + 1)
        self.api.GetWindowTextW(window, buffer, len(buffer))
        return buffer.value

    def class_name(self, window):
        buffer = C.create_unicode_buffer(256)
        self.api.GetClassNameW(window, buffer, len(buffer))
        return buffer.value

    def windows(self, parent=None):
        windows = []
        @self.callback
        def collect(window, _):
            windows.append(window)
            return 1
        if parent is None:
            self.api.EnumThreadWindows(self.thread_id, collect, 0)
        else:
            self.api.EnumChildWindows(parent, collect, 0)
        return windows

    def watch(self):
        seen = set()
        try:
            while not self.stop.is_set():
                current = set(self.windows())
                seen.intersection_update(current)
                for window in sorted(current - seen):
                    if self.class_name(window) != "#32770" or not self.api.IsWindowVisible(window):
                        continue
                    children = self.windows(window)
                    controls = []
                    for child in children:
                        name = self.class_name(child)
                        style = self.api.GetWindowLongW(child, -16) & 0xffffffff  # GWL_STYLE
                        # SS_ICON's WM_GETTEXT can return an opaque icon handle,
                        # not text. Record its control type, never that handle.
                        icon = name == "Static" and style & 0x1f == 3
                        controls.append({"class": name, "id": self.api.GetDlgCtrlID(child),
                                         "style": style, "text": None if icon else self.text(child)})
                    # MessageBox creates controls before entering its modal loop;
                    # retry an incompletely created window on the next poll.
                    if not any(c["class"] == "Button" for c in controls):
                        continue
                    title = self.text(window)
                    action = self.actions[self.consumed] if self.consumed < len(self.actions) else None
                    matches = (action is not None and title == action["title"] and
                               any(c["class"] == "Static" and isinstance(c["text"], str) and c["text"].startswith(action["text_prefix"])
                                   for c in controls))
                    event = {"title": title, "class": "#32770", "controls": controls,
                             "response": action["button"] if matches else None}
                    self.events.append(event)
                    seen.add(window)
                    # Preserve the warning even when the worker later times out.
                    (self.destination / "dialogs.json").write_text(json.dumps(self.events, indent=2) + "\n", encoding="utf-8")
                    if matches:
                        button = self.api.GetDlgItem(window, 1 if action["button"] == "ok" else 2)
                        if not button or not self.api.PostMessageW(button, 0x00F5, 0, 0):  # BM_CLICK
                            raise RuntimeError("could not click scripted dialog button")
                        self.consumed += 1
                    else:
                        self.error = "unexpected native dialog"
                self.stop.wait(0.02)
        except Exception as error:
            self.error = f"{type(error).__name__}: {error}"

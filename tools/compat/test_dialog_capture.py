import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from dialog_capture import DialogCapture


class EnumerationAPI:
    def __init__(self):
        self.called=[]
    def EnumWindows(self, callback, _):
        self.called.append('process')
        for hwnd in (10,20,30): callback(hwnd,0)
    def GetWindowThreadProcessId(self, hwnd, output):
        output._obj.value={10:42,20:99,30:42}[hwnd]
        return 7
    def EnumThreadWindows(self, thread, callback, _):
        self.called.append(('thread',thread));callback(40,0)
    def EnumChildWindows(self,parent,callback,_):
        self.called.append(('child',parent));callback(50,0)


class ProcessDialogTests(unittest.TestCase):
    def capture(self,pid=None):
        capture=DialogCapture([],Path('unused'),process_id=pid)
        capture.api=EnumerationAPI();capture.callback=lambda function:function;capture.thread_id=123
        return capture
    def test_process_filter_excludes_other_processes_and_preserves_child_scope(self):
        capture=self.capture(42)
        self.assertEqual(capture.windows(),[10,30])
        self.assertEqual(capture.windows(10),[50])
        self.assertEqual(capture.api.called,['process',('child',10)])
    def test_default_retains_thread_scope(self):
        capture=self.capture()
        self.assertEqual(capture.windows(),[40])
        self.assertEqual(capture.api.called,[('thread',123)])
    def test_recycled_hwnd_changed_text_is_new_but_unchanged_identity_responds_once(self):
        class Polls:
            index=0
            def is_set(self):return self.index>=4
            def wait(self,_):self.index+=1
        with TemporaryDirectory() as temp:
            capture=DialogCapture([{"title":"Warning","text_prefix":"A","button":"ok"},
                                   {"title":"Warning","text_prefix":"B","button":"cancel"}],Path(temp))
            polls=Polls();capture.stop=polls
            capture.windows=lambda parent=None: [111] if parent is None else [222,333,444]
            capture.class_name=lambda hwnd: "#32770" if hwnd==111 else "Static" if hwnd==444 else "Button"
            capture.text=lambda hwnd: "Warning" if hwnd==111 else ("A" if polls.index<2 else "B") if hwnd==444 else "OK" if hwnd==222 else "Cancel"
            capture.api=SimpleNamespace(IsWindowVisible=lambda _:True,GetWindowLongW=lambda *_:0,
                                        GetDlgCtrlID=lambda hwnd:{222:1,333:2,444:65535}[hwnd])
            capture.scripted_button=lambda _,action:222 if action["button"]=="ok" else 333
            responses=[]
            capture.post_response=lambda window,button,action:responses.append((window,button,action["button"]))
            capture.watch()
            self.assertIsNone(capture.error)
            self.assertEqual(capture.consumed,2)
            self.assertEqual(responses,[(111,222,"ok"),(111,333,"cancel")])
            self.assertEqual([event["controls"][2]["text"] for event in capture.events],["A","B"])

    def test_invalid_process_identity_rejected(self):
        for pid in (True,False,0,-1,2**32,'42'):
            with self.assertRaises(ValueError):self.capture(pid)


if __name__=='__main__':unittest.main()

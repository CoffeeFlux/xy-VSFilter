import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location("audit_rust_boundary", Path(__file__).with_name("audit_rust_boundary.py"))
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class BoundaryInventoryTests(unittest.TestCase):
    def test_comments_literals_and_lifetimes_do_not_invent_unsafe_sites(self):
        source = r'''// unsafe { fake() }
/* unsafe fn hidden() { /* nested */ } */
const A: &str = r##"unsafe { \" text }"##;
const B: char = '}';
const C: &str = "unsafe \\";
fn borrow<'a>(x: &'a u8) -> &'a u8 { x }
unsafe fn actual() { unsafe { operation(); } }
'''
        code = MODULE.mask_noncode(source)
        self.assertEqual(code.count("unsafe"), 2)
        self.assertEqual(code.count("{"), code.count("}"))
        self.assertIn("borrow<'a>", code)
        self.assertEqual(len(code), len(source))
        self.assertEqual(code.count("\n"), source.count("\n"))

    def test_test_module_in_middle_does_not_hide_later_production(self):
        source = '''fn first() { unsafe { one(); } }
#[cfg(test)]
mod tests { fn test() { let text = "}"; unsafe { two(); } } }
fn last() { unsafe { three(); } }
'''
        code = MODULE.mask_noncode(source)
        ranges = MODULE.test_ranges(code)
        self.assertEqual(len(ranges), 1)
        start, end = ranges[0]
        self.assertIn("unsafe", code[start:end])
        self.assertIn("first", code[:start])
        self.assertIn("last", code[end:])
        self.assertEqual((code[:start] + code[end:]).count("unsafe"), 2)


if __name__ == '__main__':
    unittest.main()

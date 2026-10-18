// SPDX-License-Identifier: GPL-2.0-or-later
//! Windows MSXML boundary for USF. System objects are owned on the calling
//! thread, outside document-registry admission; no COM pointer enters the core.
//! Layouts are checked against tools/compat/msxml_abi_native.cpp on x86/x64.
use alloc::{vec, vec::Vec};
use core::{
    ffi::c_void,
    mem,
    ptr::{self, NonNull},
};

type Hresult = i32;
type Units = Vec<u16>;
const E_OUTOFMEMORY: Hresult = 0x8007000eu32 as i32;
const VT_BSTR: u16 = 8;
const VT_UI1: u16 = 17;
const VT_ARRAY: u16 = 0x2000;
#[derive(Debug, PartialEq, Eq)]
pub(crate) enum Error {
    NativeException(Hresult),
    Protocol,
    CountOverflow,
    Unresolved,
}
#[repr(C)]
struct Guid {
    a: u32,
    b: u16,
    c: u16,
    d: [u8; 8],
}
const fn msxml_guid(a: u32) -> Guid {
    Guid {
        a,
        b: 0x7b36,
        c: 0x11d2,
        d: [0xb2, 0x0e, 0, 0xc0, 0x4f, 0x98, 0x3e, 0x60],
    }
}
const CLSID_DOCUMENT: Guid = msxml_guid(0x2933bf90);
const IID_DOCUMENT: Guid = msxml_guid(0x2933bf81);
const IID_NODE: Guid = msxml_guid(0x2933bf80);

#[repr(C)]
#[derive(Clone, Copy)]
struct Record {
    data: *mut c_void,
    info: *mut c_void,
}
#[repr(C)]
#[derive(Clone, Copy)]
union VariantData {
    bstr: *mut u16,
    array: *mut ArrayRaw,
    record: Record,
    integer: i64,
}
// The i64 union member gives the SDK's natural alignment8 on both MSVC targets.
// Do not add repr(align(8)): despite identical offsets, Rust then passes this
// aggregate indirectly on x86, which breaks load(VARIANT)'s by-value ABI.
#[repr(C)]
#[derive(Clone, Copy)]
struct VariantRaw {
    vt: u16,
    reserved: [u16; 3],
    data: VariantData,
}
impl VariantRaw {
    fn empty() -> Self {
        Self {
            vt: 0,
            reserved: [0; 3],
            data: VariantData {
                record: Record {
                    data: ptr::null_mut(),
                    info: ptr::null_mut(),
                },
            },
        }
    }
}
#[repr(C)]
struct Bound {
    elements: u32,
    lower: i32,
}
#[repr(C)]
struct ArrayRaw {
    dimensions: u16,
    features: u16,
    element_size: u32,
    locks: u32,
    data: *mut c_void,
    bounds: [Bound; 1],
}

type Query = unsafe extern "system" fn(*mut c_void, *const Guid, *mut *mut c_void) -> Hresult;
type Count = unsafe extern "system" fn(*mut c_void) -> u32;
type BstrGetter = unsafe extern "system" fn(*mut c_void, *mut *mut u16) -> Hresult;
type NodeGetter = unsafe extern "system" fn(*mut c_void, *mut *mut c_void) -> Hresult;
#[repr(C)]
struct UnknownTable {
    query: Query,
    add_ref: Count,
    release: Count,
}
// Unused method slots are opaque pointer-sized fields, never read or invoked.
// MSVC function/data pointer sizes and each used offset are SDK-probed below.
#[repr(C)]
struct NodeTable {
    unknown: UnknownTable,
    dispatch: [*const c_void; 4],
    name: BstrGetter,
    value: unsafe extern "system" fn(*mut c_void, *mut VariantRaw) -> Hresult,
    unused9_12: [*const c_void; 4],
    first_child: NodeGetter,
    unused14_15: [*const c_void; 2],
    next_sibling: NodeGetter,
    attributes: NodeGetter,
    unused18_25: [*const c_void; 8],
    text: BstrGetter,
    unused27_33: [*const c_void; 7],
    xml: BstrGetter,
    unused35_42: [*const c_void; 8],
}
#[repr(C)]
struct DocumentTable {
    node: NodeTable,
    unused43_57: [*const c_void; 15],
    load: unsafe extern "system" fn(*mut c_void, VariantRaw, *mut i16) -> Hresult,
    unused59_62: [*const c_void; 4],
    set_async: unsafe extern "system" fn(*mut c_void, i16) -> Hresult,
    unused64_75: [*const c_void; 12],
}
#[repr(C)]
struct MapTable {
    unknown: UnknownTable,
    unused3_13: [*const c_void; 11],
    next: NodeGetter,
    unused15_16: [*const c_void; 2],
}

#[link(name = "ole32")]
extern "system" {
    fn CoCreateInstance(
        class: *const Guid,
        outer: *mut c_void,
        context: u32,
        iid: *const Guid,
        object: *mut *mut c_void,
    ) -> Hresult;
}
#[link(name = "oleaut32")]
extern "system" {
    fn SysAllocString(text: *const u16) -> *mut u16;
    fn SysFreeString(text: *mut u16);
    fn SysStringLen(text: *const u16) -> u32;
    fn SafeArrayCreate(kind: u16, dimensions: u32, bounds: *mut Bound) -> *mut ArrayRaw;
    fn SafeArrayLock(array: *mut ArrayRaw) -> Hresult;
    fn SafeArrayUnlock(array: *mut ArrayRaw) -> Hresult;
    fn SafeArrayDestroy(array: *mut ArrayRaw) -> Hresult;
    fn SafeArrayCopy(array: *mut ArrayRaw, copy: *mut *mut ArrayRaw) -> Hresult;
    fn SafeArrayGetVartype(array: *mut ArrayRaw, kind: *mut u16) -> Hresult;
    fn VariantClear(value: *mut VariantRaw) -> Hresult;
}
#[link(name = "user32")]
extern "system" {
    fn CharLowerBuffW(text: *mut u16, length: u32) -> u32;
}
extern "C" {
    fn wcstol(text: *const u16, end: *mut *mut u16, base: i32) -> i32;
    fn _wcsicmp(a: *const u16, b: *const u16) -> i32;
}

/// Owns exactly one COM reference. NonNull keeps this owner !Send/!Sync.
/// Construction is private and only follows a system COM out-parameter.
struct Com(NonNull<c_void>);
impl Com {
    // SAFETY contract: nonnull raw is a live owned interface reference with
    // the IUnknown prefix, returned by the SDK-typed system calls in this module.
    unsafe fn take(raw: *mut c_void) -> Option<Self> {
        NonNull::new(raw).map(Self)
    }
    fn raw(&self) -> *mut c_void {
        self.0.as_ptr()
    }
    fn unknown(&self) -> &UnknownTable {
        // SAFETY: every live COM interface begins with a pointer to an immutable
        // vtable whose first three slots are IUnknown. self owns its reference.
        unsafe { &**(self.raw().cast::<*const UnknownTable>()) }
    }
}
impl Drop for Com {
    fn drop(&mut self) {
        // SAFETY: exactly one matching Release for this owned reference; no use
        // after release, and destruction occurs on the original calling thread.
        unsafe {
            (self.unknown().release)(self.raw());
        }
    }
}
struct Bstring(*mut u16);
impl Bstring {
    fn empty() -> Self {
        Self(ptr::null_mut())
    }
    fn lower(&mut self) {
        // SAFETY: this is an owned BSTR from MSXML/OleAut32 (or null). Its full
        // SysStringLen range is writable, including any embedded NUL. This is
        // CComBSTR::ToLower's desktop Unicode implementation, not Rust casing.
        unsafe {
            let len = SysStringLen(self.0);
            if len != 0 {
                CharLowerBuffW(self.0, len);
            }
        }
    }
    fn units(&self) -> Result<Units, Error> {
        // SAFETY: the owned BSTR remains live and is not mutated during copying.
        unsafe { copy_bstr(self.0) }
    }
}
impl Drop for Bstring {
    fn drop(&mut self) {
        // SAFETY: exactly one SysFreeString for each owned BSTR; null is allowed.
        unsafe {
            SysFreeString(self.0);
        }
    }
}
/// Copy CStringW(BSTR)'s first-NUL view, retaining arbitrary UTF-16 units.
unsafe fn copy_bstr(raw: *const u16) -> Result<Units, Error> {
    if raw.is_null() {
        return Ok(Vec::new());
    }
    // SAFETY: caller guarantees a live BSTR; SysStringLen reads its own prefix.
    let length = unsafe { SysStringLen(raw) } as usize;
    if length > isize::MAX as usize / 2 {
        return Err(Error::CountOverflow);
    }
    // SAFETY: checked slice representation bound, initialized BSTR payload and
    // lifetime supplied by caller. No borrowed slice escapes this function.
    let data = unsafe { core::slice::from_raw_parts(raw, length) };
    let end = data.iter().position(|&u| u == 0).unwrap_or(data.len());
    Ok(data[..end].to_vec())
}
struct Variant(VariantRaw);
impl Variant {
    fn empty() -> Self {
        Self(VariantRaw::empty())
    }
    fn bstr_units(&self) -> Result<Option<Units>, Error> {
        if self.0.vt != VT_BSTR {
            return Ok(None);
        }
        // SAFETY: VT_BSTR identifies this union member and Variant owns the
        // system-returned BSTR until it is cleared. Only a copied view escapes.
        unsafe { copy_bstr(self.0.data.bstr).map(Some) }
    }
}
impl Drop for Variant {
    fn drop(&mut self) {
        // SAFETY: initialized VARIANT with SDK layout. System getters transfer
        // ownership to this value; VariantClear is called once, as in ATL.
        unsafe {
            VariantClear(&mut self.0);
        }
    }
}
struct Array {
    raw: NonNull<ArrayRaw>,
    locked: bool,
}
impl Array {
    fn new(bytes: &[u8]) -> Result<Option<Self>, Error> {
        let elements = u32::try_from(bytes.len()).map_err(|_| Error::CountOverflow)?;
        let mut bound = Bound { elements, lower: 0 };
        // SAFETY: one initialized bound describes a byte array; the returned
        // SAFEARRAY is owned here and cannot alias the caller's input buffer.
        let raw = unsafe { SafeArrayCreate(VT_UI1, 1, &mut bound) };
        let Some(raw) = NonNull::new(raw) else {
            return Ok(None);
        };
        let mut array = Self { raw, locked: false };
        // SAFETY: live newly created SAFEARRAY. Lock/Unlock protect its data.
        if unsafe { SafeArrayLock(array.raw.as_ptr()) } < 0 {
            return Ok(None);
        }
        array.locked = true;
        if !bytes.is_empty() {
            // SAFETY: SafeArrayCreate allocated elements bytes with element
            // type UI1. The successful lock keeps pvData stable; source and
            // destination allocations are distinct and this is the exact bound.
            unsafe {
                ptr::copy_nonoverlapping(
                    bytes.as_ptr(),
                    (*array.raw.as_ptr()).data.cast::<u8>(),
                    bytes.len(),
                );
            }
        }
        Ok(Some(array))
    }
    fn variant_copy(&self) -> Result<Variant, Error> {
        let mut copy = ptr::null_mut();
        // SAFETY: source is a live locked SAFEARRAY; copy is a writable out slot.
        let hr = unsafe { SafeArrayCopy(self.raw.as_ptr(), &mut copy) };
        if hr < 0 {
            return Err(Error::NativeException(hr));
        }
        let copy = NonNull::new(copy).ok_or(Error::Unresolved)?;
        let mut variant = Variant::empty();
        variant.0.vt = VT_ARRAY | VT_UI1;
        variant.0.data.array = copy.as_ptr();
        let mut kind = 0;
        // SAFETY: original UI1 SAFEARRAY remains live. ATL queries this after
        // copying; the VARIANT guard already owns the successful copied array.
        let hr = unsafe { SafeArrayGetVartype(self.raw.as_ptr(), &mut kind) };
        if hr < 0 {
            return Err(Error::NativeException(hr));
        }
        if kind != VT_UI1 {
            return Err(Error::Unresolved);
        }
        Ok(variant)
    }
}
impl Drop for Array {
    fn drop(&mut self) {
        // SAFETY: one owner; a successful lock is balanced before destruction.
        // On an unexpected Unlock failure, retain the allocation rather than
        // destroying storage still reported locked (ATL also skips Destroy).
        unsafe {
            if !self.locked || SafeArrayUnlock(self.raw.as_ptr()) >= 0 {
                SafeArrayDestroy(self.raw.as_ptr());
            }
        }
    }
}
pub(crate) struct Input {
    variant: Variant,
    _original: Option<Array>,
}
impl Input {
    pub(crate) fn bytes(bytes: &[u8]) -> Result<Option<Self>, Error> {
        let Some(original) = Array::new(bytes)? else {
            return Ok(None);
        };
        let variant = original.variant_copy()?;
        Ok(Some(Self {
            variant,
            _original: Some(original),
        }))
    }
    pub(crate) fn path(path: Option<&[u16]>) -> Result<Self, Error> {
        let terminated = path.map(terminated);
        let raw = terminated.as_ref().map_or(ptr::null(), |s| s.as_ptr());
        // SAFETY: raw is either null (original null-path case) or an owned,
        // terminated UTF-16 buffer. SysAllocString uses the first-NUL prefix.
        let bstr = unsafe { SysAllocString(raw) };
        if bstr.is_null() && path.is_some() {
            return Err(Error::NativeException(E_OUTOFMEMORY));
        }
        let mut variant = Variant::empty();
        variant.0.vt = VT_BSTR;
        variant.0.data.bstr = bstr;
        Ok(Self {
            variant,
            _original: None,
        })
    }
}
struct Node(Com);
impl Node {
    fn table(&self) -> &NodeTable {
        // SAFETY: this owner was returned for IXMLDOMNode. SDK-probed layout and
        // its retained COM reference make the immutable vtable live here.
        unsafe { &**self.0.raw().cast::<*const NodeTable>() }
    }
    fn name_value(&self) -> (Bstring, Variant) {
        let mut name = Bstring::empty();
        let mut value = Variant::empty();
        // SAFETY: live node and initialized writable BSTR/VARIANT out slots.
        // Preserve native getter order and deliberately ignored HRESULTs.
        unsafe {
            (self.table().name)(self.0.raw(), &mut name.0);
        }
        name.lower();
        // SAFETY: same live node; Variant guard clears any returned value.
        unsafe {
            (self.table().value)(self.0.raw(), &mut value.0);
        }
        (name, value)
    }
    fn child(&self, next: bool) -> Option<Self> {
        let mut raw = ptr::null_mut();
        let method = if next {
            self.table().next_sibling
        } else {
            self.table().first_child
        };
        // SAFETY: SDK-typed getter on a live node, initialized out slot. A
        // nonnull result owns one node reference; null is the end sentinel.
        unsafe {
            method(self.0.raw(), &mut raw);
            Com::take(raw).map(Self)
        }
    }
    fn text(&self, xml: bool) -> Result<Units, Error> {
        let mut value = Bstring::empty();
        let method = if xml {
            self.table().xml
        } else {
            self.table().text
        };
        // SAFETY: live node and owned BSTR out slot, freed after copying.
        unsafe {
            method(self.0.raw(), &mut value.0);
        }
        value.units()
    }
    fn attribute(&self, key: &[u16]) -> Result<Units, Error> {
        let mut raw = ptr::null_mut();
        // SAFETY: live node; the out slot receives an owned named-node-map ref.
        let hr = unsafe { (self.table().attributes)(self.0.raw(), &mut raw) };
        // SAFETY: nonnull result from the typed system COM getter is owned here.
        let map = unsafe { Com::take(raw) };
        if hr < 0 {
            return Ok(Vec::new());
        }
        let Some(map) = map else {
            return Ok(Vec::new());
        };
        // SAFETY: get_attributes returned IXMLDOMNamedNodeMap; its vtable stays
        // live for this owner's lifetime and SDK layout is checked below.
        let table = unsafe { &**map.raw().cast::<*const MapTable>() };
        loop {
            let mut raw = ptr::null_mut();
            // SAFETY: live map, initialized out slot. nextNode transfers one
            // IXMLDOMNode ref when nonnull; it is dropped before the next call.
            let node = unsafe {
                (table.next)(map.raw(), &mut raw);
                Com::take(raw).map(Node)
            };
            let Some(node) = node else {
                break;
            };
            let (name, value) = node.name_value();
            if name.units()? == key {
                if let Some(value) = value.bstr_units()? {
                    return Ok(value);
                }
            }
        }
        Ok(Vec::new())
    }
}
/// Field drop order retains source VARIANT/SAFEARRAY throughout traversal,
/// dropping nodes and document before the copied VARIANT and original array.
pub(crate) struct Xml {
    nodes: Vec<Option<Node>>,
    _document: Com,
    _input: Input,
}
impl Xml {
    pub(crate) fn load(input: Input) -> Result<Option<Self>, Error> {
        let mut raw = ptr::null_mut();
        // SAFETY: SDK GUIDs, no aggregation, CLSCTX_ALL as CComPtr::CoCreateInstance,
        // and one initialized writable interface out slot. Host initializes COM.
        let hr = unsafe {
            CoCreateInstance(
                &CLSID_DOCUMENT,
                ptr::null_mut(),
                23,
                &IID_DOCUMENT,
                &mut raw,
            )
        };
        // SAFETY: CoCreateInstance's nonnull interface result owns one reference.
        let document = unsafe { Com::take(raw) };
        if hr < 0 {
            return Ok(None);
        }
        let document = document.ok_or(Error::Unresolved)?;
        // SAFETY: the creation requested IID_IXMLDOMDocument and owns its reference.
        let table = unsafe { &**document.raw().cast::<*const DocumentTable>() };
        // SAFETY: live document and exact VARIANT_BOOL false parameter.
        if unsafe { (table.set_async)(document.raw(), 0) } < 0 {
            return Ok(None);
        }
        let mut accepted = 0i16;
        // SAFETY: initialized SDK-layout VARIANT passed by value as specified;
        // Input owns all referenced resources until after document destruction.
        let hr = unsafe { (table.load)(document.raw(), input.variant.0, &mut accepted) };
        if hr < 0 || accepted != -1 {
            return Ok(None);
        }
        let mut root = ptr::null_mut();
        // SAFETY: live document supports IUnknown, initialized out slot and node IID.
        let hr = unsafe { (table.node.unknown.query)(document.raw(), &IID_NODE, &mut root) };
        // SAFETY: QueryInterface supplies an owned IXMLDOMNode reference when nonnull.
        let root = unsafe { Com::take(root).map(Node) };
        if hr < 0 || root.is_none() {
            return Err(Error::Unresolved);
        }
        Ok(Some(Self {
            nodes: vec![None, root],
            _document: document,
            _input: input,
        }))
    }
    /// Each call is outside registry admission and returns only owned units or
    /// bounded integer node IDs. GetAttrib enumerates anew for every request.
    pub(crate) fn service(
        &mut self,
        op: u32,
        index: u32,
        text: &[u16],
    ) -> Result<(i32, Units), Error> {
        if op >= 7 {
            let value = match op {
                7 => scalar(1, text, 0)?,
                8 => number(text, 10).0,
                9 => scalar(2, text, 0)?,
                10 => compare(text, &"Default".encode_utf16().collect::<Units>()),
                _ => return Err(Error::Protocol),
            };
            return Ok((value, Vec::new()));
        }
        let node = self
            .nodes
            .get(index as usize)
            .and_then(Option::as_ref)
            .ok_or(Error::Protocol)?;
        match op {
            1 => {
                let (name, _value) = node.name_value();
                Ok((0, name.units()?))
            }
            2 | 3 => {
                let child = node.child(op == 3);
                if child.is_none() {
                    return Ok((0, Vec::new()));
                }
                let id = i32::try_from(self.nodes.len()).map_err(|_| Error::CountOverflow)?;
                self.nodes.push(child);
                Ok((id, Vec::new()))
            }
            4 => Ok((0, node.attribute(text)?)),
            5 | 6 => Ok((0, node.text(op == 6)?)),
            _ => Err(Error::Protocol),
        }
    }
}
fn terminated(input: &[u16]) -> Units {
    let mut out = input.to_vec();
    out.push(0);
    out
}
fn number(input: &[u16], base: i32) -> (i32, u32) {
    let input = terminated(input);
    let mut end = ptr::null_mut();
    // SAFETY: terminated owned UTF-16 input, valid end-pointer slot; callers use
    // base10 or16. Windows long is32bits on both SDK-probed MSVC architectures.
    let value = unsafe { wcstol(input.as_ptr(), &mut end, base) };
    (value, u32::from(end.cast_const() > input.as_ptr()))
}
fn compare(a: &[u16], b: &[u16]) -> i32 {
    let a = terminated(a);
    let b = terminated(b);
    // SAFETY: two live terminated UTF-16 buffers; the exact CRT function used by
    // CStringW::CompareNoCase, retaining native locale and first-NUL semantics.
    unsafe { _wcsicmp(a.as_ptr(), b.as_ptr()) }
}
fn scalar(kind: u32, input: &[u16], argument: i32) -> Result<i32, Error> {
    let mut run = xy_vsfilter_compat::usf_scalar::Run::new(kind, input, argument)
        .map_err(|_| Error::Protocol)?;
    loop {
        let bytes = run.snapshot().map_err(|_| Error::CountOverflow)?;
        let words: Vec<u32> = bytes[..20]
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
            .collect();
        let text: Units = bytes[20..]
            .chunks_exact(2)
            .map(|b| u16::from_le_bytes([b[0], b[1]]))
            .collect();
        let (a, b) = text.split_at(words[3] as usize);
        let (value, consumed) = match words[0] {
            1 => number(a, words[1] as i32),
            2 => (compare(a, b), 0),
            3 => return Ok(words[2] as i32),
            _ => return Err(Error::Protocol),
        };
        run.step(words[0], value, consumed)
            .map_err(|_| Error::Protocol)?;
    }
}

// These assertions mirror the SDK C++ probe's actual x86/x64 output. In addition
// to Rust unit checks, the Windows direct probe executes load(VARIANT by value).
const _: () = {
    let p = mem::size_of::<usize>();
    assert!(mem::size_of::<Guid>() == 16 && mem::align_of::<Guid>() == 4);
    assert!(mem::size_of::<VariantRaw>() == 8 + 2 * p && mem::align_of::<VariantRaw>() == 8);
    assert!(mem::offset_of!(VariantRaw, vt) == 0 && mem::offset_of!(VariantRaw, data) == 8);
    assert!(mem::size_of::<Bound>() == 8 && mem::align_of::<Bound>() == 4);
    assert!(mem::size_of::<ArrayRaw>() == 16 + 2 * p && mem::align_of::<ArrayRaw>() == p);
    assert!(mem::offset_of!(ArrayRaw, data) == if p == 8 { 16 } else { 12 });
    assert!(mem::offset_of!(ArrayRaw, bounds) == if p == 8 { 24 } else { 16 });
    assert!(
        mem::size_of::<NodeTable>() == 43 * p
            && mem::size_of::<DocumentTable>() == 76 * p
            && mem::size_of::<MapTable>() == 17 * p
    );
    assert!(
        mem::offset_of!(NodeTable, name) == 7 * p && mem::offset_of!(NodeTable, value) == 8 * p
    );
    assert!(
        mem::offset_of!(NodeTable, first_child) == 13 * p
            && mem::offset_of!(NodeTable, next_sibling) == 16 * p
            && mem::offset_of!(NodeTable, attributes) == 17 * p
    );
    assert!(
        mem::offset_of!(NodeTable, text) == 26 * p && mem::offset_of!(NodeTable, xml) == 34 * p
    );
    assert!(
        mem::offset_of!(DocumentTable, load) == 58 * p
            && mem::offset_of!(DocumentTable, set_async) == 63 * p
    );
    assert!(mem::offset_of!(MapTable, next) == 14 * p);
};

#[cfg(test)]
pub(crate) struct TestApartment;
#[cfg(test)]
extern "system" {
    fn CoInitializeEx(reserved: *mut c_void, mode: u32) -> Hresult;
    fn CoUninitialize();
    fn SysAllocStringLen(text: *const u16, length: u32) -> *mut u16;
}
#[cfg(test)]
impl TestApartment {
    pub(crate) fn sta() -> Self {
        // SAFETY: null reserved argument and SDK COINIT_APARTMENTTHREADED.
        assert!(unsafe { CoInitializeEx(ptr::null_mut(), 2) } >= 0);
        Self
    }
}
#[cfg(test)]
impl Drop for TestApartment {
    fn drop(&mut self) {
        // SAFETY: balances the successful initialization on this test thread.
        unsafe { CoUninitialize() };
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use xy_vsfilter_compat::usf_model::State;

    fn units(s: &str) -> Units {
        s.encode_utf16().collect()
    }
    fn load(state: &mut State, bytes: &[u8]) -> bool {
        let Some(mut xml) = Xml::load(Input::bytes(bytes).unwrap().unwrap()).unwrap() else {
            return false;
        };
        state.begin().unwrap();
        for _ in 0..10000 {
            let q = state.service_request().unwrap();
            if q.op == 11 {
                return q.accepted;
            }
            let (value, text) = xml.service(q.op, q.node, &q.text).unwrap();
            state.step(q.op, value, &text).unwrap();
        }
        panic!("MSXML traversal did not terminate")
    }
    #[test]
    fn automation_buffers_match_atl_copy_lock_and_first_nul_behavior() {
        let raw = [65u16, 0, 66];
        // SAFETY: readable three-unit buffer; Bstring takes sole ownership.
        let mut bstr = Bstring(unsafe { SysAllocStringLen(raw.as_ptr(), 3) });
        assert!(!bstr.0.is_null());
        assert_eq!(bstr.units().unwrap(), [65]);
        bstr.lower();
        assert_eq!(bstr.units().unwrap(), [97]);
        // SAFETY: owned BSTR has exactly three initialized units.
        unsafe {
            assert_eq!(SysStringLen(bstr.0), 3);
            assert_eq!(*bstr.0.add(2), 98);
        }
        let mut bytes = [1, 2, 3];
        let input = Input::bytes(&bytes).unwrap().unwrap();
        bytes.fill(9);
        // SAFETY: Input owns the original locked UI1 array and copied variant.
        unsafe {
            let original = input._original.as_ref().unwrap().raw.as_ref();
            let copy = &*input.variant.0.data.array;
            assert_eq!((original.locks, copy.locks), (1, 0));
            assert_eq!((original.features, copy.features), (128, 128));
            assert_ne!(original.data, copy.data);
            assert_eq!(*copy.data.cast::<[u8; 3]>(), [1, 2, 3]);
        }
        assert_eq!(scalar(1, &units("1:2:3"), 0), Ok(62003));
        assert_eq!(scalar(2, &units("#-1"), 0), Ok(-1));
        assert_eq!(scalar(4, &units("tOpLeFt\0ignored"), 0), Ok(7));
    }
    #[test]
    fn system_dom_traversal_and_rejected_reload_preserve_reference_state() {
        let _apartment = TestApartment::sta();
        let mut state = State::default();
        assert!(load(&mut state, br#"<USFSubtitles><metadata><language>Loaded</language></metadata><subtitles><subtitle start="1" stop="2"><text><b>A &amp; B</b></text></subtitle></subtitles></USFSubtitles>"#));
        let before = state.model.snapshot().unwrap();
        let language: Vec<u8> = units("Loaded")
            .into_iter()
            .flat_map(u16::to_le_bytes)
            .collect();
        let caption: Vec<u8> = units("{\\b1}A &amp; B{\\b}")
            .into_iter()
            .flat_map(u16::to_le_bytes)
            .collect();
        assert!(before.windows(language.len()).any(|w| w == language));
        assert!(before.windows(caption.len()).any(|w| w == caption));
        assert!(!load(&mut state, b"<USFSubtitles>"));
        assert_eq!(state.model.snapshot().unwrap(), before);
        assert!(!load(&mut state, b"<wrong/>"));
        let cleared = state.model.snapshot().unwrap();
        assert!(cleared.windows(language.len()).any(|w| w == language));
        assert!(!cleared.windows(caption.len()).any(|w| w == caption));
        assert!(load(&mut state, b"<USFSubtitles/>"));
        assert_eq!(state.model.snapshot().unwrap(), cleared);
    }
    #[test]
    fn loading_does_not_initialize_com_for_the_caller() {
        std::thread::spawn(|| {
            let input = Input::bytes(b"<USFSubtitles/>").unwrap().unwrap();
            assert!(Xml::load(input).unwrap().is_none());
        })
        .join()
        .unwrap();
    }
}

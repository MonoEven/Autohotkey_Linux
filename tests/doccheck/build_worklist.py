#!/usr/bin/env python3
"""Build the doc-check worklist: implemented functions/vars (from the Linux
registry sources) joined with their official doc entries."""
import csv
import re
import os

BASE = "/mnt/f/AI/Codex/Autohotkey_Linux"

# 1) Implemented MdFunc entries from core_mdfunc_linux.cpp (LMD_IMPL)
impl = set()
ni = set()
src = open(os.path.join(BASE, "source/linux/core/core_mdfunc_linux.cpp"), encoding="utf-8").read()
for m in re.finditer(r"LMD_IMPL\((\w+),", src):
    impl.add(m.group(1))
for m in re.finditer(r"LMD_NI\((\w+),", src):
    ni.add(m.group(1))

# 2) g_BIF entries from script.cpp (real ones, not stubs)
g_bif = set()
src = open(os.path.join(BASE, "source/script.cpp"), encoding="utf-8").read()
for m in re.finditer(r"BIF[1ni]?\((\w+),", src):
    g_bif.add(m.group(1))
# which g_BIF funcs are stubbed in core_builtin_stubs.cpp?
stubs = set()
src = open(os.path.join(BASE, "source/linux/core/core_builtin_stubs.cpp"), encoding="utf-8").read()
for m in re.finditer(r"LINUX_BIF_STUB_ERR?\((\w+)\)", src):
    stubs.add(m.group(1))
    if m.group(1).startswith("BIF_"):
        stubs.add(m.group(1)[4:])
# Shared implementations: one stub BIF covers several g_BIF names.
SHARED_STUBS = {
    "BIF_Reg": ["RegCreateKey", "RegDelete", "RegDeleteKey", "RegRead", "RegWrite"],
    "BIF_RegEx": ["RegExMatch", "RegExReplace"],
    "BIF_Sound": ["SoundGetInterface", "SoundGetMute", "SoundGetName", "SoundGetVolume",
                  "SoundSetMute", "SoundSetVolume"],
    "BIF_DllCall": ["ComCall", "DllCall"],
    "BIF_ComObj": ["ComObjFromPtr"],
    "BIF_StrGetPut": ["StrGet", "StrPut"],
    "BIF_StrPtr": ["StrPtr"],
    "BIF_NumGet": ["NumGet"],
    "BIF_NumPut": ["NumPut"],
    "BIF_Click": ["Click"],
    "BIF_CaretGetPos": ["CaretGetPos"],
}
for stub_name, names in SHARED_STUBS.items():
    if stub_name in stubs:
        stubs.update(names)
# Stubbed g_BIF functions are NOT_IMPL (they raise a clear "not ported"
# error at runtime), not silently dropped from the worklist.
ni.update(stubs & g_bif)
g_bif_impl = g_bif - stubs

# 2b) Class constructors / object types that are fully usable on Linux but
# are not plain g_BIF entries (they are registered via DefineClasses or the
# GUI/COM backends).  Verified at runtime; listed here so the worklist covers
# every doc/lib page.
CLASS_IMPL = {
    # Primitive/container constructors.
    "Buffer", "ClipboardAll", "Float", "Integer", "Number", "String",
    "Array", "Map", "Object", "Error", "Enumerator", "Func", "Class",
    # COM (D-Bus) object constructors.
    "ComObject", "ComValue",
    # GUI classes (GTK3 backend).
    "Gui", "GuiControl", "Menu", "ListView", "TreeView",
    # Doc name variants of implemented functions.
    "DriveGetFileSystem", "IsSet",
}
# InputHook needs the keyboard-hook input capture (input.cpp), which is not
# ported; ComObjArray needs SafeArray marshalling, also unavailable.  Both
# raise a clear runtime error.
CLASS_NOT_IMPL = {"InputHook", "ComObjArray", "ComObjConnect", "ComObjQuery"}
impl |= CLASS_IMPL
ni |= CLASS_NOT_IMPL

# 3) Built-in variables implemented in core_builtin_stubs.cpp (BIV_ definitions)
biv = set()
for m in re.finditer(r"\nvoid (BIV_\w+)\(", src):
    biv.add(m.group(1))
# plus BIV_* implemented in core_mdfunc_linux.cpp
src = open(os.path.join(BASE, "source/linux/core/core_mdfunc_linux.cpp"), encoding="utf-8").read()
for m in re.finditer(r"\nvoid (BIV_\w+)\(", src):
    biv.add(m.group(1))

# 4) Doc index
with open(os.path.join(BASE, "tests/doccheck/doc_index.tsv"), encoding="utf-8") as f:
    doc = {r["name"]: r for r in csv.DictReader(f, delimiter="\t")}

with open(os.path.join(BASE, "tests/doccheck/worklist.tsv"), "w", encoding="utf-8") as f:
    f.write("name\tstatus\tdoc_desc\tdoc_syntax\tdoc_params\tdoc_returns\n")
    seen = set()
    for name in sorted(impl | g_bif_impl):
        if name in seen:
            continue
        seen.add(name)
        d = doc.get(name, {})
        f.write("\t".join([
            name, "IMPL",
            d.get("desc", ""), d.get("syntax", ""), d.get("params", ""), d.get("returns", ""),
        ]) + "\n")
    for name in sorted(ni):
        if name in seen:
            continue
        seen.add(name)
        d = doc.get(name, {})
        f.write("\t".join([name, "NOT_IMPL", d.get("desc", ""), d.get("syntax", ""), d.get("params", ""), d.get("returns", "")]) + "\n")

print(f"IMPL funcs: {len(impl | g_bif_impl)} (mdFunc={len(impl)}, g_BIF={len(g_bif_impl)})")
print(f"NOT_IMPL funcs: {len(ni)}")
print(f"BIV implemented: {len(biv)}")
withdoc = sum(1 for n in (impl | g_bif_impl | ni) if n in doc)
print(f"functions with doc pages: {withdoc}")

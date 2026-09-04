; P2-6 rich ClipboardAll doc-check (check_detail0901 搂18).
; Runs under a dedicated Xvfb display and uses xclip_probe processes as an
; independent owner/reader oracle:
;   1. A foreign owner advertises text, HTML, PNG, URI-list and custom bytes.
;   2. ClipboardAll captures them, then the foreign owner exits.
;   3. AHK restores the snapshot and fresh probe readers verify TARGETS and
;      every representation byte-for-byte.
;   4. Corrupt, truncated and synthetic oversized snapshots are rejected
;      without replacing any part of the valid restored clipboard.
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_clipall_out.txt"
PROBE := "out/xclip_probe"
TBASE := "/tmp/ahk_dc_p26"

TEXT_HEX := "7032362D6F776E65722D74657874" ; p26-owner-text
HTML_HEX := "3C623E7032363C2F623E"         ; <b>p26</b>
PNG_HEX := "89504E470D0A1A0A"             ; PNG signature
URI_HEX := "66696C653A2F2F2F746D702F7032362D6F6E650D0A"
    . "66696C653A2F2F2F746D702F7032362D74776F0D0A"
CUSTOM_HEX := "00010203FEFF"

OWNER_TARGETS := [
    "TARGETS", "STRING", "UTF8_STRING", "text/plain;charset=utf-8",
    "text/html", "image/png", "text/uri-list", "application/x-ahk-p26"
]
RESTORED_TARGETS := [
    "TARGETS", "STRING", "UTF8_STRING", "text/html", "image/png",
    "text/uri-list", "application/x-ahk-p26"
]

FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

; The probe *function* is named RunProbe because AHK v2 names are
; case-insensitive: a global PROBE variable plus a Probe() function would
; collide into one symbol ("This Func cannot be used as an output variable").
RunProbe(opts, tag) {
    global PROBE, TBASE
    path := TBASE "_" tag ".txt"
    FileDelete(path)
    try {
        rc := RunWait(PROBE " " opts " -out " path)
    } catch {
        return "__RUN_ERROR__"
    }
    if rc != 0
        return "__EXIT_" rc "__"
    return FileExist(path) ? RTrim(FileRead(path), "`r`n") : "__NO_OUTPUT__"
}

WaitOwnerReady(path, pid) {
    Loop 80 {
        if FileExist(path) {
            try {
                if InStr(FileRead(path), "probe-owner-ready")
                    return true
            }
        }
        if pid && !ProcessExist(pid)
            return false
        Sleep(50)
    }
    return false
}

TargetSetEquals(text, expected) {
    got := []
    for raw in StrSplit(RTrim(text, "`r`n"), "`n") {
        name := RTrim(raw, "`r")
        if name != ""
            got.Push(name)
    }
    if got.Length != expected.Length
        return false
    for wanted in expected {
        matches := 0
        for actual in got
            if actual == wanted
                matches += 1
        if matches != 1
            return false
    }
    return true
}

CopyPrefix(source, size) {
    copy := Buffer(size, 0)
    Loop size
        NumPut("UChar", NumGet(source, A_Index - 1, "UChar"),
            copy, A_Index - 1)
    return copy
}

AssignmentRejected(snapshot) {
    try {
        A_Clipboard := snapshot
    } catch {
        return true
    }
    return false
}

SnapshotStillExact(tag) {
    global RESTORED_TARGETS, TEXT_HEX, HTML_HEX, PNG_HEX, URI_HEX, CUSTOM_HEX
    targets := RunProbe("--targets", tag "_targets")
    text := RunProbe("--get-target UTF8_STRING", tag "_text")
    html := RunProbe("--get-target text/html", tag "_html")
    png := RunProbe("--get-target image/png", tag "_png")
    uris := RunProbe("--get-target text/uri-list", tag "_uri")
    custom := RunProbe("--get-target application/x-ahk-p26", tag "_custom")
    return TargetSetEquals(targets, RESTORED_TARGETS)
        && text == TEXT_HEX && html == HTML_HEX && png == PNG_HEX
        && uris == URI_HEX && custom == CUSTOM_HEX
}

SyntheticOversizedSnapshot() {
    ; A valid AHKCB1 header/item prefix declaring 128 MiB + 1 byte of data.
    ; The backing buffer stays tiny: this checks bounded rejection without a
    ; CI-hostile allocation of the declared payload.
    raw := Buffer(23, 0)
    NumPut("UInt", 0x31424B48, raw, 0) ; HKB1 magic, little-endian
    NumPut("UInt", 1, raw, 4)          ; version
    NumPut("UInt", 1, raw, 8)          ; item count
    NumPut("UShort", 1, raw, 12)       ; MIME length
    NumPut("UChar", Ord("x"), raw, 14)
    NumPut("UInt", 0x08000001, raw, 15) ; declared data length
    NumPut("UInt", 0, raw, 19)          ; checksum (never reached)
    return ClipboardAll(raw, raw.Size)
}

; Start the external owner asynchronously.  Waiting is only for its explicit
; ready marker; RunWait here would wait out the owner's lifetime before the
; ClipboardAll read and leave nothing to capture.
ownerOut := TBASE "_owner.txt"
FileDelete(ownerOut)
ownerCmd := Format('{} --set-mime'
    . ' --mime "text/plain;charset=utf-8={}"'
    . ' --mime "text/html={}"'
    . ' --mime "image/png={}"'
    . ' --mime "text/uri-list={}"'
    . ' --mime "application/x-ahk-p26={}"'
    . ' --delay 5000 -out {}',
    PROBE, TEXT_HEX, HTML_HEX, PNG_HEX, URI_HEX, CUSTOM_HEX, ownerOut)
ownerPid := 0
Run(ownerCmd,,, &ownerPid)
ownerReady := WaitOwnerReady(ownerOut, ownerPid)
Log("owner_ready=" (ownerReady ? 1 : 0))

ownerTargets := RunProbe("--targets", "owner_targets")
Log("owner_targets_exact=" (TargetSetEquals(ownerTargets, OWNER_TARGETS) ? 1 : 0))

; ClipboardAll must read while the independent owner is still serving.
saved := ClipboardAll()
Log("all_type=" (Type(saved) == "ClipboardAll" ? 1 : 0))
Log("all_size=" (saved.Size > 0 ? 1 : 0))
Log("all_rich=" (saved.Size >= 140 ? 1 : 0))
itemCount := saved.Size >= 12 ? NumGet(saved, 8, "UInt") : 0
Log("all_item_count=" (itemCount >= 5 ? 1 : 0))

; Let the owner terminate naturally, then prove there is no owner to mask a
; failed restore.  ProcessWaitClose returns zero on success and a PID on
; timeout in AHK v2.
stillRunning := ProcessWaitClose(ownerPid, 8)
ownerExited := stillRunning == 0
if stillRunning {
    try ProcessClose(ownerPid)
    ProcessWaitClose(ownerPid, 2)
}
Log("owner_exited=" (ownerExited ? 1 : 0))
Log("owner_gone=" (RunProbe("--targets", "owner_gone") == "" ? 1 : 0))

; Restore only after the source process is gone.  Every following read comes
; from a newly launched X11 client, not from AHK reading its own state.
A_Clipboard := saved
Sleep(100)
restoredTargets := RunProbe("--targets", "restored_targets")
restoredText := RunProbe("--get-target UTF8_STRING", "restored_text")
restoredHtml := RunProbe("--get-target text/html", "restored_html")
restoredPng := RunProbe("--get-target image/png", "restored_png")
restoredUri := RunProbe("--get-target text/uri-list", "restored_uri")
restoredCustom := RunProbe("--get-target application/x-ahk-p26", "restored_custom")
Log("restore_targets_exact=" (TargetSetEquals(restoredTargets, RESTORED_TARGETS) ? 1 : 0))
Log("restore_text_exact=" (restoredText == TEXT_HEX ? 1 : 0))
Log("restore_html_exact=" (restoredHtml == HTML_HEX ? 1 : 0))
Log("restore_png_exact=" (restoredPng == PNG_HEX ? 1 : 0))
Log("restore_uri_exact=" (restoredUri == URI_HEX ? 1 : 0))
Log("restore_custom_exact=" (restoredCustom == CUSTOM_HEX ? 1 : 0))

; Corrupt a real snapshot's final data byte so its checksum is wrong.
if saved.Size > 12 {
    corruptRaw := CopyPrefix(saved, saved.Size)
    last := NumGet(corruptRaw, corruptRaw.Size - 1, "UChar")
    NumPut("UChar", last == 0 ? 1 : 0, corruptRaw, corruptRaw.Size - 1)
    corrupt := ClipboardAll(corruptRaw, corruptRaw.Size)
    corruptRejected := AssignmentRejected(corrupt)
} else {
    corruptRejected := false
}
Log("corrupt_rejected=" (corruptRejected ? 1 : 0))
Log("corrupt_no_partial=" (SnapshotStillExact("after_corrupt") ? 1 : 0))

; Truncate the same valid snapshot by one byte.
if saved.Size > 12 {
    truncatedRaw := CopyPrefix(saved, saved.Size - 1)
    truncated := ClipboardAll(truncatedRaw, truncatedRaw.Size)
    truncatedRejected := AssignmentRejected(truncated)
} else {
    truncatedRejected := false
}
Log("truncated_rejected=" (truncatedRejected ? 1 : 0))
Log("truncated_no_partial=" (SnapshotStillExact("after_truncated") ? 1 : 0))

; A declared 128 MiB + 1 item is rejected from a 23-byte synthetic prefix;
; no huge test allocation is required.
oversized := SyntheticOversizedSnapshot()
oversizeRejected := AssignmentRejected(oversized)
Log("oversize_decl_rejected=" (oversizeRejected ? 1 : 0))
Log("oversize_no_partial=" (SnapshotStillExact("after_oversize") ? 1 : 0))

A_Clipboard := ""
Sleep(100)
ExitApp 0

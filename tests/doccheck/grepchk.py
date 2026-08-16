import subprocess
base = "/mnt/f/AI/Codex/Autohotkey_Linux"
for pat, paths in [
    ("IsNumeric", ["source/util.h", "source/script.h"]),
    ("LinuxWinSetPersistent", ["source/linux/core/core_win_linux.cpp"]),
    ("ctoupper", ["source/util.h"]),
    ("DefaultDialogTitle", ["source/script.h"]),
]:
    out = subprocess.run(["grep", "-rn", pat] + [base + "/" + p for p in paths],
                         capture_output=True, text=True).stdout
    print("==", pat)
    print(out[:800])

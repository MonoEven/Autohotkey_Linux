import os, sys, subprocess, tempfile

HK = "SHA256:ei6fd/v+2AeZcQg6FVPXsOavH1CE176BlhCLdC8m178"
HST = "mono@192.168.111.130"
PASS = "123456"
ENC = dict(encoding="utf-8", errors="replace")


def vm_run_script(script_text: str, timeout_s: int = 120) -> tuple:
    fd, path = tempfile.mkstemp(suffix=".sh", prefix="vmc_")
    with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
        f.write(script_text)
    try:
        r = subprocess.run(["plink", "-hostkey", HK, "-pw", PASS, HST, "-m", path],
                           capture_output=True, text=True, timeout=timeout_s, **ENC)
    finally:
        os.unlink(path)
    return r.stdout, r.stderr, r.returncode


def vm_run_cmd(cmd: str, timeout_s: int = 60) -> tuple:
    r = subprocess.run(["plink", "-hostkey", HK, "-pw", PASS, HST, cmd],
                       capture_output=True, text=True, timeout=timeout_s, **ENC)
    return r.stdout, r.stderr, r.returncode


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "cmd"
    if mode == "cmd":
        out, err, rc = vm_run_cmd(" ".join(sys.argv[2:]))
    else:
        out, err, rc = vm_run_script(open(sys.argv[2], encoding="utf-8").read())
    sys.stdout.write(out or "")
    if err.strip():
        sys.stderr.write(err[:1200] + "\n")
    print("RC", rc)
// Case-shim for source/util.h (script.h includes "Util.h" with the
// Windows casing; the file is util.h in the tree).  On case-insensitive
// mounts (WSL drvfs) the original include resolves anyway; this forwarder
// makes it build on case-sensitive filesystems (CI/ext4).
#pragma once
#include "../../util.h"

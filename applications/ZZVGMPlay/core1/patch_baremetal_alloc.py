#!/usr/bin/env python3
"""Offline source-only bare-metal allocator redirection for ZZVGMPlay Core1.

Do not modify upstream third_party/.  Creates a temporary source copy
and fails closed if any targeted allocator call has not been redirected.
Usage: python patch_baremetal_alloc.py nes|pce|opll|gens SRC DEST
"""
from pathlib import Path
import re
import sys

SYMBOLS = {
    "nes": {"malloc": "zz_nes_malloc"},
    "pce": {"malloc": "zz_pce_malloc", "free": "zz_pce_free"},
    "opll": {"malloc": "zz_opll_malloc", "calloc": "zz_opll_calloc", "free": "zz_opll_free"},
    "gens": {"malloc": "zz_gens_malloc", "free": "zz_gens_free"},
}
DECL = {
    "nes":  "extern void *zz_nes_malloc(size_t n);",
    "pce":  "extern void *zz_pce_malloc(size_t n);\nextern void zz_pce_free(void *p);",
    "opll": "extern void *zz_opll_malloc(size_t n);\nextern void *zz_opll_calloc(size_t n, size_t s);\nextern void zz_opll_free(void *p);",
    "gens": 'extern "C" void *zz_gens_malloc(size_t n);\nextern "C" void zz_gens_free(void *p);',
}


def convert(kind: str, source: str) -> str:
    if kind not in SYMBOLS:
        raise ValueError(f"unknown core: {kind}")
    # Preserve source/headers as shipped; only adapt a local generated copy.
    source = re.sub(r"(?m)^\s*#\s*include\s*<stdlib\.h>[^\n]*\n", "", source)
    if kind == "gens":
        # The GENS core also uses C++ headers: never use preprocessor
        # -Dmalloc=..., because this can rewrite STL declarations.
        pat = r"(?<![A-Za-z0-9_:.])malloc\s*\(\s*sizeof\s*\*\s*impl\s*\)"
        source, n1 = re.subn(pat, "zz_gens_malloc(sizeof *impl)", source)
        pat = r"(?<![A-Za-z0-9_:.])free\s*\(\s*impl\s*\)"
        source, n2 = re.subn(pat, "zz_gens_free(impl)", source)
        if n1 < 1 or n2 < 1:
            raise ValueError(f"GENS malloc/free adaptation missing: malloc={n1}, free={n2}; upstream changed?")
    else:
        replacements = 0
        for sym, target in SYMBOLS[kind].items():
            # Only replace standalone calls, not names embedded in symbols.
            pat = r"(?<![A-Za-z0-9_:.])" + sym + r"\s*\("
            source, n = re.subn(pat, target + "(", source)
            replacements += n
        if replacements == 0:
            raise ValueError(f"{kind}: no allocator call found; upstream changed?")
    # GENS is C++. The last #include can be inside an inactive #if branch
    # (or another block scope); declarations inserted there disappear.
    # Put GENS declarations before the original source, at global scope,
    # *before* any conditionals, namespaces or function definitions.
    # The corresponding definitions in gens2612_wrap.cpp use C linkage.
    if kind == "gens":
        source = "#include <stddef.h>\n" + DECL[kind] + "\n\n" + source
    else:
        # Keep the existing placement for the C-based cores which already
        # compiled successfully with FIX2.
        insertion = "\n#include <stddef.h>\n" + DECL[kind] + "\n"
        headers = list(re.finditer(r"(?m)^\s*#\s*include[^\n]*\n", source))
        if not headers:
            raise ValueError(f"{kind}: no #include lines found")
        i = headers[-1].end()
        source = source[:i] + insertion + source[i:]
    # Fail closed on incomplete redirection. New helper symbols are *not*
    # mistaken for malloc() because of the negative lookbehind.
    for sym in SYMBOLS[kind]:
        left = re.search(r"(?<![A-Za-z0-9_:.])" + sym + r"\s*\(", source)
        if left:
            line = source.count("\n", 0, left.start()) + 1
            raise ValueError(f"{kind}: unpatched {sym}() remains at line {line}")
    return source


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    kind, src, dst = sys.argv[1:]
    try:
        data = Path(src).read_text(encoding="utf-8", errors="surrogateescape")
        patched = convert(kind, data)
        Path(dst).write_bytes(patched.encode("utf-8", errors="surrogateescape"))
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(f"OK: {kind} allocator redirection -> {dst}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())

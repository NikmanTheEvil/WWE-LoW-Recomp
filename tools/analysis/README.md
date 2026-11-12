# Analysis tools (maintainers)

Only needed to re-examine the game binary, e.g. after updating the ReXGlue SDK or when a new
crash points to a missing function boundary. Not used by the installer.

| Tool | Purpose |
|---|---|
| `xexinfo.py <default.xex>` | prints the XEX2 headers: title/media ID, base, entry, sections, imports, XDK libraries |
| `xexdump.cpp` | builds against XenonUtils (part of the ReXGlue SDK sources): decrypts and unpacks the XEX and writes `image.bin` (memory image at 0x82000000) plus the import list |
| `find_gap_funcs.py <image.bin> [imports.txt] [--diag]` | finds functions without `.pdata` entries (COM/vtable methods, callbacks) by scanning data sections for pointers into code gaps; prints `[functions]` entries for `project/config/default_codegen.toml` |
| `dis.sh <start> <end> [image.bin]` | disassembles an address range (llvm-objdump, PowerPC + AltiVec) |

The output of these tools is derived from the game's code - keep it local, do not commit it.

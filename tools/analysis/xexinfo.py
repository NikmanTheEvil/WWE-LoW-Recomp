#!/usr/bin/env python3
"""xexinfo – liest die XEX2-Header einer Xbox-360-Executable (ohne Entschlüsselung).

Gibt aus: Titel-/Media-ID, Version, Kompression/Verschlüsselung, Einsprung,
Image-Basis, Stack/Heap, System-Flags, XDK-Version (statische Libs),
Import-Bibliotheken mit Version und Anzahl, TLS, Ressourcen.

Aufruf: python3 xexinfo.py default.xex
"""
import struct
import sys

NAMES = {
    0x000002FF: "Ressourcen",
    0x000003FF: "Dateiformat",
    0x000005FF: "Delta-Patch",
    0x00000405: "Basisreferenz",
    0x000080FF: "Bounding-Pfad",
    0x00008105: "Geräte-ID",
    0x00010001: "Original-Basisadresse",
    0x00010100: "Einsprungpunkt",
    0x00010201: "Image-Basisadresse",
    0x000103FF: "Import-Bibliotheken",
    0x00018002: "Checksumme/Zeitstempel",
    0x00018102: "Callcap",
    0x00018200: "Fastcap",
    0x000183FF: "Original-PE-Name",
    0x000200FF: "Statische Bibliotheken",
    0x00020104: "TLS-Info",
    0x00020200: "Standard-Stackgröße",
    0x00020301: "Dateisystem-Cache",
    0x00020401: "Standard-Heapgröße",
    0x00028002: "Page-Heap",
    0x00030000: "System-Flags",
    0x00040006: "Ausführungsinfo",
    0x00040201: "Title-Workspace",
    0x00040310: "Altersfreigaben",
    0x00040404: "LAN-Key",
    0x000405FF: "Xbox-360-Logo",
    0x000406FF: "Multidisc-Media-IDs",
    0x000407FF: "Alternative Titel-IDs",
    0x00040801: "Zusätzlicher Titelspeicher",
    0x00E10402: "Exporte nach Name",
}

SYSTEM_FLAGS = [
    "NO_FORCED_REBOOT", "FOREGROUND_TASKS", "NO_ODD_MAPPING", "HANDLE_MCE_INPUT",
    "RESTRICTED_HUD_FEATURES", "HANDLE_GAMEPAD_DISCONNECT", "INSECURE_SOCKETS",
    "XBOX1_INTEROPERABILITY", "DASH_CONTEXT", "USES_GAME_VOICE_CHANNEL",
    "PAL50_INCOMPATIBLE", "INSECURE_UTILITY_DRIVE", "XAM_HOOKS",
    "ACCESS_PII", "CROSS_PLATFORM_SYSTEM_LINK", "MULTIDISC_SWAP",
    "MULTIDISC_INSECURE_MEDIA", "AP25_MEDIA", "NO_CONFIRM_EXIT",
    "ALLOW_BACKGROUND_DOWNLOAD", "CREATE_PERSISTABLE_RAMDRIVE",
    "INHERIT_PERSISTENT_RAMDRIVE", "ALLOW_HUD_VIBRATION",
    "ACCESS_UTILITY_PARTITIONS", "IPTV_INPUT_SUPPORTED", "PREFER_BIG_BUTTON_INPUT",
    "ALLOW_EXTENDED_SYSTEM_RESERVATION", "MULTIDISC_CROSS_TITLE",
    "INSTALL_INCOMPATIBLE", "ALLOW_AVATAR_GET_METADATA_BY_XUID",
    "ALLOW_CONTROLLER_SWAPPING", "DASH_EXTENSIBILITY_MODULE",
]

ENCRYPTION = {0: "keine", 1: "AES (Retail/Devkit-Key)"}
COMPRESSION = {0: "keine", 1: "Basic (Blöcke mit Lücken)", 2: "Normal (LZX)", 3: "Delta"}


def u16(b, o): return struct.unpack_from(">H", b, o)[0]
def u32(b, o): return struct.unpack_from(">I", b, o)[0]


def cstr(b, o, n=None):
    end = b.index(b"\0", o) if n is None else o + n
    return b[o:end].split(b"\0")[0].decode("ascii", "replace")


def main(path):
    d = open(path, "rb").read()
    if d[:4] != b"XEX2":
        sys.exit(f"Keine XEX2-Datei (Magic {d[:4]!r})")

    mod_flags, pe_off, _, sec_off, count = struct.unpack_from(">IIIII", d, 4)
    print(f"Datei:          {path} ({len(d):,} Byte)")
    print(f"Modul-Flags:    0x{mod_flags:08X}  "
          + ", ".join(n for bit, n in [(1, "TITLE"), (2, "EXPORTS_TO_TITLE"), (4, "SYSTEM_DEBUGGER"),
                                       (8, "DLL"), (0x10, "MODULE_PATCH"), (0x20, "PATCH_FULL"),
                                       (0x40, "PATCH_DELTA"), (0x80, "USER_MODE")] if mod_flags & bit))
    print(f"PE-Daten ab:    0x{pe_off:X}")

    # Security-Info
    s = sec_off
    img_size = u32(d, s + 4)
    img_flags = u32(d, s + 0x10C)
    load_addr = u32(d, s + 0x110)
    region = u32(d, s + 0x178)
    media = u32(d, s + 0x17C)
    pages = u32(d, s + 0x180)
    print(f"Image-Größe:    0x{img_size:08X} ({img_size // 1024} KiB), Ladeadresse 0x{load_addr:08X}")
    print(f"Image-Flags:    0x{img_flags:08X}, Region 0x{region:08X}, Medien 0x{media:08X}, Seiten {pages}")

    # Seiten-Deskriptoren: Code-/Datenanteile
    code = data = 0
    page = 64 * 1024 if not (img_flags & 0x10000000) else 4 * 1024
    for i in range(pages):
        info = u32(d, s + 0x184 + i * 24)
        size = (info >> 4) * page
        if info & 0xF == 1:
            code += size
        else:
            data += size
    print(f"Seiten:         Code {code // 1024} KiB, Daten/RO {data // 1024} KiB (Seitengröße {page // 1024} KiB)")
    print()

    hdr = {}
    for i in range(count):
        key, val = struct.unpack_from(">II", d, 0x18 + i * 8)
        hdr[key] = val

    for key, val in hdr.items():
        name = NAMES.get(key, "?")
        size = key & 0xFF
        if size in (0, 1):
            print(f"[{key:08X}] {name:<24} = 0x{val:08X}")
        else:
            print(f"[{key:08X}] {name:<24} @ 0x{val:X}")

    print()

    if 0x00040006 in hdr:
        o = hdr[0x00040006]
        media_id, ver, base_ver, title = struct.unpack_from(">IIII", d, o)
        plat, exe_tab, disc, discs = d[o + 16:o + 20]
        v = lambda x: f"{x >> 28}.{(x >> 24) & 0xF}.{(x >> 8) & 0xFFFF}.{x & 0xFF}"
        print(f"Titel-ID:       {title:08X}  (Publisher '{chr(title >> 24)}{chr((title >> 16) & 0xFF)}', Nr. {title & 0xFFFF})")
        print(f"Media-ID:       {media_id:08X}")
        print(f"Version:        {v(ver)} (Basis {v(base_ver)}), Disc {disc}/{discs}")

    if 0x000003FF in hdr:
        o = hdr[0x000003FF]
        enc, comp = u16(d, o + 4), u16(d, o + 6)
        print(f"Verschlüsselung: {ENCRYPTION.get(enc, enc)}")
        print(f"Kompression:     {COMPRESSION.get(comp, comp)}")

    if 0x000183FF in hdr:
        o = hdr[0x000183FF]
        print(f"Original-PE:    {cstr(d, o + 4)}")

    for key, label in [(0x00020200, "Stack"), (0x00020401, "Heap"), (0x00020301, "FS-Cache"),
                       (0x00040201, "Title-Workspace"), (0x00040801, "Zusatzspeicher")]:
        if key in hdr:
            print(f"{label + ':':<16}0x{hdr[key]:X} ({hdr[key] // 1024} KiB)")

    if 0x00030000 in hdr:
        f = hdr[0x00030000]
        print("System-Flags:   " + (", ".join(n for i, n in enumerate(SYSTEM_FLAGS) if f & (1 << i)) or "keine"))

    if 0x00020104 in hdr:
        o = hdr[0x00020104]
        slots, raw, dsize, rsize = struct.unpack_from(">IIII", d, o)
        print(f"TLS:            {slots} Slots, Daten 0x{raw:08X}, {dsize} Byte (roh {rsize})")

    if 0x000200FF in hdr:
        o = hdr[0x000200FF]
        n = (u32(d, o) - 4) // 16
        print(f"\nStatische Bibliotheken ({n}) – verrät die XDK-Version:")
        for i in range(n):
            e = o + 4 + i * 16
            name = cstr(d, e, 8)
            maj, mn, bld, qfe = struct.unpack_from(">HHHH", d, e + 8)
            print(f"  {name:<10} {maj}.{mn}.{bld}.{qfe & 0xFF}")

    if 0x000103FF in hdr:
        o = hdr[0x000103FF]
        str_size, n = u32(d, o + 4), u32(d, o + 8)
        strs, p = [], o + 12
        for _ in range(n):
            s_ = cstr(d, p)
            strs.append(s_)
            p += (len(s_) + 1 + 3) & ~3
        p = o + 12 + str_size
        print(f"\nImport-Bibliotheken ({n}):")
        for _ in range(n):
            size = u32(d, p)
            ver, ver_min = u32(d, p + 28), u32(d, p + 32)
            idx, cnt = u16(d, p + 36), u16(d, p + 38)
            v = lambda x: f"{x >> 28}.{(x >> 24) & 0xF}.{(x >> 8) & 0xFFFF}.{x & 0xFF}"
            print(f"  {strs[idx]:<16} v{v(ver)} (min {v(ver_min)}), {cnt} Einträge")
            p += size

    if 0x000002FF in hdr:
        o = hdr[0x000002FF]
        n = (u32(d, o) - 4) // 16
        print(f"\nRessourcen ({n}):")
        for i in range(n):
            e = o + 4 + i * 16
            name = cstr(d, e, 8)
            addr, size = u32(d, e + 8), u32(d, e + 12)
            print(f"  {name:<10} @ 0x{addr:08X}, {size:,} Byte")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])

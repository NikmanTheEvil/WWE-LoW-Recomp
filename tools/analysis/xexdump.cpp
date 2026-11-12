// xexdump – lädt eine XEX2 über XenonUtils (entschlüsselt + entpackt),
// gibt Basisdaten, Sections und importierte Funktionen aus und schreibt
// das Speicherabbild für weitere Analyse (Strings, Signaturen, Disassembly).
//
// Aufruf: xexdump <default.xex> <ausgabe-ordner>
#include <file.h>
#include <image.h>
#include <xex.h>
#include <fmt/core.h>
#include <fmt/os.h>
#include <filesystem>
#include <map>

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fmt::print("Aufruf: xexdump <default.xex> <ausgabe-ordner>\n");
        return 1;
    }

    const auto file = LoadFile(argv[1]);
    if (file.empty())
    {
        fmt::print("Datei nicht lesbar: {}\n", argv[1]);
        return 1;
    }

    std::filesystem::path out = argv[2];
    std::filesystem::create_directories(out);

    Image image = Image::ParseImage(file.data(), file.size());
    if (image.data == nullptr)
    {
        fmt::print("Laden fehlgeschlagen (Format/Schlüssel?)\n");
        return 2;
    }

    fmt::print("Image-Basis:   0x{:08X}\n", image.base);
    fmt::print("Image-Größe:   0x{:08X} ({} KiB)\n", image.size, image.size / 1024);
    fmt::print("Einsprung:     0x{:08X}\n\n", image.entry_point);

    fmt::print("{:<10} {:>10} {:>10}  {}\n", "Section", "Adresse", "Größe", "Typ");
    size_t codeBytes = 0;
    for (const auto& s : image.sections)
    {
        const char* type = (s.flags & SectionFlags_Code) ? "Code" : (s.flags & SectionFlags_Data) ? "Daten" : "-";
        if (s.flags & SectionFlags_Code)
            codeBytes += s.size;
        fmt::print("{:<10} 0x{:08X} 0x{:08X}  {}\n", s.name, s.base, s.size, type);
    }
    fmt::print("Code gesamt: {} KiB (~{} PPC-Instruktionen)\n\n", codeBytes / 1024, codeBytes / 4);

    // Importierte Funktionen (Namen aus Xenias xboxkrnl/xam-Tabellen)
    auto imports = fmt::output_file((out / "imports.txt").string());
    size_t n = 0;
    for (const auto& sym : image.symbols)
    {
        if (sym.type == Symbol_Function)
        {
            imports.print("0x{:08X} {}\n", sym.address, sym.name);
            ++n;
        }
    }
    imports.close();
    fmt::print("Importierte Funktionen mit Namen: {} -> imports.txt\n", n);

    // Speicherabbild schreiben (Adresse = Basis + Offset)
    auto bin = fmt::output_file((out / "image.bin").string());
    bin.close();
    FILE* f = fopen((out / "image.bin").string().c_str(), "wb");
    fwrite(image.data.get(), 1, image.size, f);
    fclose(f);
    fmt::print("Speicherabbild -> image.bin\n");
    return 0;
}

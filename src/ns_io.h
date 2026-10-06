// ns_io.h - Shinobi Striker: Pak-Archive (UE 4.16, Pak-Version 4)
//
// Die Indizes beider Paks sind AES-256-verschluesselt (Flag vor der Magic im Fuss),
// die Dateien selbst fast alle nicht. Der Schluessel ist 32 Zeichen ASCII und in der
// Modding-Szene oeffentlich (gildor.org, umodel/FModel). AES laeuft ueber Windows-BCrypt.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace ns {

// Bekannter Schluessel fuer den Pak-Index. NSIMPORT_AES (Umgebung) ersetzt ihn:
// 32 Zeichen Text oder 0x + 64 Hexziffern.
extern const char* const kStandardSchluessel;
bool SchluesselAusText(const std::string& s, uint8_t key[32]);

class Pak {
public:
    ~Pak();
    bool Open(const std::wstring& path, const uint8_t key[32], std::string& err);
    const std::wstring& Name() const { return name_; }
    const std::string& Mount() const { return mount_; }   // ohne "../../../"

    struct Entry {
        std::string path;           // voller Pfad, z. B. "NARUTO/Content/Characters/.../X.uasset"
        uint64_t offset = 0, size = 0, usize = 0;
        uint32_t method = 0;        // 0 = roh, 1 = zlib
        bool encrypted = false;
        uint32_t blockSize = 0;
        std::vector<std::pair<uint64_t, uint64_t>> blocks;   // absolut (Pak-Version < 5)
    };
    const std::vector<Entry>& Entries() const { return entries_; }
    bool Read(const Entry& e, std::vector<uint8_t>& out, std::string& err) const;
    // nur einen Ausschnitt (nur bei unkomprimierten, unverschluesselten Eintraegen direkt)
    bool ReadRange(const Entry& e, uint64_t off, uint64_t len, std::vector<uint8_t>& out, std::string& err) const;

private:
    bool ReadAt(uint64_t off, void* dst, size_t len) const;
    std::wstring name_;
    std::string mount_;
    std::vector<Entry> entries_;
    uint8_t key_[32] = {};
    void* fh_ = nullptr;
};

// Alle Paks des Spiels (NARUTO\Content\Paks\*.pak)
class Archive {
public:
    bool Open(const std::wstring& gameDir, std::string& err);
    std::wstring GameDir() const { return gameDir_; }
    std::vector<std::unique_ptr<Pak>>& Paks() { return paks_; }

    struct FileRef { std::string path; int pak; uint32_t entry; };
    const std::vector<FileRef>& AllFiles() const { return all_; }
    // Pfad wie "NARUTO/Content/Characters/.../Foo.uasset" (Gross/Klein egal)
    const FileRef* Find(const std::string& path) const;
    bool ReadFile(const std::string& path, std::vector<uint8_t>& out, std::string& err) const;
    bool ReadFileRange(const std::string& path, uint64_t off, uint64_t len, std::vector<uint8_t>& out, std::string& err) const;
    bool Has(const std::string& path) const { return Find(path) != nullptr; }

private:
    std::wstring gameDir_;
    std::vector<std::unique_ptr<Pak>> paks_;
    std::vector<FileRef> all_;
    std::unordered_map<std::string, size_t> byPath_;   // kleingeschrieben
};

// Spielordner pruefen: ...\Naruto To Boruto mit NARUTO\Content\Paks\*.pak
bool IstSpielordner(const std::wstring& o);

std::string Lower(std::string s);
std::string Narrow(const std::wstring& w);
std::wstring Widen(const std::string& s);

} // namespace ns

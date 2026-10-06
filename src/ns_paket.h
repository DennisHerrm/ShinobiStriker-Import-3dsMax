// ns_paket.h - UE-4.16-Pakete (.uasset + .uexp): Namen, Importe, Exporte, Eigenschaften
//
// Gekocht und unversioniert (LegacyFileVersion -7, FileVersionUE4 0): die Engine-Version
// ist fest 4.16 (Engine\Build\Build.version). Die Exportdaten liegen in der .uexp; ihre
// SerialOffsets zaehlen ab dem Anfang der .uasset, als waeren beide Dateien eine.
#pragma once
#include "ns_io.h"
#include <map>
#include <cstring>

namespace ns {

// Spielpfad "NARUTO/Content/X.uasset" <-> Paketname "/Game/X"
std::string FileToPackageName(const std::string& file);
std::string PackageNameToFile(const std::string& pkg);

struct Import {
    std::string classPackage, className, objectName;
    int32_t outer = 0;
};

struct Export {
    std::string name;
    std::string className;          // "SkeletalMesh" usw.
    int32_t cls = 0, super = 0, tmpl = 0, outer = 0;
    uint32_t flags = 0;
    int64_t serialSize = 0, serialOffset = 0;
    uint64_t dataOffset = 0;        // im Paketpuffer (= serialOffset)
};

struct Package {
    std::string name;               // /Game/...
    std::string file;               // NARUTO/Content/...uasset
    uint32_t flags = 0;
    int32_t headerSize = 0;         // TotalHeaderSize = Groesse der .uasset
    int64_t bulkStart = 0;          // BulkDataStartOffset
    std::vector<std::string> names;
    std::vector<Import> imports;
    std::vector<Export> exports;
    std::vector<uint8_t> data;      // .uasset + .uexp
    std::string Name(int32_t idx, int32_t num) const;
    int FindExport(const std::string& cls) const;   // erster Export dieser Klasse
};

// Leser fuer serialisierte Daten mit FName-Aufloesung ueber das Paket
struct Reader {
    const uint8_t* p; size_t n; size_t o = 0; bool bad = false;
    const Package* pkg = nullptr;
    Reader(const uint8_t* d, size_t s, const Package* k = nullptr) : p(d), n(s), pkg(k) {}
    template<class T> T get() { T v{}; if (o + sizeof(T) > n) { bad = true; o = n; return v; } memcpy(&v, p + o, sizeof(T)); o += sizeof(T); return v; }
    uint8_t u8() { return get<uint8_t>(); }
    uint16_t u16() { return get<uint16_t>(); }
    uint32_t u32() { return get<uint32_t>(); }
    int32_t i32() { return get<int32_t>(); }
    int64_t i64() { return get<int64_t>(); }
    float f32() { return get<float>(); }
    bool b32() { return u32() != 0; }
    void skip(size_t k) { if (o + k > n) { bad = true; o = n; } else o += k; }
    std::string fname() { int32_t i = i32(); int32_t m = i32(); return pkg ? pkg->Name(i, m) : std::string(); }
    std::string fstr();
    bool ok(size_t need) { if (o + need > n) { bad = true; return false; } return true; }
};

// --- Eigenschaften (getaggt) als Baum, fuer Dump und gezieltes Auslesen ---
struct Prop {
    std::string name, type, structName, inner;
    int32_t arrayIndex = 0;
    double num = 0;              // Int/Float/Bool/Byte-Zahl
    std::string str;             // Name/Str/Enum/Objektpfad
    int32_t obj = 0;             // FPackageIndex bei ObjectProperty
    std::vector<Prop> kids;      // Struct-Felder oder Array-Elemente
    std::vector<double> vec;     // Vector/Rotator/Quat/... Komponenten
    size_t valOffset = 0, valSize = 0;
    const Prop* Get(const std::string& n) const { for (auto& k : kids) if (k.name == n) return &k; return nullptr; }
};
bool ReadProps(Reader& r, std::vector<Prop>& out, int depth = 0);
const Prop* FindProp(const std::vector<Prop>& ps, const std::string& n);
void DumpProps(const std::vector<Prop>& ps, std::string& out, int ind = 0);

class Game {
public:
    bool Open(const std::wstring& gameDir, std::string& err);
    Archive& Ar() { return ar_; }
    // Datei "NARUTO/Content/...uasset" oder Paketname "/Game/..."
    bool LoadPackage(const std::string& fileOrName, Package& pk, std::string& err);
    // nur Kopf (Namen, Importe, Exporte) - schnell, fuer den Katalog
    bool LoadHeader(const std::string& fileOrName, Package& pk, std::string& err);
    // Import -> Paketname, Objektname, Klasse
    bool ResolveImport(const Package& pk, int importIdx, std::string& pkgName, std::string& objName, std::string& className) const;
    // FPackageIndex (neg = Import, pos = Export+1) als lesbarer Pfad "/Game/A/B.C"
    std::string IndexToPath(const Package& pk, int32_t idx) const;
    // Bulkdaten: in der .ubulk (separat) oder am Ende des Pakets
    bool ReadBulk(const Package& pk, uint32_t flags, int64_t offset, int64_t size, std::vector<uint8_t>& out, std::string& err);

private:
    bool Lade(const std::string& fileOrName, Package& pk, bool mitDaten, std::string& err);
    Archive ar_;
};

// Bulk-Kopf (FUntypedBulkData, 4.16): Flags, ElementCount, SizeOnDisk, OffsetInFile
struct BulkKopf {
    uint32_t flags = 0;
    int64_t count = 0, size = 0, offset = 0;
    size_t inlineAt = 0;         // bei ForceInline: Position der Daten im Leser
};
bool LiesBulkKopf(Reader& r, BulkKopf& b);
enum : uint32_t {
    BULK_AmEnde = 0x1, BULK_Zlib = 0x2, BULK_Unbenutzt = 0x20, BULK_Inline = 0x40, BULK_Separat = 0x100
};

} // namespace ns

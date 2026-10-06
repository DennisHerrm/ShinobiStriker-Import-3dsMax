// ns_paket.cpp - UE-4.16-Pakete lesen
#include "ns_paket.h"
#include <algorithm>
#include <cstdio>

#pragma warning(push, 0)
#include "miniz.h"
#pragma warning(pop)

namespace ns {

std::string FileToPackageName(const std::string& file) {
    std::string f = file;
    size_t dot = f.find_last_of('.');
    if (dot != std::string::npos && f.find('/', dot) == std::string::npos) f = f.substr(0, dot);
    // "NARUTO/Content/X" -> "/Game/X", "Engine/Content/X" -> "/Engine/X",
    // "<...>/Plugins/<P>/Content/X" -> "/<P>/X"
    size_t c = f.find("/Content/");
    if (c == std::string::npos) return "/" + f;
    std::string root = f.substr(0, c), rest = f.substr(c + 9);
    size_t sl = root.find_last_of('/');
    std::string mod = sl == std::string::npos ? root : root.substr(sl + 1);
    if (_stricmp(mod.c_str(), "NARUTO") == 0) mod = "Game";
    return "/" + mod + "/" + rest;
}

std::string PackageNameToFile(const std::string& pkg) {
    std::string p = pkg;
    size_t dot = p.find('.');
    if (dot != std::string::npos) p = p.substr(0, dot);
    if (p.rfind("/Game/", 0) == 0) return "NARUTO/Content/" + p.substr(6) + ".uasset";
    if (p.rfind("/Engine/", 0) == 0) return "Engine/Content/" + p.substr(8) + ".uasset";
    return p.empty() ? p : p.substr(1) + ".uasset";
}

std::string Package::Name(int32_t idx, int32_t num) const {
    std::string s = idx >= 0 && (size_t)idx < names.size() ? names[(size_t)idx] : ("?name" + std::to_string(idx));
    if (num > 0) s += "_" + std::to_string(num - 1);
    return s;
}

int Package::FindExport(const std::string& cls) const {
    for (size_t i = 0; i < exports.size(); i++) if (exports[i].className == cls) return (int)i;
    return -1;
}

std::string Reader::fstr() {
    int32_t len = i32();
    if (len == 0) return {};
    if (len > 0) {
        if (!ok((size_t)len) || len > (1 << 20)) { bad = true; return {}; }
        std::string s((const char*)p + o, (size_t)len - 1); o += (size_t)len; return s;
    }
    size_t cnt = (size_t)(-(int64_t)len);
    if (cnt > (1 << 20) || !ok(cnt * 2)) { bad = true; return {}; }
    std::wstring w((const wchar_t*)(p + o), cnt - 1); o += cnt * 2; return Narrow(w);
}

bool LiesBulkKopf(Reader& r, BulkKopf& b) {
    b.flags = r.u32();
    b.count = r.i32();
    b.size = r.i32();
    b.offset = r.i64();
    if (b.flags & BULK_Inline) {
        b.inlineAt = r.o;
        if (b.size < 0 || !r.ok((size_t)b.size)) return false;
        r.skip((size_t)b.size);
    }
    return !r.bad;
}

// ---------------------------------------------------------------- Spiel
bool Game::Open(const std::wstring& gameDir, std::string& err) {
    return ar_.Open(gameDir, err);
}

bool Game::LoadPackage(const std::string& fileOrName, Package& pk, std::string& err) { return Lade(fileOrName, pk, true, err); }
bool Game::LoadHeader(const std::string& fileOrName, Package& pk, std::string& err) { return Lade(fileOrName, pk, false, err); }

bool Game::Lade(const std::string& fileOrName, Package& pk, bool mitDaten, std::string& err) {
    std::string file = fileOrName;
    if (!file.empty() && file[0] == '/') file = PackageNameToFile(fileOrName);
    pk = Package();
    std::vector<uint8_t> kopf;
    if (!ar_.ReadFile(file, kopf, err)) {
        // .umap statt .uasset
        std::string m = file.size() > 7 ? file.substr(0, file.size() - 7) + ".umap" : file;
        if (!ar_.ReadFile(m, kopf, err)) { err = "Paket nicht gefunden: " + fileOrName; return false; }
        file = m;
    }
    pk.file = ar_.Find(file)->path;
    pk.name = FileToPackageName(pk.file);
    Reader r(kopf.data(), kopf.size());
    if (r.u32() != 0x9E2A83C1u) { err = "kein UE-Paket"; return false; }
    int32_t legacy = r.i32();
    if (legacy != -7 && legacy != -6) { err = "Paketversion " + std::to_string(legacy) + " unbekannt"; return false; }
    r.i32();                                   // LegacyUE3Version
    r.i32(); r.i32();                          // FileVersionUE4, Licensee (0 = unversioniert)
    int32_t ncv = r.i32();                     // CustomVersions (Guid + int32)
    if (ncv < 0 || ncv > 1000) { err = "CustomVersions kaputt"; return false; }
    r.skip((size_t)ncv * 20);
    pk.headerSize = r.i32();
    r.fstr();                                  // FolderName
    pk.flags = r.u32();
    int32_t nameCount = r.i32(), nameOff = r.i32();
    r.i32(); r.i32();                          // GatherableTextData
    int32_t expCount = r.i32(), expOff = r.i32(), impCount = r.i32(), impOff = r.i32();
    r.i32();                                   // DependsOffset
    r.i32(); r.i32();                          // StringAssetReferences
    r.i32();                                   // SearchableNamesOffset
    r.i32();                                   // ThumbnailTableOffset
    r.skip(16);                                // Guid
    int32_t ngen = r.i32();
    if (ngen < 0 || ngen > 1000) { err = "Generations kaputt"; return false; }
    r.skip((size_t)ngen * 8);
    for (int k = 0; k < 2; k++) { r.skip(10); r.fstr(); }   // Saved-/CompatibleWithEngineVersion
    r.u32();                                   // CompressionFlags
    int32_t ncc = r.i32();
    if (ncc != 0) { err = "komprimiertes Paket nicht unterstuetzt"; return false; }
    r.u32();                                   // PackageSource
    int32_t nadd = r.i32();
    for (int32_t i = 0; i < nadd && !r.bad; i++) r.fstr();
    r.i32();                                   // AssetRegistryDataOffset
    pk.bulkStart = r.i64();
    if (r.bad) { err = "Paketkopf kaputt"; return false; }

    auto innen = [&](int64_t off, int64_t len) { return off >= 0 && len >= 0 && (uint64_t)(off + len) <= kopf.size(); };
    if (nameCount < 0 || !innen(nameOff, 0) || expCount < 0 || impCount < 0 ||
        !innen(impOff, (int64_t)impCount * 28) || !innen(expOff, (int64_t)expCount * 104)) {
        err = "Tabellen ausserhalb"; return false;
    }
    Reader nr(kopf.data() + nameOff, kopf.size() - (size_t)nameOff);
    pk.names.reserve((size_t)nameCount);
    for (int32_t i = 0; i < nameCount; i++) {
        pk.names.push_back(nr.fstr());
        nr.skip(4);                            // Hashes
        if (nr.bad) { err = "Namensliste kaputt"; return false; }
    }
    Reader ir(kopf.data() + impOff, (size_t)impCount * 28, &pk);
    pk.imports.resize((size_t)impCount);
    for (auto& im : pk.imports) {
        im.classPackage = ir.fname();
        im.className = ir.fname();
        im.outer = ir.i32();
        im.objectName = ir.fname();
    }
    Reader er(kopf.data() + expOff, (size_t)expCount * 104, &pk);
    pk.exports.resize((size_t)expCount);
    for (auto& ex : pk.exports) {
        ex.cls = er.i32(); ex.super = er.i32(); ex.tmpl = er.i32(); ex.outer = er.i32();
        ex.name = er.fname();
        ex.flags = er.u32();
        ex.serialSize = er.i64();
        ex.serialOffset = er.i64();
        er.skip(12 + 16 + 4 + 8 + 20);
        ex.dataOffset = (uint64_t)std::max<int64_t>(0, ex.serialOffset);
    }
    for (auto& ex : pk.exports) {
        if (ex.cls < 0 && (size_t)(-ex.cls - 1) < pk.imports.size()) ex.className = pk.imports[(size_t)(-ex.cls - 1)].objectName;
        else if (ex.cls > 0 && (size_t)ex.cls <= pk.exports.size()) ex.className = pk.exports[(size_t)ex.cls - 1].name;
        else ex.className = "Class";
    }
    if (!mitDaten) return true;
    pk.data.swap(kopf);
    const std::string uexp = pk.file.substr(0, pk.file.find_last_of('.')) + ".uexp";
    if (ar_.Has(uexp)) {
        std::vector<uint8_t> rest;
        if (!ar_.ReadFile(uexp, rest, err)) return false;
        pk.data.insert(pk.data.end(), rest.begin(), rest.end());
    }
    for (auto& ex : pk.exports)
        if (ex.serialSize < 0 || ex.dataOffset + (uint64_t)ex.serialSize > pk.data.size()) { err = "Export " + ex.name + " ausserhalb"; return false; }
    return true;
}

bool Game::ResolveImport(const Package& pk, int importIdx, std::string& pkgName, std::string& objName, std::string& className) const {
    if (importIdx < 0 || importIdx >= (int)pk.imports.size()) return false;
    const Import& im = pk.imports[(size_t)importIdx];
    objName = im.objectName;
    className = im.className;
    // Kette der Outer bis zum Paket (Outer 0)
    int32_t o = im.outer;
    std::string zwischen;
    for (int tiefe = 0; o != 0 && tiefe < 16; tiefe++) {
        if (o > 0) { pkgName = pk.name; return true; }
        const Import& oi = pk.imports[(size_t)(-o - 1)];
        if (oi.outer == 0) { pkgName = oi.objectName; if (!zwischen.empty()) objName = zwischen + "." + objName; return true; }
        zwischen = zwischen.empty() ? oi.objectName : oi.objectName + "." + zwischen;
        o = oi.outer;
    }
    pkgName = im.objectName;   // selbst ein Paket
    return true;
}

std::string Game::IndexToPath(const Package& pk, int32_t idx) const {
    if (idx == 0) return "None";
    if (idx > 0) return (size_t)idx - 1 < pk.exports.size() ? pk.name + "." + pk.exports[(size_t)idx - 1].name : "?";
    std::string pn, on, cn;
    if (ResolveImport(pk, -idx - 1, pn, on, cn)) return pn + "." + on;
    char b[32]; snprintf(b, sizeof b, "Import#%d", -idx - 1); return b;
}

bool Game::ReadBulk(const Package& pk, uint32_t flags, int64_t offset, int64_t size, std::vector<uint8_t>& out, std::string& err) {
    if (size < 0 || offset < 0) { err = "Bulk-Kopf kaputt"; return false; }
    if (flags & BULK_Separat) {
        const std::string ub = pk.file.substr(0, pk.file.find_last_of('.')) + ".ubulk";
        if (!ar_.ReadFileRange(ub, (uint64_t)offset, (uint64_t)size, out, err)) return false;
    } else {
        // am Ende des Pakets: Offset zaehlt ab BulkDataStartOffset
        const uint64_t o = (uint64_t)(offset + ((flags & BULK_AmEnde) ? pk.bulkStart : 0));
        if (o + (uint64_t)size > pk.data.size()) { err = "Bulk ausserhalb des Pakets"; return false; }
        out.assign(pk.data.begin() + (size_t)o, pk.data.begin() + (size_t)(o + (uint64_t)size));
    }
    return true;
}

// ---------------------------------------------------------------- Eigenschaften
static int NativeStructSize(const std::string& s) {
    static const std::map<std::string, int> m = {
        {"Vector", 12}, {"Vector4", 16}, {"Vector2D", 8}, {"Rotator", 12}, {"Quat", 16}, {"Guid", 16},
        {"Color", 4}, {"LinearColor", 16}, {"IntPoint", 8}, {"IntVector", 12}, {"Box", 25}, {"Box2D", 17},
        {"Plane", 16}, {"Matrix", 64}, {"BoxSphereBounds", 28}, {"DateTime", 8}, {"Timespan", 8},
        {"Vector_NetQuantize", 12}, {"Vector_NetQuantizeNormal", 12}, {"Vector_NetQuantize10", 12},
        {"Vector_NetQuantize100", 12}, {"TwoVectors", 24} };
    auto it = m.find(s);
    return it == m.end() ? -1 : it->second;
}

static void ReadNativeStruct(Reader& r, Prop& p, int size) {
    int nf = size / 4;
    if (p.structName == "Color") { for (int i = 0; i < 4; i++) p.vec.push_back(r.u8()); return; }
    if (p.structName == "Guid") { for (int i = 0; i < 4; i++) p.vec.push_back(r.u32()); return; }
    if (p.structName == "Box" || p.structName == "Box2D") { for (int i = 0; i < nf; i++) p.vec.push_back(r.f32()); r.u8(); return; }
    if (p.structName == "DateTime" || p.structName == "Timespan") { p.vec.push_back((double)r.i64()); return; }
    for (int i = 0; i < nf; i++) p.vec.push_back(r.f32());
}

// Wert lesen (ohne Tag). size = Bytes laut Tag (nur fuer Unbekanntes noetig)
static void ReadValue(Reader& r, Prop& p, const std::string& type, size_t size, bool inArray, int depth) {
    size_t start = r.o;
    if (type == "IntProperty" || type == "UInt32Property") p.num = type[0] == 'I' ? r.i32() : r.u32();
    else if (type == "Int8Property") p.num = (int8_t)r.u8();
    else if (type == "Int16Property" || type == "UInt16Property") p.num = type[0] == 'I' ? (int16_t)r.u16() : r.u16();
    else if (type == "Int64Property" || type == "UInt64Property") p.num = (double)r.i64();
    else if (type == "FloatProperty") p.num = r.f32();
    else if (type == "DoubleProperty") p.num = r.get<double>();
    else if (type == "BoolProperty") { if (inArray) p.num = r.u8(); }
    else if (type == "NameProperty") p.str = r.fname();
    else if (type == "StrProperty") p.str = r.fstr();
    else if (type == "ObjectProperty" || type == "ClassProperty" || type == "WeakObjectProperty" ||
             type == "InterfaceProperty" || type == "LazyObjectProperty") p.obj = r.i32();
    else if (type == "AssetObjectProperty" || type == "AssetClassProperty") p.str = r.fstr();      // 4.16: FStringAssetReference
    else if (type == "SoftObjectProperty" || type == "SoftClassProperty") { p.str = r.fname(); std::string sub = r.fstr(); if (!sub.empty()) p.str += ":" + sub; }
    else if (type == "EnumProperty") p.str = r.fname();
    else if (type == "ByteProperty") {
        if (inArray) { if (size == 8) p.str = r.fname(); else p.num = r.u8(); }
        else if (p.structName.empty() || p.structName == "None") p.num = r.u8();   // structName haelt hier den EnumName
        else p.str = r.fname();
    }
    else if (type == "TextProperty") { r.skip(size); }
    else if (type == "StructProperty") {
        int ns = NativeStructSize(p.structName);
        if (ns > 0) ReadNativeStruct(r, p, ns);
        else if (p.structName == "StringAssetReference" || p.structName == "StringClassReference") p.str = r.fstr();
        else if (!inArray && size > 0) {
            Reader sub(r.p + r.o, size, r.pkg);
            if (!ReadProps(sub, p.kids, depth + 1) || sub.o != size) p.kids.clear();
            r.skip(size);
        } else if (inArray) ReadProps(r, p.kids, depth + 1);
    }
    else if (type == "ArrayProperty" || type == "SetProperty") {
        Reader sub(r.p + r.o, size, r.pkg);
        if (type == "SetProperty") sub.i32();   // zu entfernende Elemente
        int32_t cnt = sub.i32();
        std::string it = p.inner;
        Prop tmpl;
        if (it == "StructProperty" && type == "ArrayProperty") {
            tmpl.name = sub.fname(); sub.fname(); sub.i32(); sub.i32();
            tmpl.structName = sub.fname(); sub.skip(16); if (sub.u8()) sub.skip(16);
        }
        size_t elemSize = cnt > 0 ? (size - sub.o) / (size_t)cnt : 0;
        for (int32_t i = 0; i < cnt && !sub.bad && i < 1000000; i++) {
            Prop e; e.name = std::to_string(i); e.type = it; e.structName = tmpl.structName;
            ReadValue(sub, e, it, elemSize, true, depth + 1);
            p.kids.push_back(std::move(e));
        }
        r.skip(size);
    }
    else r.skip(size);   // MapProperty, Delegates usw.
    if (type != "ArrayProperty" && type != "SetProperty" && type != "StructProperty" && !inArray && type != "TextProperty" && type != "BoolProperty")
        if (r.o - start != size) r.o = start + size;   // Sicherheitsnetz
    p.valOffset = start; p.valSize = r.o - start;
}

bool ReadProps(Reader& r, std::vector<Prop>& out, int depth) {
    if (depth > 32) return false;
    while (!r.bad) {
        Prop p;
        p.name = r.fname();
        if (r.bad) return false;
        if (p.name == "None") return true;
        p.type = r.fname();
        int32_t size = r.i32();
        p.arrayIndex = r.i32();
        if (p.type == "StructProperty") { p.structName = r.fname(); r.skip(16); }
        else if (p.type == "BoolProperty") { p.num = r.u8(); }
        else if (p.type == "ByteProperty" || p.type == "EnumProperty") { p.structName = r.fname(); }
        else if (p.type == "ArrayProperty" || p.type == "SetProperty") { p.inner = r.fname(); }
        else if (p.type == "MapProperty") { p.inner = r.fname(); r.fname(); }
        if (r.u8()) r.skip(16);
        if (size < 0 || !r.ok((size_t)size)) { r.bad = true; return false; }
        size_t end = r.o + (size_t)size;
        ReadValue(r, p, p.type, (size_t)size, false, depth);
        r.o = end;
        out.push_back(std::move(p));
    }
    return false;
}

const Prop* FindProp(const std::vector<Prop>& ps, const std::string& n) {
    for (auto& p : ps) if (p.name == n) return &p;
    return nullptr;
}

void DumpProps(const std::vector<Prop>& ps, std::string& out, int ind) {
    char b[256];
    for (auto& p : ps) {
        out.append((size_t)ind * 2, ' ');
        out += p.name;
        if (p.arrayIndex) out += "[" + std::to_string(p.arrayIndex) + "]";
        out += " (" + p.type + (p.structName.empty() ? "" : " " + p.structName) + (p.inner.empty() ? "" : " of " + p.inner) + ")";
        if (!p.str.empty()) out += " = " + p.str;
        else if (p.type == "ObjectProperty") out += " = obj " + std::to_string(p.obj);
        else if (!p.vec.empty()) { out += " ="; for (double v : p.vec) { snprintf(b, sizeof b, " %g", v); out += b; } }
        else if (p.kids.empty()) { snprintf(b, sizeof b, " = %g", p.num); out += b; }
        if ((p.type == "ArrayProperty" || p.type == "SetProperty") && p.kids.size() > 0) out += " [" + std::to_string(p.kids.size()) + "]";
        out += "\n";
        if (!p.kids.empty()) {
            size_t lim = std::min<size_t>(p.kids.size(), 40);
            std::vector<Prop> sub(p.kids.begin(), p.kids.begin() + (ptrdiff_t)lim);
            DumpProps(sub, out, ind + 1);
            if (lim < p.kids.size()) { out.append((size_t)ind * 2 + 2, ' '); out += "...\n"; }
        }
    }
}

} // namespace ns

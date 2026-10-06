// ns_io.cpp - Pak-Archive lesen (UE 4.16, Pak-Version 4, Index AES-256-ECB)
#include "ns_io.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <cstdio>

#pragma warning(push, 0)
#include "miniz.h"
#pragma warning(pop)

#pragma comment(lib, "bcrypt.lib")

namespace ns {

const char* const kStandardSchluessel = "L%#)TuTw=n@fYRZa=~l>~ENkUE%i/>S(";

std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

bool SchluesselAusText(const std::string& s, uint8_t key[32]) {
    if (s.size() == 66 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        for (int i = 0; i < 32; i++) {
            unsigned v = 0;
            if (sscanf_s(s.c_str() + 2 + i * 2, "%2x", &v) != 1) return false;
            key[i] = (uint8_t)v;
        }
        return true;
    }
    if (s.size() != 32) return false;
    memcpy(key, s.data(), 32);
    return true;
}

namespace {

// AES-256-ECB entschluesseln (in place, Laenge Vielfaches von 16)
bool AesEntschluesseln(const uint8_t key[32], uint8_t* d, size_t n) {
    if (n % 16) return false;
    if (n == 0) return true;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE k = nullptr;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0) == 0 &&
            BCryptGenerateSymmetricKey(alg, &k, nullptr, 0, (PUCHAR)key, 32, 0) == 0) {
            ok = true;
            for (size_t o = 0; o < n && ok; o += 0x40000000) {
                ULONG len = (ULONG)std::min<size_t>(n - o, 0x40000000), got = 0;
                ok = BCryptDecrypt(k, d + o, len, nullptr, nullptr, 0, d + o, len, &got, 0) == 0 && got == len;
            }
            BCryptDestroyKey(k);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return ok;
}

struct Rd {
    const uint8_t* p; size_t n; size_t o = 0; bool bad = false;
    Rd(const uint8_t* d, size_t s) : p(d), n(s) {}
    template<class T> T get() { T v{}; if (o + sizeof(T) > n) { bad = true; o = n; return v; } memcpy(&v, p + o, sizeof(T)); o += sizeof(T); return v; }
    void skip(size_t k) { if (o + k > n) { bad = true; o = n; } else o += k; }
    std::string fstr() {
        int32_t len = get<int32_t>();
        if (len == 0) return {};
        if (len > 0) {
            if ((size_t)len > n - o || len > 4096) { bad = true; return {}; }
            std::string s((const char*)p + o, len - 1); o += len; return s;
        }
        size_t cnt = (size_t)(-(int64_t)len);
        if (cnt * 2 > n - o || cnt > 4096) { bad = true; return {}; }
        std::wstring w((const wchar_t*)(p + o), cnt - 1); o += cnt * 2; return Narrow(w);
    }
};

std::string OhneDotDot(std::string m) {
    while (m.rfind("../", 0) == 0) m = m.substr(3);
    return m;
}

constexpr uint32_t kPakMagic = 0x5A6F12E1;

} // namespace

Pak::~Pak() { if (fh_) CloseHandle((HANDLE)fh_); }

bool Pak::ReadAt(uint64_t off, void* dst, size_t len) const {
    // Positionslesen mit OVERLAPPED: threadsicher ohne Sperre
    uint8_t* d = static_cast<uint8_t*>(dst);
    while (len > 0) {
        DWORD chunk = (DWORD)std::min<size_t>(len, 0x10000000), got = 0;
        OVERLAPPED ov{};
        ov.Offset = (DWORD)off; ov.OffsetHigh = (DWORD)(off >> 32);
        if (!::ReadFile((HANDLE)fh_, d, chunk, &got, &ov) || got != chunk) return false;
        d += chunk; off += chunk; len -= chunk;
    }
    return true;
}

bool Pak::Open(const std::wstring& path, const uint8_t key[32], std::string& err) {
    name_ = path.substr(path.find_last_of(L"\\/") + 1);
    memcpy(key_, key, 32);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = "Pak nicht lesbar: " + Narrow(name_); return false; }
    fh_ = h;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    // Fuss Version 4: bEncryptedIndex u8, Magic, Version, IndexOffset, IndexSize, Hash[20] = 45 Bytes
    uint8_t ft[45];
    if (sz.QuadPart < 45 || !ReadAt((uint64_t)sz.QuadPart - 45, ft, 45)) { err = "Pak-Fuss nicht lesbar"; return false; }
    uint32_t magic; int32_t ver; int64_t ioff, isize;
    memcpy(&magic, ft + 1, 4); memcpy(&ver, ft + 5, 4); memcpy(&ioff, ft + 9, 8); memcpy(&isize, ft + 17, 8);
    const bool encIndex = ft[0] != 0;
    if (magic != kPakMagic) { err = "keine Pak-Datei (Magic fehlt): " + Narrow(name_); return false; }
    if (ver < 3 || ver > 4) { err = "Pak-Version " + std::to_string(ver) + " nicht unterstuetzt"; return false; }
    if (ioff < 0 || isize <= 0 || ioff + isize > sz.QuadPart || isize > 0x40000000) { err = "Pak-Index ausserhalb"; return false; }
    std::vector<uint8_t> idx((size_t)isize);
    if (!ReadAt((uint64_t)ioff, idx.data(), idx.size())) { err = "Pak-Index nicht lesbar"; return false; }
    if (encIndex && !AesEntschluesseln(key_, idx.data(), idx.size() & ~(size_t)15)) { err = "AES fehlgeschlagen"; return false; }
    Rd r(idx.data(), idx.size());
    mount_ = OhneDotDot(r.fstr());
    int32_t n = r.get<int32_t>();
    if (r.bad || n < 0 || n > 10000000) {
        err = encIndex ? "Pak-Index nicht entschluesselbar - falscher AES-Schluessel?" : "Pak-Index kaputt";
        return false;
    }
    entries_.resize((size_t)n);
    for (auto& e : entries_) {
        e.path = mount_ + r.fstr();
        e.offset = (uint64_t)r.get<int64_t>();
        e.size = (uint64_t)r.get<int64_t>();
        e.usize = (uint64_t)r.get<int64_t>();
        e.method = r.get<uint32_t>();
        r.skip(20);                                   // SHA1
        if (e.method) {
            int32_t nb = r.get<int32_t>();
            if (nb < 0 || nb > 1000000) { r.bad = true; break; }
            e.blocks.resize((size_t)nb);
            for (auto& b : e.blocks) { b.first = (uint64_t)r.get<int64_t>(); b.second = (uint64_t)r.get<int64_t>(); }
        }
        e.encrypted = r.get<uint8_t>() != 0;
        e.blockSize = r.get<uint32_t>();
        if (r.bad) break;
    }
    if (r.bad) { err = "Pak-Index kaputt (" + Narrow(name_) + ")"; entries_.clear(); return false; }
    return true;
}

static size_t KopfGroesse(const Pak::Entry& e) {
    // FPakEntry vor den Daten: Offset, Size, USize, Method, Hash, [Bloecke], Encrypted, BlockSize
    return 8 + 8 + 8 + 4 + 20 + (e.method ? 4 + e.blocks.size() * 16 : 0) + 1 + 4;
}

bool Pak::Read(const Entry& e, std::vector<uint8_t>& out, std::string& err) const {
    if (e.usize > 0x7FFFFFFF) { err = "Datei zu gross"; return false; }
    if (e.method == 0) {
        size_t lesen = (size_t)e.size;
        if (e.encrypted) lesen = (lesen + 15) & ~(size_t)15;
        out.resize(lesen);
        if (!ReadAt(e.offset + KopfGroesse(e), out.data(), lesen)) { err = "Lesefehler"; return false; }
        if (e.encrypted && !AesEntschluesseln(key_, out.data(), lesen)) { err = "AES fehlgeschlagen"; return false; }
        out.resize((size_t)e.size);
        return true;
    }
    if (e.method != 1) { err = "Kompression " + std::to_string(e.method) + " nicht unterstuetzt"; return false; }
    out.resize((size_t)e.usize);
    size_t pos = 0;
    std::vector<uint8_t> blk;
    for (auto& b : e.blocks) {
        size_t len = (size_t)(b.second - b.first);
        size_t lesen = e.encrypted ? (len + 15) & ~(size_t)15 : len;
        blk.resize(lesen);
        if (!ReadAt(b.first, blk.data(), lesen)) { err = "Lesefehler"; return false; }
        if (e.encrypted && !AesEntschluesseln(key_, blk.data(), lesen)) { err = "AES fehlgeschlagen"; return false; }
        mz_ulong dl = (mz_ulong)std::min<size_t>(e.blockSize ? e.blockSize : out.size() - pos, out.size() - pos);
        if (mz_uncompress(out.data() + pos, &dl, blk.data(), (mz_ulong)len) != MZ_OK) { err = "zlib-Fehler"; return false; }
        pos += dl;
    }
    if (pos != out.size()) { err = "entpackte Groesse passt nicht"; return false; }
    return true;
}

bool Pak::ReadRange(const Entry& e, uint64_t off, uint64_t len, std::vector<uint8_t>& out, std::string& err) const {
    if (off + len > e.usize) { err = "Bereich ausserhalb der Datei"; return false; }
    if (e.method == 0 && !e.encrypted) {
        out.resize((size_t)len);
        if (!ReadAt(e.offset + KopfGroesse(e) + off, out.data(), (size_t)len)) { err = "Lesefehler"; return false; }
        return true;
    }
    std::vector<uint8_t> all;
    if (!Read(e, all, err)) return false;
    out.assign(all.begin() + (size_t)off, all.begin() + (size_t)(off + len));
    return true;
}

bool IstSpielordner(const std::wstring& o) {
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((o + L"\\NARUTO\\Content\\Paks\\*.pak").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return false;
    FindClose(f);
    return true;
}

bool Archive::Open(const std::wstring& gameDir, std::string& err) {
    gameDir_ = gameDir;
    uint8_t key[32];
    SchluesselAusText(kStandardSchluessel, key);
    wchar_t env[200];
    DWORD el = GetEnvironmentVariableW(L"NSIMPORT_AES", env, 200);
    if (el > 0 && el < 200 && !SchluesselAusText(Narrow(env), key)) { err = "NSIMPORT_AES ist kein gueltiger Schluessel"; return false; }

    const std::wstring dir = gameDir + L"\\NARUTO\\Content\\Paks\\";
    std::vector<std::wstring> namen;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"*.pak").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) { err = "keine .pak in NARUTO\\Content\\Paks"; return false; }
    do namen.push_back(fd.cFileName); while (FindNextFileW(f, &fd));
    FindClose(f);
    // UE-Reihenfolge: Patch-Paks (_P) zuletzt, damit sie gewinnen
    std::sort(namen.begin(), namen.end(), [](const std::wstring& a, const std::wstring& b) {
        const bool pa = a.find(L"_P.pak") != std::wstring::npos, pb = b.find(L"_P.pak") != std::wstring::npos;
        if (pa != pb) return pb;
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
    for (auto& n : namen) {
        auto p = std::make_unique<Pak>();
        std::string e;
        if (!p->Open(dir + n, key, e)) { err = e; return false; }
        paks_.push_back(std::move(p));
    }
    for (size_t pi = 0; pi < paks_.size(); pi++) {
        const auto& es = paks_[pi]->Entries();
        for (uint32_t i = 0; i < es.size(); i++) {
            std::string l = Lower(es[i].path);
            auto it = byPath_.find(l);
            if (it != byPath_.end()) { all_[it->second] = { es[i].path, (int)pi, i }; continue; }
            byPath_[l] = all_.size();
            all_.push_back({ es[i].path, (int)pi, i });
        }
    }
    if (all_.empty()) { err = "Paks sind leer"; return false; }
    return true;
}

const Archive::FileRef* Archive::Find(const std::string& path) const {
    auto it = byPath_.find(Lower(path));
    return it == byPath_.end() ? nullptr : &all_[it->second];
}

bool Archive::ReadFile(const std::string& path, std::vector<uint8_t>& out, std::string& err) const {
    const FileRef* r = Find(path);
    if (!r) { err = "Datei nicht gefunden: " + path; return false; }
    const Pak& p = *paks_[r->pak];
    return p.Read(p.Entries()[r->entry], out, err);
}

bool Archive::ReadFileRange(const std::string& path, uint64_t off, uint64_t len, std::vector<uint8_t>& out, std::string& err) const {
    const FileRef* r = Find(path);
    if (!r) { err = "Datei nicht gefunden: " + path; return false; }
    const Pak& p = *paks_[r->pak];
    return p.ReadRange(p.Entries()[r->entry], off, len, out, err);
}

} // namespace ns

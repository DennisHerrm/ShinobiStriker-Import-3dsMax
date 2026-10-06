// ns_tex.cpp - Texturen und Materialien
#include "ns_tex.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <cstdio>

#pragma warning(push, 0)
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"
#include "miniz.h"
#pragma warning(pop)

namespace ns {

namespace {

float Half(uint16_t h) {
    uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    float v;
    if (e == 0) v = std::ldexp((float)m, -24);
    else if (e == 31) v = m ? 0.f : 65504.f;
    else v = std::ldexp((float)(m | 1024), (int)e - 25);
    return s ? -v : v;
}

uint8_t Byte(float f) { return (uint8_t)std::lround(std::min(1.f, std::max(0.f, f)) * 255.f); }

struct Mip { uint32_t flags = 0; int64_t count = 0, size = 0, offset = 0; size_t inlineAt = 0; int w = 0, h = 0; };

// Blockformate: Bytes je 4x4-Block, 0 = unkomprimiert
int BlockBytes(const std::string& f) {
    if (f == "PF_DXT1" || f == "PF_BC4") return 8;
    if (f == "PF_DXT3" || f == "PF_DXT5" || f == "PF_BC5" || f == "PF_BC6H" || f == "PF_BC7") return 16;
    return 0;
}
int PixelBytes(const std::string& f) {
    if (f == "PF_B8G8R8A8" || f == "PF_R8G8B8A8") return 4;
    if (f == "PF_G8" || f == "PF_A8" || f == "PF_L8") return 1;
    if (f == "PF_FloatRGBA") return 8;
    if (f == "PF_G16") return 2;
    return 0;
}

bool Dekodiere(const std::string& f, const uint8_t* d, size_t n, int w, int h, std::vector<uint8_t>& rgba, std::string& err) {
    rgba.assign((size_t)w * h * 4, 255);
    int bb = BlockBytes(f);
    if (bb) {
        int bw = (w + 3) / 4, bh = (h + 3) / 4;
        if ((size_t)bw * bh * bb > n) { err = "Mip zu kurz"; return false; }
        uint8_t blk[4 * 4 * 4];
        float fblk[4 * 4 * 3];
        for (int by = 0; by < bh; by++)
            for (int bx = 0; bx < bw; bx++) {
                const uint8_t* q = d + ((size_t)by * bw + bx) * bb;
                if (f == "PF_DXT1") bcdec_bc1(q, blk, 16);
                else if (f == "PF_DXT3") bcdec_bc2(q, blk, 16);
                else if (f == "PF_DXT5") bcdec_bc3(q, blk, 16);
                else if (f == "PF_BC7") bcdec_bc7(q, blk, 16);
                else if (f == "PF_BC4") {
                    uint8_t r[16]; bcdec_bc4(q, r, 4);
                    for (int i = 0; i < 16; i++) { blk[i * 4] = blk[i * 4 + 1] = blk[i * 4 + 2] = r[i]; blk[i * 4 + 3] = 255; }
                } else if (f == "PF_BC5") {
                    uint8_t rg[32]; bcdec_bc5(q, rg, 8);
                    for (int i = 0; i < 16; i++) {
                        float x = rg[i * 2] / 127.5f - 1.f, y = rg[i * 2 + 1] / 127.5f - 1.f;
                        float z = std::sqrt(std::max(0.f, 1.f - x * x - y * y));
                        blk[i * 4] = rg[i * 2]; blk[i * 4 + 1] = rg[i * 2 + 1]; blk[i * 4 + 2] = Byte(z * 0.5f + 0.5f); blk[i * 4 + 3] = 255;
                    }
                } else if (f == "PF_BC6H") {
                    bcdec_bc6h_float(q, fblk, 12, 0);
                    for (int i = 0; i < 16; i++) {
                        for (int c = 0; c < 3; c++) { float v = fblk[i * 3 + c]; blk[i * 4 + c] = Byte(v / (1.f + v)); }
                        blk[i * 4 + 3] = 255;
                    }
                }
                for (int y = 0; y < 4; y++) {
                    int py = by * 4 + y; if (py >= h) break;
                    for (int x = 0; x < 4; x++) {
                        int px = bx * 4 + x; if (px >= w) break;
                        memcpy(&rgba[((size_t)py * w + px) * 4], &blk[(y * 4 + x) * 4], 4);
                    }
                }
            }
        return true;
    }
    int pb = PixelBytes(f);
    if (!pb) { err = "Pixelformat " + f + " nicht unterstuetzt"; return false; }
    if ((size_t)w * h * pb > n) { err = "Mip zu kurz"; return false; }
    for (size_t i = 0; i < (size_t)w * h; i++) {
        uint8_t* o = &rgba[i * 4];
        const uint8_t* s = d + i * pb;
        if (f == "PF_B8G8R8A8") { o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = s[3]; }
        else if (f == "PF_R8G8B8A8") memcpy(o, s, 4);
        else if (pb == 1) { o[0] = o[1] = o[2] = s[0]; }
        else if (f == "PF_G16") { o[0] = o[1] = o[2] = s[1]; }
        else if (f == "PF_FloatRGBA") {
            for (int c = 0; c < 4; c++) { uint16_t hv; memcpy(&hv, s + c * 2, 2); o[c] = Byte(Half(hv)); }
        }
    }
    return true;
}

std::string OhnePunkt(const std::string& p) {
    size_t d = p.find('.');
    return d == std::string::npos ? p : p.substr(0, d);
}

bool Enthaelt(const std::string& s, const char* w) { return Lower(s).find(w) != std::string::npos; }

bool EndetMit(const std::string& s, const char* e) {
    std::string l = Lower(s);
    size_t n = strlen(e);
    return l.size() >= n && l.compare(l.size() - n, n, e) == 0;
}

} // namespace

bool LiesTextur(Game& g, const std::string& texPfad, int maxGroesse, std::vector<uint8_t>& rgba, int& w, int& h,
                std::string& format, std::string& err) {
    Package pk;
    if (!g.LoadPackage(texPfad, pk, err)) return false;
    int e = pk.FindExport("Texture2D");
    if (e < 0) {
        for (size_t i = 0; i < pk.exports.size(); i++)
            if (pk.exports[i].className.find("Texture") != std::string::npos) { e = (int)i; break; }
    }
    if (e < 0) { err = "kein Texture2D-Export"; return false; }
    const Export& ex = pk.exports[e];
    Reader r(pk.data.data() + ex.dataOffset, (size_t)ex.serialSize, &pk);
    std::vector<Prop> ps;
    if (!ReadProps(r, ps)) { err = "Textur-Eigenschaften kaputt"; return false; }
    // UE 4.16: bHasGuid, StripFlags UTexture + UTexture2D, bCooked, dann je Pixelformat
    // FName, SkipOffset (int32), SizeX, SizeY, NumSlices, PixelFormat (FString), FirstMip, Mips
    if (r.b32()) r.skip(16);
    r.u16(); r.u16();
    if (!r.b32()) { err = "Textur nicht gekocht"; return false; }
    std::string pf = r.fname();
    if (pf == "None" || r.bad) { err = "keine Plattformdaten"; return false; }
    r.i32();                          // SkipOffset
    int sx = r.i32(), sy = r.i32();
    r.i32();                          // NumSlices
    format = r.fstr();
    r.i32();                          // FirstMip
    int nmips = r.i32();
    if (nmips <= 0 || nmips > 20 || r.bad) { err = "Mip-Anzahl kaputt"; return false; }
    std::vector<Mip> mips((size_t)nmips);
    for (auto& m : mips) {
        r.b32();                      // bCooked
        BulkKopf bk;
        if (!LiesBulkKopf(r, bk)) { err = "Mip-Kopf kaputt"; return false; }
        m.flags = bk.flags; m.count = bk.count; m.size = bk.size; m.offset = bk.offset; m.inlineAt = bk.inlineAt;
        m.w = r.i32(); m.h = r.i32();
        if (r.bad) { err = "Mip-Kopf kaputt"; return false; }
    }
    (void)sx; (void)sy;
    // groesste Stufe bis maxGroesse, deren Daten lesbar sind
    for (const Mip& m : mips) {
        if (maxGroesse > 0 && std::max(m.w, m.h) > maxGroesse) continue;
        if (m.flags & BULK_Unbenutzt) continue;
        std::vector<uint8_t> d;
        std::string e2;
        if (m.flags & BULK_Inline) d.assign(r.p + m.inlineAt, r.p + m.inlineAt + (size_t)m.size);
        else if (!g.ReadBulk(pk, m.flags, m.offset, m.size, d, e2)) continue;
        if (m.flags & BULK_Zlib) continue;
        if (d.size() < (size_t)m.size) continue;
        if (!Dekodiere(format, d.data(), d.size(), m.w, m.h, rgba, err)) return false;
        w = m.w; h = m.h;
        return true;
    }
    err = "keine lesbare Mip-Stufe";
    return false;
}

std::wstring TexturAlsPng(Game& g, const std::string& texPfad, const std::wstring& cacheOrdner, bool normal, int maxGroesse, std::string& err,
                          int kanal) {
    std::string rel = OhnePunkt(texPfad);
    if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
    std::replace(rel.begin(), rel.end(), '/', '\\');
    std::wstring ziel = cacheOrdner + L"\\" + Widen(rel) + (maxGroesse > 0 ? L"_" + std::to_wstring(maxGroesse) : L"") +
                        (kanal >= 0 ? std::wstring(L"_") + L"RGBA"[kanal & 3] : L"") + L".png";
    if (GetFileAttributesW(ziel.c_str()) != INVALID_FILE_ATTRIBUTES) return ziel;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    std::string fmt;
    if (!LiesTextur(g, texPfad, maxGroesse, rgba, w, h, fmt, err)) return std::wstring();
    if (normal) {
        // UE rekonstruiert Z aus X/Y; Blau enthaelt bei BC7-Normalen etwas anderes (Cere: Mittel 126).
        // Max liest Blau als Z - also Z hier neu berechnen. DXT5nm (X im Alpha, Rot ~255) erkennen.
        const size_t n = (size_t)w * h;
        double sumR = 0, sumA = 0, sumA2 = 0;
        for (size_t i = 0; i < n; i += 61) { sumR += rgba[i * 4]; sumA += rgba[i * 4 + 3]; sumA2 += (double)rgba[i * 4 + 3] * rgba[i * 4 + 3]; }
        const double cnt = (double)((n + 60) / 61);
        const double mA = sumA / cnt, sdA = std::sqrt(std::max(0.0, sumA2 / cnt - mA * mA));
        const bool xImAlpha = sumR / cnt > 240 && sdA > 4;
        for (size_t i = 0; i < n; i++) {
            uint8_t* p = &rgba[i * 4];
            if (xImAlpha) p[0] = p[3];
            const float x = p[0] / 127.5f - 1.f, y = p[1] / 127.5f - 1.f;
            const float z = std::sqrt(std::max(0.f, 1.f - x * x - y * y));
            p[2] = (uint8_t)std::lround((z * 0.5f + 0.5f) * 255.f);
            p[3] = 255;
        }
    }
    for (size_t i = 3; i < ziel.size(); ++i)
        if (ziel[i] == L'\\') CreateDirectoryW(ziel.substr(0, i).c_str(), nullptr);
    bool mitAlpha = false;
    if (!normal && kanal < 0) for (size_t i = 3; i < rgba.size(); i += 4 * 97) if (rgba[i] < 250) { mitAlpha = true; break; }
    std::vector<uint8_t> px;
    int kan = mitAlpha ? 4 : 3;
    if (kanal >= 0) {
        // Ein Kanal als Graustufen (PNG mit 1 Kanal)
        kan = 1;
        px.resize((size_t)w * h);
        for (size_t i = 0; i < (size_t)w * h; i++) px[i] = rgba[i * 4 + (kanal & 3)];
    } else if (mitAlpha) px.swap(rgba);
    else {
        px.resize((size_t)w * h * 3);
        for (size_t i = 0; i < (size_t)w * h; i++) memcpy(&px[i * 3], &rgba[i * 4], 3);
    }
    size_t laenge = 0;
    void* png = tdefl_write_image_to_png_file_in_memory_ex(px.data(), w, h, kan, &laenge, 1, MZ_FALSE);
    if (png == nullptr) { err = "PNG-Kodierung fehlgeschlagen"; return std::wstring(); }
    HANDLE f = CreateFileW(ziel.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool ok = false;
    if (f != INVALID_HANDLE_VALUE) {
        DWORD n = 0;
        ok = WriteFile(f, png, (DWORD)laenge, &n, nullptr) && n == laenge;
        CloseHandle(f);
    }
    mz_free(png);
    if (!ok) { DeleteFileW(ziel.c_str()); err = "PNG nicht schreibbar"; return std::wstring(); }
    return ziel;
}

bool SchreibePng(const std::wstring& datei, const uint8_t* px, int w, int h, int kanaele) {
    for (size_t i = 3; i < datei.size(); ++i)
        if (datei[i] == L'\\') CreateDirectoryW(datei.substr(0, i).c_str(), nullptr);
    size_t laenge = 0;
    void* png = tdefl_write_image_to_png_file_in_memory_ex(px, w, h, kanaele, &laenge, 6, MZ_FALSE);
    if (png == nullptr) return false;
    bool ok = false;
    HANDLE f = CreateFileW(datei.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD n = 0;
        ok = WriteFile(f, png, (DWORD)laenge, &n, nullptr) && n == laenge;
        CloseHandle(f);
    }
    mz_free(png);
    return ok;
}

namespace {

uint8_t ZuSrgb(float lin) {
    lin = std::min(1.f, std::max(0.f, lin));
    float s = lin <= 0.0031308f ? lin * 12.92f : 1.055f * std::pow(lin, 1.f / 2.4f) - 0.055f;
    return (uint8_t)std::lround(s * 255.f);
}

} // namespace

bool LoeseMaterial(Game& g, const std::string& matPfad, const std::wstring& cacheOrdner, TexturSatz& ts, std::vector<std::string>* prot,
                   const uint8_t* farben) {
    // Shinobi Striker (Toon-Shading von CyberConnect2): BC_Map ist ein Paletten-Atlas (Farbfelder,
    // auf die die UVs zeigen), SC_Map dieselbe Palette fuer die Schattenseite, N_Map die Normal-Map.
    // Gesichter haben keine Farbtextur, sondern den Vektor Skin_BC.
    // Parameter der ganzen Kette sammeln; was das Kind setzt, gilt (erster Fund).
    std::map<std::string, std::string> texturen;          // Parametername (klein) -> Texturpfad
    std::map<std::string, std::vector<double>> vektoren;  // Parametername (klein) -> RGBA linear
    std::vector<std::string> reihenfolge, kette;
    std::string blend;
    std::vector<std::string> fallback;                    // Texturen des Materialpakets (Namensregel)
    std::string pfad = matPfad;
    auto parName = [](const Prop& k) -> std::string {
        if (const Prop* n = k.Get("ParameterName")) return n->str;                      // 4.16
        if (const Prop* i = k.Get("ParameterInfo")) if (const Prop* n = i->Get("Name")) return n->str;
        return {};
    };
    for (int tiefe = 0; tiefe < 8 && !pfad.empty(); tiefe++) {
        Package pk; std::string e;
        if (!g.LoadPackage(pfad, pk, e)) { if (prot) prot->push_back("Material " + pfad + ": " + e); break; }
        kette.push_back(pfad.substr(pfad.find_last_of('/') + 1));
        if (pk.exports.empty()) break;
        int ei = 0;
        for (size_t i = 0; i < pk.exports.size(); i++)
            if (pk.exports[i].className.find("Material") != std::string::npos) { ei = (int)i; break; }
        const Export& ex = pk.exports[(size_t)ei];
        Reader r(pk.data.data() + ex.dataOffset, (size_t)ex.serialSize, &pk);
        std::vector<Prop> ps;
        ReadProps(r, ps);
        if (const Prop* tp = FindProp(ps, "TextureParameterValues"))
            for (const Prop& k : tp->kids) {
                const std::string nm = Lower(parName(k));
                const Prop* val = k.Get("ParameterValue");
                if (nm.empty() || !val || val->obj == 0) continue;
                std::string tex = OhnePunkt(g.IndexToPath(pk, val->obj));
                if (tex.empty() || tex[0] != '/') continue;
                if (!texturen.count(nm)) { texturen[nm] = tex; reihenfolge.push_back(nm); }
            }
        if (const Prop* vp = FindProp(ps, "VectorParameterValues"))
            for (const Prop& k : vp->kids) {
                const std::string nm = Lower(parName(k));
                const Prop* val = k.Get("ParameterValue");
                if (nm.empty() || !val || val->vec.size() < 3) continue;
                if (!vektoren.count(nm)) vektoren[nm] = val->vec;
            }
        if (blend.empty()) {
            if (const Prop* bo = FindProp(ps, "BasePropertyOverrides")) {
                const Prop* ob = bo->Get("bOverride_BlendMode");
                const Prop* bm = bo->Get("BlendMode");
                if (ob && ob->num != 0 && bm) blend = bm->str;
            }
            if (blend.empty()) if (const Prop* bm = FindProp(ps, "BlendMode")) blend = bm->str;
        }
        for (size_t i = 0; i < pk.imports.size(); i++) {
            const Import& im = pk.imports[i];
            if (im.className != "Texture2D") continue;
            std::string pn, on, cn;
            if (g.ResolveImport(pk, (int)i, pn, on, cn) && !pn.empty() && pn[0] == '/') fallback.push_back(pn);
        }
        std::string parent;
        if (const Prop* p = FindProp(ps, "Parent")) parent = OhnePunkt(g.IndexToPath(pk, p->obj));
        pfad = (!parent.empty() && parent[0] == '/') ? parent : std::string();
    }
    auto farbWert = [](const std::string& n) {
        if (n.find("shadow") != std::string::npos || n.find("mask") != std::string::npos || n.find("damage") != std::string::npos) return 0;
        if (n == "bc_map" || n == "basecolor" || n == "base_color" || n == "diffuse") return 10;
        if (n.find("bc_map") != std::string::npos) return 8;
        if (n.find("basecolor") != std::string::npos || n.find("diffuse") != std::string::npos || n.find("albedo") != std::string::npos) return 7;
        if (n.size() > 3 && n.compare(n.size() - 3, 3, "_bc") == 0) return 5;
        if (n.find("color") != std::string::npos && n.find("map") != std::string::npos) return 3;
        return 0;
    };
    auto normalWert = [](const std::string& n) {
        if (n == "n_map" || n == "normal" || n == "normalmap" || n == "normal_map") return 10;
        if (n.find("normal") != std::string::npos || n.find("n_map") != std::string::npos) return 6;
        return 0;
    };
    auto opazWert = [](const std::string& n) {
        if (n.find("opacity") != std::string::npos) return 10;
        if (n.find("alpha") != std::string::npos) return 6;
        return 0;
    };
    auto beste = [&](auto wert, std::string& wahl, std::string& name) {
        int b = 0;
        for (const std::string& n : reihenfolge) {
            const int w = wert(n);
            if (w > b) { b = w; wahl = texturen[n]; name = n; }
        }
    };
    std::string farbe, normal, opaz, nF, nN, nO;
    beste(farbWert, farbe, nF);
    beste(normalWert, normal, nN);
    beste(opazWert, opaz, nO);
    // Keine Farbtextur als Parameter? Namensregel ueber die Texturen der Kette (fest im Master)
    if (farbe.empty())
        for (const std::string& pn : fallback) {
            const std::string b = Lower(pn.substr(pn.find_last_of('/') + 1));
            if (Enthaelt(b, "default") || Enthaelt(b, "noise") || Enthaelt(b, "shadow") || Enthaelt(b, "dummy")) continue;
            if (EndetMit(b, "_bc") || EndetMit(b, "_d") || EndetMit(b, "_diffuse") || EndetMit(b, "_c")) { farbe = pn; nF = "(Namensregel)"; break; }
        }
    if (normal.empty())
        for (const std::string& pn : fallback) {
            const std::string b = Lower(pn.substr(pn.find_last_of('/') + 1));
            if (EndetMit(b, "_n") && !Enthaelt(b, "flat") && !Enthaelt(b, "default")) { normal = pn; break; }
        }
    // Farbe als Wert: Hautton (Gesicht), sonst eine passende Grundfarbe
    if (farbe.empty()) {
        static const char* const reihe[] = { "skin_bc", "base_color", "basecolor", "color", "bc", "costume_01_bc" };
        for (const char* k : reihe) {
            auto it = vektoren.find(k);
            if (it == vektoren.end()) continue;
            for (int c = 0; c < 3; c++) ts.farbWert[c] = ZuSrgb((float)it->second[(size_t)c]);
            ts.hatFarbWert = true;
            break;
        }
        // Linien-Material (Stirnbandbaender, Haarstraehnen als Linien): dunkel
        if (!ts.hatFarbWert && !kette.empty() && Enthaelt(kette.back(), "line")) {
            ts.farbWert[0] = ts.farbWert[1] = ts.farbWert[2] = 40;
            ts.hatFarbWert = true;
        }
    }
    const bool maskiert = blend.find("Masked") != std::string::npos;
    const bool durchsichtig = blend.find("Translucent") != std::string::npos || blend.find("Additive") != std::string::npos ||
                              blend.find("Modulate") != std::string::npos;
    ts.alpha = maskiert || durchsichtig;
    ts.unsichtbar = durchsichtig && farbe.empty() && !ts.hatFarbWert;
    const int maxG = 4096;
    std::string e;
    // Avatar-Kleidung: Palette (BC_Map) mal Farbe je Maskenkanal - R Haut, G Farbe 0, B Farbe 1
    // (an der Konoha-Weste geprueft: G = Aermel/Hemd, B = Weste, R = Haende/Hals)
    auto maske = texturen.find("colormask_rgb");
    if (!farbe.empty() && farben != nullptr && maske != texturen.end()) {
        char hex[32];
        snprintf(hex, sizeof hex, "_%02x%02x%02x%02x%02x%02x%02x%02x%02x", farben[0], farben[1], farben[2], farben[3], farben[4], farben[5],
                 farben[6], farben[7], farben[8]);
        std::string rel = OhnePunkt(farbe);
        if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
        std::replace(rel.begin(), rel.end(), '/', '\\');
        const std::wstring ziel = cacheOrdner + L"\\" + Widen(rel) + Widen(hex) + L".png";
        if (GetFileAttributesW(ziel.c_str()) != INVALID_FILE_ATTRIBUTES) ts.farbe = ziel;
        else {
            std::vector<uint8_t> bc, mk;
            int bw = 0, bh = 0, mw = 0, mh = 0;
            std::string f1, f2;
            if (LiesTextur(g, farbe, maxG, bc, bw, bh, f1, e) && LiesTextur(g, maske->second, maxG, mk, mw, mh, f2, e)) {
                std::vector<uint8_t> px((size_t)bw * bh * 3);
                for (int y = 0; y < bh; y++)
                    for (int x = 0; x < bw; x++) {
                        const uint8_t* q = &bc[((size_t)y * bw + x) * 4];
                        const uint8_t* m = &mk[((size_t)(y * mh / bh) * mw + (size_t)(x * mw / bw)) * 4];
                        for (int c = 0; c < 3; c++) {
                            double v = q[c] / 255.0;
                            for (int k = 0; k < 3; k++) {
                                const double w = m[k] / 255.0;
                                v = v * (1.0 - w) + v * (farben[k * 3 + c] / 255.0) * w;
                            }
                            px[((size_t)y * bw + x) * 3 + c] = (uint8_t)std::lround(std::min(1.0, std::max(0.0, v)) * 255.0);
                        }
                    }
                if (SchreibePng(ziel, px.data(), bw, bh, 3)) ts.farbe = ziel;
            }
            if (ts.farbe.empty() && prot) prot->push_back("Einfaerben " + farbe + ": " + e);
        }
        if (prot && !ts.farbe.empty()) prot->push_back("  eingefaerbt mit Maske " + maske->second);
    }
    if (!farbe.empty() && ts.farbe.empty()) {
        ts.farbe = TexturAlsPng(g, farbe, cacheOrdner, false, maxG, e);
        if (ts.farbe.empty() && prot) prot->push_back("Textur " + farbe + ": " + e);
    }
    if (!normal.empty()) {
        ts.normal = TexturAlsPng(g, normal, cacheOrdner, true, maxG, e);
        if (ts.normal.empty() && prot) prot->push_back("Textur " + normal + ": " + e);
    }
    if (!opaz.empty() && (maskiert || durchsichtig)) {
        ts.opazitaet = TexturAlsPng(g, opaz, cacheOrdner, false, maxG, e, 0);
        if (ts.opazitaet.empty() && prot) prot->push_back("Textur " + opaz + ": " + e);
    }
    if (prot) {
        char fw[64] = "";
        if (ts.hatFarbWert) snprintf(fw, sizeof fw, ", Farbwert %d %d %d", ts.farbWert[0], ts.farbWert[1], ts.farbWert[2]);
        std::string k;
        for (auto& s : kette) k += (k.empty() ? "" : " <- ") + s;
        prot->push_back("Material " + k + ": Farbe " + (farbe.empty() ? "-" : farbe + " (" + nF + ")") + ", Normal " +
                        (normal.empty() ? "-" : normal) + ", Deckkraft " + (opaz.empty() ? "-" : opaz) + fw + ", " +
                        (blend.empty() ? "Opaque" : blend) + (ts.unsichtbar ? " -> ausgeblendet" : ""));
    }
    return !ts.farbe.empty() || !ts.normal.empty() || ts.hatFarbWert || ts.unsichtbar;
}

} // namespace ns

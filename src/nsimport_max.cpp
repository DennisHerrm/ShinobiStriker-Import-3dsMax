// ============================================================
//  Shinobi Striker Import - der Max-Teil: Skelett, Meshes, Skin,
//  Materialien, Animation.
//
//  Raum: UE rechnet linkshaendig, Z oben, in cm, mit Spaltenvektoren
//  (v' = M v). Nach Max (rechtshaendig, Z oben, Zeilenvektoren) geht
//  es mit der Spiegelung S = diag(1,-1,1): M_max = S M S, dann
//  transponiert; Verschiebungen mal Massstab. Weil S S = 1 ist, gilt
//  das fuer Welt- UND lokale Lagen gleich, auch fuer Animationskeys.
//  UE-Vorderseiten laufen im Uhrzeigersinn; gespiegelt ist das Max' Gegen-
//  uhrzeigersinn - die Indexreihenfolge bleibt.
//
//  Die Figuren blicken im Spiel nach +Y (UE), in Max also nach -Y:
//  in die Vorderansicht. Keine weitere Drehung noetig.
//
//  Einzelheiten aus SWBF2/TFU2 Import uebernommen (Auto-Align aus,
//  Einhaengen vor dem Setzen der Weltlage, Skin erst nach
//  EvalWorldState fuellen, Keys ueber SetValue(CTRL_ABSOLUTE) im
//  Animationsmodus).
// ============================================================
#include "nsimport.h"
#include "ns_tex.h"

#include <MeshNormalSpec.h>
#include <iskin.h>
#include <stdmat.h>
#include <bitmap.h>
#include <modstack.h>
#include <notetrck.h>
#include <maxscript/maxscript.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

#ifndef BONE_OBJ_CLASSID
#define BONE_OBJ_CLASSID Class_ID(BONE_OBJ_CLASS_ID, 0)
#endif

extern HINSTANCE hInstance;

namespace nsi {

std::wstring Breit(const std::string& s) { return ns::Widen(s); }
std::string Utf8(const std::wstring& w) { return ns::Narrow(w); }

// ------------------------------------------------------------
//  Ablage, Einstellungen, Protokoll
// ------------------------------------------------------------
std::wstring Ablage() {
    wchar_t puffer[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", puffer, MAX_PATH);
    std::wstring o = (n > 0 && n < MAX_PATH) ? std::wstring(puffer) : std::wstring(L".");
    o += L"\\NSImport";
    CreateDirectoryW(o.c_str(), nullptr);
    return o;
}

namespace {

std::map<std::wstring, std::wstring> LiesEinstellungen() {
    std::map<std::wstring, std::wstring> m;
    FILE* f = _wfopen((Ablage() + L"\\einstellungen.txt").c_str(), L"rb");
    if (f == nullptr) return m;
    std::string inhalt;
    char puffer[4096];
    size_t n;
    while ((n = std::fread(puffer, 1, sizeof puffer, f)) > 0) inhalt.append(puffer, n);
    std::fclose(f);
    size_t p = 0;
    while (p < inhalt.size()) {
        size_t e = inhalt.find('\n', p);
        if (e == std::string::npos) e = inhalt.size();
        std::string z = inhalt.substr(p, e - p);
        p = e + 1;
        while (!z.empty() && (z.back() == '\r' || z.back() == ' ')) z.pop_back();
        const size_t g = z.find('=');
        if (g == std::string::npos || z.empty() || z[0] == '#') continue;
        m[Breit(z.substr(0, g))] = Breit(z.substr(g + 1));
    }
    return m;
}

FILE* g_log = nullptr;

} // namespace

std::wstring LiesEinstellung(const std::wstring& schluessel) {
    const auto m = LiesEinstellungen();
    const auto it = m.find(schluessel);
    return it == m.end() ? std::wstring() : it->second;
}

void SchreibeEinstellung(const std::wstring& schluessel, const std::wstring& wert) {
    auto m = LiesEinstellungen();
    m[schluessel] = wert;
    FILE* f = _wfopen((Ablage() + L"\\einstellungen.txt").c_str(), L"wb");
    if (f == nullptr) return;
    std::fputs("# Shinobi Striker Import - Einstellungen\r\n", f);
    for (const auto& kv : m) {
        const std::string z = Utf8(kv.first) + "=" + Utf8(kv.second) + "\r\n";
        std::fwrite(z.data(), 1, z.size(), f);
    }
    std::fclose(f);
}

void LogNeu(const char* titel) {
    if (g_log != nullptr) std::fclose(g_log);
    g_log = _wfopen((Ablage() + L"\\import.log").c_str(), L"wb");
    if (g_log != nullptr) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::fprintf(g_log, "Shinobi Striker Import %ls  %04d-%02d-%02d %02d:%02d:%02d  Max %d\r\n%s\r\n", NSIMPORT_VERSION_STR, st.wYear,
                     st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, MAX_RELEASE, titel);
        std::fflush(g_log);
    }
}

void Log(const char* format, ...) {
    if (g_log == nullptr) return;
    char puffer[2048];
    va_list a;
    va_start(a, format);
    std::vsnprintf(puffer, sizeof puffer, format, a);
    va_end(a);
    std::fputs(puffer, g_log);
    std::fputs("\r\n", g_log);
    std::fflush(g_log);
    // Verlauf ueber mehrere Importe (import.log gilt nur fuer den letzten)
    if (FILE* v = _wfopen((Ablage() + L"\\verlauf.log").c_str(), L"ab")) {
        std::fputs(puffer, v);
        std::fputs("\r\n", v);
        std::fclose(v);
    }
}

// ------------------------------------------------------------
//  Spielordner und Spiel
// ------------------------------------------------------------
bool IstSpielordner(const std::wstring& o) {
    return !o.empty() && ns::IstSpielordner(o);
}

namespace {
std::wstring Registrywert(HKEY wurzel, const wchar_t* schluessel, const wchar_t* name) {
    wchar_t puffer[MAX_PATH] = {};
    DWORD groesse = sizeof puffer;
    if (RegGetValueW(wurzel, schluessel, name, RRF_RT_REG_SZ, nullptr, puffer, &groesse) != ERROR_SUCCESS) return std::wstring();
    std::wstring s = puffer;
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}
}

std::wstring SucheSpiel() {
    // Steam-Ordner heisst "Naruto To Boruto" (darin NARUTO\Content\Paks)
    for (const wchar_t* o : { L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Naruto To Boruto",
                              L"C:\\Program Files\\Steam\\steamapps\\common\\Naruto To Boruto" })
        if (IstSpielordner(o)) return o;
    // Steam-Bibliotheken
    const std::wstring steam = Registrywert(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (!steam.empty()) {
        FILE* vdf = _wfopen((steam + L"\\steamapps\\libraryfolders.vdf").c_str(), L"rb");
        if (vdf != nullptr) {
            std::string t;
            char b[4096];
            size_t n;
            while ((n = std::fread(b, 1, sizeof b, vdf)) > 0) t.append(b, n);
            std::fclose(vdf);
            size_t p = 0;
            while ((p = t.find("\"path\"", p)) != std::string::npos) {
                const size_t a = t.find('"', p + 6), e = (a == std::string::npos) ? a : t.find('"', a + 1);
                if (a == std::string::npos || e == std::string::npos) break;
                std::string pfad = t.substr(a + 1, e - a - 1), sauber;
                for (size_t i = 0; i < pfad.size(); ++i) {
                    if (pfad[i] == '\\' && i + 1 < pfad.size() && pfad[i + 1] == '\\') ++i;
                    sauber += pfad[i];
                }
                const std::wstring kand = Breit(sauber) + L"\\steamapps\\common\\Naruto To Boruto";
                if (IstSpielordner(kand)) return kand;
                p = e + 1;
            }
        }
    }
    return std::wstring();
}

bool Spiel(const std::wstring& ordner, ns::Game*& game, const ns::Katalog*& katalog, std::string& fehler) {
    static std::unique_ptr<ns::Game> g;
    static std::unique_ptr<ns::Katalog> k;
    static std::wstring geladen;
    std::wstring o = ordner;
    if (o.empty()) o = LiesEinstellung(L"Spielordner");
    if (o.empty()) o = SucheSpiel();
    if (!g || _wcsicmp(o.c_str(), geladen.c_str()) != 0) {
        if (!IstSpielordner(o)) { fehler = "not a Shinobi Striker folder (NARUTO\\Content\\Paks\\*.pak missing)"; return false; }
        auto ng = std::make_unique<ns::Game>();
        auto nk = std::make_unique<ns::Katalog>();
        if (!ng->Open(o, fehler)) return false;
        if (!nk->Baue(*ng, Ablage() + L"\\katalog.txt", fehler)) return false;
        g = std::move(ng);
        k = std::move(nk);
        geladen = o;
        SchreibeEinstellung(L"Spielordner", o);
    }
    game = g.get();
    katalog = k.get();
    return true;
}

namespace {

// ------------------------------------------------------------
//  Zahlen und Matrizen
// ------------------------------------------------------------
// Massstab je cm: 1 m = 39,37 Einheiten (wie SWBF2/TFU2 Import); anders mit
// EinheitenJeMeter=... in einstellungen.txt, 0 = Systemeinheit.
float Massstab() {
    double jeMeter = 39.37007874015748;
    const std::wstring w = LiesEinstellung(L"EinheitenJeMeter");
    if (!w.empty()) {
        const std::string s = Utf8(w);
        double v = 0.0;
        const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec == std::errc() && v >= 0.0 && v < 1e6) jeMeter = v;
    }
    if (jeMeter <= 0.0) {
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
        const double m = GetSystemUnitScale(UNITS_METERS);
#else
        const double m = GetMasterScale(UNITS_METERS);
#endif
        jeMeter = m > 0.0 ? 1.0 / m : 1.0;
    }
    return static_cast<float>(jeMeter / 100.0);
}

// UE-Matrix (Spaltenvektoren, cm) -> Max (Zeilenvektoren, gespiegelt, Einheiten)
Matrix3 ZuMax(const ns::M34& g, float mass) {
    static const double s[3] = { 1.0, -1.0, 1.0 };
    Matrix3 r;
    r.IdentityMatrix();
    for (int c = 0; c < 3; ++c)
        r.SetRow(c, Point3(static_cast<float>(s[0] * g.m[0 * 4 + c] * s[c]), static_cast<float>(s[1] * g.m[1 * 4 + c] * s[c]),
                           static_cast<float>(s[2] * g.m[2 * 4 + c] * s[c])));
    r.SetRow(3, Point3(static_cast<float>(g.m[3] * mass), static_cast<float>(-g.m[7] * mass), static_cast<float>(g.m[11] * mass)));
    return r;
}

std::string ZahlText(float v) {
    char b[32];
    const auto r = std::to_chars(b, b + sizeof b, v);
    return std::string(b, r.ptr);
}

std::string Dez(double v, int stellen) {
    char b[64];
    const auto r = std::to_chars(b, b + sizeof b, v, std::chars_format::fixed, stellen);
    return std::string(b, r.ptr);
}

std::string MatrixText(const Matrix3& m) {
    std::string s;
    for (int z = 0; z < 4; ++z) {
        const Point3 p = m.GetRow(z);
        s += ZahlText(p.x) + " " + ZahlText(p.y) + " " + ZahlText(p.z) + (z < 3 ? " " : "");
    }
    return s;
}

bool MatrixAusText(const std::string& s, Matrix3& m) {
    float v[12];
    const char* p = s.data();
    const char* e = s.data() + s.size();
    for (int i = 0; i < 12; ++i) {
        while (p < e && *p == ' ') ++p;
        const auto r = std::from_chars(p, e, v[i]);
        if (r.ec != std::errc()) return false;
        p = r.ptr;
    }
    m.IdentityMatrix();
    for (int z = 0; z < 4; ++z) m.SetRow(z, Point3(v[z * 3], v[z * 3 + 1], v[z * 3 + 2]));
    return true;
}

MSTR M(const std::string& s) { return MSTR::FromUTF8(s.c_str()); }

std::string NodeProp(INode* n, const MCHAR* key) {
    MSTR v;
    if (n == nullptr || !n->GetUserPropString(MSTR(key), v)) return std::string();
    return Utf8(std::wstring(v.data()));
}

struct AnimationAus {
    BOOL vorher;
    AnimationAus() : vorher(Animating()) { SuspendAnimate(); AnimateOff(); }
    ~AnimationAus() { ResumeAnimate(); if (vorher) AnimateOn(); }
};

std::string NeueId() {
    static unsigned zaehler = 0;
    char b[48];
    std::snprintf(b, sizeof b, "%llx-%u", static_cast<unsigned long long>(std::chrono::system_clock::now().time_since_epoch().count()), ++zaehler);
    return b;
}

std::string Verbinde(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? ";" : "") + v[i];
    return s;
}

std::vector<std::string> Trenne(const std::string& s) {
    std::vector<std::string> o;
    size_t p = 0;
    while (p <= s.size()) {
        size_t e = s.find(';', p);
        if (e == std::string::npos) e = s.size();
        if (e > p) o.push_back(s.substr(p, e - p));
        p = e + 1;
    }
    return o;
}

// ------------------------------------------------------------
//  Skelett
// ------------------------------------------------------------
std::vector<INode*> BaueSkelett(Interface* ip, const ns::Figur& f, float mass, const std::string& id, const std::string& figurName) {
    const size_t n = f.skel.bones.size();
    std::vector<INode*> knoten(n, nullptr);
    std::vector<Matrix3> welt(n);
    for (size_t i = 0; i < n; ++i) welt[i] = ZuMax(f.global[i], mass);
    const std::string skelette = Verbinde(f.skelette);
    AnimationAus keineKeys;
    for (size_t i = 0; i < n; ++i) {
        Object* obj = static_cast<Object*>(ip->CreateInstance(GEOMOBJECT_CLASS_ID, BONE_OBJ_CLASSID));
        const bool istBone = obj != nullptr;
        if (obj == nullptr) obj = static_cast<Object*>(ip->CreateInstance(HELPER_CLASS_ID, Class_ID(POINTHELP_CLASS_ID, 0)));
        if (obj == nullptr) continue;
        INode* node = ip->CreateObjectNode(obj);
        if (node == nullptr) continue;
        MSTR name = M(f.skel.bones[i].name);
        node->SetName(name.data());
        node->ShowBone(1);
        node->SetBoneNodeOnOff(TRUE, 0);
        node->SetBoneAutoAlign(FALSE);
        node->SetBoneFreezeLen(TRUE);
        node->SetRenderable(FALSE);
        if (istBone) {
            Object* bo = node->GetObjectRef();
            if (bo != nullptr) bo = bo->FindBaseObject();
            IParamBlock2* pb = bo ? bo->GetParamBlockByID(0) : nullptr;
            if (pb != nullptr) { pb->SetValue(0, 0, 0.0f); pb->SetValue(1, 0, 0.0f); }
        }
        knoten[i] = node;
    }
    for (size_t i = 0; i < n; ++i) {
        const int e = f.skel.bones[i].parent;
        if (knoten[i] != nullptr && e >= 0 && knoten[static_cast<size_t>(e)] != nullptr) knoten[static_cast<size_t>(e)]->AttachChild(knoten[i], 0);
    }
    // Eltern stehen vor ihren Kindern (UE-Reihenfolge, beim Zusammenfuehren erhalten).
    for (size_t i = 0; i < n; ++i) {
        INode* node = knoten[i];
        if (node == nullptr) continue;
        node->SetNodeTM(0, welt[i]);
        const int e = f.skel.bones[i].parent;
        Matrix3 lokal = welt[i];
        if (e >= 0) lokal = welt[i] * Inverse(welt[static_cast<size_t>(e)]);
        node->SetUserPropString(MSTR(_T("ns_rest")), M(MatrixText(lokal)));
        node->SetUserPropString(MSTR(_T("ns_bone")), M(f.skel.bones[i].name));
        node->SetUserPropString(MSTR(_T("ns_id")), M(id));
        node->SetUserPropString(MSTR(_T("ns_figur")), M(figurName));
        node->SetUserPropString(MSTR(_T("ns_skel")), M(skelette));
    }
    return knoten;
}

// ------------------------------------------------------------
//  Mesh
// ------------------------------------------------------------
INode* BaueMesh(Interface* ip, const ns::SkelMesh& m, const std::string& name, float mass) {
    const int nv = static_cast<int>(m.numVerts);
    size_t ntri = 0;
    for (const auto& s : m.sections) ntri += s.numTris;
    const int nf = static_cast<int>(ntri);
    if (nv == 0 || nf == 0) return nullptr;
    TriObject* tri = CreateNewTriObject();
    if (tri == nullptr) return nullptr;
    Mesh& mesh = tri->GetMesh();
    mesh.setNumVerts(nv);
    for (int v = 0; v < nv; ++v) {
        const float* q = &m.pos[static_cast<size_t>(v) * 3];
        mesh.setVert(v, q[0] * mass, -q[1] * mass, q[2] * mass);
    }
    mesh.setNumFaces(nf);
    std::vector<int> ecken(static_cast<size_t>(nf) * 3);
    int f = 0;
    for (const auto& s : m.sections) {
        for (uint32_t t = 0; t < s.numTris; ++t, ++f) {
            const size_t b = static_cast<size_t>(s.baseIndex) + static_cast<size_t>(t) * 3;
            // UE fuehrt Vorderseiten im Uhrzeigersinn (linkshaendig, gemessen: 99,96 % der Dreiecke
            // gegen die Vertexnormale). Die Spiegelung macht daraus genau Max' Gegenuhrzeigersinn -
            // die Reihenfolge bleibt also.
            const int a = static_cast<int>(m.indices[b]), c = static_cast<int>(m.indices[b + 1]), d = static_cast<int>(m.indices[b + 2]);
            ecken[static_cast<size_t>(f) * 3] = a;
            ecken[static_cast<size_t>(f) * 3 + 1] = c;
            ecken[static_cast<size_t>(f) * 3 + 2] = d;
            mesh.faces[f].setVerts(a, c, d);
            mesh.faces[f].setEdgeVisFlags(1, 1, 1);
            mesh.faces[f].setSmGroup(1);
            mesh.faces[f].setMatID(static_cast<MtlID>(s.material));
        }
    }
    // UV-Kanaele: UE V laeuft nach unten, Max nach oben
    for (uint32_t k = 0; k < m.numUV && k < 8; ++k) {
        const int ch = static_cast<int>(k) + 1;
        mesh.setMapSupport(ch, TRUE);
        mesh.setNumMapVerts(ch, nv);
        for (int v = 0; v < nv; ++v) {
            const float* uv = &m.uv[(static_cast<size_t>(v) * m.numUV + k) * 2];
            mesh.setMapVert(ch, v, UVVert(uv[0], 1.0f - uv[1], 0.0f));
        }
        mesh.setNumMapFaces(ch, nf);
        TVFace* tf = mesh.mapFaces(ch);
        for (int i = 0; i < nf; ++i) tf[i].setTVerts(ecken[static_cast<size_t>(i) * 3], ecken[static_cast<size_t>(i) * 3 + 1], ecken[static_cast<size_t>(i) * 3 + 2]);
    }
    if (m.color.size() == m.numVerts) {
        mesh.setMapSupport(0, TRUE);
        mesh.setNumMapVerts(0, nv);
        for (int v = 0; v < nv; ++v) {
            const uint32_t c = m.color[static_cast<size_t>(v)];   // FColor: B G R A
            mesh.setMapVert(0, v, Point3(((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f));
        }
        mesh.setNumMapFaces(0, nf);
        TVFace* tf = mesh.mapFaces(0);
        for (int i = 0; i < nf; ++i) tf[i].setTVerts(ecken[static_cast<size_t>(i) * 3], ecken[static_cast<size_t>(i) * 3 + 1], ecken[static_cast<size_t>(i) * 3 + 2]);
    }
    mesh.InvalidateTopologyCache();
    mesh.InvalidateGeomCache();
    mesh.buildNormals();
    // Normalen der Datei als explizite Normalen (UV-Naehte trennen Vertices)
    if (m.nrm.size() == static_cast<size_t>(nv) * 3) {
        mesh.SpecifyNormals();
        MeshNormalSpec* ns = mesh.GetSpecifiedNormals();
        if (ns != nullptr) {
            ns->ClearAndFree();
            ns->SetParent(&mesh);
            ns->SetNumNormals(nv);
            for (int v = 0; v < nv; ++v) {
                const float* q = &m.nrm[static_cast<size_t>(v) * 3];
                Point3 n(q[0], -q[1], q[2]);
                const float l = Length(n);
                ns->Normal(v) = (l > 1e-8f) ? n / l : Point3(0, 0, 1);
                ns->SetNormalExplicit(v, true);
            }
            ns->SetNumFaces(nf);
            for (int i = 0; i < nf; ++i) {
                MeshNormalFace& nfc = ns->Face(i);
                nfc.SpecifyAll(true);
                for (int k = 0; k < 3; ++k) nfc.SetNormalID(k, ecken[static_cast<size_t>(i) * 3 + static_cast<size_t>(k)]);
            }
            ns->SetFlag(MESH_NORMAL_MODIFIER_SUPPORT, true);
        }
    }
    INode* node = ip->CreateObjectNode(tri);
    if (node == nullptr) return nullptr;
    MSTR mn = M(name);
    node->SetName(mn.data());
    Matrix3 eins;
    eins.IdentityMatrix();
    node->SetNodeTM(0, eins);
    return node;
}

// ------------------------------------------------------------
//  Skin (bis 8 Einfluesse je Vertex)
// ------------------------------------------------------------
bool BaueSkin(Interface* ip, INode* node, const ns::SkelMesh& m, const std::vector<INode*>& knoten, size_t& gewichtet) {
    std::set<int> benutzt;
    for (size_t i = 0; i < m.infBone.size(); ++i)
        if (m.infWeight[i] > 0 && m.infBone[i] < knoten.size() && knoten[m.infBone[i]] != nullptr) benutzt.insert(m.infBone[i]);
    if (benutzt.empty()) return false;
    Modifier* mod = static_cast<Modifier*>(ip->CreateInstance(OSM_CLASS_ID, SKIN_CLASSID));
    ISkinImportData* imp = mod ? static_cast<ISkinImportData*>(mod->GetInterface(I_SKINIMPORTDATA)) : nullptr;
    if (mod == nullptr || imp == nullptr) return false;
    Interface7* ip7 = GetCOREInterface7();
    if (ip7 == nullptr || ip7->AddModifier(*node, *mod) != Interface7::kRES_SUCCESS) return false;
    const auto t0 = std::chrono::steady_clock::now();
    auto ms = [&t0]() { return static_cast<long long>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()); };
    // Erst auswerten, DANN die Knochen anmelden: so legt Skin seine Daten an, ohne fuer jeden
    // Knochen Huellen und Startgewichte zu rechnen (die wir ohnehin ueberschreiben). Andersherum
    // dauerte das beim Gesicht von Cal (450 Knochen) 260 s, beim Koerper 7 s.
    node->EvalWorldState(ip->GetTime());
    const long long tEval = ms();
    for (int b : benutzt) imp->AddBoneEx(knoten[static_cast<size_t>(b)], FALSE);
    const long long tBones = ms();
    // Den LETZTEN Vertex zuerst: dann legt Skin (bonesdef, AddWeights) seine Vertexliste in
    // einem Zug an. Vorwaerts haengt es jeden Vertex einzeln an und ordnet dabei jedes Mal neu -
    // quadratisch, beim Koerper (77 757 Vertices) 69 s.
    for (uint32_t z = 0; z < m.numVerts; ++z) {
        const uint32_t v = (z == 0) ? m.numVerts - 1 : z - 1;
        Tab<INode*> bones;
        Tab<float> w;
        for (uint32_t k = 0; k < m.maxInf; ++k) {
            const size_t i = static_cast<size_t>(v) * m.maxInf + k;
            const uint16_t b = m.infBone[i];
            float g = m.infWeight[i] / 255.0f;
            if (g <= 0.0f || b >= knoten.size() || knoten[b] == nullptr) continue;
            INode* bn = knoten[b];
            bones.Append(1, &bn);
            w.Append(1, &g);
        }
        if (bones.Count() > 0 && imp->AddWeights(node, static_cast<int>(v), bones, w)) ++gewichtet;
    }
    const long long tGew = ms();
    node->EvalWorldState(ip->GetTime());
    Log("  Skin: erste Auswertung %lld ms, %zu Knochen angemeldet %lld ms, Gewichte %lld ms, zweite Auswertung %lld ms",
        tEval, benutzt.size(), tBones - tEval, tGew - tBones, ms() - tGew);
    return true;
}

// ------------------------------------------------------------
//  Materialien
// ------------------------------------------------------------
const Class_ID kNormalBump(0x243e22c6, 0x63f6a014);

BitmapTex* Bitmap(const std::wstring& datei, bool linear) {
    BitmapTex* bt = NewDefaultBitmapTex();
    if (bt == nullptr) return nullptr;
    bt->SetMapName(datei.c_str());
    const size_t p = datei.find_last_of(L"\\/");
    bt->SetName(MSTR(datei.substr(p == std::wstring::npos ? 0 : p + 1).c_str()));
    if (linear) {
        BitmapInfo bi;
        bi.SetName(datei.c_str());
        bi.SetCustomGamma(1.0f);
        bi.SetCustomFlag(BMM_CUSTOM_GAMMA);
        bt->SetBitmapInfo(bi);
    }
    return bt;
}

Mtl* BaueMaterial(Interface* ip, const std::string& name, const ns::TexturSatz& ts) {
    StdMat2* m = NewDefaultStdMat();
    if (m == nullptr) return nullptr;
    MSTR mn = M(name);
    m->SetName(mn);
    if (ts.unsichtbar) {
        // Durchsichtige Ueberlagerung ohne eigene Farbe (Augenschatten, Traenenfilm): im Spiel kaum
        // sichtbar, in Max sonst eine weisse Schale ueber dem Auge.
        m->SetOpacity(0.0f, 0);
        return m;
    }
    if (ts.hatFarbWert && ts.farbe.empty())
        m->SetDiffuse(Color(ts.farbWert[0] / 255.0f, ts.farbWert[1] / 255.0f, ts.farbWert[2] / 255.0f), 0);
    if (!ts.opazitaet.empty()) {
        if (BitmapTex* ot = Bitmap(ts.opazitaet, true)) {
            const int k = static_cast<int>(m->StdIDToChannel(ID_OP));
            m->SetSubTexmap(k, ot);
            m->EnableMap(k, TRUE);
            m->SetTwoSided(TRUE);
        }
    }
    if (!ts.farbe.empty()) {
        if (BitmapTex* bt = Bitmap(ts.farbe, false)) {
            const int k = static_cast<int>(m->StdIDToChannel(ID_DI));
            m->SetSubTexmap(k, bt);
            m->EnableMap(k, TRUE);
            m->SetMtlFlag(MTL_TEX_DISPLAY_ENABLED, TRUE);
            ip->ActivateTexture(bt, m);
        }
        if (ts.alpha && ts.opazitaet.empty()) {
            if (BitmapTex* ot = Bitmap(ts.farbe, false)) {
                ot->SetAlphaAsMono(TRUE);
                const int k = static_cast<int>(m->StdIDToChannel(ID_OP));
                m->SetSubTexmap(k, ot);
                m->EnableMap(k, TRUE);
                m->SetTwoSided(TRUE);
            }
        }
    }
    if (!ts.normal.empty()) {
        BitmapTex* nb = Bitmap(ts.normal, true);
        Texmap* gn = static_cast<Texmap*>(ip->CreateInstance(TEXMAP_CLASS_ID, kNormalBump));
        IParamBlock2* pb = gn ? gn->GetParamBlockByID(0) : nullptr;
        if (nb != nullptr && pb != nullptr) {
            pb->SetValue(2 /*normal_map*/, 0, static_cast<Texmap*>(nb));
            const int k = static_cast<int>(m->StdIDToChannel(ID_BU));
            m->SetSubTexmap(k, gn);
            m->EnableMap(k, TRUE);
            m->SetTexmapAmt(k, 1.0f, 0);
        }
    }
    return m;
}

// ------------------------------------------------------------
//  Szene durchsuchen
// ------------------------------------------------------------
void Sammle(INode* n, std::vector<INode*>& aus) {
    if (n == nullptr) return;
    aus.push_back(n);
    for (int i = 0; i < n->NumberOfChildren(); ++i) Sammle(n->GetChildNode(i), aus);
}

std::vector<INode*> AlleKnoten(Interface* ip) {
    std::vector<INode*> alle;
    INode* root = ip->GetRootNode();
    for (int i = 0; i < root->NumberOfChildren(); ++i) Sammle(root->GetChildNode(i), alle);
    return alle;
}

std::string ZielId(Interface* ip) {
    for (int i = 0; i < ip->GetSelNodeCount(); ++i) {
        const std::string id = NodeProp(ip->GetSelNode(i), _T("ns_id"));
        if (!id.empty()) return id;
    }
    INode* beste = nullptr;
    for (INode* n : AlleKnoten(ip)) {
        if (NodeProp(n, _T("ns_bone")).empty() || NodeProp(n, _T("ns_id")).empty()) continue;
        if (beste == nullptr || n->GetHandle() > beste->GetHandle()) beste = n;
    }
    return beste ? NodeProp(beste, _T("ns_id")) : std::string();
}

std::vector<INode*> KnochenMitId(Interface* ip, const std::string& id) {
    std::vector<INode*> aus;
    if (id.empty()) return aus;
    for (INode* n : AlleKnoten(ip))
        if (!NodeProp(n, _T("ns_bone")).empty() && NodeProp(n, _T("ns_id")) == id) aus.push_back(n);
    return aus;
}

std::string KurzName(const std::string& pfad) {
    const size_t p = pfad.find_last_of("/.");
    return p == std::string::npos ? pfad : pfad.substr(p + 1);
}

} // namespace

// ------------------------------------------------------------
//  Figur importieren
// ------------------------------------------------------------
bool ImportiereFigur(ns::Game& g, const std::vector<std::string>& pfade, const ImportOptionen& o, std::wstring& bericht) {
    std::vector<ns::FigurTeil> t;
    for (const std::string& p : pfade) t.push_back({ p, {}, std::string() });
    return ImportiereFigur(g, t, std::string(), o, bericht);
}

bool ImportiereFigur(ns::Game& g, const std::vector<ns::FigurTeil>& teileIn, const std::string& name, const ImportOptionen& o, std::wstring& bericht) {
    Interface* ip = GetCOREInterface();
    {
        std::string titel = "Figur " + name + ":";
        for (const auto& t : teileIn) titel += " " + t.mesh;
        LogNeu(titel.c_str());
    }
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<ns::SkelMesh> teile;
    std::string fehler, tlog;
    ns::LadeTeile(g, teileIn, teile, tlog, fehler);
    Log("%s", tlog.c_str());
    if (teile.empty()) { bericht = Breit(fehler.empty() ? std::string("nothing to import") : fehler); return false; }
    ns::Figur f;
    std::string flog;
    auto uhr = [&t0]() { return Dez(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), 2); };
    Log("[%s s] Teile gelesen", uhr().c_str());
    ns::BaueFigur(teile, f, flog);
    Log("%s", flog.c_str());
    const float mass = Massstab();
    const std::string figurName = name.empty() ? f.teile.front().name : name;
    if (o.versatzX != 0.0f) {
        // Ganze Figur verschieben: Bindepose der Knochen und alle Vertices
        for (ns::M34& gm : f.global) gm.m[3] += o.versatzX;
        for (size_t i = 0; i < f.skel.bones.size(); ++i) if (f.skel.bones[i].parent < 0) f.skel.bones[i].pos[0] += o.versatzX;
        for (ns::SkelMesh& m : f.teile) for (uint32_t v = 0; v < m.numVerts; ++v) m.pos[static_cast<size_t>(v) * 3] += o.versatzX;
    }

    theHold.Suspend();
    ip->DisableSceneRedraw();
    const std::string id = NeueId();
    std::vector<INode*> knoten = BaueSkelett(ip, f, mass, id, figurName);
    Log("[%s s] Skelett", uhr().c_str());
    size_t bones = 0;
    for (INode* n : knoten) if (n) ++bones;

    std::vector<INode*> meshKnoten(f.teile.size(), nullptr);
    size_t verts = 0, tris = 0, geskinnt = 0, gewichtet = 0;
    for (size_t i = 0; i < f.teile.size(); ++i) {
        const ns::SkelMesh& m = f.teile[i];
        meshKnoten[i] = BaueMesh(ip, m, m.name, mass);
        Log("[%s s] Mesh %s", uhr().c_str(), m.name.c_str());
        if (meshKnoten[i] == nullptr) continue;
        verts += m.numVerts;
        tris += m.indices.size() / 3;
        meshKnoten[i]->SetUserPropString(MSTR(_T("ns_id")), M(id));
        meshKnoten[i]->SetUserPropString(MSTR(_T("ns_mesh")), M(m.packageName));
        if (o.skin && BaueSkin(ip, meshKnoten[i], m, knoten, gewichtet)) ++geskinnt;
        Log("[%s s] Skin %s", uhr().c_str(), m.name.c_str());
    }

    // Materialien: je Teil ein Multi/Sub mit einem Standardmaterial je Slot
    size_t materialien = 0, texturen = 0;
    const std::wstring cache = Ablage() + L"\\textures";
    CreateDirectoryW(cache.c_str(), nullptr);
    std::map<std::string, Mtl*> fertig;
    int slot = 0;
    for (size_t i = 0; i < f.teile.size(); ++i) {
        if (meshKnoten[i] == nullptr) continue;
        const ns::SkelMesh& m = f.teile[i];
        MultiMtl* multi = NewDefaultMultiMtl();
        if (multi == nullptr) continue;
        multi->SetName(M(m.name));
        multi->SetNumSubMtls(static_cast<int>(std::max<size_t>(1, m.materials.size())));
        for (size_t k = 0; k < m.materials.size(); ++k) {
            const std::string& mp = m.materials[k];
            // gleiches Material mit anderen Avatar-Farben ist ein eigenes Max-Material
            std::string schl = mp;
            if (m.hatFarben) { char h[24]; for (int c = 0; c < 9; ++c) { std::snprintf(h, sizeof h, "%02x", m.farben[c]); schl += h; } }
            auto it = fertig.find(schl);
            if (it == fertig.end()) {
                ns::TexturSatz ts;
                if (o.texturen && !mp.empty() && mp != "None") {
                    std::string te;
                    std::vector<std::string> prot;
                    ns::LoeseMaterial(g, mp, cache, ts, &prot, m.hatFarben ? m.farben : nullptr);
                    for (const std::string& z : prot) Log("  %s", z.c_str());
                    texturen += (ts.farbe.empty() ? 0 : 1) + (ts.normal.empty() ? 0 : 1) + (ts.opazitaet.empty() ? 0 : 1);
                }
                Log("[%s s] Texturen %s", uhr().c_str(), mp.c_str());
                if (!o.normalMaps) ts.normal.clear();
                Mtl* mtl = BaueMaterial(ip, KurzName(mp.empty() ? m.slotNames[k] : mp), ts);
                Log("[%s s] Material", uhr().c_str());
                if (mtl != nullptr) ++materialien;
                it = fertig.emplace(schl, mtl).first;
            }
            MSTR sn = M(m.slotNames[k]);
            if (it->second != nullptr) multi->SetSubMtlAndName(static_cast<int>(k), it->second, sn);
        }
        meshKnoten[i]->SetMtl(multi);
        if (slot < 24) ip->PutMtlToMtlEditor(multi, slot++);
    }

    ip->ClearNodeSelection(FALSE);
    for (INode* n : meshKnoten) if (n) ip->SelectNode(n, 0);
    ip->EnableSceneRedraw();
    theHold.Resume();
    ip->RedrawViews(ip->GetTime());
    Log("[%s s] Neuzeichnen", uhr().c_str());

    const double sek = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    Log("Fertig: %zu Knochen, %zu Meshes (%zu Vertices, %zu Dreiecke), %zu mit Skin (%zu Vertices gewichtet), %zu Materialien, %zu Texturen, %s s",
        bones, f.teile.size(), verts, tris, geskinnt, gewichtet, materialien, texturen, Dez(sek, 1).c_str());
    char b[600];
    std::snprintf(b, sizeof b, "%s: %zu part(s), %zu bones, %zu vertices, %zu triangles, %zu skinned, %zu materials, %zu textures (%s s)%s%s",
                  figurName.c_str(), f.teile.size(), bones, verts, tris, geskinnt, materialien, texturen, Dez(sek, 1).c_str(),
                  fehler.empty() ? "" : " - skipped: ", fehler.c_str());
    bericht = Breit(b);
    return bones > 0 || verts > 0;
}

// ------------------------------------------------------------
//  Animation
// ------------------------------------------------------------
namespace {

bool FrischeController(Interface* ip, INode* n, Control*& rot, Control*& pos, Control*& scl) {
    Control* tmc = n->GetTMController();
    if (tmc == nullptr) return false;
    Control* r = static_cast<Control*>(ip->CreateInstance(CTRL_ROTATION_CLASS_ID, Class_ID(LININTERP_ROTATION_CLASS_ID, 0)));
    Control* q = static_cast<Control*>(ip->CreateInstance(CTRL_POSITION_CLASS_ID, Class_ID(LININTERP_POSITION_CLASS_ID, 0)));
    Control* s = static_cast<Control*>(ip->CreateInstance(CTRL_SCALE_CLASS_ID, Class_ID(LININTERP_SCALE_CLASS_ID, 0)));
    if (r != nullptr) tmc->SetRotationController(r);
    if (q != nullptr) tmc->SetPositionController(q);
    if (s != nullptr) tmc->SetScaleController(s);
    rot = tmc->GetRotationController();
    pos = tmc->GetPositionController();
    scl = tmc->GetScaleController();
    return rot != nullptr && pos != nullptr;
}

struct Bein {
    INode* n = nullptr;
    Matrix3 ruhe;
    bool istWurzel = false;
};

struct ZielSkelett {
    std::vector<Bein> knochen;
    std::unordered_map<std::string, size_t> nachName;
    std::string figur;
    std::vector<std::string> skelette;
};

bool HoleSkelett(Interface* ip, const std::string& idWunsch, ZielSkelett& z) {
    const std::string id = idWunsch.empty() ? ZielId(ip) : idWunsch;
    for (INode* n : KnochenMitId(ip, id)) {
        Bein b;
        if (!MatrixAusText(NodeProp(n, _T("ns_rest")), b.ruhe)) continue;
        b.n = n;
        b.istWurzel = n->GetParentNode() == nullptr || n->GetParentNode()->IsRootNode();
        z.nachName[ns::Lower(NodeProp(n, _T("ns_bone")))] = z.knochen.size();
        z.knochen.push_back(b);
        if (z.figur.empty()) { z.figur = NodeProp(n, _T("ns_figur")); z.skelette = Trenne(NodeProp(n, _T("ns_skel"))); }
    }
    return !z.knochen.empty();
}

// Spur je Knochen (Index in z.knochen), nullptr = keine
std::vector<const ns::AnimTrack*> SpurenJeKnochen(const ns::AnimClip& c, const ZielSkelett& z, size_t& passend) {
    std::vector<const ns::AnimTrack*> aus(z.knochen.size(), nullptr);
    passend = 0;
    for (const ns::AnimTrack& s : c.tracks) {
        if (s.bone.empty()) continue;
        const auto it = z.nachName.find(ns::Lower(s.bone));
        if (it == z.nachName.end()) continue;
        ++passend;
        // Wurzel des Clip-Skeletts, die in der Figur einen Eltern hat (Gesichtsclips: spineC
        // liegt im Gesichtsraum, in der Figur tief in der Koerperkette) - Ruhelage behalten.
        if (s.wurzel && !z.knochen[it->second].istWurzel) continue;
        aus[it->second] = &s;
    }
    return aus;
}

bool PasstClip(const ns::AnimClip& c, size_t passend) {
    size_t mitName = 0;
    for (const ns::AnimTrack& s : c.tracks) if (!s.bone.empty()) ++mitName;
    return passend > 0 && passend * 2 >= mitName;
}

// Lokale Lage (Max) bei Frame k
Matrix3 LokalBei(const Bein& b, const ns::AnimTrack* s, const ns::AnimClip& c, int k, bool wurzelBewegung, float mass) {
    if (s == nullptr || (b.istWurzel && !wurzelBewegung)) return b.ruhe;
    const ns::M34 l = ns::M34::From(&s->rot[static_cast<size_t>(k) * 4], &s->pos[static_cast<size_t>(k) * 3], &s->scl[static_cast<size_t>(k) * 3]);
    Matrix3 m = ZuMax(l, mass);
    if (!c.additive.empty()) {
        // UE: Drehung = Delta * Basis, Verschiebung addiert (Basis = Ruhelage)
        Matrix3 r = b.ruhe;
        const Point3 t = r.GetRow(3) + m.GetRow(3);
        r.SetRow(3, Point3(0, 0, 0));
        Matrix3 d = m;
        d.SetRow(3, Point3(0, 0, 0));
        m = r * d;
        m.SetRow(3, t);
    }
    return m;
}

// Keys setzen mit fortlaufendem Quaternion-Vorzeichen; Skalierung nur, wo sie von 1 abweicht.
struct KeySetzer {
    Control* rot = nullptr;
    Control* pos = nullptr;
    Control* scl = nullptr;
    bool mitSkala = false;
    Quat vorher;
    bool erster = true;
    size_t keys = 0, flips = 0;
    void Setze(TimeValue t, const Matrix3& l) {
        Point3 ax[3] = { l.GetRow(0), l.GetRow(1), l.GetRow(2) };
        Point3 sk(Length(ax[0]), Length(ax[1]), Length(ax[2]));
        Matrix3 r;
        r.IdentityMatrix();
        for (int i = 0; i < 3; ++i) r.SetRow(i, (&sk.x)[i] > 1e-8f ? ax[i] / (&sk.x)[i] : ax[i]);
        Quat q(r);
        if (!erster && (q.x * vorher.x + q.y * vorher.y + q.z * vorher.z + q.w * vorher.w) < 0.0f) {
            q = Quat(-q.x, -q.y, -q.z, -q.w);
            ++flips;
        }
        erster = false;
        vorher = q;
        Point3 p = l.GetRow(3);
        rot->SetValue(t, &q, 1, CTRL_ABSOLUTE);
        pos->SetValue(t, &p, 1, CTRL_ABSOLUTE);
        if (mitSkala && scl != nullptr) {
            ScaleValue sv(sk);
            scl->SetValue(t, &sv, 1, CTRL_ABSOLUTE);
        }
        ++keys;
    }
};

bool HatSkala(const ns::AnimTrack* s) {
    if (s == nullptr) return false;
    for (float v : s->scl) if (std::fabs(v - 1.0f) > 1e-3f) return true;
    return false;
}

#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
bool FuehreSkript(const std::wstring& skript) {
    return ExecuteMAXScriptScript(skript.c_str(), MAXScript::ScriptSource::NonEmbedded, TRUE) != FALSE;
}
#else
bool FuehreSkript(const std::wstring& skript) {
    return ExecuteMAXScriptScript(skript.c_str(), TRUE) != FALSE;
}
#endif

std::wstring SkriptText(const std::string& s) {
    std::wstring w;
    for (wchar_t c : Breit(s)) {
        if (c == L'\\' || c == L'"') w += L'\\';
        w += c;
    }
    return w;
}

void LoescheNotizspuren(INode* w) {
    while (w != nullptr && w->NumNoteTracks() > 0) w->DeleteNoteTrack(w->GetNoteTrack(0), TRUE);
}

DWORD WINAPI KeinFortschritt(LPVOID) { return 0; }

// Skelett-Cache fuer Clips (Namensaufloesung), je Skelettpfad
const ns::RefSkeleton* SkelettFuer(ns::Game& g, const std::string& pfad) {
    static std::map<std::string, std::unique_ptr<ns::RefSkeleton>> cache;
    auto it = cache.find(pfad);
    if (it != cache.end()) return it->second.get();
    auto sk = std::make_unique<ns::RefSkeleton>();
    std::string e;
    if (!ns::ReadSkeleton(g, pfad, *sk, e)) sk.reset();
    return cache.emplace(pfad, std::move(sk)).first->second.get();
}

bool LiesClip(ns::Game& g, const std::string& pfad, ns::AnimClip& c, std::string& fehler) {
    ns::Package pk;
    if (!g.LoadPackage(pfad, pk, fehler)) return false;
    const int e = pk.FindExport("AnimSequence");
    if (e < 0) { fehler = "no AnimSequence"; return false; }
    ns::AnimClip info;
    ns::ReadAnimInfo(g, pk, e, info);
    return ns::ReadAnimSequence(g, pfad, c, fehler, SkelettFuer(g, info.skeletonPath));
}

} // namespace

std::vector<SzenenFigur> FigurenInSzene() {
    std::vector<SzenenFigur> aus;
    Interface* ip = GetCOREInterface();
    if (ip == nullptr) return aus;
    std::map<std::string, size_t> nachId;
    std::map<std::string, ULONG> neueste;
    for (INode* n : AlleKnoten(ip)) {
        if (NodeProp(n, _T("ns_bone")).empty()) continue;
        const std::string id = NodeProp(n, _T("ns_id"));
        if (id.empty()) continue;
        auto it = nachId.find(id);
        if (it == nachId.end()) {
            SzenenFigur f;
            f.id = id;
            f.name = NodeProp(n, _T("ns_figur"));
            f.skelette = Trenne(NodeProp(n, _T("ns_skel")));
            it = nachId.emplace(id, aus.size()).first;
            aus.push_back(f);
        }
        aus[it->second].knochen++;
        neueste[id] = std::max(neueste[id], n->GetHandle());
    }
    const std::string gewaehlt = ZielId(ip);
    std::sort(aus.begin(), aus.end(), [&](const SzenenFigur& a, const SzenenFigur& b) {
        if ((a.id == gewaehlt) != (b.id == gewaehlt)) return a.id == gewaehlt;
        return neueste[a.id] > neueste[b.id];
    });
    return aus;
}

bool WendeAnimationAn(ns::Game& g, const std::string& animPfad, bool wurzelBewegung, const std::string& id, std::wstring& bericht) {
    Interface* ip = GetCOREInterface();
    LogNeu(("Animation " + animPfad).c_str());
    ZielSkelett z;
    if (!HoleSkelett(ip, id, z)) { bericht = L"No Shinobi Striker character in the scene - import one first."; return false; }
    std::string fehler;
    ns::AnimClip c;
    if (!LiesClip(g, animPfad, c, fehler)) {
        Log("FEHLER %s", fehler.c_str());
        bericht = Breit(ns::Katalog::Blatt(animPfad) + ": " + fehler);
        return false;
    }
    size_t passend = 0;
    const std::vector<const ns::AnimTrack*> spuren = SpurenJeKnochen(c, z, passend);
    Log("Clip: %s s, %d Bilder @ %s Hz, %zu Spuren, davon %zu im Skelett (%zu Knochen) der Figur %s, additiv '%s'", Dez(c.length, 3).c_str(),
        c.numFrames, Dez(c.rate, 1).c_str(), c.tracks.size(), passend, z.knochen.size(), z.figur.c_str(), c.additive.c_str());
    if (!PasstClip(c, passend)) {
        char b[256];
        std::snprintf(b, sizeof b, "This animation does not fit the character: only %zu of %zu tracks match its skeleton. Nothing changed.",
                      passend, c.tracks.size());
        bericht = Breit(b);
        Log("%s", b);
        return false;
    }
    const int fps = std::max(1, static_cast<int>(std::lround(c.rate)));
    if (GetFrameRate() != fps) SetFrameRate(fps);
    const int tpf = GetTicksPerFrame();
    const float mass = Massstab();
    size_t keys = 0, mitKeys = 0, flips = 0;
    theHold.Suspend();
    ip->DisableSceneRedraw();
    LoescheNotizspuren(ip->GetRootNode());
    SuspendAnimate();
    for (size_t i = 0; i < z.knochen.size(); ++i) {
        const Bein& b = z.knochen[i];
        KeySetzer ks;
        if (!FrischeController(ip, b.n, ks.rot, ks.pos, ks.scl)) continue;
        ks.Setze(0, b.ruhe);                   // Grundwert (ohne Animationsmodus: kein Key)
        ks.keys = 0;
        const ns::AnimTrack* s = spuren[i];
        if (s == nullptr) continue;
        ++mitKeys;
        ks.mitSkala = HatSkala(s);
        AnimateOn();
        for (int k = 0; k < c.numFrames; ++k) ks.Setze(static_cast<TimeValue>(k) * tpf, LokalBei(b, s, c, k, wurzelBewegung, mass));
        AnimateOff();
        keys += ks.keys;
        flips += ks.flips;
    }
    ResumeAnimate();
    ip->SetAnimRange(Interval(0, std::max(1, c.numFrames - 1) * tpf));
    ip->SetTime(0);
    ip->EnableSceneRedraw();
    theHold.Resume();
    ip->RedrawViews(ip->GetTime());
    const std::string name = ns::Katalog::Blatt(animPfad);
    Log("Fertig: %zu Knochen mit Keys, %zu Keys, %zu Vorzeichenwechsel, Bereich 0..%d", mitKeys, keys, flips, c.numFrames - 1);
    char b[400];
    std::snprintf(b, sizeof b, "%s: %d frames (%s s), %zu of %zu tracks on %s, %zu keys%s", name.c_str(), c.numFrames,
                  Dez(c.length, 2).c_str(), passend, c.tracks.size(), z.figur.c_str(), keys, c.additive.empty() ? "" : " (additive, on the bind pose)");
    bericht = Breit(b);
    return true;
}

bool WendeFolgeAn(ns::Game& g, const std::vector<std::string>& pfade, int abstand, bool notiz, bool wurzelBewegung,
                  const std::string& id, std::vector<Sequenz>& plan, std::wstring& bericht) {
    const auto t0 = std::chrono::steady_clock::now();
    Interface* ip = GetCOREInterface();
    LogNeu(("Folge mit " + std::to_string(pfade.size()) + " Clips").c_str());
    plan.clear();
    ZielSkelett z;
    if (!HoleSkelett(ip, id, z)) { bericht = L"No Shinobi Striker character in the scene - import one first."; return false; }
    abstand = std::max(0, std::min(abstand, 1000));

    std::vector<ns::AnimClip> clips;
    std::vector<std::string> namen;
    size_t unpassend = 0, kaputt = 0;
    for (const std::string& pfad : pfade) {
        std::string fehler;
        ns::AnimClip c;
        if (!LiesClip(g, pfad, c, fehler)) { ++kaputt; Log("nicht lesbar %s: %s", pfad.c_str(), fehler.c_str()); continue; }
        size_t passend = 0;
        SpurenJeKnochen(c, z, passend);
        if (!PasstClip(c, passend)) { ++unpassend; continue; }
        namen.push_back(ns::Katalog::Blatt(pfad));
        clips.push_back(std::move(c));
    }
    if (clips.empty()) {
        bericht = L"None of the " + std::to_wstring(pfade.size()) + L" clips fits this character - nothing changed.";
        return false;
    }
    // Zeitplan in Bildern (30 fps; Clips mit anderer Rate werden umgerechnet)
    if (GetFrameRate() != 30) SetFrameRate(30);
    const TimeValue tpf = GetTicksPerFrame();
    int at = abstand;
    for (size_t i = 0; i < clips.size(); ++i) {
        Sequenz sq;
        sq.name = namen[i];
        sq.start = at;
        const int bilder = static_cast<int>(std::lround((clips[i].numFrames - 1) * 30.0 / std::max(1.0f, clips[i].rate)));
        sq.ende = at + std::max(1, bilder);
        plan.push_back(sq);
        at = sq.ende + abstand;
    }
    std::vector<std::vector<const ns::AnimTrack*>> spuren;
    for (const ns::AnimClip& c : clips) { size_t p = 0; spuren.push_back(SpurenJeKnochen(c, z, p)); }

    const float mass = Massstab();
    size_t keys = 0, flips = 0;
    bool abgebrochen = false;
    theHold.Suspend();
    ip->DisableSceneRedraw();
    ip->ProgressStart(_T("Shinobi Striker: loading animations into the timeline"), TRUE, KeinFortschritt, nullptr);
    SuspendAnimate();
    for (size_t i = 0; i < z.knochen.size() && !abgebrochen; ++i) {
        const Bein& b = z.knochen[i];
        KeySetzer ks;
        if (!FrischeController(ip, b.n, ks.rot, ks.pos, ks.scl)) continue;
        for (size_t c = 0; c < clips.size(); ++c) if (HatSkala(spuren[c][i])) ks.mitSkala = true;
        ks.Setze(0, b.ruhe);
        ks.keys = 0;
        AnimateOn();
        ks.Setze(0, b.ruhe);                                  // Bild 0: Bindepose
        for (size_t c = 0; c < clips.size(); ++c) {
            const TimeValue a = plan[c].start * tpf, e = plan[c].ende * tpf;
            const ns::AnimTrack* s = spuren[c][i];
            if (s == nullptr) {
                ks.Setze(a, b.ruhe);
                ks.Setze(e, b.ruhe);
                continue;
            }
            const ns::AnimClip& cl = clips[c];
            for (int k = 0; k < cl.numFrames; ++k) {
                TimeValue t = a + static_cast<TimeValue>(std::lround(k * (double)TIME_TICKSPERSEC / std::max(1.0f, cl.rate)));
                if (t > e) t = e;
                ks.Setze(t, LokalBei(b, s, cl, k, wurzelBewegung, mass));
            }
        }
        AnimateOff();
        keys += ks.keys;
        flips += ks.flips;
        ip->ProgressUpdate(static_cast<int>(100.0 * static_cast<double>(i + 1) / static_cast<double>(z.knochen.size())), FALSE);
        if (ip->GetCancel()) abgebrochen = true;
    }
    ResumeAnimate();
    ip->ProgressEnd();
    if (abgebrochen) ip->SetCancel(FALSE);

    size_t notizKeys = 0;
    INode* w = ip->GetRootNode();
    LoescheNotizspuren(w);
    if (notiz && w != nullptr) {
        DefNoteTrack* nt = static_cast<DefNoteTrack*>(NewDefaultNoteTrack());
        if (nt != nullptr) {
            for (const Sequenz& sq : plan) {
                const MSTR text = M(sq.name);
                NoteKey* k0 = new NoteKey(sq.start * tpf, text, 0);
                NoteKey* k1 = new NoteKey(sq.ende * tpf, text, 0);
                nt->keys.Append(1, &k0);
                nt->keys.Append(1, &k1);
            }
            w->AddNoteTrack(nt);
            notizKeys = static_cast<size_t>(nt->keys.Count());
        }
    }
    // Custom Attributes NeoDexSequenceData (Definition wie WhiteoutDex/SWBF2/TFU2)
    bool caOk = false;
    if (notiz) {
        std::wstring namenL, startL, endeL, nlL, rarL, spL, extL, grL;
        for (size_t i = 0; i < plan.size(); ++i) {
            const wchar_t* k = i ? L"," : L"";
            namenL += k + (L"\"" + SkriptText(plan[i].name) + L"\"");
            startL += k + std::to_wstring(plan[i].start);
            endeL += k + std::to_wstring(plan[i].ende);
            nlL += k + std::wstring(L"false");
            rarL += k + std::wstring(L"0.0");
            spL += k + std::wstring(L"0.0");
            extL += k + std::wstring(L"\"\"");
            grL += k + std::wstring(L"\"\"");
        }
        const std::wstring sk =
            L"(\n local ca = undefined\n try (ca = ::NeoDexSequenceCA) catch (ca = undefined)\n"
            L" if (ca == undefined) do ca = attributes \"NeoDexSequenceData\" (\n  parameters main (\n"
            L"   seqNames type:#stringTab tabSizeVariable:true\n   startFrames type:#intTab tabSizeVariable:true\n"
            L"   endFrames type:#intTab tabSizeVariable:true\n   nonLooping type:#boolTab tabSizeVariable:true\n"
            L"   rarity type:#floatTab tabSizeVariable:true\n   moveSpeed type:#floatTab tabSizeVariable:true\n"
            L"   seqExtents type:#stringTab tabSizeVariable:true\n   sharedGroup type:#stringTab tabSizeVariable:true\n  )\n )\n"
            L" for i = custAttributes.count rootNode to 1 by -1 do if (custAttributes.get rootNode i).name == \"NeoDexSequenceData\" do custAttributes.delete rootNode i\n"
            L" custAttributes.add rootNode ca\n"
            L" rootNode.seqNames = #(" + namenL + L")\n rootNode.startFrames = #(" + startL + L")\n rootNode.endFrames = #(" + endeL +
            L")\n rootNode.nonLooping = #(" + nlL + L")\n rootNode.rarity = #(" + rarL + L")\n rootNode.moveSpeed = #(" + spL +
            L")\n rootNode.seqExtents = #(" + extL + L")\n rootNode.sharedGroup = #(" + grL + L")\n OK\n)\n";
        caOk = FuehreSkript(sk);
    }
    ip->SetAnimRange(Interval(0, std::max(1, plan.back().ende) * tpf));
    ip->SetTime(0);
    ip->EnableSceneRedraw();
    theHold.Resume();
    ip->RedrawViews(ip->GetTime());

    const double sek = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    Log("Folge: %zu Clips (%zu passen nicht, %zu nicht lesbar), %zu Knochen, %zu Keys, %zu Vorzeichenwechsel, Bereich 0..%d, %s s, "
        "Notizspur %zu Keys, Custom Attributes %s%s",
        clips.size(), unpassend, kaputt, z.knochen.size(), keys, flips, plan.back().ende, Dez(sek, 1).c_str(), notizKeys,
        notiz ? (caOk ? "ok" : "FEHLGESCHLAGEN") : "aus", abgebrochen ? " - ABGEBROCHEN" : "");
    char b[400];
    std::snprintf(b, sizeof b, "%zu clips in the timeline (gap %d frames, 0 - %d)%s%s; %zu keys in %s s%s%s", clips.size(), abstand,
                  plan.back().ende, unpassend ? (", " + std::to_string(unpassend) + " did not fit").c_str() : "",
                  kaputt ? (", " + std::to_string(kaputt) + " unreadable").c_str() : "", keys, Dez(sek, 1).c_str(),
                  notiz ? "; note track + sequence attributes" : "", abgebrochen ? " - CANCELLED, bones after that keep the bind pose" : "");
    bericht = Breit(b);
    return true;
}

bool LiesSequenzen(std::vector<Sequenz>& aus) {
    aus.clear();
    Interface* ip = GetCOREInterface();
    INode* w = ip ? ip->GetRootNode() : nullptr;
    if (w == nullptr) return false;
    const TimeValue tpf = std::max(1, GetTicksPerFrame());
    for (int i = 0; i < w->NumNoteTracks(); ++i) {
        DefNoteTrack* nt = static_cast<DefNoteTrack*>(w->GetNoteTrack(i));
        if (nt == nullptr) continue;
        for (int k = 0; k + 1 < nt->keys.Count(); ++k) {
            NoteKey* a = nt->keys[k];
            NoteKey* e = nt->keys[k + 1];
            if (a == nullptr || e == nullptr || a->note != e->note) continue;
            Sequenz sq;
            sq.name = Utf8(std::wstring(a->note.data()));
            sq.start = static_cast<int>(a->time / tpf);
            sq.ende = static_cast<int>(e->time / tpf);
            aus.push_back(sq);
            ++k;
        }
    }
    return !aus.empty();
}

void ZeigeBereich(int startBild, int endeBild) {
    Interface* ip = GetCOREInterface();
    if (ip == nullptr) return;
    const TimeValue tpf = GetTicksPerFrame();
    ip->SetAnimRange(Interval(startBild * tpf, std::max(startBild + 1, endeBild) * tpf));
    ip->SetTime(startBild * tpf);
}

uint32_t ThemeFarbe(int welche) { return static_cast<uint32_t>(GetCustSysColor(welche)); }

// ------------------------------------------------------------
//  Datei -> Importieren: .pak/.exe des Spiels oeffnet das Fenster
// ------------------------------------------------------------
int ImportiereEingang(const MCHAR* pfad, BOOL) {
    std::wstring w = pfad ? pfad : L"";
    // vom Container oder der EXE zum Spielordner hochgehen
    for (int i = 0; i < 5 && !w.empty() && !IstSpielordner(w); ++i) {
        const size_t p = w.find_last_of(L"\\/");
        if (p == std::wstring::npos) break;
        w = w.substr(0, p);
    }
    if (IstSpielordner(w)) SchreibeEinstellung(L"Spielordner", w);
    return OeffneFenster() > 0 ? 1 : 0;
}

} // namespace nsi

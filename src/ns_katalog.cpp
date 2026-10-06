// ns_katalog.cpp - Katalog aufbauen (parallel) und zwischenspeichern
#include "ns_katalog.h"
#include "ns_anim.h"
#include <windows.h>
#include <atomic>
#include <thread>
#include <cstdio>
#include <algorithm>
#include <map>
#include <set>
#include <array>
#include <mutex>

namespace ns {

static const char* kCacheKennung = "NSIKATALOG 2";

std::string Katalog::Blatt(const std::string& file) {
    size_t sl = file.find_last_of('/');
    std::string b = sl == std::string::npos ? file : file.substr(sl + 1);
    size_t dot = b.find_last_of('.');
    return dot == std::string::npos ? b : b.substr(0, dot);
}

std::string Katalog::Gruppe(const std::string& file) {
    size_t c = file.find("/Content/");
    std::string g = c == std::string::npos ? file : file.substr(c + 9);
    if (file.rfind("NARUTO/", 0) != 0 && c != std::string::npos) g = file.substr(0, c) + "/" + g;   // Engine/Plugins
    size_t sl = g.find_last_of('/');
    return sl == std::string::npos ? std::string() : g.substr(0, sl);
}

static int KategorieVon(const std::string& file) {
    const std::string l = Lower(file);
    if (l.rfind("naruto/content/characters/original/", 0) == 0) return K_Helden;
    if (l.rfind("naruto/content/characters/custom/", 0) == 0) return K_Avatar;
    if (l.rfind("naruto/content/weapons/", 0) == 0 || l.find("/items/") != std::string::npos || l.find("/props/") != std::string::npos)
        return K_Gegenstaende;
    return K_Andere;
}

// Schluessel fuer den Cache: Groesse + Zeit aller .pak
static std::string Fingerabdruck(Game& g) {
    std::string s = kCacheKennung;
    std::wstring paks = g.Ar().GameDir() + L"\\NARUTO\\Content\\Paks\\";
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((paks + L"*.pak").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return s;
    std::vector<std::string> teile;
    do {
        char b[400];
        snprintf(b, sizeof b, "|%s:%llu:%llu", Narrow(fd.cFileName).c_str(),
                 ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow,
                 ((unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime);
        teile.push_back(b);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    std::sort(teile.begin(), teile.end());
    for (auto& t : teile) s += t;
    return s;
}

static std::vector<std::string> Spalten(const std::string& z) {
    std::vector<std::string> o;
    size_t p = 0;
    while (true) {
        size_t t = z.find('\t', p);
        o.push_back(z.substr(p, t == std::string::npos ? std::string::npos : t - p));
        if (t == std::string::npos) break;
        p = t + 1;
    }
    return o;
}

static bool LiesCache(const std::wstring& datei, const std::string& fp, Katalog& k) {
    FILE* f = _wfopen(datei.c_str(), L"rb");
    if (!f) return false;
    std::string inhalt;
    char b[65536]; size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) inhalt.append(b, n);
    fclose(f);
    size_t p = inhalt.find('\n');
    if (p == std::string::npos || inhalt.substr(0, p) != fp) return false;
    p++;
    while (p < inhalt.size()) {
        size_t e = inhalt.find('\n', p);
        if (e == std::string::npos) e = inhalt.size();
        std::vector<std::string> s = Spalten(inhalt.substr(p, e - p));
        p = e + 1;
        if (s.size() >= 3 && s[0] == "M") {
            MeshEintrag m; m.file = s[1]; m.skeleton = s[2];
            m.name = Katalog::Blatt(m.file); m.gruppe = Katalog::Gruppe(m.file); m.kategorie = KategorieVon(m.file);
            k.meshes.push_back(std::move(m));
        } else if (s.size() >= 4 && s[0] == "F") {
            FigurEintrag fe; fe.file = s[1]; fe.name = s[2];
            // je Teil: mesh|slot|mat,mat,...
            for (size_t i = 3; i < s.size(); i++) {
                FigurTeil t;
                const std::string& z = s[i];
                const size_t p1 = z.find('|'), p2 = p1 == std::string::npos ? p1 : z.find('|', p1 + 1);
                t.mesh = z.substr(0, p1);
                if (p1 != std::string::npos) t.slot = z.substr(p1 + 1, p2 == std::string::npos ? std::string::npos : p2 - p1 - 1);
                if (p2 != std::string::npos && p2 + 1 < z.size()) {
                    size_t q = p2 + 1;
                    while (q <= z.size()) { size_t e2 = z.find(',', q); if (e2 == std::string::npos) e2 = z.size(); t.materialien.push_back(z.substr(q, e2 - q)); q = e2 + 1; }
                }
                fe.teile.push_back(t);
            }
            fe.gruppe = Katalog::Gruppe(fe.file); fe.kategorie = KategorieVon(fe.file);
            k.figuren.push_back(std::move(fe));
        } else if (s.size() >= 6 && s[0] == "A") {
            AnimEintrag a; a.file = s[1]; a.skeleton = s[2]; a.frames = atoi(s[3].c_str()); a.laenge = (float)atof(s[4].c_str()); a.additiv = s[5] == "1";
            a.name = Katalog::Blatt(a.file); a.gruppe = Katalog::Gruppe(a.file);
            k.anims.push_back(std::move(a));
        }
    }
    return !k.meshes.empty();
}

static void SchreibeCache(const std::wstring& datei, const std::string& fp, const Katalog& k) {
    std::wstring tmp = datei + L".neu";
    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f) return;
    fprintf(f, "%s\n", fp.c_str());
    for (auto& fi : k.figuren) {
        fprintf(f, "F\t%s\t%s", fi.file.c_str(), fi.name.c_str());
        for (auto& t : fi.teile) {
            fprintf(f, "\t%s|%s|", t.mesh.c_str(), t.slot.c_str());
            for (size_t i = 0; i < t.materialien.size(); i++) fprintf(f, "%s%s", i ? "," : "", t.materialien[i].c_str());
        }
        fprintf(f, "\n");
    }
    for (auto& m : k.meshes) fprintf(f, "M\t%s\t%s\n", m.file.c_str(), m.skeleton.c_str());
    for (auto& a : k.anims) fprintf(f, "A\t%s\t%s\t%d\t%.4f\t%d\n", a.file.c_str(), a.skeleton.c_str(), a.frames, a.laenge, a.additiv ? 1 : 0);
    fclose(f);
    MoveFileExW(tmp.c_str(), datei.c_str(), MOVEFILE_REPLACE_EXISTING);
}

std::map<std::string, std::string> LadeTexte(Game& g, const char* sprache) {
    std::map<std::string, std::string> aus;
    const std::string ordner = std::string("NARUTO/Content/L10N/") + sprache + "/Localize/DataTable/";
    for (auto& fr : g.Ar().AllFiles()) {
        if (fr.path.size() < ordner.size() || _strnicmp(fr.path.c_str(), ordner.c_str(), ordner.size()) != 0) continue;
        if (fr.path.size() < 7 || _stricmp(fr.path.c_str() + fr.path.size() - 7, ".uasset") != 0) continue;
        Package pk; std::string e;
        if (!g.LoadPackage(fr.path, pk, e) || pk.exports.empty()) continue;
        const Export& ex = pk.exports[0];
        Reader r(pk.data.data() + ex.dataOffset, (size_t)ex.serialSize, &pk);
        std::vector<Prop> ps;
        if (!ReadProps(r, ps)) continue;
        if (r.b32()) r.skip(16);
        // CC2-"Spreadsheet": Zeilenanzahl, dann je Zeile eine Eigenschaftsliste (Id + Text)
        const int32_t n = r.i32();
        for (int32_t i = 0; i < n && !r.bad && i < 100000; i++) {
            std::vector<Prop> zeile;
            if (!ReadProps(r, zeile)) break;
            const Prop* id = FindProp(zeile, "Id");
            if (!id) continue;
            for (const Prop& p : zeile)
                if (p.type == "StrProperty" && !p.str.empty()) { aus.emplace(id->str, p.str); break; }
        }
    }
    return aus;
}

// Alle Zeilen einer CC2-Tabelle (Spreadsheet) als Eigenschaftslisten
static bool TabellenZeilen(Game& g, const std::string& tabelle, std::vector<std::vector<Prop>>& zeilen) {
    Package pk; std::string e;
    if (!g.LoadPackage(tabelle, pk, e) || pk.exports.empty()) return false;
    const Export& ex = pk.exports[0];
    Reader r(pk.data.data() + ex.dataOffset, (size_t)ex.serialSize, &pk);
    std::vector<Prop> ps;
    if (!ReadProps(r, ps)) return false;
    if (r.b32()) r.skip(16);
    const int32_t n = r.i32();
    for (int32_t i = 0; i < n && !r.bad && i < 100000; i++) {
        std::vector<Prop> z;
        if (!ReadProps(r, z)) return false;
        zeilen.push_back(std::move(z));
    }
    return true;
}

bool AvatarFarben(Game& g, const std::string& meshPfad, uint8_t farben[9]) {
    struct Daten {
        std::map<std::string, std::pair<std::string, std::string>> jeMesh;   // Paketname (klein) -> Farb-IDs
        std::map<std::string, std::array<uint8_t, 3>> farbe;                 // ID -> RGB
    };
    static Daten d;
    static std::once_flag einmal;
    std::call_once(einmal, [&g]() {
        std::vector<std::vector<Prop>> z;
        if (TabellenZeilen(g, "/Game/DataTable/Game/Player/Custom/DT_PartsColors", z))
            for (auto& zeile : z) {
                const Prop* id = FindProp(zeile, "IDName");
                const Prop* c = FindProp(zeile, "BaseColor");
                if (id && c && c->vec.size() >= 3)   // FColor liegt als B G R A
                    d.farbe[id->str] = { (uint8_t)c->vec[2], (uint8_t)c->vec[1], (uint8_t)c->vec[0] };
            }
        for (const char* t : { "DT_CustomBase", "DT_CustomJacket", "DT_CustomPants", "DT_CustomHair", "DT_CustomAccessories", "DT_CustomForehead" }) {
            z.clear();
            if (!TabellenZeilen(g, std::string("/Game/DataTable/Game/Player/Custom/") + t, z)) continue;
            for (auto& zeile : z) {
                const Prop* d0 = FindProp(zeile, "IDPartsColorDef0");
                const Prop* d1 = FindProp(zeile, "IDPartsColorDef1");
                if (!d0 || !d1) continue;
                for (const char* feld : { "RefMesh", "RefMesh_S", "RefMesh_U", "RefMesh_Z" }) {
                    const Prop* m = FindProp(zeile, feld);
                    if (!m || m->str.empty() || m->str[0] != '/') continue;
                    d.jeMesh.emplace(Lower(m->str.substr(0, m->str.find('.'))), std::make_pair(d0->str, d1->str));
                }
            }
        }
    });
    std::string k = Lower(meshPfad);
    if (k.rfind("naruto/content/", 0) == 0) k = Lower(FileToPackageName(meshPfad));
    auto it = d.jeMesh.find(k);
    if (it == d.jeMesh.end()) return false;
    auto farbeVon = [&](const std::string& id, uint8_t* aus) {
        auto f = d.farbe.find(id);
        if (f == d.farbe.end()) { aus[0] = aus[1] = aus[2] = 255; return; }
        aus[0] = f->second[0]; aus[1] = f->second[1]; aus[2] = f->second[2];
    };
    farbeVon("ID_PartsColor_Skin_00", farben);
    farbeVon(it->second.first, farben + 3);
    farbeVon(it->second.second, farben + 6);
    return true;
}

// Ein Kleidungsstueck aus einer Charaktereditor-Tabelle (DT_CustomJacket usw.): Zeile mit IDName,
// RefMesh und Materialien (RefMaterialArray[i] fuer Slot MaterialIndexArray[i])
static bool AvatarTeil(Game& g, const char* tabelle, const char* id, const char* slot, FigurTeil& t) {
    Package pk; std::string e;
    if (!g.LoadPackage(std::string("/Game/DataTable/Game/Player/Custom/") + tabelle, pk, e) || pk.exports.empty()) return false;
    const Export& ex = pk.exports[0];
    Reader r(pk.data.data() + ex.dataOffset, (size_t)ex.serialSize, &pk);
    std::vector<Prop> ps;
    if (!ReadProps(r, ps)) return false;
    if (r.b32()) r.skip(16);
    const int32_t n = r.i32();
    for (int32_t i = 0; i < n && !r.bad && i < 100000; i++) {
        std::vector<Prop> z;
        if (!ReadProps(r, z)) return false;
        const Prop* name = FindProp(z, "IDName");
        if (!name || name->str != id) continue;
        const Prop* mesh = FindProp(z, "RefMesh");
        if (!mesh || mesh->str.empty() || mesh->str[0] != '/') return false;
        t.mesh = mesh->str.substr(0, mesh->str.find('.'));
        t.quelle = std::string(tabelle) + ":" + id;
        t.slot = slot;
        const Prop* mats = FindProp(z, "RefMaterialArray");
        const Prop* idx = FindProp(z, "MaterialIndexArray");
        if (mats)
            for (size_t k = 0; k < mats->kids.size(); k++) {
                const std::string m = mats->kids[k].str.substr(0, mats->kids[k].str.find('.'));
                const int slotNr = idx && k < idx->kids.size() ? (int)idx->kids[k].num : (int)k;
                if (m.empty() || m[0] != '/' || slotNr < 0 || slotNr > 64) continue;
                if ((int)t.materialien.size() <= slotNr) t.materialien.resize((size_t)slotNr + 1);
                t.materialien[(size_t)slotNr] = m;
            }
        return true;
    }
    return false;
}

// Anzeigename einer Figur aus ihrem Ordner (Original/D21/D21_Default -> "Character.D21")
static std::string FigurName(const std::string& file, const std::map<std::string, std::string>& texte) {
    // file: NARUTO/Content/Characters/<Art>/<Figur>/<Variante>/...
    std::vector<std::string> t;
    size_t p = 0;
    while (p <= file.size()) { size_t e = file.find('/', p); if (e == std::string::npos) e = file.size(); t.push_back(file.substr(p, e - p)); p = e + 1; }
    if (t.size() < 6) return Katalog::Blatt(file);
    // DLC-Pakete liegen eine Ebene tiefer (Original/DLC9/D91/D91_Default)
    size_t k = 4;
    if (t[k].rfind("DLC", 0) == 0 && t.size() >= 8 && t[k + 1] != "Meshes") k++;
    std::string figur = t[k], variante = t[k + 1];
    // Ordnernamen, die in den Texten anders heissen
    static const std::map<std::string, std::string> alias = { { "Bee", "KillerBee" }, { "Sarada", "Salad" } };
    auto al = alias.find(figur);
    const std::string schluessel = al != alias.end() ? al->second : figur;
    std::string name;
    auto it = texte.find("Character." + schluessel);
    // "D92_ZON" -> Kakuzu (ZON)
    if (it == texte.end() && figur.find('_') != std::string::npos) {
        auto ip = texte.find("Character." + figur.substr(0, figur.find('_')));
        if (ip != texte.end()) { name = ip->second + " (" + figur.substr(figur.find('_') + 1) + ")"; variante = "Meshes"; }
    }
    if (name.empty()) name = it != texte.end() ? it->second : figur;
    // Variante: "Naruto_Next" -> "(Next)"; "_Default", "Meshes", "Default" ohne Zusatz
    std::string zusatz = variante;
    if (zusatz.rfind(figur + "_", 0) == 0) zusatz = zusatz.substr(figur.size() + 1);
    if (zusatz != "Default" && zusatz != "Meshes" && !zusatz.empty() && zusatz != figur) {
        auto iv = texte.find("Character." + variante);
        if (iv != texte.end() && iv->second != name) name = iv->second;
        else name += " (" + zusatz + ")";
    }
    return name;
}

bool Katalog::Baue(Game& g, const std::wstring& cacheDatei, std::string& err, const std::function<void(size_t, size_t)>& fortschritt) {
    meshes.clear(); anims.clear(); figuren.clear();
    const std::string fp = Fingerabdruck(g);
    if (!cacheDatei.empty() && LiesCache(cacheDatei, fp, *this)) return true;
    meshes.clear(); anims.clear(); figuren.clear();

    std::vector<const Archive::FileRef*> liste;
    for (auto& fr : g.Ar().AllFiles()) {
        const size_t l = fr.path.size();
        if (l < 7 || _stricmp(fr.path.c_str() + l - 7, ".uasset") != 0) continue;
        if (_strnicmp(fr.path.c_str(), "NARUTO/Content/L10N/", 20) == 0) continue;    // Sprachfassungen
        liste.push_back(&fr);
    }
    struct Ergebnis { char art = 0; std::string skeleton; int frames = 0; float laenge = 0; bool additiv = false; };
    std::vector<Ergebnis> erg(liste.size());
    std::atomic<size_t> naechster{ 0 }, fertig{ 0 };
    auto arbeit = [&]() {
        for (;;) {
            const size_t i = naechster++;
            if (i >= liste.size()) break;
            Package kopf; std::string e;
            // Erst nur der Kopf (.uasset): ist es ueberhaupt ein Mesh/Clip?
            if (g.LoadHeader(liste[i]->path, kopf, e)) {
                const int me = kopf.FindExport("SkeletalMesh");
                const int ae = me < 0 ? kopf.FindExport("AnimSequence") : -1;
                if (me >= 0) {
                    erg[i].art = 'M';
                    // Skelett aus den Importen (ohne die .uexp zu lesen)
                    for (size_t k = 0; k < kopf.imports.size(); k++)
                        if (kopf.imports[k].className == "Skeleton") {
                            std::string pn, on, cn;
                            if (g.ResolveImport(kopf, (int)k, pn, on, cn)) erg[i].skeleton = pn;
                            break;
                        }
                } else if (ae >= 0) {
                    Package pk;
                    AnimClip a;
                    if (g.LoadPackage(liste[i]->path, pk, e) && ReadAnimInfo(g, pk, ae, a)) {
                        erg[i].art = 'A';
                        erg[i].skeleton = a.skeletonPath;
                        erg[i].frames = a.length > 0 ? (int)(a.length * 30.f + 1.5f) : a.numFrames;
                        erg[i].laenge = a.length;
                        erg[i].additiv = !a.additive.empty();
                    }
                }
            }
            const size_t f = ++fertig;
            if (fortschritt && (f % 2000 == 0 || f == liste.size())) fortschritt(f, liste.size());
        }
    };
    const unsigned n = std::max(2u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<std::thread> th;
    for (unsigned t = 0; t < n; t++) th.emplace_back(arbeit);
    for (auto& t : th) t.join();

    for (size_t i = 0; i < liste.size(); i++) {
        const Ergebnis& e = erg[i];
        if (e.art == 'M') {
            MeshEintrag m; m.file = liste[i]->path; m.skeleton = e.skeleton;
            m.name = Blatt(m.file); m.gruppe = Gruppe(m.file); m.kategorie = KategorieVon(m.file);
            meshes.push_back(std::move(m));
        } else if (e.art == 'A') {
            AnimEintrag a; a.file = liste[i]->path; a.skeleton = e.skeleton; a.frames = e.frames; a.laenge = e.laenge; a.additiv = e.additiv;
            a.name = Blatt(a.file); a.gruppe = Gruppe(a.file);
            anims.push_back(std::move(a));
        }
    }
    auto nachPfad = [](const auto& a, const auto& b) { return _stricmp(a.file.c_str(), b.file.c_str()) < 0; };
    std::sort(meshes.begin(), meshes.end(), nachPfad);
    std::sort(anims.begin(), anims.end(), nachPfad);
    if (meshes.empty()) { err = "keine SkeletalMeshes gefunden"; return false; }

    // Figuren: je Figurenordner Characters/<Original|NPC|Animal>/<Figur>/<Variante> das SK_CHR_-Mesh
    // mit dem kuerzesten Namen (die anderen sind Zusatzteile, Ninjutsu-Effekte, Vorschauen)
    const std::map<std::string, std::string> texte = LadeTexte(g);
    std::map<std::string, size_t> jeOrdner;
    for (size_t i = 0; i < meshes.size(); i++) {
        const MeshEintrag& m = meshes[i];
        const std::string l = Lower(m.file);
        if (l.rfind("naruto/content/characters/original/", 0) != 0 && l.rfind("naruto/content/characters/npc/", 0) != 0 &&
            l.rfind("naruto/content/characters/animal/", 0) != 0)
            continue;
        if (m.name.rfind("SK_CHR_", 0) != 0 || Lower(m.name).find("preview") != std::string::npos) continue;
        // Ordner bis zur Variante (6 Teile)
        size_t p = 0;
        for (int k = 0; k < 6 && p != std::string::npos; k++) p = m.file.find('/', p + 1);
        if (p == std::string::npos) continue;
        const std::string ordner = m.file.substr(0, p);
        auto it = jeOrdner.find(ordner);
        if (it == jeOrdner.end() || m.name.size() < meshes[it->second].name.size()) jeOrdner[ordner] = i;
    }
    for (auto& kv : jeOrdner) {
        const MeshEintrag& m = meshes[kv.second];
        FigurEintrag f;
        f.file = m.file;
        f.name = FigurName(m.file, texte);
        f.gruppe = m.gruppe;
        f.kategorie = KategorieVon(m.file);
        FigurTeil t; t.mesh = m.file;
        f.teile.push_back(t);
        figuren.push_back(std::move(f));
    }
    // Gleiche Anzeigenamen (Bee und D22 heissen beide "Killer Bee"): Figurenordner anhaengen
    {
        std::map<std::string, int> anzahl;
        for (auto& f : figuren) anzahl[f.name]++;
        for (auto& f : figuren)
            if (anzahl[f.name] > 1) {
                const size_t a = f.file.find("/Characters/");
                size_t b = a == std::string::npos ? a : f.file.find('/', a + 12);
                b = b == std::string::npos ? b : f.file.find('/', b + 1);
                const size_t c = b == std::string::npos ? b : f.file.find('/', b + 1);
                if (c != std::string::npos) f.name += " [" + f.file.substr(b + 1, c - b - 1) + "]";
            }
    }
    // Avatare aus dem Charaktereditor: Grundkopf + Standardhaare, -jacke, -hose
    struct Vorgabe { const char* name; const char* haar; const char* jacke; const char* hose; };
    const Vorgabe vorgaben[] = { { "Avatar (male)", "ID_CustomHair_M_01", "ID_CustomJacket_M_00", "ID_CustomPants_M_00" },
                                 { "Avatar (female)", "ID_CustomHair_F_00", "ID_CustomJacket_F_00", "ID_CustomPants_F_00" } };
    for (const Vorgabe& v : vorgaben) {
        FigurEintrag f;
        f.name = v.name;
        f.kategorie = K_Avatar;
        FigurTeil t;
        if (AvatarTeil(g, "DT_CustomBase", "ID_CustomBase_U_00", "Face", t)) f.teile.push_back(t);
        t = FigurTeil();
        if (AvatarTeil(g, "DT_CustomJacket", v.jacke, "BodyUpper", t)) f.teile.push_back(t);
        t = FigurTeil();
        if (AvatarTeil(g, "DT_CustomPants", v.hose, "BodyLower", t)) f.teile.push_back(t);
        t = FigurTeil();
        if (AvatarTeil(g, "DT_CustomHair", v.haar, "Hair", t)) f.teile.push_back(t);
        if (f.teile.size() < 2) continue;
        f.file = PackageNameToFile(f.teile.front().mesh);
        f.gruppe = "Characters/Custom";
        figuren.push_back(std::move(f));
    }
    // Helden zuerst, dann Avatare, dann NPC/Tiere; jeweils nach Name
    std::sort(figuren.begin(), figuren.end(), [](const FigurEintrag& a, const FigurEintrag& b) {
        if (a.kategorie != b.kategorie) return a.kategorie < b.kategorie;
        return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    if (!cacheDatei.empty()) SchreibeCache(cacheDatei, fp, *this);
    return true;
}

} // namespace ns

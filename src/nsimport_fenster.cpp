// ============================================================
//  Shinobi Striker Import - das Figurenfenster (modal)
//
//  Aufbau wie beim SWBF2/TFU2 Import: Spielordner oben, Statuszeile,
//  Reiter nach Art, Suche, selbst gezeichnete zweizeilige Liste,
//  Detailzeile, Fusszeile mit den Knoepfen. Neu: Mehrfachauswahl -
//  Koerper, Kopf und Haare (Strg+Klick) werden EINE Figur auf einem
//  gemeinsamen Skelett.
// ============================================================
#include "nsimport.h"
#include "nsimport_res.h"
#include "ns_ui.h"
#include "ns_tex.h"

#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <string>
#include <vector>

extern HINSTANCE hInstance;

namespace nsi {

namespace {

// Reiter 0 = fertige Figuren (Helden, NPC, Tiere), 1..5 = einzelne Meshes nach Art
constexpr int kKategorien = 6;
const wchar_t* const kReiter[kKategorien] = { L"Characters", L"Hero meshes", L"Avatar parts", L"Weapons + items", L"Other", L"All meshes" };
constexpr int kFiguren = 0;
// Reiter 1..4 -> ns::Kategorie der Meshes, 5 = alle
int MeshKategorie(int reiter) { return reiter - 1; }
bool IstFigurReiter(int reiter) { return reiter == kFiguren; }
bool FigurSichtbar(const ns::FigurEintrag&) { return true; }

enum : unsigned { kL = 1, kO = 2, kR = 4, kU = 8 };
struct Anker {
    int id;
    RECT start;
    unsigned flags;
};

struct Fenster {
    HWND h = nullptr;
    nsui::Palette pal;
    HFONT fontNormal = nullptr, fontFett = nullptr;
    int zeilenHoehe = 16;
    ns::Game* game = nullptr;
    const ns::Katalog* katalog = nullptr;
    std::wstring ordner;
    std::vector<size_t> sichtbar;
    size_t anzahl[kKategorien] = {};
    int kategorie = 0;
    bool texturen = true;
    bool importiert = false;
    std::wstring status;
    bool statusFehler = false;
    std::vector<Anker> anker;
    SIZE startClient{ 0, 0 }, startFenster{ 0, 0 };
};

Fenster* Zustand(HWND h) { return reinterpret_cast<Fenster*>(GetWindowLongPtrW(h, GWLP_USERDATA)); }

void Status(Fenster& f, const std::wstring& t, bool fehler = false) {
    f.status = t;
    f.statusFehler = fehler;
    InvalidateRect(GetDlgItem(f.h, IDC_STATUS), nullptr, FALSE);
    UpdateWindow(GetDlgItem(f.h, IDC_STATUS));
}

bool WaehleOrdner(HWND besitzer, std::wstring& ordner) {
#ifndef __IFileOpenDialog_INTERFACE_DEFINED__
    BROWSEINFOW bi = {};
    bi.hwndOwner = besitzer;
    bi.lpszTitle = L"Naruto to Boruto: Shinobi Striker - the game folder (Naruto To Boruto, with NARUTO inside)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderW(&bi);
    if (id == nullptr) return false;
    wchar_t pfad[MAX_PATH] = {};
    const bool ok = SHGetPathFromIDListW(id, pfad) != FALSE;
    CoTaskMemFree(id);
    if (ok) ordner = pfad;
    return ok;
#else
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    DWORD opt = 0;
    dlg->GetOptions(&opt);
    dlg->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"Naruto to Boruto: Shinobi Striker - the game folder (Naruto To Boruto, with NARUTO inside)");
    bool ok = false;
    if (SUCCEEDED(dlg->Show(besitzer))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR pfad = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pfad))) {
                ordner = pfad;
                CoTaskMemFree(pfad);
                ok = true;
            }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
#endif
}

// ------------------------------------------------------------
//  Masse und Anordnung (mitwachsendes Fenster)
// ------------------------------------------------------------
int EintragsHoehe(HWND h) {
    const int z = nsui::ZeilenHoehe(h);
    return 2 * z + std::max(4, z / 3) + 2;
}

void MerkeAnker(Fenster& f) {
    struct { int id; unsigned fl; } const liste[] = {
        { IDC_ORDNER_LABEL, kL | kO }, { IDC_ORDNER, kL | kO | kR }, { IDC_DURCHSUCHEN, kO | kR }, { IDC_STATUS, kL | kO | kR },
        { IDC_TAB0, kL | kO }, { IDC_TAB0 + 1, kL | kO }, { IDC_TAB0 + 2, kL | kO }, { IDC_TAB0 + 3, kL | kO }, { IDC_TAB0 + 4, kL | kO }, { IDC_TAB0 + 5, kL | kO },
        { IDC_TEXTUREN, kO | kR }, { IDC_SUCHE_LABEL, kL | kO }, { IDC_SUCHE, kL | kO | kR }, { IDC_ANZAHL, kO | kR },
        { IDC_LISTE, kL | kO | kR | kU }, { IDC_DETAIL, kL | kR | kU }, { IDC_FUSS, kL | kR | kU },
        { IDC_ANIMFENSTER, kR | kU }, { IDOK, kR | kU }, { IDCANCEL, kR | kU },
    };
    RECT c{};
    GetClientRect(f.h, &c);
    f.startClient = { c.right - c.left, c.bottom - c.top };
    RECT w{};
    GetWindowRect(f.h, &w);
    f.startFenster = { w.right - w.left, w.bottom - w.top };
    for (const auto& e : liste) {
        HWND h = GetDlgItem(f.h, e.id);
        if (h == nullptr) continue;
        RECT r{};
        GetWindowRect(h, &r);
        MapWindowPoints(nullptr, f.h, reinterpret_cast<POINT*>(&r), 2);
        f.anker.push_back({ e.id, r, e.fl });
    }
}

void Ordne(Fenster& f, int cx, int cy) {
    if (f.anker.empty()) return;
    const int dx = cx - f.startClient.cx, dy = cy - f.startClient.cy;
    HDWP h = BeginDeferWindowPos(static_cast<int>(f.anker.size()));
    for (const Anker& a : f.anker) {
        RECT n = a.start;
        if (a.flags & kR) { if (a.flags & kL) n.right += dx; else { n.left += dx; n.right += dx; } }
        if (a.flags & kU) { if (a.flags & kO) n.bottom += dy; else { n.top += dy; n.bottom += dy; } }
        HWND c = GetDlgItem(f.h, a.id);
        if (h != nullptr && c != nullptr)
            h = DeferWindowPos(h, c, nullptr, n.left, n.top, n.right - n.left, n.bottom - n.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (h != nullptr) EndDeferWindowPos(h);
    InvalidateRect(f.h, nullptr, TRUE);
}

std::string Blatt(const std::string& p) {
    const size_t s = p.find_last_of('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

// ------------------------------------------------------------
//  Zeichnen
// ------------------------------------------------------------
void ZeichneEintrag(const Fenster& f, const DRAWITEMSTRUCT& d) {
    HDC dc = d.hDC;
    const RECT r = d.rcItem;
    const bool sel = (d.itemState & ODS_SELECTED) != 0;
    HBRUSH hg = CreateSolidBrush(sel ? f.pal.auswahl : f.pal.feld);
    FillRect(dc, &r, hg);
    DeleteObject(hg);
    if (f.katalog == nullptr || d.itemID == static_cast<UINT>(-1) || d.itemID >= f.sichtbar.size()) return;
    const int rand = std::max(4, f.zeilenHoehe / 3);
    const RECT z1 = { r.left + rand, r.top + rand / 2, r.right - rand, r.top + rand / 2 + f.zeilenHoehe };
    const RECT z2 = { z1.left, z1.bottom, z1.right, z1.bottom + f.zeilenHoehe };
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ altF = SelectObject(dc, f.fontNormal);

    // Figur: Name, rechts die Teilezahl, darunter Ordner und Teile. Mesh: Name, Skelett, Ordner.
    std::wstring meta, name, unter;
    if (IstFigurReiter(f.kategorie)) {
        const ns::FigurEintrag& fe = f.katalog->figuren[f.sichtbar[d.itemID]];
        name = Breit(fe.name);
        meta = std::to_wstring(fe.teile.size()) + (fe.teile.size() == 1 ? L" part" : L" parts");
        unter = Breit(fe.gruppe) + L"   ·   ";
        for (size_t i = 0; i < fe.teile.size(); ++i) unter += (i ? L", " : L"") + Breit(Blatt(fe.teile[i].mesh));
    } else {
        const ns::MeshEintrag& m = f.katalog->meshes[f.sichtbar[d.itemID]];
        name = Breit(m.name);
        meta = Breit(Blatt(m.skeleton));
        if (meta.empty()) meta = L"no skeleton";
        unter = Breit(m.gruppe);
    }
    RECT mess = z1;
    DrawTextW(dc, meta.c_str(), -1, &mess, DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
    const int mbreite = std::min(static_cast<int>(mess.right - mess.left), static_cast<int>(z1.right - z1.left) / 2);
    RECT mz = { z1.right - mbreite, z1.top, z1.right, z1.bottom };
    SetTextColor(dc, sel ? f.pal.auswahlDim : f.pal.feldDim);
    DrawTextW(dc, meta.c_str(), -1, &mz, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

    RECT nz = z1;
    nz.right = mz.left - 2 * rand;
    SelectObject(dc, f.fontFett != nullptr ? f.fontFett : f.fontNormal);
    SetTextColor(dc, sel ? f.pal.auswahlText : f.pal.feldText);
    DrawTextW(dc, name.c_str(), -1, &nz, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    SelectObject(dc, f.fontNormal);
    RECT uz = z2;
    SetTextColor(dc, sel ? f.pal.auswahlDim : f.pal.feldDim);
    DrawTextW(dc, unter.c_str(), -1, &uz, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, altF);
    if (!sel) nsui::Trennlinie(f.pal, dc, r, rand);
    if ((d.itemState & ODS_FOCUS) != 0 && (d.itemState & ODS_NOFOCUSRECT) == 0) {
        RECT fr = r;
        DrawFocusRect(dc, &fr);
    }
}

// ------------------------------------------------------------
//  Liste und Anzeige
// ------------------------------------------------------------
std::vector<size_t> Gewaehlt(const Fenster& f) {
    std::vector<size_t> aus;
    HWND lb = GetDlgItem(f.h, IDC_LISTE);
    const LRESULT n = SendMessageW(lb, LB_GETSELCOUNT, 0, 0);
    if (n <= 0) return aus;
    std::vector<int> idx(static_cast<size_t>(n));
    const LRESULT g = SendMessageW(lb, LB_GETSELITEMS, static_cast<WPARAM>(n), reinterpret_cast<LPARAM>(idx.data()));
    for (LRESULT i = 0; i < g; ++i)
        if (idx[static_cast<size_t>(i)] >= 0 && static_cast<size_t>(idx[static_cast<size_t>(i)]) < f.sichtbar.size())
            aus.push_back(f.sichtbar[static_cast<size_t>(idx[static_cast<size_t>(i)])]);
    return aus;
}

void Bedienbarkeit(Fenster& f) {
    const bool liste = f.katalog != nullptr;
    for (int k = 0; k < kKategorien; ++k) EnableWindow(GetDlgItem(f.h, IDC_TAB0 + k), liste);
    EnableWindow(GetDlgItem(f.h, IDC_SUCHE), liste);
    EnableWindow(GetDlgItem(f.h, IDC_LISTE), liste);
    EnableWindow(GetDlgItem(f.h, IDOK), liste && !Gewaehlt(f).empty());
    EnableWindow(GetDlgItem(f.h, IDC_ANIMFENSTER), liste);
    InvalidateRect(GetDlgItem(f.h, IDOK), nullptr, FALSE);
}

void ZeigeDetail(Fenster& f) {
    const std::vector<size_t> g = Gewaehlt(f);
    std::wstring t;
    if (IstFigurReiter(f.kategorie)) {
        if (g.size() == 1) {
            const ns::FigurEintrag& fe = f.katalog->figuren[g[0]];
            t = Breit(fe.file) + L"  ->  ";
            for (size_t i = 0; i < fe.teile.size(); ++i) t += (i ? L" + " : L"") + Breit(Blatt(fe.teile[i].mesh));
        } else if (g.size() > 1) t = std::to_wstring(g.size()) + L" characters - imported side by side.";
        else if (!f.sichtbar.empty()) t = L"Every playable master, NPC and summon as one character. Double-click to import.";
        else if (f.katalog != nullptr) t = L"Nothing matches - try another tab or a shorter search.";
    } else if (g.size() == 1) t = Breit(f.katalog->meshes[g[0]].file);
    else if (g.size() > 1) {
        t = std::to_wstring(g.size()) + L" parts -> one character:  ";
        for (size_t i = 0; i < g.size(); ++i) t += (i ? L" + " : L"") + Breit(f.katalog->meshes[g[i]].name);
    } else if (!f.sichtbar.empty()) t = L"Single meshes. Double-click to import; Ctrl+click several parts (avatar face, top, pants ...) to combine them on one skeleton.";
    else if (f.katalog != nullptr) t = L"Nothing matches - try another tab or a shorter search.";
    SetDlgItemTextW(f.h, IDC_DETAIL, t.c_str());
}

std::vector<std::string> Suchwoerter(HWND h, int id) {
    wchar_t puffer[512] = {};
    GetDlgItemTextW(h, id, puffer, 512);
    const std::string s = ns::Lower(Utf8(puffer));
    std::vector<std::string> woerter;
    std::string wort;
    for (char c : s) {
        if (c == ' ' || c == '\t') { if (!wort.empty()) { woerter.push_back(wort); wort.clear(); } }
        else wort += c;
    }
    if (!wort.empty()) woerter.push_back(wort);
    return woerter;
}

void FuelleListe(Fenster& f) {
    HWND lb = GetDlgItem(f.h, IDC_LISTE);
    const std::vector<std::string> woerter = Suchwoerter(f.h, IDC_SUCHE);
    f.sichtbar.clear();
    std::fill(std::begin(f.anzahl), std::end(f.anzahl), size_t(0));
    auto passt = [&woerter](const std::string& text) {
        const std::string k = ns::Lower(text);
        for (const std::string& w : woerter) if (k.find(w) == std::string::npos) return false;
        return true;
    };
    if (f.katalog != nullptr) {
        for (size_t i = 0; i < f.katalog->figuren.size(); ++i) {
            const ns::FigurEintrag& fe = f.katalog->figuren[i];
            if (!FigurSichtbar(fe)) continue;
            ++f.anzahl[kFiguren];
            if (!IstFigurReiter(f.kategorie)) continue;
            std::string text = fe.name + " " + fe.file;
            for (const auto& t : fe.teile) text += " " + t.mesh;
            if (passt(text)) f.sichtbar.push_back(i);
        }
        for (size_t i = 0; i < f.katalog->meshes.size(); ++i) {
            const ns::MeshEintrag& m = f.katalog->meshes[i];
            ++f.anzahl[1 + m.kategorie];
            ++f.anzahl[kKategorien - 1];
            if (IstFigurReiter(f.kategorie)) continue;
            if (f.kategorie < kKategorien - 1 && m.kategorie != MeshKategorie(f.kategorie)) continue;
            if (passt(m.file)) f.sichtbar.push_back(i);
        }
    }
    for (int k = 0; k < kKategorien; ++k) {
        std::wstring t = kReiter[k];
        if (f.katalog != nullptr) t += L"  " + std::to_wstring(f.anzahl[k]);
        SetDlgItemTextW(f.h, IDC_TAB0 + k, t.c_str());
        InvalidateRect(GetDlgItem(f.h, IDC_TAB0 + k), nullptr, FALSE);
    }
    SendMessageW(lb, WM_SETREDRAW, FALSE, 0);
    SendMessageW(lb, LB_SETCOUNT, static_cast<WPARAM>(f.sichtbar.size()), 0);
    SendMessageW(lb, LB_SETSEL, FALSE, -1);
    SendMessageW(lb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lb, nullptr, TRUE);
    std::wstring z;
    if (f.katalog != nullptr) z = std::to_wstring(f.sichtbar.size()) + L" of " + std::to_wstring(f.anzahl[f.kategorie]);
    SetDlgItemTextW(f.h, IDC_ANZAHL, z.c_str());
    ZeigeDetail(f);
    Bedienbarkeit(f);
}

void Lade(Fenster& f) {
    SetDlgItemTextW(f.h, IDC_ORDNER, f.ordner.empty() ? L"(not found - choose it with Browse)" : f.ordner.c_str());
    if (!IstSpielordner(f.ordner)) {
        f.game = nullptr;
        f.katalog = nullptr;
        Status(f, L"Choose the game folder (Steam\\steamapps\\common\\Naruto To Boruto).", !f.ordner.empty());
        FuelleListe(f);
        return;
    }
    Status(f, L"Reading the game archives… (the first start builds an index, about 10-20 s)");
    HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    std::string fehler;
    const bool ok = Spiel(f.ordner, f.game, f.katalog, fehler);
    SetCursor(alt);
    if (!ok) {
        f.game = nullptr;
        f.katalog = nullptr;
        Status(f, L"Could not read the game: " + Breit(fehler), true);
    } else {
        size_t figuren = 0;
        for (const auto& fe : f.katalog->figuren) if (FigurSichtbar(fe)) ++figuren;
        Status(f, std::to_wstring(figuren) + L" characters, " + std::to_wstring(f.katalog->meshes.size()) + L" skeletal meshes, " +
                      std::to_wstring(f.katalog->anims.size()) + L" animations in " + std::to_wstring(f.game->Ar().Paks().size()) +
                      L" .pak archives.");
    }
    FuelleListe(f);
}

void Importiere(Fenster& f) {
    const std::vector<size_t> g = Gewaehlt(f);
    if (g.empty() || f.katalog == nullptr || f.game == nullptr) return;
    ImportOptionen o;
    o.texturen = f.texturen;
    if (IstFigurReiter(f.kategorie)) {
        // Jede Figur fuer sich, nebeneinander (1,5 m Abstand)
        size_t gut = 0;
        std::wstring letzter;
        for (size_t n = 0; n < g.size(); ++n) {
            const ns::FigurEintrag& fe = f.katalog->figuren[g[n]];
            Status(f, L"Importing " + Breit(fe.name) + L" (" + std::to_wstring(n + 1) + L"/" + std::to_wstring(g.size()) + L", " +
                          std::to_wstring(fe.teile.size()) + L" parts)…");
            HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
            o.versatzX = 150.0f * static_cast<float>(n);
            std::wstring bericht;
            const bool ok = ImportiereFigur(*f.game, fe.teile, fe.name, o, bericht);
            SetCursor(alt);
            if (ok) { ++gut; f.importiert = true; }
            letzter = (ok ? L"Imported " : L"Import failed: ") + bericht;
        }
        Status(f, g.size() == 1 ? letzter : std::to_wstring(gut) + L" of " + std::to_wstring(g.size()) + L" characters imported. " + letzter, gut == 0);
        return;
    }
    std::vector<std::string> pfade;
    for (size_t i : g) pfade.push_back(f.katalog->meshes[i].file);
    Status(f, L"Importing " + std::to_wstring(pfade.size()) + L" part(s) (skeleton, meshes, skin" + (f.texturen ? L", textures" : L"") + L")…");
    HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    std::wstring bericht;
    const bool ok = ImportiereFigur(*f.game, pfade, o, bericht);
    SetCursor(alt);
    if (ok) f.importiert = true;
    Status(f, (ok ? L"Imported " : L"Import failed: ") + bericht, !ok);
}

void Einrichten(Fenster& f) {
    f.pal.Baue(&ThemeFarbe);
    f.fontNormal = reinterpret_cast<HFONT>(SendMessageW(f.h, WM_GETFONT, 0, 0));
    LOGFONTW lf{};
    if (f.fontNormal != nullptr && GetObjectW(f.fontNormal, sizeof(lf), &lf) == sizeof(lf)) {
        lf.lfWeight = FW_SEMIBOLD;
        f.fontFett = CreateFontIndirectW(&lf);
    }
    f.zeilenHoehe = nsui::ZeilenHoehe(f.h);
    SendDlgItemMessageW(f.h, IDC_LISTE, LB_SETITEMHEIGHT, 0, EintragsHoehe(f.h));
    nsui::DunkleTitelleiste(f.h, f.pal.dunkel);
    if (f.pal.dunkel) SetWindowTheme(GetDlgItem(f.h, IDC_LISTE), L"DarkMode_Explorer", nullptr);
    SetWindowTextW(f.h, (std::wstring(L"Shinobi Striker Import ") + NSIMPORT_VERSION_STR).c_str());
    {
        const int innen = std::max(3, f.zeilenHoehe / 4);
        SendDlgItemMessageW(f.h, IDC_SUCHE, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(innen, innen));
    }
    SendDlgItemMessageW(f.h, IDC_SUCHE, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Filter by name or folder, e.g.  naruto  or  D45"));
    SetDlgItemTextW(f.h, IDC_FUSS, (std::wstring(L"Shinobi Striker Import ") + NSIMPORT_VERSION_STR).c_str());
    MerkeAnker(f);
    f.kategorie = std::clamp(_wtoi(LiesEinstellung(L"Kategorie").c_str()), 0, kKategorien - 1);
    f.texturen = LiesEinstellung(L"Texturen") != L"0";
    f.ordner = LiesEinstellung(L"Spielordner");
    if (!IstSpielordner(f.ordner)) {
        const std::wstring gefunden = SucheSpiel();
        if (!gefunden.empty()) f.ordner = gefunden;
    }
    Lade(f);
    SetFocus(GetDlgItem(f.h, f.katalog != nullptr ? IDC_SUCHE : IDC_DURCHSUCHEN));
    {
        wchar_t s[256] = {};
        if (GetEnvironmentVariableW(L"NSIMPORT_TESTSUCHE", s, 256) > 0) SetDlgItemTextW(f.h, IDC_SUCHE, s);
    }
    TestFotoStarten(f.h);
}

INT_PTR Verarbeite(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) {
        Fenster* neu = reinterpret_cast<Fenster*>(lp);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(neu));
        neu->h = h;
        Einrichten(*neu);
        return FALSE;
    }
    if (msg == WM_MEASUREITEM) {
        MEASUREITEMSTRUCT* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if (mi != nullptr && mi->CtlID == IDC_LISTE) { mi->itemHeight = static_cast<UINT>(EintragsHoehe(h)); return TRUE; }
        return FALSE;
    }
    Fenster* f = Zustand(h);
    if (f == nullptr) return FALSE;
    switch (msg) {
    case WM_CTLCOLORDLG:
        return reinterpret_cast<INT_PTR>(f->pal.pinselGrund);
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wp);
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
        const bool leise = id == IDC_ORDNER_LABEL || id == IDC_SUCHE_LABEL || id == IDC_DETAIL || id == IDC_FUSS || id == IDC_ANZAHL;
        SetBkColor(dc, f->pal.grund);
        SetTextColor(dc, leise ? f->pal.dim : f->pal.text);
        return reinterpret_cast<INT_PTR>(f->pal.pinselGrund);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetBkColor(dc, f->pal.feld);
        SetTextColor(dc, f->pal.feldText);
        return reinterpret_cast<INT_PTR>(f->pal.pinselFeld);
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* d = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        if (d == nullptr) return FALSE;
        if (d->CtlType == ODT_LISTBOX) ZeichneEintrag(*f, *d);
        else if (d->CtlType == ODT_STATIC) nsui::ZeichneStatus(f->pal, f->fontNormal, *d, f->status, f->statusFehler);
        else if (d->CtlType == ODT_BUTTON) {
            const int id = static_cast<int>(d->CtlID);
            const bool betont = (id >= IDC_TAB0 && id < IDC_TAB0 + kKategorien && id - IDC_TAB0 == f->kategorie) ||
                                (id == IDC_TEXTUREN && f->texturen) || id == IDOK;
            nsui::ZeichneKnopf(f->pal, f->fontNormal, *d, betont);
        }
        return TRUE;
    }
    case WM_SIZE:
        Ordne(*f, LOWORD(lp), HIWORD(lp));
        return TRUE;
    case WM_TIMER:
        if (wp == kTestFotoTimer) {
            // Erst die Liste waehlen (zeigt die Auswahlfarbe), dann fotografieren
            SendDlgItemMessageW(h, IDC_LISTE, LB_SETSEL, TRUE, 0);
            ZeigeDetail(*f);
            Bedienbarkeit(*f);
            UpdateWindow(h);
            TestFotoMachen(h, L"figuren");
            EndDialog(h, 0);
        }
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(h, &ps);
        for (int id : { IDC_SUCHE, IDC_LISTE }) nsui::Kante(h, dc, id, f->pal.linie);
        EndPaint(h, &ps);
        return TRUE;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lp);
        if (mm != nullptr && f->startFenster.cx > 0) { mm->ptMinTrackSize.x = f->startFenster.cx; mm->ptMinTrackSize.y = f->startFenster.cy; }
        return TRUE;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wp), code = HIWORD(wp);
        if (id >= IDC_TAB0 && id < IDC_TAB0 + kKategorien) {
            if (code == BN_CLICKED) {
                f->kategorie = id - IDC_TAB0;
                SchreibeEinstellung(L"Kategorie", std::to_wstring(f->kategorie));
                FuelleListe(*f);
            }
            return TRUE;
        }
        switch (id) {
        case IDC_DURCHSUCHEN:
            if (code == BN_CLICKED) {
                std::wstring o;
                if (WaehleOrdner(h, o)) {
                    // Auch ein Unterordner des Spiels (NARUTO, Paks ...) ist recht.
                    for (int i = 0; i < 4 && !IstSpielordner(o); ++i) {
                        const size_t p = o.find_last_of(L"\\/");
                        if (p == std::wstring::npos) break;
                        o = o.substr(0, p);
                    }
                    f->ordner = o;
                    Lade(*f);
                }
            }
            return TRUE;
        case IDC_TEXTUREN:
            if (code == BN_CLICKED) {
                f->texturen = !f->texturen;
                SchreibeEinstellung(L"Texturen", f->texturen ? L"1" : L"0");
                InvalidateRect(GetDlgItem(h, IDC_TEXTUREN), nullptr, FALSE);
            }
            return TRUE;
        case IDC_SUCHE:
            if (code == EN_CHANGE) FuelleListe(*f);
            return TRUE;
        case IDC_LISTE:
            if (code == LBN_SELCHANGE) { ZeigeDetail(*f); Bedienbarkeit(*f); }
            else if (code == LBN_DBLCLK && IsWindowEnabled(GetDlgItem(h, IDOK))) Importiere(*f);
            return TRUE;
        case IDOK:
            // Eingabetaste im Suchfeld: erst den ersten Treffer waehlen, dann importieren.
            if (Gewaehlt(*f).empty() && !f->sichtbar.empty()) {
                SendDlgItemMessageW(h, IDC_LISTE, LB_SETSEL, TRUE, 0);
                ZeigeDetail(*f);
                Bedienbarkeit(*f);
                SetFocus(GetDlgItem(h, IDC_LISTE));
            } else if (IsWindowEnabled(GetDlgItem(h, IDOK))) {
                Importiere(*f);
            }
            return TRUE;
        case IDC_ANIMFENSTER:
            if (code == BN_CLICKED) OeffneAnimFenster(h);
            return TRUE;
        case IDCANCEL:
            EndDialog(h, f->importiert ? 1 : 0);
            return TRUE;
        default:
            break;
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(h, f->importiert ? 1 : 0);
        return TRUE;
    case WM_DESTROY:
        f->pal.Frei();
        if (f->fontFett != nullptr) { DeleteObject(f->fontFett); f->fontFett = nullptr; }
        return FALSE;
    default:
        break;
    }
    return FALSE;
}

// Keine Ausnahme darf in Windows' Nachrichtenschleife und damit in Max gelangen.
INT_PTR CALLBACK DlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    try {
        return Verarbeite(h, msg, wp, lp);
    } catch (const std::exception& x) {
        if (Fenster* f = Zustand(h)) Status(*f, L"Internal error: " + Breit(x.what()), true);
    } catch (...) {
        if (Fenster* f = Zustand(h)) Status(*f, L"Internal error.", true);
    }
    return FALSE;
}

} // namespace

void TestFotoStarten(HWND h) {
    wchar_t p[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"NSIMPORT_TESTFOTO", p, MAX_PATH) > 0) SetTimer(h, kTestFotoTimer, 4000, nullptr);
}

bool TestFotoMachen(HWND h, const wchar_t* name) {
    KillTimer(h, kTestFotoTimer);
    wchar_t p[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"NSIMPORT_TESTFOTO", p, MAX_PATH) == 0) return false;
    RECT r{};
    GetWindowRect(h, &r);
    const int w = r.right - r.left, hh = r.bottom - r.top;
    if (w <= 0 || hh <= 0) return false;
    HDC sdc = GetDC(nullptr);
    HDC mdc = CreateCompatibleDC(sdc);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -hh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ alt = SelectObject(mdc, bmp);
    // Nur das Fenster selbst - PW_RENDERFULLCONTENT (2) auch fuer DWM-Inhalte
    const BOOL ok = PrintWindow(h, mdc, 2);
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * hh * 3);
    const uint8_t* q = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < static_cast<size_t>(w) * hh; ++i) { rgb[i * 3] = q[i * 4 + 2]; rgb[i * 3 + 1] = q[i * 4 + 1]; rgb[i * 3 + 2] = q[i * 4]; }
    SelectObject(mdc, alt);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(nullptr, sdc);
    if (!ok) return false;
    std::wstring datei = std::wstring(p) + L"_" + name + L".png";
    return ns::SchreibePng(datei, rgb.data(), w, hh, 3);
}

int OeffneFenster() {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Fenster f;
    const INT_PTR r = DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_FIGUREN), GetCOREInterface()->GetMAXHWnd(), &DlgProc,
                                      reinterpret_cast<LPARAM>(&f));
    if (co == S_OK || co == S_FALSE) CoUninitialize();
    return r == -1 ? -1 : (r == 1 ? 1 : 0);
}

} // namespace nsi

// ============================================================
//  Shinobi Striker Import - das Animationsfenster (modal)
//
//  Aufbau wie beim SWBF2/TFU2 Import: oben Figur, Auswahl und Suche,
//  dann die Clipliste mit Kopfzeile (virtuell, LBS_NODATA),
//  Statuszeile, Sequenzen der Zeitleiste und unten Abstand, Notizspur,
//  Wurzelbewegung, "Load all to timeline", "Load", "Close".
//
//  Show:
//    Own clips      Clips aus den Ordnern der Figur selbst (mit passendem Skelett) -
//                   die DLC9/DLC10-Figuren teilen sich ein Skelett, Zwischensequenzen
//                   (ML_*) benutzen Heldenskelette
//    Own skeleton   alle Clips, deren Skelett eines der Figurenteile ist
//    All            ohne Filter
// ============================================================
#include "nsimport.h"
#include "nsimport_res.h"
#include "ns_ui.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

extern HINSTANCE hInstance;

namespace nsi {

namespace {

const wchar_t* const kZeige[] = { L"Own clips", L"Own skeleton", L"All" };

struct Fenster {
    HWND h = nullptr;
    nsui::Palette pal;
    HFONT fontNormal = nullptr, fontFett = nullptr;
    int zeilenHoehe = 16;
    ns::Game* game = nullptr;
    const ns::Katalog* katalog = nullptr;
    std::vector<SzenenFigur> figuren;
    int figurWahl = -1;
    int zeige = 0;
    std::vector<size_t> sichtbar;
    std::vector<Sequenz> sequenzen;
    bool notiz = true;
    bool wurzel = true;
    std::wstring status;
    bool statusFehler = false;
};

Fenster* Zustand(HWND h) { return reinterpret_cast<Fenster*>(GetWindowLongPtrW(h, GWLP_USERDATA)); }

void Status(Fenster& f, const std::wstring& t, bool fehler = false) {
    f.status = t;
    f.statusFehler = fehler;
    InvalidateRect(GetDlgItem(f.h, IDC_A_STATUS), nullptr, FALSE);
    UpdateWindow(GetDlgItem(f.h, IDC_A_STATUS));
}

std::vector<std::string> Suchwoerter(HWND h) {
    wchar_t puffer[512] = {};
    GetDlgItemTextW(h, IDC_A_SUCHE, puffer, 512);
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

const SzenenFigur* GewaehlteFigur(const Fenster& f) {
    return (f.figurWahl >= 0 && static_cast<size_t>(f.figurWahl) < f.figuren.size()) ? &f.figuren[static_cast<size_t>(f.figurWahl)] : nullptr;
}

// Pfad in Teile ab "characters/" (klein)
std::vector<std::string> CharTeile(const std::string& pfad) {
    std::vector<std::string> t;
    const std::string l = ns::Lower(pfad);
    const size_t c = l.find("characters/");
    if (c == std::string::npos) return t;
    size_t p = c;
    while (p <= l.size()) { size_t e = l.find('/', p); if (e == std::string::npos) e = l.size(); t.push_back(l.substr(p, e - p)); p = e + 1; }
    return t;
}

// Ordner, deren Clips zu einer Figur gehoeren (Praefixe ab "characters/"):
//   Held  /Game/Characters/Original/Naruto/...       -> characters/original/naruto/
//   DLC   /Game/Characters/Original/DLC9/D91/...     -> characters/original/dlc9/d91/ + .../dlc9/dlc9_skeleton/
//   Avatar /Game/Characters/Custom/...               -> characters/custom/
void EigeneOrdner(const std::string& meshPaket, std::set<std::string>& aus) {
    const std::vector<std::string> t = CharTeile(meshPaket);
    if (t.size() < 3) return;
    if (t[1] == "custom") { aus.insert("characters/custom/"); return; }
    if (t.size() < 4) return;
    std::string o = t[0] + "/" + t[1] + "/" + t[2] + "/";
    if (t[2].rfind("dlc", 0) == 0 && t.size() >= 5 && t[3] != "meshes") {
        aus.insert(o + t[2] + "_skeleton/");                 // gemeinsame Grundbewegungen des DLC-Pakets
        o += t[3] + "/";
    }
    aus.insert(o);
}


// ------------------------------------------------------------
//  Liste fuellen
// ------------------------------------------------------------
void Fuelle(Fenster& f) {
    HWND lb = GetDlgItem(f.h, IDC_A_LISTE);
    f.sichtbar.clear();
    std::wstring hinweis;
    if (f.katalog != nullptr) {
        const std::vector<std::string> woerter = Suchwoerter(f.h);
        const SzenenFigur* sf = GewaehlteFigur(f);
        int zeige = f.zeige;
        std::set<std::string> skelette;
        std::set<std::string> ordner;
        if (sf != nullptr) {
            for (const std::string& s : sf->skelette) skelette.insert(ns::Lower(s));
            for (const std::string& m : sf->meshes) EigeneOrdner(m, ordner);
            if (ordner.empty())          // Figur aus einer aelteren Fassung ohne ns_mesh: Skelett-Ordner
                for (const std::string& s : sf->skelette) EigeneOrdner(s, ordner);
        }
        if (zeige != 2 && sf == nullptr) {
            zeige = 2;
            hinweis = L"No Shinobi Striker character in the scene - showing all clips. Import a character first.";
        }
        auto passt = [&](const ns::AnimEintrag& a, int modus) {
            if (modus != 2 && !skelette.count(ns::Lower(a.skeleton))) return false;
            if (modus != 0) return true;
            // nur Clips aus den Ordnern der Figur selbst (andere Figuren teilen sich teils ein Skelett)
            std::string pfad;
            for (const std::string& s : CharTeile(a.file)) pfad += s + "/";
            for (const std::string& o : ordner) if (pfad.rfind(o, 0) == 0) return true;
            return false;
        };
        if (zeige == 0) {
            // Figuren ohne eigenen Animationsordner (Beschwoerungen, Ninjutsu-Figuren): alle Clips des Skeletts
            bool irgendeiner = false;
            for (const ns::AnimEintrag& a : f.katalog->anims) if (passt(a, 0)) { irgendeiner = true; break; }
            if (!irgendeiner) {
                zeige = 1;
                hinweis = L"no clips in the character's own folder - showing every clip on its skeleton";
            }
        }
        for (size_t i = 0; i < f.katalog->anims.size(); ++i) {
            const ns::AnimEintrag& a = f.katalog->anims[i];
            if (!passt(a, zeige)) continue;
            const std::string k = ns::Lower(a.file);
            bool alle = true;
            for (const std::string& w : woerter) if (k.find(w) == std::string::npos) { alle = false; break; }
            if (alle) f.sichtbar.push_back(i);
        }
    }
    SendMessageW(lb, WM_SETREDRAW, FALSE, 0);
    SendMessageW(lb, LB_SETCOUNT, static_cast<WPARAM>(f.sichtbar.size()), 0);
    SendMessageW(lb, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    SendMessageW(lb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lb, nullptr, TRUE);
    InvalidateRect(GetDlgItem(f.h, IDC_A_KOPF), nullptr, FALSE);
    if (f.katalog != nullptr) {
        std::wstring t = std::to_wstring(f.sichtbar.size()) + L" clips";
        if (const SzenenFigur* sf = GewaehlteFigur(f)) t += L" for " + Breit(sf->name);
        if (!hinweis.empty()) t += L"  ·  " + hinweis;
        Status(f, t);
    }
}

void FuelleFiguren(Fenster& f) {
    std::string vorher;
    if (const SzenenFigur* sf = GewaehlteFigur(f)) vorher = sf->id;
    f.figuren = FigurenInSzene();
    HWND c = GetDlgItem(f.h, IDC_A_FIGUR);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    f.figurWahl = -1;
    if (f.figuren.empty()) {
        SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(no Shinobi Striker character in the scene)"));
        SendMessageW(c, CB_SETCURSEL, 0, 0);
        return;
    }
    for (size_t i = 0; i < f.figuren.size(); ++i) {
        const std::wstring t = Breit(f.figuren[i].name) + L"  (" + std::to_wstring(f.figuren[i].knochen) + L" bones)";
        SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str()));
        if (!vorher.empty() && f.figuren[i].id == vorher) f.figurWahl = static_cast<int>(i);
    }
    if (f.figurWahl < 0) f.figurWahl = 0;
    SendMessageW(c, CB_SETCURSEL, static_cast<WPARAM>(f.figurWahl), 0);
}

void FuelleSequenzen(Fenster& f) {
    HWND c = GetDlgItem(f.h, IDC_A_SEQUENZ);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    if (f.sequenzen.empty()) {
        SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(no sequences - use \"Load all to timeline\")"));
        SendMessageW(c, CB_SETCURSEL, 0, 0);
        EnableWindow(c, FALSE);
        return;
    }
    int ende = 0;
    for (const Sequenz& s : f.sequenzen) ende = std::max(ende, s.ende);
    const std::wstring alle = L"Whole timeline  (0 - " + std::to_wstring(ende) + L")";
    SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(alle.c_str()));
    for (size_t i = 0; i < f.sequenzen.size(); ++i) {
        const Sequenz& s = f.sequenzen[i];
        const std::wstring t = std::to_wstring(i + 1) + L"   " + Breit(s.name) + L"  (" + std::to_wstring(s.start) + L" - " +
                               std::to_wstring(s.ende) + L")";
        SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str()));
    }
    SendMessageW(c, CB_SETCURSEL, 0, 0);
    EnableWindow(c, TRUE);
}

void ZeigeSequenz(Fenster& f) {
    if (f.sequenzen.empty()) return;
    const LRESULT i = SendDlgItemMessageW(f.h, IDC_A_SEQUENZ, CB_GETCURSEL, 0, 0);
    if (i == CB_ERR) return;
    if (i == 0) {
        int ende = 0;
        for (const Sequenz& s : f.sequenzen) ende = std::max(ende, s.ende);
        ZeigeBereich(0, ende);
        return;
    }
    const Sequenz& s = f.sequenzen[static_cast<size_t>(i - 1)];
    ZeigeBereich(s.start, s.ende);
    Status(f, L"Sequence " + std::to_wstring(i) + L": " + Breit(s.name) + L"  (" + std::to_wstring(s.start) + L" - " +
                  std::to_wstring(s.ende) + L")");
}

// ------------------------------------------------------------
//  Laden
// ------------------------------------------------------------
std::string FigurId(const Fenster& f) {
    const SzenenFigur* sf = GewaehlteFigur(f);
    return sf ? sf->id : std::string();
}

void Laden(Fenster& f) {
    if (f.katalog == nullptr || f.game == nullptr) return;
    const LRESULT sel = SendDlgItemMessageW(f.h, IDC_A_LISTE, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || static_cast<size_t>(sel) >= f.sichtbar.size()) { Status(f, L"Select a clip first."); return; }
    const ns::AnimEintrag& a = f.katalog->anims[f.sichtbar[static_cast<size_t>(sel)]];
    std::wstring bericht;
    HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    const bool ok = WendeAnimationAn(*f.game, a.file, f.wurzel, FigurId(f), bericht);
    SetCursor(alt);
    Status(f, bericht, !ok);
    if (ok) {
        f.sequenzen.clear();
        FuelleSequenzen(f);
    }
}

void AlleLaden(Fenster& f) {
    if (f.katalog == nullptr || f.game == nullptr) return;
    if (f.sichtbar.empty()) { Status(f, L"No clips in the list."); return; }
    BOOL ok = FALSE;
    int abstand = static_cast<int>(GetDlgItemInt(f.h, IDC_A_ABSTAND, &ok, FALSE));
    if (!ok || abstand < 0) abstand = 10;
    abstand = std::min(abstand, 1000);
    if (f.sichtbar.size() > 50) {
        const SzenenFigur* sf = GewaehlteFigur(f);
        wchar_t frage[400];
        swprintf(frage, 400, L"Load %zu clips one after another onto %ls (gap %d frames)?\n\nThe timeline starts at frame 0 with the bind pose.",
                 f.sichtbar.size(), sf ? Breit(sf->name).c_str() : L"the skeleton", abstand);
        if (MessageBoxW(f.h, frage, L"Shinobi Striker Animations", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }
    std::vector<std::string> pfade;
    for (size_t i : f.sichtbar) pfade.push_back(f.katalog->anims[i].file);
    SchreibeEinstellung(L"Abstand", std::to_wstring(abstand));
    Status(f, L"Setting keys for " + std::to_wstring(pfade.size()) + L" clips… (Esc cancels)");
    HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    std::wstring bericht;
    std::vector<Sequenz> plan;
    const bool gut = WendeFolgeAn(*f.game, pfade, abstand, f.notiz, f.wurzel, FigurId(f), plan, bericht);
    SetCursor(alt);
    Status(f, bericht, !gut);
    if (gut) {
        f.sequenzen = plan;
        FuelleSequenzen(f);
    }
}

// ------------------------------------------------------------
//  Zeichnen
// ------------------------------------------------------------
struct Spalten { int name, bilder, ordner, ende; };

Spalten SpaltenFuer(const RECT& r, int rand) {
    const int b = static_cast<int>(r.right - r.left) - 2 * rand;
    Spalten s{};
    s.name = r.left + rand;
    s.ordner = r.left + rand + b * 52 / 100;
    s.bilder = s.ordner - b * 10 / 100;
    s.ende = r.right - rand;
    return s;
}

void Text(HDC dc, const std::wstring& t, int links, int rechts, const RECT& r, UINT ausrichtung) {
    RECT z = { links, r.top, rechts, r.bottom };
    DrawTextW(dc, t.c_str(), -1, &z, ausrichtung | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void ZeichneKopf(const Fenster& f, const DRAWITEMSTRUCT& d) {
    HDC dc = d.hDC;
    const RECT r = d.rcItem;
    FillRect(dc, &r, f.pal.pinselGrund);
    const int rand = std::max(4, f.zeilenHoehe / 3);
    const Spalten s = SpaltenFuer(r, rand);
    HGDIOBJ alt = SelectObject(dc, f.fontFett != nullptr ? f.fontFett : f.fontNormal);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, f.pal.dim);
    Text(dc, L"Name", s.name, s.bilder - rand, r, DT_LEFT);
    Text(dc, L"Frames", s.bilder, s.ordner - 2 * rand, r, DT_RIGHT);
    Text(dc, L"Folder", s.ordner, s.ende, r, DT_LEFT);
    SelectObject(dc, alt);
}

void ZeichneZeile(const Fenster& f, const DRAWITEMSTRUCT& d) {
    HDC dc = d.hDC;
    const RECT r = d.rcItem;
    const bool sel = (d.itemState & ODS_SELECTED) != 0;
    HBRUSH hg = CreateSolidBrush(sel ? f.pal.auswahl : f.pal.feld);
    FillRect(dc, &r, hg);
    DeleteObject(hg);
    if (f.katalog == nullptr || d.itemID == static_cast<UINT>(-1) || d.itemID >= f.sichtbar.size()) return;
    const ns::AnimEintrag& a = f.katalog->anims[f.sichtbar[d.itemID]];
    const int rand = std::max(4, f.zeilenHoehe / 3);
    const Spalten s = SpaltenFuer(r, rand);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ alt = SelectObject(dc, f.fontNormal);
    SetTextColor(dc, sel ? f.pal.auswahlText : f.pal.feldText);
    Text(dc, Breit(a.name) + (a.additiv ? L"  (additive)" : L""), s.name, s.bilder - rand, r, DT_LEFT);
    SetTextColor(dc, sel ? f.pal.auswahlDim : f.pal.feldDim);
    Text(dc, a.frames > 0 ? std::to_wstring(a.frames) : std::wstring(L"-"), s.bilder, s.ordner - 2 * rand, r, DT_RIGHT);
    Text(dc, Breit(a.gruppe), s.ordner, s.ende, r, DT_LEFT);
    SelectObject(dc, alt);
    if (!sel) nsui::Trennlinie(f.pal, dc, r, rand);
}

INT_PTR Verarbeite(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Fenster* f = Zustand(h);
    switch (msg) {
    case WM_INITDIALOG: {
        f = reinterpret_cast<Fenster*>(lp);
        f->h = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(f));
        f->pal.Baue(&ThemeFarbe);
        f->fontNormal = reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0));
        LOGFONTW lf{};
        if (f->fontNormal != nullptr && GetObjectW(f->fontNormal, sizeof(lf), &lf) == sizeof(lf)) {
            lf.lfWeight = FW_SEMIBOLD;
            f->fontFett = CreateFontIndirectW(&lf);
        }
        f->zeilenHoehe = nsui::ZeilenHoehe(h);
        nsui::DunkleTitelleiste(h, f->pal.dunkel);
        if (f->pal.dunkel) {
            SetWindowTheme(GetDlgItem(h, IDC_A_LISTE), L"DarkMode_Explorer", nullptr);
            for (int id : { IDC_A_FIGUR, IDC_A_ZEIGE, IDC_A_SEQUENZ }) SetWindowTheme(GetDlgItem(h, id), L"DarkMode_CFD", nullptr);
        }
        SendDlgItemMessageW(h, IDC_A_LISTE, LB_SETITEMHEIGHT, 0, f->zeilenHoehe + 6);
        SetWindowTextW(h, (std::wstring(L"Shinobi Striker Animations ") + NSIMPORT_VERSION_STR).c_str());
        const std::wstring abstand = LiesEinstellung(L"Abstand");
        SetDlgItemInt(h, IDC_A_ABSTAND, abstand.empty() ? 10 : static_cast<UINT>(_wtoi(abstand.c_str())), FALSE);
        f->notiz = LiesEinstellung(L"Notizspur") != L"0";
        f->wurzel = LiesEinstellung(L"Wurzelbewegung") != L"0";
        f->zeige = std::clamp(_wtoi(LiesEinstellung(L"Zeige").c_str()), 0, 2);
        for (const wchar_t* t : kZeige) SendDlgItemMessageW(h, IDC_A_ZEIGE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
        SendDlgItemMessageW(h, IDC_A_ZEIGE, CB_SETCURSEL, static_cast<WPARAM>(f->zeige), 0);
        SendDlgItemMessageW(h, IDC_A_SUCHE, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"e.g.  idle  or  rasengan"));
        LiesSequenzen(f->sequenzen);
        FuelleSequenzen(*f);
        FuelleFiguren(*f);
        std::string fehler;
        HCURSOR alt = SetCursor(LoadCursor(nullptr, IDC_WAIT));
        const bool ok = Spiel(LiesEinstellung(L"Spielordner"), f->game, f->katalog, fehler);
        SetCursor(alt);
        if (!ok) {
            f->game = nullptr;
            f->katalog = nullptr;
            Status(*f, L"No game folder yet - open the character window (Import Shinobi Striker) once and pick the game folder.", true);
        } else {
            Fuelle(*f);
        }
        TestFotoStarten(h);
        return TRUE;
    }
    case WM_TIMER:
        if (f != nullptr && wp == kTestFotoTimer) {
            SendDlgItemMessageW(h, IDC_A_LISTE, LB_SETCURSEL, 0, 0);
            UpdateWindow(h);
            TestFotoMachen(h, L"animationen");
            EndDialog(h, 1);
        }
        return TRUE;
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* m = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        const int zh = (f != nullptr) ? f->zeilenHoehe : nsui::ZeilenHoehe(h);
        m->itemHeight = static_cast<UINT>(m->CtlType == ODT_COMBOBOX ? zh + 4 : zh + 6);
        return TRUE;
    }
    case WM_DRAWITEM: {
        if (f == nullptr) break;
        const DRAWITEMSTRUCT* d = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        switch (d->CtlID) {
        case IDC_A_LISTE: ZeichneZeile(*f, *d); return TRUE;
        case IDC_A_KOPF: ZeichneKopf(*f, *d); return TRUE;
        case IDC_A_STATUS: nsui::ZeichneStatus(f->pal, f->fontNormal, *d, f->status, f->statusFehler); return TRUE;
        case IDC_A_FIGUR:
        case IDC_A_ZEIGE:
        case IDC_A_SEQUENZ: nsui::ZeichneAuswahlfeld(f->pal, f->fontNormal, *d); return TRUE;
        case IDC_A_NOTIZ: nsui::ZeichneKnopf(f->pal, f->fontNormal, *d, f->notiz); return TRUE;
        case IDC_A_WURZEL: nsui::ZeichneKnopf(f->pal, f->fontNormal, *d, f->wurzel); return TRUE;
        case IDC_A_LADEN: nsui::ZeichneKnopf(f->pal, f->fontNormal, *d, true); return TRUE;
        default: nsui::ZeichneKnopf(f->pal, f->fontNormal, *d, false); return TRUE;
        }
    }
    case WM_COMMAND: {
        if (f == nullptr) break;
        const int code = HIWORD(wp);
        switch (LOWORD(wp)) {
        case IDC_A_SUCHE:
            if (code == EN_CHANGE) Fuelle(*f);
            return TRUE;
        case IDC_A_FIGUR:
            if (code == CBN_DROPDOWN) FuelleFiguren(*f);
            if (code == CBN_SELCHANGE) {
                f->figurWahl = static_cast<int>(SendDlgItemMessageW(h, IDC_A_FIGUR, CB_GETCURSEL, 0, 0));
                if (f->figurWahl >= static_cast<int>(f->figuren.size())) f->figurWahl = -1;
                Fuelle(*f);
            }
            return TRUE;
        case IDC_A_ZEIGE:
            if (code == CBN_SELCHANGE) {
                f->zeige = static_cast<int>(SendDlgItemMessageW(h, IDC_A_ZEIGE, CB_GETCURSEL, 0, 0));
                SchreibeEinstellung(L"Zeige", std::to_wstring(f->zeige));
                Fuelle(*f);
            }
            return TRUE;
        case IDC_A_SEQUENZ:
            if (code == CBN_SELCHANGE) ZeigeSequenz(*f);
            return TRUE;
        case IDC_A_NOTIZ:
            f->notiz = !f->notiz;
            SchreibeEinstellung(L"Notizspur", f->notiz ? L"1" : L"0");
            InvalidateRect(GetDlgItem(h, IDC_A_NOTIZ), nullptr, TRUE);
            return TRUE;
        case IDC_A_WURZEL:
            f->wurzel = !f->wurzel;
            SchreibeEinstellung(L"Wurzelbewegung", f->wurzel ? L"1" : L"0");
            InvalidateRect(GetDlgItem(h, IDC_A_WURZEL), nullptr, TRUE);
            return TRUE;
        case IDC_A_LISTE:
            if (code == LBN_DBLCLK) Laden(*f);
            return TRUE;
        case IDC_A_LADEN: Laden(*f); return TRUE;
        case IDC_A_ALLE: AlleLaden(*f); return TRUE;
        case IDOK: Laden(*f); return TRUE;
        case IDCANCEL: EndDialog(h, 1); return TRUE;
        default: break;
        }
        break;
    }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        if (f != nullptr && f->pal.pinselGrund != nullptr) {
            SetTextColor(reinterpret_cast<HDC>(wp), f->pal.text);
            SetBkColor(reinterpret_cast<HDC>(wp), f->pal.grund);
            return reinterpret_cast<INT_PTR>(f->pal.pinselGrund);
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        if (f != nullptr && f->pal.pinselFeld != nullptr) {
            SetTextColor(reinterpret_cast<HDC>(wp), f->pal.feldText);
            SetBkColor(reinterpret_cast<HDC>(wp), f->pal.feld);
            return reinterpret_cast<INT_PTR>(f->pal.pinselFeld);
        }
        break;
    case WM_PAINT: {
        if (f == nullptr) break;
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        for (int id : { IDC_A_SUCHE, IDC_A_ABSTAND, IDC_A_LISTE }) nsui::Kante(h, dc, id, f->pal.linie);
        EndPaint(h, &ps);
        return TRUE;
    }
    case WM_DESTROY:
        if (f != nullptr) {
            if (f->fontFett != nullptr) DeleteObject(f->fontFett);
            f->pal.Frei();
        }
        break;
    default:
        break;
    }
    return FALSE;
}

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

int OeffneAnimFenster(HWND eltern) {
    Fenster f;
    const INT_PTR r = DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_ANIMATIONEN),
                                      eltern != nullptr ? eltern : GetCOREInterface()->GetMAXHWnd(), &DlgProc, reinterpret_cast<LPARAM>(&f));
    return r == -1 ? -1 : 1;
}

} // namespace nsi

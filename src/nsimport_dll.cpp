// ============================================================
//  Shinobi Striker Import - DLL-Einstieg, Importer-Klasse und MAXScript
//
//  Die Exporte sind nicht extern "C"; die .def-Datei sorgt dafuer,
//  dass Max die undekorierten Namen findet (wie beim SWBF2 Import).
// ============================================================
#include "nsimport.h"

#include <iFnPub.h>

HINSTANCE hInstance = nullptr;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

class NSSceneImport : public SceneImport {
public:
    int ExtCount() override { return 2; }
    const MCHAR* Ext(int i) override {
        switch (i) {
        case 0: return _T("pak");
        case 1: return _T("exe");
        default: return _T("");
        }
    }
    const MCHAR* ShortDesc() override { return _T("Naruto to Boruto: Shinobi Striker"); }
    const MCHAR* LongDesc() override { return _T("Shinobi Striker game (any .pak or NARUTO.exe -> character window)"); }
    const MCHAR* AuthorName() override { return _T("DennisH"); }
    const MCHAR* CopyrightMessage() override { return _T(""); }
    const MCHAR* OtherMessage1() override { return _T(""); }
    const MCHAR* OtherMessage2() override { return _T(""); }
    unsigned int Version() override { return NSIMPORT_VERSION; }
    void ShowAbout(HWND hWnd) override {
        MessageBox(hWnd, _T("Shinobi Striker Import ") NSIMPORT_VERSION_STR _T("\n\nCharacters and animations straight from\n")
                         _T("Naruto to Boruto: Shinobi Striker.\n\nNot affiliated with CyberConnect2, Bandai Namco, Shueisha or Autodesk."),
                   _T("Shinobi Striker Import"), MB_ICONINFORMATION);
    }
    int DoImport(const MCHAR* name, ImpInterface*, Interface*, BOOL suppressPrompts) override {
        return nsi::ImportiereEingang(name, suppressPrompts);
    }
};

class NSSceneImportClassDesc : public ClassDesc2 {
public:
    int IsPublic() override { return TRUE; }
    void* Create(BOOL) override { return new NSSceneImport(); }
    const MCHAR* ClassName() override { return _T("Shinobi Striker Import"); }
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
    const MCHAR* NonLocalizedClassName() override { return _T("Shinobi Striker Import"); }
#endif
    SClass_ID SuperClassID() override { return SCENE_IMPORT_CLASS_ID; }
    Class_ID ClassID() override { return NSIMPORT_SCENE_CLASS_ID; }
    const MCHAR* Category() override { return _T("Import"); }
    const MCHAR* InternalName() override { return _T("NSSceneImport"); }
    HINSTANCE HInstance() override { return hInstance; }
};

static NSSceneImportClassDesc theSceneImportClassDesc;

// ------------------------------------------------------------
//  MAXScript: ShinobiCpp
//
//    ShinobiCpp.showDialog()                              das Figurenfenster
//    ShinobiCpp.showAnimDialog()                          das Animationsfenster
//    ShinobiCpp.version()                                 Fassung der .dlu
//    ShinobiCpp.importCharacter <ordner> <teile> <flags>  Teile ";"-getrennt; flags 1 Texturen, 2 Normal-Maps,
//                                                      4 ohne Skin, -1 wie im Fenster (Tests)
//    ShinobiCpp.applyAnimation <ordner> <pfad> <bool>     Clip auf die Figur (Tests)
//    ShinobiCpp.loadAnimations <ordner> <pfade> <abstand> Clips hintereinander (Tests)
//    ShinobiCpp.importFigure <ordner> <name> <flags>      Figur aus dem Reiter "Characters" ("Naruto Uzumaki")
//
//  Ordner leer = gespeicherter/gefundener Spielordner. Antwort ist der
//  Bericht; "ERROR: ..." bei Fehlern.
// ------------------------------------------------------------
namespace {
std::vector<std::string> Liste(const MCHAR* s) {
    std::vector<std::string> o;
    const std::string t = nsi::Utf8(s ? s : _T(""));
    size_t p = 0;
    while (p <= t.size()) {
        size_t e = t.find(';', p);
        if (e == std::string::npos) e = t.size();
        if (e > p) o.push_back(t.substr(p, e - p));
        p = e + 1;
    }
    return o;
}
}

class NSImportFP : public FPStaticInterface {
public:
    enum { fn_showDialog = 0, fn_version = 1, fn_importCharacter = 2, fn_applyAnimation = 3, fn_showAnimDialog = 4, fn_loadAnims = 5,
           fn_importFigure = 6 };

    // Figur aus dem Reiter "Characters": Anzeigename ("Naruto Uzumaki") oder Pfad des Hauptmeshes
    const MCHAR* importFigure(const MCHAR* ordner, const MCHAR* bp, int flags) {
        static MSTR antwort;
        ns::Game* g = nullptr;
        const ns::Katalog* k = nullptr;
        std::string fehler;
        if (!nsi::Spiel(ordner ? ordner : _T(""), g, k, fehler)) { antwort = MSTR(_T("ERROR: ")) + MSTR::FromUTF8(fehler.c_str()); return antwort.data(); }
        const std::string w = nsi::Utf8(bp ? bp : _T(""));
        std::vector<ns::FigurTeil> teile;
        std::string name;
        for (const auto& fe : k->figuren)
            if (_stricmp(fe.name.c_str(), w.c_str()) == 0 || _stricmp(fe.file.c_str(), w.c_str()) == 0) { teile = fe.teile; name = fe.name; break; }
        if (teile.empty()) { antwort = _T("ERROR: no such character"); return antwort.data(); }
        nsi::ImportOptionen o;
        if (flags < 0) o.texturen = nsi::LiesEinstellung(L"Texturen") != L"0";
        else { o.texturen = (flags & 1) != 0; o.normalMaps = (flags & 2) != 0; o.skin = (flags & 4) == 0; }
        std::wstring bericht;
        const bool ok = nsi::ImportiereFigur(*g, teile, name, o, bericht);
        antwort = MSTR(ok ? _T("") : _T("ERROR: ")) + MSTR(bericht.c_str());
        return antwort.data();
    }

    BOOL showDialog() { return nsi::OeffneFenster() >= 0 ? TRUE : FALSE; }
    BOOL showAnimDialog() { return nsi::OeffneAnimFenster() >= 0 ? TRUE : FALSE; }
    const MCHAR* version() { return NSIMPORT_VERSION_STR; }

    // flags: 1 = Texturen, 2 = Normal-Maps, 4 = ohne Skin; -1 = Einstellungen des Fensters
    const MCHAR* importCharacter(const MCHAR* ordner, const MCHAR* teile, int flags) {
        static MSTR antwort;
        ns::Game* g = nullptr;
        const ns::Katalog* k = nullptr;
        std::string fehler;
        if (!nsi::Spiel(ordner ? ordner : _T(""), g, k, fehler)) { antwort = MSTR(_T("ERROR: ")) + MSTR::FromUTF8(fehler.c_str()); return antwort.data(); }
        std::wstring bericht;
        nsi::ImportOptionen o;
        if (flags < 0) o.texturen = nsi::LiesEinstellung(L"Texturen") != L"0";
        else { o.texturen = (flags & 1) != 0; o.normalMaps = (flags & 2) != 0; o.skin = (flags & 4) == 0; }
        const bool ok = nsi::ImportiereFigur(*g, Liste(teile), o, bericht);
        antwort = MSTR(ok ? _T("") : _T("ERROR: ")) + MSTR(bericht.c_str());
        return antwort.data();
    }

    const MCHAR* applyAnimation(const MCHAR* ordner, const MCHAR* pfad, BOOL wurzel) {
        static MSTR antwort;
        ns::Game* g = nullptr;
        const ns::Katalog* k = nullptr;
        std::string fehler;
        if (!nsi::Spiel(ordner ? ordner : _T(""), g, k, fehler)) { antwort = MSTR(_T("ERROR: ")) + MSTR::FromUTF8(fehler.c_str()); return antwort.data(); }
        std::wstring bericht;
        const bool ok = nsi::WendeAnimationAn(*g, nsi::Utf8(pfad ? pfad : _T("")), wurzel != FALSE, std::string(), bericht);
        antwort = MSTR(ok ? _T("") : _T("ERROR: ")) + MSTR(bericht.c_str());
        return antwort.data();
    }

    const MCHAR* loadAnimations(const MCHAR* ordner, const MCHAR* pfade, int abstand) {
        static MSTR antwort;
        ns::Game* g = nullptr;
        const ns::Katalog* k = nullptr;
        std::string fehler;
        if (!nsi::Spiel(ordner ? ordner : _T(""), g, k, fehler)) { antwort = MSTR(_T("ERROR: ")) + MSTR::FromUTF8(fehler.c_str()); return antwort.data(); }
        std::vector<nsi::Sequenz> plan;
        std::wstring bericht;
        const bool ok = nsi::WendeFolgeAn(*g, Liste(pfade), abstand, true, true, std::string(), plan, bericht);
        antwort = MSTR(ok ? _T("") : _T("ERROR: ")) + MSTR(bericht.c_str());
        return antwort.data();
    }

    DECLARE_DESCRIPTOR(NSImportFP)
    BEGIN_FUNCTION_MAP
        FN_0(fn_showDialog, TYPE_BOOL, showDialog)
        FN_0(fn_version, TYPE_STRING, version)
        FN_3(fn_importCharacter, TYPE_STRING, importCharacter, TYPE_STRING, TYPE_STRING, TYPE_INT)
        FN_3(fn_applyAnimation, TYPE_STRING, applyAnimation, TYPE_STRING, TYPE_STRING, TYPE_BOOL)
        FN_0(fn_showAnimDialog, TYPE_BOOL, showAnimDialog)
        FN_3(fn_loadAnims, TYPE_STRING, loadAnimations, TYPE_STRING, TYPE_STRING, TYPE_INT)
        FN_3(fn_importFigure, TYPE_STRING, importFigure, TYPE_STRING, TYPE_STRING, TYPE_INT)
    END_FUNCTION_MAP
};

static NSImportFP theNSImportFP(
    NSIMPORT_FP_ID, _T("ShinobiCpp"), 0, &theSceneImportClassDesc, FP_CORE,
    NSImportFP::fn_showDialog, _T("showDialog"), 0, TYPE_BOOL, 0, 0,
    NSImportFP::fn_version, _T("version"), 0, TYPE_STRING, 0, 0,
    NSImportFP::fn_importCharacter, _T("importCharacter"), 0, TYPE_STRING, 0, 3,
        _T("gameFolder"), 0, TYPE_STRING,
        _T("parts"), 0, TYPE_STRING,
        _T("flags"), 0, TYPE_INT,
    NSImportFP::fn_applyAnimation, _T("applyAnimation"), 0, TYPE_STRING, 0, 3,
        _T("gameFolder"), 0, TYPE_STRING,
        _T("animation"), 0, TYPE_STRING,
        _T("rootMotion"), 0, TYPE_BOOL,
    NSImportFP::fn_showAnimDialog, _T("showAnimDialog"), 0, TYPE_BOOL, 0, 0,
    NSImportFP::fn_loadAnims, _T("loadAnimations"), 0, TYPE_STRING, 0, 3,
        _T("gameFolder"), 0, TYPE_STRING,
        _T("animations"), 0, TYPE_STRING,
        _T("gap"), 0, TYPE_INT,
    NSImportFP::fn_importFigure, _T("importFigure"), 0, TYPE_STRING, 0, 3,
        _T("gameFolder"), 0, TYPE_STRING,
        _T("character"), 0, TYPE_STRING,
        _T("flags"), 0, TYPE_INT,
    p_end);

__declspec(dllexport) const TCHAR* LibDescription() {
    return _T("Shinobi Striker Import ") NSIMPORT_VERSION_STR _T(" - Naruto to Boruto: Shinobi Striker Importer");
}
__declspec(dllexport) int LibNumberClasses() { return 1; }
__declspec(dllexport) ClassDesc* LibClassDesc(int i) { return (i == 0) ? &theSceneImportClassDesc : nullptr; }
__declspec(dllexport) ULONG LibVersion() { return VERSION_3DSMAX; }

// Fehlt die Anmeldung des Kerninterfaces, wird sie nachgeholt (Lehre aus dem SWBF2 Import).
__declspec(dllexport) int LibInitialize() {
    static bool erledigt = false;
    if (erledigt) return TRUE;
    erledigt = true;
    if (GetCOREInterface(NSIMPORT_FP_ID) == nullptr) RegisterCOREInterface(&theNSImportFP);
    return TRUE;
}
__declspec(dllexport) int CanAutoDefer() { return FALSE; }

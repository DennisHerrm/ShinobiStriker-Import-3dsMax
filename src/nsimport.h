// ============================================================
//  Shinobi Striker Import - Plugin-Kopf
//
//  Figuren und Animationen aus Naruto to Boruto: Shinobi Striker direkt aus
//  den Pak-Archiven (NARUTO\Content\Paks\*.pak, Index AES-verschluesselt) nach
//  3ds Max: Skelett, Meshes (mehrere Teile zu einer Figur), Skin,
//  Materialien, Animationen.
//
//  Der Leser (ns_*.h) ist derselbe, den nsdump.exe benutzt.
// ============================================================
#pragma once

#include "ns_katalog.h"
#include "ns_figur.h"
#include "ns_anim.h"

#include <max.h>
#include <iparamb2.h>
#include <impexp.h>
#include <istdplug.h>

#include <string>
#include <vector>

#define NSIMPORT_VERSION      10
#define NSIMPORT_VERSION_STR  _T("0.1.0")

// Einmalig gezogen, nie wieder aendern.
#define NSIMPORT_SCENE_CLASS_ID  Class_ID(0xb8e14f11, 0xf7674677)
#define NSIMPORT_FP_ID           Interface_ID(0xdc94809a, 0x7c8179a0)

namespace nsi {

// Ablage: %LOCALAPPDATA%\NSImport (Einstellungen, Protokoll, Katalog, Texturen)
std::wstring Ablage();
std::wstring LiesEinstellung(const std::wstring& schluessel);
void SchreibeEinstellung(const std::wstring& schluessel, const std::wstring& wert);

void LogNeu(const char* titel);
void Log(const char* format, ...);

std::wstring Breit(const std::string& s);
std::string Utf8(const std::wstring& w);

// Spiel laden (Paks + Katalog), einmal je Ordner.
bool IstSpielordner(const std::wstring& o);
std::wstring SucheSpiel();
bool Spiel(const std::wstring& ordner, ns::Game*& game, const ns::Katalog*& katalog, std::string& fehler);

struct ImportOptionen {
    bool texturen = true;
    bool normalMaps = true;
    bool skin = true;
    float versatzX = 0.0f;   // cm (UE-Raum), damit mehrere Figuren nebeneinander stehen
};

// Mehrere SkeletalMeshes als eine Figur importieren (Pfade "NARUTO/Content/...uasset").
bool ImportiereFigur(ns::Game& g, const std::vector<std::string>& teile, const ImportOptionen& o, std::wstring& bericht);
// Teile mit ersetzten Materialien, name = Figurenname ("Naruto Uzumaki")
bool ImportiereFigur(ns::Game& g, const std::vector<ns::FigurTeil>& teile, const std::string& name, const ImportOptionen& o,
                     std::wstring& bericht);

// Eine importierte Figur in der Szene (alle Knoten tragen dieselbe ns_id).
struct SzenenFigur {
    std::string id, name;
    std::vector<std::string> skelette;
    size_t knochen = 0;
};
std::vector<SzenenFigur> FigurenInSzene();

struct Sequenz {
    std::string name;
    int start = 0, ende = 0;
};

bool WendeAnimationAn(ns::Game& g, const std::string& animPfad, bool wurzelBewegung, const std::string& id, std::wstring& bericht);
bool WendeFolgeAn(ns::Game& g, const std::vector<std::string>& pfade, int abstand, bool notiz, bool wurzelBewegung,
                  const std::string& id, std::vector<Sequenz>& plan, std::wstring& bericht);
bool LiesSequenzen(std::vector<Sequenz>& aus);
void ZeigeBereich(int startBild, int endeBild);

uint32_t ThemeFarbe(int welche);

int OeffneFenster();
int OeffneAnimFenster(HWND eltern = nullptr);

// Selbsttest: Ist NSIMPORT_TESTFOTO=<datei ohne Endung> gesetzt, zeichnen sich die Fenster nach
// 4 s ueber PrintWindow selbst in <datei>_<name>.png und schliessen sich (nur das Fenster selbst,
// nichts vom Bildschirm dahinter).
constexpr UINT_PTR kTestFotoTimer = 0x4A53;
void TestFotoStarten(HWND h);
bool TestFotoMachen(HWND h, const wchar_t* name);

int ImportiereEingang(const MCHAR* pfad, BOOL ohneRueckfragen);

} // namespace nsi

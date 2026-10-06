// ns_katalog.h - Verzeichnis aller Figuren, SkeletalMeshes und AnimSequences mit ihrem Skelett
// Aufbau einmal (parallel), danach aus der Cache-Datei.
#pragma once
#include "ns_bp.h"
#include <functional>
#include <map>

namespace ns {

// Mesh-Arten (Reiter im Figurenfenster)
enum Kategorie { K_Helden = 0, K_Avatar = 1, K_Gegenstaende = 2, K_Andere = 3, K_Alle = 4, K_Anzahl = 5 };

struct MeshEintrag {
    std::string file;       // NARUTO/Content/...uasset
    std::string name;       // Dateiname ohne Endung
    std::string gruppe;     // Ordner relativ zu Content
    std::string skeleton;   // /Game/...Skeleton
    int kategorie = K_Andere;
};

struct AnimEintrag {
    std::string file, name, gruppe, skeleton;
    int frames = 0;
    float laenge = 0;
    bool additiv = false;
};

// Eine Figur, wie das Spiel sie zeigt: Hauptmesh eines Figurenordners (Original/Naruto/Naruto_Default)
struct FigurEintrag {
    std::string file, name, gruppe;   // file = Hauptmesh, name = Anzeigename ("Naruto Uzumaki")
    std::vector<FigurTeil> teile;
    int kategorie = K_Helden;
};

class Katalog {
public:
    std::vector<FigurEintrag> figuren;
    std::vector<MeshEintrag> meshes;
    std::vector<AnimEintrag> anims;
    // cacheDatei leer = ohne Cache; fortschritt(fertig, gesamt) kommt aus Arbeitsthreads
    bool Baue(Game& g, const std::wstring& cacheDatei, std::string& err,
              const std::function<void(size_t, size_t)>& fortschritt = {});
    static std::string Gruppe(const std::string& file);
    static std::string Blatt(const std::string& file);
};

// Englische Anzeigenamen aus den Sprachtabellen (L10N/en/Localize/DataTable): Id -> Text,
// z. B. "Character.D21" -> Name der DLC-Figur
std::map<std::string, std::string> LadeTexte(Game& g, const char* sprache = "en");

// Standardfarben eines Avatar-Kleidungsstuecks (erste Tabellenzeile, die das Mesh benutzt):
// farben = Haut, Farbe 0, Farbe 1 (je RGB). meshPfad: "/Game/..." oder "NARUTO/Content/...uasset"
bool AvatarFarben(Game& g, const std::string& meshPfad, uint8_t farben[9]);

} // namespace ns

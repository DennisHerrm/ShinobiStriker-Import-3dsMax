// ns_bp.h - Teile einer Figur (Mesh + ersetzte Materialien + Socket)
//
// Shinobi Striker setzt die Helden aus EINEM SkeletalMesh zusammen (SK_CHR_Naruto). Mehrere
// Teile gibt es bei den Avatar-Kleidungsstuecken (Custom: Gesicht, Oberkoerper, Hose, Kopf)
// und bei Zusatzmeshes (Arme, Schweife, Waffen am Socket).
#pragma once
#include "ns_paket.h"

namespace ns {

struct FigurTeil {
    std::string mesh;                         // /Game/... (Paketname) oder NARUTO/Content/...uasset
    std::vector<std::string> materialien;     // Override je Materialslot ("" = keins)
    std::string quelle;                       // woher das Teil kommt (Anzeige)
    std::string slot;                         // Kleidungsslot ("Face", "BodyUpper" ...)
    // An einem Socket des Hauptmeshes (Waffe in der Hand); sonst leer = Leader-Pose ueber Knochennamen
    std::string socket;
    float relRot[4] = { 0, 0, 0, 1 }, relPos[3] = { 0, 0, 0 }, relScale[3] = { 1, 1, 1 };
    // Avatar-Kleidung: Farben fuer die Farbmaske ColorMask_RGB (R Haut, G Farbe 0, B Farbe 1), je RGB
    bool hatFarben = false;
    uint8_t farben[9] = {};
};

// UE FRotator (Pitch, Yaw, Roll in Grad) -> Quaternion x y z w
void RotatorZuQuat(const double pyr[3], float q[4]);

} // namespace ns

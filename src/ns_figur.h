// ns_figur.h - mehrere SkeletalMeshes (Koerper, Kopf, Haare, Kleidung) zu einer Figur
//
// Im Spiel uebernehmen Kopf und Haare die Pose des Koerpers ueber gleichnamige
// Knochen (Leader-Pose). Ihre eigene Bindepose liegt aber woanders (der Kopf
// von Cal ist 172 cm versetzt modelliert). Deshalb werden die Teile auf das
// Skelett des Hauptteils umgerechnet: v' = Summe w * G_haupt(b) * G_teil(b)^-1 * v.
// Knochen, die nur ein Teil hat, haengen mit ihrer lokalen Lage am Eltern.
#pragma once
#include "ns_mesh.h"
#include "ns_bp.h"

namespace ns {

// 3x4-Matrix fuer Spaltenvektoren (v' = M v), Zeile r = m[r*4 .. r*4+3]
struct M34 {
    double m[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    static M34 From(const float* q, const float* t, const float* s);
    M34 operator*(const M34& b) const;
    M34 Inverse() const;
    void Apply(const float* v, double* out) const;        // Punkt
    void ApplyDir(const float* v, double* out) const;     // Richtung
};

struct Figur {
    RefSkeleton skel;
    std::vector<M34> global;              // Bindepose je Knochen (UE-Raum)
    std::vector<SkelMesh> teile;          // Vertices/Normalen umgerechnet, infBone -> skel
    std::vector<std::string> skelette;    // Skelett-Pakete aller Teile (fuer Animationsfilter)
};

// teile wird verschoben. Das Teil mit den meisten Knochen gibt das Skelett vor.
bool BaueFigur(std::vector<SkelMesh>& teile, Figur& f, std::string& log);

// Teile einer Figur lesen: ersetzte Materialien eintragen, Socket-Teile (Waffen) am Hauptmesh
// (erstes Teil) befestigen. log: eine Zeile je Teil; fehler: was weggelassen wurde.
bool LadeTeile(Game& g, const std::vector<FigurTeil>& teile, std::vector<SkelMesh>& aus, std::string& log, std::string& fehler);

std::vector<M34> GlobalPose(const RefSkeleton& sk);

} // namespace ns

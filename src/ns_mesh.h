// ns_mesh.h - SkeletalMesh/Skeleton aus Shinobi Striker (UE 4.16)
// Alle Werte in UE-Koordinaten (linkshaendig, Z oben, cm). Umrechnung macht die Max-Seite.
#pragma once
#include "ns_paket.h"

namespace ns {

struct Bone {
    std::string name;
    int parent = -1;
    float rot[4] = { 0, 0, 0, 1 };   // x y z w
    float pos[3] = { 0, 0, 0 };
    float scale[3] = { 1, 1, 1 };
};

struct RefSkeleton {
    std::vector<Bone> bones;
    int Find(const std::string& n) const;
};
bool ReadRefSkeleton(Reader& r, RefSkeleton& sk);

struct MeshSection {
    int material = 0;
    uint32_t baseIndex = 0, numTris = 0, baseVertex = 0, numVerts = 0;
    std::vector<uint16_t> boneMap;
    bool disabled = false;
};

struct SkelMesh {
    std::string name, packageName;
    std::string skeletonPath;               // /Game/...Skeleton
    RefSkeleton skel;
    std::vector<std::string> materials;     // Paketpfad des Materials
    std::vector<std::string> slotNames;
    std::vector<MeshSection> sections;
    uint32_t numVerts = 0, numUV = 0, maxInf = 0;
    std::vector<float> pos;                 // 3 je Vertex
    std::vector<float> nrm;                 // 3 je Vertex (leer, wenn das Spiel sie weggelassen hat)
    std::vector<float> uv;                  // numUV*2 je Vertex
    std::vector<uint32_t> color;            // optional RGBA
    std::vector<uint16_t> infBone;          // maxInf je Vertex, Index in skel.bones
    std::vector<uint8_t> infWeight;         // maxInf je Vertex
    std::vector<uint32_t> indices;
    int lodCount = 0, lodUsed = 0;
    // Am Socket befestigt (Waffe): Knochen der Figur + Lage relativ dazu (3x4, Spaltenvektoren, UE)
    bool hatAnhang = false;
    std::string anhangKnochen;
    double anhangLage[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    // Avatar-Kleidung: Farben fuer die Farbmaske (R Haut, G Farbe 0, B Farbe 1), je RGB
    bool hatFarben = false;
    uint8_t farben[9] = {};
};

// Socket eines Meshes (sonst seines Skeletts): Knochen und Lage (Quaternion, Verschiebung, Skalierung)
bool FindeSocket(Game& g, const SkelMesh& m, const std::string& name, std::string& knochen, float rot[4], float pos[3], float scale[3]);

// trace != nullptr -> Ablaufprotokoll mit Offsets
bool ReadSkeletalMesh(Game& g, const std::string& file, SkelMesh& m, std::string& err, std::string* trace = nullptr);
bool ReadSkeleton(Game& g, const std::string& fileOrName, RefSkeleton& sk, std::string& err);

} // namespace ns

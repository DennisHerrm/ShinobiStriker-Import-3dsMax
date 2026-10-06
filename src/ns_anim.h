// ns_anim.h - AnimSequence aus Shinobi Striker (UE 4.16, klassische Key-Kompression)
// Werte sind lokale Knochentransformationen in UE-Koordinaten.
#pragma once
#include "ns_mesh.h"

namespace ns {

struct AnimTrack {
    std::string bone;        // Knochenname aus dem Skelett
    int skelIndex = -1;      // Index im Skelett
    bool wurzel = false;     // Wurzel im Skelett des Clips (z. B. spineC beim Gesichtsskelett)
    std::vector<float> rot;  // 4 je Frame (x y z w)
    std::vector<float> pos;  // 3 je Frame
    std::vector<float> scl;  // 3 je Frame
};

struct AnimClip {
    std::string name, packageName, skeletonPath;
    int numFrames = 0;
    float rate = 30.f;
    float length = 0.f;
    float rateScale = 1.f;   // Abspieltempo im Spiel (RateScale), nur zur Info
    bool rootMotion = false;
    std::string additive;    // leer = nicht additiv, sonst AAT_*
    std::vector<AnimTrack> tracks;
};

// skel: Skelett zur Namensaufloesung (sonst wird skeletonPath geladen)
bool ReadAnimSequence(Game& g, const std::string& file, AnimClip& a, std::string& err,
                      const RefSkeleton* skel = nullptr, std::string* trace = nullptr);
// Nur Kopfdaten (Skelett, Frames) - schnell, fuer Listen
bool ReadAnimInfo(Game& g, const Package& pk, int exportIndex, AnimClip& a);

} // namespace ns

// ns_tex.h - Materialien und Texturen (UE 4.16): MaterialInstanceConstant -> Texture2D -> PNG
#pragma once
#include "ns_paket.h"

namespace ns {

struct TexturSatz {
    std::wstring farbe, normal;   // PNG-Dateien im Cache (leer = keine)
    std::wstring opazitaet;       // Graustufen-PNG (Haarkarten, Wimpern)
    bool alpha = false;           // Material maskiert/durchsichtig
    bool unsichtbar = false;      // durchsichtige Ueberlagerung ohne Farbe (Augenschatten, Traenenfilm)
    bool hatFarbWert = false;     // keine Farbtextur, aber ein Farbparameter (sRGB)
    uint8_t farbWert[3] = { 128, 128, 128 };
};

// matPfad "/Game/.../M_x". prot: Protokollzeilen.
// farben (9 Bytes, R Haut / G Farbe 0 / B Farbe 1 als RGB): Avatar-Kleidung mit ColorMask_RGB einfaerben
bool LoeseMaterial(Game& g, const std::string& matPfad, const std::wstring& cacheOrdner, TexturSatz& ts,
                   std::vector<std::string>* prot = nullptr, const uint8_t* farben = nullptr);

// Texture2D als PNG schreiben (groesste Mip-Stufe bis maxGroesse). Liefert den Pfad.
// kanal >= 0: nur dieser Kanal (0 R, 1 G, 2 B, 3 A) als Graustufenbild (Deckkraft)
std::wstring TexturAlsPng(Game& g, const std::string& texPfad, const std::wstring& cacheOrdner, bool normal,
                          int maxGroesse, std::string& err, int kanal = -1);

// PNG schreiben (kanaele 3 oder 4), Ordner werden angelegt
bool SchreibePng(const std::wstring& datei, const uint8_t* px, int w, int h, int kanaele);

// Rohdekodierung (fuer Tests): RGBA8, oberste brauchbare Mip
bool LiesTextur(Game& g, const std::string& texPfad, int maxGroesse, std::vector<uint8_t>& rgba, int& w, int& h,
                std::string& format, std::string& err);

} // namespace ns

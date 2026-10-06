// nsdump - Gegenprobe fuer den Shinobi-Striker-Leser (ohne Max-SDK)
#include "ns_anim.h"
#include "ns_tex.h"
#include "ns_katalog.h"
#include "ns_figur.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <cmath>
#include <string>

using namespace ns;

static const wchar_t* kGame = L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Naruto To Boruto";

static void Hex(const uint8_t* p, size_t n, size_t base) {
    for (size_t i = 0; i < n; i += 16) {
        printf("%08zx:", base + i);
        for (size_t k = 0; k < 16 && i + k < n; k++) printf(" %02x", p[i + k]);
        printf("  ");
        for (size_t k = 0; k < 16 && i + k < n; k++) { uint8_t c = p[i + k]; printf("%c", c >= 32 && c < 127 ? c : '.'); }
        printf("\n");
    }
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        printf("nsdump info                     Paks\n"
               "nsdump liste [filter]           Dateien auflisten\n"
               "nsdump roh <pfad> <ziel>        Paketdaten (.uasset+.uexp) roh speichern\n"
               "nsdump paket <pfad> [exp] [n]   Exporte + Eigenschaften (+ n Bytes Hex-Rest des Exports)\n"
               "nsdump klassen <filter>         Exportklassen aller passenden Pakete zaehlen\n");
        return 1;
    }
    std::wstring cmd = argv[1];
    Game g; std::string err;
    DWORD t0 = GetTickCount();
    if (!g.Open(kGame, err)) { printf("Fehler: %s\n", err.c_str()); return 2; }
    Archive& ar = g.Ar();
    if (cmd == L"info") {
        for (auto& p : ar.Paks()) {
            size_t enc = 0, komp = 0;
            for (auto& e : p->Entries()) { enc += e.encrypted; komp += e.method != 0; }
            printf("%-40s mount '%s' %7zu Dateien, %zu verschluesselt, %zu komprimiert\n", Narrow(p->Name()).c_str(), p->Mount().c_str(),
                   p->Entries().size(), enc, komp);
        }
        printf("Dateien gesamt: %zu, geoeffnet in %lu ms\n", ar.AllFiles().size(), GetTickCount() - t0);
    } else if (cmd == L"liste") {
        std::string f = argc > 2 ? Lower(Narrow(argv[2])) : "";
        for (auto& fr : ar.AllFiles())
            if (f.empty() || Lower(fr.path).find(f) != std::string::npos) printf("%s\n", fr.path.c_str());
    } else if (cmd == L"roh" && argc > 3) {
        Package pk;
        if (!g.LoadPackage(Narrow(argv[2]), pk, err)) { printf("Fehler: %s\n", err.c_str()); return 3; }
        FILE* o = _wfopen(argv[3], L"wb"); fwrite(pk.data.data(), 1, pk.data.size(), o); fclose(o);
        printf("%zu Bytes (Kopf %d, Bulk ab %lld)\n", pk.data.size(), pk.headerSize, (long long)pk.bulkStart);
    } else if (cmd == L"paket" && argc > 2) {
        Package pk;
        if (!g.LoadPackage(Narrow(argv[2]), pk, err)) { printf("Fehler: %s\n", err.c_str()); return 3; }
        printf("%s  flags %08x  %zu Namen  %zu Importe  %zu Exporte  Kopf %d  Bulk ab %lld  Daten %zu\n", pk.name.c_str(), pk.flags,
               pk.names.size(), pk.imports.size(), pk.exports.size(), pk.headerSize, (long long)pk.bulkStart, pk.data.size());
        for (size_t i = 0; i < pk.imports.size(); i++) {
            std::string pn, on, cn;
            g.ResolveImport(pk, (int)i, pn, on, cn);
            printf("  imp %2zu %s.%s (%s)\n", i, pn.c_str(), on.c_str(), cn.c_str());
        }
        int only = argc > 3 ? _wtoi(argv[3]) : -1;
        for (size_t i = 0; i < pk.exports.size(); i++) {
            const Export& ex = pk.exports[i];
            printf("exp %zu %s : %s  outer %d  @%llx +%llx\n", i, ex.name.c_str(), ex.className.c_str(), ex.outer,
                   (unsigned long long)ex.dataOffset, (unsigned long long)ex.serialSize);
            if (only >= 0 && (int)i != only) continue;
            Reader r(pk.data.data() + ex.dataOffset, (size_t)ex.serialSize, &pk);
            std::vector<Prop> ps;
            bool ok = ReadProps(r, ps);
            std::string s; DumpProps(ps, s, 2);
            printf("%s", s.c_str());
            printf("    -- Eigenschaften %s, Ende bei +%zx von %llx\n", ok ? "ok" : "KAPUTT", r.o, (unsigned long long)ex.serialSize);
            if (only >= 0) Hex(r.p + r.o, std::min<size_t>(r.n - r.o, argc > 4 ? (size_t)_wtoi(argv[4]) : 512), r.o);
        }
    } else if (cmd == L"mesh" && argc > 2) {
        SkelMesh m; std::string tr;
        bool ok = ReadSkeletalMesh(g, Narrow(argv[2]), m, err, &tr);
        printf("%s", tr.c_str());
        if (!ok) { printf("FEHLER: %s\n", err.c_str()); return 5; }
        printf("%s: %u Verts, %zu Tris, %zu Sektionen, %zu Knochen, %u UV, maxInf %u, Farben %zu\n", m.name.c_str(), m.numVerts,
               m.indices.size() / 3, m.sections.size(), m.skel.bones.size(), m.numUV, m.maxInf, m.color.size());
        for (size_t i = 0; i < m.materials.size(); i++) printf("  mat %zu %s (%s)\n", i, m.materials[i].c_str(), m.slotNames[i].c_str());
        // Gewichtssummen und Umlaufsinn gegen die Vertexnormalen
        size_t schlecht = 0;
        for (uint32_t v = 0; v < m.numVerts; v++) {
            int s = 0;
            for (uint32_t k = 0; k < m.maxInf; k++) s += m.infWeight[v * m.maxInf + k];
            if (s < 250 || s > 260) schlecht++;
        }
        size_t gleich = 0, n = 0;
        for (size_t t = 0; t + 2 < m.indices.size(); t += 3 * 7) {
            const float* a = &m.pos[m.indices[t] * 3]; const float* b = &m.pos[m.indices[t + 1] * 3]; const float* c = &m.pos[m.indices[t + 2] * 3];
            float u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, w[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
            float x[3] = { u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0] };
            float nn[3] = { 0, 0, 0 };
            for (int k = 0; k < 3; k++) for (int c2 = 0; c2 < 3; c2++) nn[c2] += m.nrm[m.indices[t + k] * 3 + c2];
            if (x[0] * nn[0] + x[1] * nn[1] + x[2] * nn[2] > 0) gleich++;
            n++;
        }
        float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
        for (uint32_t v = 0; v < m.numVerts; v++) for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], m.pos[v * 3 + k]); hi[k] = std::max(hi[k], m.pos[v * 3 + k]); }
        printf("Gewichtssumme ungleich 255: %zu Vertices; Umlauf: %zu von %zu Dreiecken mit Kreuzprodukt in Normalenrichtung\n", schlecht, gleich, n);
        printf("Ausdehnung: %.1f..%.1f  %.1f..%.1f  %.1f..%.1f\n", lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
        if (argc > 3) {   // OBJ zur Sichtpruefung (Z oben -> Y oben)
            FILE* o = _wfopen(argv[3], L"w");
            for (uint32_t v = 0; v < m.numVerts; v++) fprintf(o, "v %f %f %f\n", m.pos[v * 3], m.pos[v * 3 + 2], m.pos[v * 3 + 1]);
            for (uint32_t v = 0; v < m.numVerts; v++) fprintf(o, "vt %f %f\n", m.uv[v * m.numUV * 2], 1 - m.uv[v * m.numUV * 2 + 1]);
            for (auto& s : m.sections) {
                fprintf(o, "g sec_mat%d\n", s.material);
                for (uint32_t t = 0; t < s.numTris; t++) {
                    uint32_t a = m.indices[s.baseIndex + t * 3] + 1, b = m.indices[s.baseIndex + t * 3 + 1] + 1, c = m.indices[s.baseIndex + t * 3 + 2] + 1;
                    fprintf(o, "f %u/%u %u/%u %u/%u\n", a, a, b, b, c, c);
                }
            }
            fclose(o);
        }
    } else if (cmd == L"anim" && argc > 2) {
        AnimClip a; std::string tr;
        bool ok = ReadAnimSequence(g, Narrow(argv[2]), a, err, nullptr, &tr);
        printf("%s", tr.c_str());
        if (!ok) { printf("FEHLER: %s\n", err.c_str()); return 5; }
        printf("%s: %d Bilder @ %.1f Hz, %zu Spuren, Laenge %.3f s\n", a.name.c_str(), a.numFrames, a.rate, a.tracks.size(), a.length);
        int zeige = argc > 3 ? _wtoi(argv[3]) : 4;
        for (int i = 0; i < (int)a.tracks.size() && i < zeige; i++) {
            const AnimTrack& t = a.tracks[i];
            printf("  %d %s (skel %d)\n", i, t.bone.c_str(), t.skelIndex);
            for (int f = 0; f < a.numFrames; f += std::max(1, a.numFrames / 4))
                printf("    f%-3d rot %.4f %.4f %.4f %.4f  pos %.3f %.3f %.3f  scl %.3f\n", f,
                       t.rot[f * 4], t.rot[f * 4 + 1], t.rot[f * 4 + 2], t.rot[f * 4 + 3],
                       t.pos[f * 3], t.pos[f * 3 + 1], t.pos[f * 3 + 2], t.scl[f * 3]);
        }
    } else if (cmd == L"pose" && argc > 5) {
        // nsdump pose <mesh> <anim|-> <frame> <obj> : Mesh in Pose als OBJ (CPU-Skinning)
        SkelMesh m;
        if (!ReadSkeletalMesh(g, Narrow(argv[2]), m, err)) { printf("FEHLER: %s\n", err.c_str()); return 5; }
        AnimClip a;
        bool hasAnim = std::wstring(argv[3]) != L"-";
        if (hasAnim && !ReadAnimSequence(g, Narrow(argv[3]), a, err)) { printf("FEHLER: %s\n", err.c_str()); return 5; }
        int frame = _wtoi(argv[4]);
        auto mat = [](const float* q, const float* t, const float* s, double* M) {   // 3x4, Spalten-Vektoren
            double x = q[0], y = q[1], z = q[2], w = q[3];
            double R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y),
                            2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
                            2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y) };
            for (int r = 0; r < 3; r++) { for (int c = 0; c < 3; c++) M[r * 4 + c] = R[r * 3 + c] * s[c]; M[r * 4 + 3] = t[r]; }
        };
        auto mul = [](const double* A, const double* B, double* C) {
            for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) {
                double v = A[r * 4 + 0] * B[0 * 4 + c] + A[r * 4 + 1] * B[1 * 4 + c] + A[r * 4 + 2] * B[2 * 4 + c];
                if (c == 3) v += A[r * 4 + 3];
                C[r * 4 + c] = v;
            }
        };
        auto inv = [](const double* A, double* B) {   // allgemeine 3x3-Inverse + Translation
            double a = A[0], b = A[1], c = A[2], d = A[4], e = A[5], f = A[6], gg = A[8], h = A[9], i = A[10];
            double det = a * (e * i - f * h) - b * (d * i - f * gg) + c * (d * h - e * gg);
            double I[9] = { (e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                            (f * gg - d * i) / det, (a * i - c * gg) / det, (c * d - a * f) / det,
                            (d * h - e * gg) / det, (b * gg - a * h) / det, (a * e - b * d) / det };
            for (int r = 0; r < 3; r++) { for (int k = 0; k < 3; k++) B[r * 4 + k] = I[r * 3 + k];
                B[r * 4 + 3] = -(I[r * 3] * A[3] + I[r * 3 + 1] * A[7] + I[r * 3 + 2] * A[11]); }
        };
        size_t nb = m.skel.bones.size();
        std::vector<double> ref(nb * 12), cur(nb * 12), skin(nb * 12);
        std::vector<int> trackOf(nb, -1);
        RefSkeleton sk; std::vector<int> mapAnim(nb, -1);
        for (size_t t = 0; t < a.tracks.size(); t++) { int b = m.skel.Find(a.tracks[t].bone); if (b >= 0) trackOf[b] = (int)t; }
        for (size_t b = 0; b < nb; b++) {
            const Bone& bn = m.skel.bones[b];
            double L[12], C[12];
            mat(bn.rot, bn.pos, bn.scale, L);
            if (bn.parent >= 0) mul(&ref[bn.parent * 12], L, &ref[b * 12]); else memcpy(&ref[b * 12], L, 96);
            if (trackOf[b] >= 0 && frame < a.numFrames) {
                const AnimTrack& t = a.tracks[trackOf[b]];
                mat(&t.rot[frame * 4], &t.pos[frame * 3], &t.scl[frame * 3], C);
            } else memcpy(C, L, 96);
            if (bn.parent >= 0) mul(&cur[bn.parent * 12], C, &cur[b * 12]); else memcpy(&cur[b * 12], C, 96);
            double Ri[12]; inv(&ref[b * 12], Ri); mul(&cur[b * 12], Ri, &skin[b * 12]);
        }
        FILE* o = _wfopen(argv[5], L"w");
        for (uint32_t v = 0; v < m.numVerts; v++) {
            double p[3] = { 0, 0, 0 }, ws = 0;
            for (uint32_t k = 0; k < m.maxInf; k++) {
                double w = m.infWeight[v * m.maxInf + k] / 255.0; if (w <= 0) continue;
                const double* S = &skin[m.infBone[v * m.maxInf + k] * 12];
                for (int r = 0; r < 3; r++) p[r] += w * (S[r * 4] * m.pos[v * 3] + S[r * 4 + 1] * m.pos[v * 3 + 1] + S[r * 4 + 2] * m.pos[v * 3 + 2] + S[r * 4 + 3]);
                ws += w;
            }
            if (ws > 0) for (auto& c : p) c /= ws;
            fprintf(o, "v %f %f %f\n", p[0], p[2], p[1]);
        }
        for (auto& s : m.sections)
            for (uint32_t t = 0; t < s.numTris; t++)
                fprintf(o, "f %u %u %u\n", m.indices[s.baseIndex + t * 3] + 1, m.indices[s.baseIndex + t * 3 + 1] + 1, m.indices[s.baseIndex + t * 3 + 2] + 1);
        fclose(o);
        printf("ok, %zu Knochen, %zu mit Spur\n", nb, (size_t)std::count_if(trackOf.begin(), trackOf.end(), [](int x) { return x >= 0; }));
    } else if (cmd == L"textur" && argc > 3) {
        // nsdump textur <pfad> <ziel.png> [maxGroesse]
        std::vector<uint8_t> rgba; int w = 0, h = 0; std::string fmt;
        if (!LiesTextur(g, Narrow(argv[2]), argc > 4 ? _wtoi(argv[4]) : 0, rgba, w, h, fmt, err)) { printf("FEHLER: %s\n", err.c_str()); return 5; }
        double sum[4] = { 0, 0, 0, 0 };
        for (size_t i = 0; i < rgba.size(); i++) sum[i & 3] += rgba[i];
        const double n = (double)w * h;
        printf("%s %dx%d, Mittel RGBA %.0f %.0f %.0f %.0f\n", fmt.c_str(), w, h, sum[0] / n, sum[1] / n, sum[2] / n, sum[3] / n);
        SchreibePng(argv[3], rgba.data(), w, h, 4);
    } else if (cmd == L"material" && argc > 2) {
        TexturSatz ts; std::vector<std::string> prot;
        LoeseMaterial(g, Narrow(argv[2]), argc > 3 ? std::wstring(argv[3]) : std::wstring(L"."), ts, &prot);
        for (auto& z : prot) printf("%s\n", z.c_str());
    } else if (cmd == L"katalog") {
        // nsdump katalog [filter] : ohne Cache neu bauen, Figuren + Zahlen
        Katalog k;
        DWORD t1 = GetTickCount();
        if (!k.Baue(g, L"", err)) { printf("FEHLER: %s\n", err.c_str()); return 5; }
        printf("%zu Figuren, %zu Meshes, %zu Clips in %lu ms\n", k.figuren.size(), k.meshes.size(), k.anims.size(), GetTickCount() - t1);
        std::string f = argc > 2 ? Lower(Narrow(argv[2])) : "";
        for (auto& fe : k.figuren)
            if (f.empty() || Lower(fe.name + fe.file).find(f) != std::string::npos) printf("  %-40s %s\n", fe.name.c_str(), fe.file.c_str());
        std::map<std::string, int> skel;
        for (auto& a : k.anims) skel[a.skeleton]++;
        int ohne = 0;
        for (auto& m : k.meshes) if (m.skeleton.empty()) ohne++;
        printf("%zu Skelette mit Clips, %d Meshes ohne Skelett\n", skel.size(), ohne);
    } else if (cmd == L"texte" && argc > 2) {
        auto t = LadeTexte(g);
        std::string f = Lower(Narrow(argv[2]));
        printf("%zu Texte\n", t.size());
        for (auto& kv : t) if (Lower(kv.first).find(f) != std::string::npos) printf("  %s = %s\n", kv.first.c_str(), kv.second.c_str());
    } else if (cmd == L"meshtest" || cmd == L"animtest") {
        // Volltest ueber den Katalog: nsdump meshtest|animtest [filter]
        Katalog k;
        if (!k.Baue(g, L"", err)) { printf("FEHLER: %s\n", err.c_str()); return 5; }
        std::vector<std::string> liste;
        std::string f = argc > 2 ? Lower(Narrow(argv[2])) : "";
        if (cmd == L"meshtest") { for (auto& m : k.meshes) if (f.empty() || Lower(m.file).find(f) != std::string::npos) liste.push_back(m.file); }
        else for (auto& a : k.anims) if (f.empty() || Lower(a.file).find(f) != std::string::npos) liste.push_back(a.file);
        int n = 0, okc = 0;
        std::map<std::string, int> fehler;
        std::map<std::string, RefSkeleton> skels;
        DWORD t1 = GetTickCount();
        for (const std::string& p : liste) {
            n++;
            std::string e;
            bool ok;
            if (cmd == L"meshtest") {
                SkelMesh m;
                ok = ReadSkeletalMesh(g, p, m, e);
                if (ok) {
                    for (uint32_t ix : m.indices) if (ix >= m.numVerts) { ok = false; e = "Index ausserhalb"; break; }
                    for (uint32_t v = 0; ok && v < m.numVerts; v++) {
                        int s = 0; for (uint32_t kk = 0; kk < m.maxInf; kk++) s += m.infWeight[v * m.maxInf + kk];
                        if (s < 250 || s > 260) { ok = false; e = "Gewichtssumme " + std::to_string(s); }
                    }
                    for (float x : m.pos) if (!std::isfinite(x) || std::fabs(x) > 1e5f) { ok = false; e = "Position kaputt"; break; }
                }
            } else {
                AnimClip a;
                Package pk;
                ok = g.LoadPackage(p, pk, e);
                if (ok) {
                    int ei = pk.FindExport("AnimSequence");
                    if (ei < 0) { ok = false; e = "kein AnimSequence"; }
                    else {
                        ReadAnimInfo(g, pk, ei, a);
                        auto it = skels.find(a.skeletonPath);
                        if (it == skels.end()) { RefSkeleton sk; std::string se; ReadSkeleton(g, a.skeletonPath, sk, se); it = skels.emplace(a.skeletonPath, sk).first; }
                        if (it->second.bones.empty()) { ok = false; e = "Skelett nicht lesbar"; }
                        else ok = ReadAnimSequence(g, p, a, e, &it->second);
                        // Plausibilitaet: Einheitsquaternionen, endliche Verschiebungen
                        for (auto& t : a.tracks) {
                            if (!ok) break;
                            for (size_t q = 0; q + 3 < t.rot.size(); q += 4) {
                                const double l = t.rot[q] * t.rot[q] + t.rot[q + 1] * t.rot[q + 1] + t.rot[q + 2] * t.rot[q + 2] + t.rot[q + 3] * t.rot[q + 3];
                                if (!(std::fabs(l - 1.0) < 0.02)) { ok = false; e = "Quaternion nicht normiert"; break; }
                            }
                            for (float x : t.pos) if (!std::isfinite(x) || std::fabs(x) > 1e7f) { ok = false; e = "Verschiebung kaputt"; break; }
                        }
                    }
                }
            }
            if (ok) okc++;
            else { fehler[e]++; if (fehler[e] <= 3) printf("  %s: %s\n", p.c_str(), e.c_str()); }
            if (n % 2000 == 0) { printf("... %d (%d ok)\n", n, okc); fflush(stdout); }
        }
        printf("%d von %d ok in %lu ms\n", okc, n, GetTickCount() - t1);
        for (auto& fe : fehler) printf("%6d x %s\n", fe.second, fe.first.c_str());
    } else if (cmd == L"tabelle" && argc > 2) {
        // nsdump tabelle <pfad> [filter] : Zeilen einer CC2-Tabelle (Spreadsheet)
        Package pk;
        if (!g.LoadPackage(Narrow(argv[2]), pk, err)) { printf("Fehler: %s\n", err.c_str()); return 3; }
        const Export& ex = pk.exports[0];
        Reader r(pk.data.data() + ex.dataOffset, (size_t)ex.serialSize, &pk);
        std::vector<Prop> ps;
        ReadProps(r, ps);
        if (r.b32()) r.skip(16);
        int32_t n = r.i32();
        std::string f = argc > 3 ? Lower(Narrow(argv[3])) : "";
        printf("%d Zeilen\n", n);
        for (int32_t i = 0; i < n && !r.bad; i++) {
            std::vector<Prop> z;
            if (!ReadProps(r, z)) { printf("Zeile %d kaputt\n", i); break; }
            std::string s; DumpProps(z, s, 1);
            if (f.empty() || Lower(s).find(f) != std::string::npos) printf("--- Zeile %d\n%s", i, s.c_str());
        }
    } else if (cmd == L"klassen" && argc > 2) {
        std::string f = Lower(Narrow(argv[2]));
        std::map<std::string, int> n;
        for (auto& fr : ar.AllFiles()) {
            if (Lower(fr.path).find(f) == std::string::npos || fr.path.size() < 7 || fr.path.compare(fr.path.size() - 7, 7, ".uasset")) continue;
            Package pk;
            if (!g.LoadHeader(fr.path, pk, err)) { n["FEHLER " + err]++; continue; }
            if (!pk.exports.empty()) n[pk.exports[0].className]++;
        }
        for (auto& kv : n) printf("%6d %s\n", kv.second, kv.first.c_str());
    } else { printf("unbekannter Befehl\n"); return 1; }
    return 0;
}

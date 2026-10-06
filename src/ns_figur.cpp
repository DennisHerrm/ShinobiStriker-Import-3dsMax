// ns_figur.cpp - Teile auf ein gemeinsames Skelett umrechnen
#include "ns_figur.h"
#include "ns_katalog.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace ns {

M34 M34::From(const float* q, const float* t, const float* s) {
    M34 r;
    double x = q[0], y = q[1], z = q[2], w = q[3];
    double n = std::sqrt(x * x + y * y + z * z + w * w);
    if (n > 1e-12) { x /= n; y /= n; z /= n; w /= n; }
    const double R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y),
                          2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
                          2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y) };
    for (int i = 0; i < 3; i++) {
        for (int c = 0; c < 3; c++) r.m[i * 4 + c] = R[i * 3 + c] * s[c];
        r.m[i * 4 + 3] = t[i];
    }
    return r;
}

M34 M34::operator*(const M34& b) const {
    M34 c;
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 4; k++) {
            double v = m[i * 4] * b.m[k] + m[i * 4 + 1] * b.m[4 + k] + m[i * 4 + 2] * b.m[8 + k];
            if (k == 3) v += m[i * 4 + 3];
            c.m[i * 4 + k] = v;
        }
    return c;
}

M34 M34::Inverse() const {
    const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) < 1e-20) det = 1e-20;
    const double I[9] = { (e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                          (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                          (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det };
    M34 r;
    for (int k = 0; k < 3; k++) {
        for (int j = 0; j < 3; j++) r.m[k * 4 + j] = I[k * 3 + j];
        r.m[k * 4 + 3] = -(I[k * 3] * m[3] + I[k * 3 + 1] * m[7] + I[k * 3 + 2] * m[11]);
    }
    return r;
}

void M34::Apply(const float* v, double* o) const {
    for (int k = 0; k < 3; k++) o[k] = m[k * 4] * v[0] + m[k * 4 + 1] * v[1] + m[k * 4 + 2] * v[2] + m[k * 4 + 3];
}
void M34::ApplyDir(const float* v, double* o) const {
    for (int k = 0; k < 3; k++) o[k] = m[k * 4] * v[0] + m[k * 4 + 1] * v[1] + m[k * 4 + 2] * v[2];
}

bool LadeTeile(Game& g, const std::vector<FigurTeil>& teile, std::vector<SkelMesh>& aus, std::string& log, std::string& fehler) {
    char b[700];
    for (const FigurTeil& tl : teile) {
        SkelMesh m;
        std::string e;
        if (!ReadSkeletalMesh(g, tl.mesh, m, e)) {
            snprintf(b, sizeof b, "FEHLER %s: %s\n", tl.mesh.c_str(), e.c_str());
            log += b;
            fehler += m.name.empty() ? tl.mesh : m.name;
            fehler += ": " + e + "; ";
            continue;
        }
        // Avatar-Kleidung: Farben fuer die Farbmaske (aus der Figur oder der Charaktereditor-Tabelle)
        if (tl.hatFarben) { m.hatFarben = true; memcpy(m.farben, tl.farben, 9); }
        else m.hatFarben = AvatarFarben(g, m.packageName, m.farben);
        // Materialien, die die Tabelle (Avatar) ersetzt
        for (size_t k = 0; k < tl.materialien.size() && k < m.materials.size(); ++k)
            if (!tl.materialien[k].empty()) {
                snprintf(b, sizeof b, "  Slot %zu: %s statt %s\n", k, tl.materialien[k].c_str(), m.materials[k].c_str());
                log += b;
                m.materials[k] = tl.materialien[k];
            }
        // Am Socket des Hauptmeshes (erstes Teil = CharacterMesh0) befestigt: Waffe in der Hand
        if (!tl.socket.empty()) {
            std::string knochen;
            float sr[4], sp[3], ss[3];
            bool gefunden = !aus.empty() && FindeSocket(g, aus.front(), tl.socket, knochen, sr, sp, ss);
            if (!gefunden && !aus.empty() && aus.front().skel.Find(tl.socket) >= 0) {
                // UE haengt auch direkt an einen Knochen (AttachToName = Knochenname)
                knochen = tl.socket;
                sr[0] = sr[1] = sr[2] = 0; sr[3] = 1;
                sp[0] = sp[1] = sp[2] = 0;
                ss[0] = ss[1] = ss[2] = 1;
                gefunden = true;
            }
            if (!gefunden) {
                snprintf(b, sizeof b, "Teil %s: Socket %s nicht gefunden - weggelassen\n", m.name.c_str(), tl.socket.c_str());
                log += b;
                fehler += m.name + ": socket " + tl.socket + " not found; ";
                continue;
            }
            const M34 lage = M34::From(sr, sp, ss) * M34::From(tl.relRot, tl.relPos, tl.relScale);
            m.hatAnhang = true;
            m.anhangKnochen = knochen;
            for (int c = 0; c < 12; ++c) m.anhangLage[c] = lage.m[c];
            snprintf(b, sizeof b, "  am Socket %s (Knochen %s)\n", tl.socket.c_str(), knochen.c_str());
            log += b;
        }
        snprintf(b, sizeof b, "Teil %s: %u Vertices, %zu Dreiecke, %zu Knochen, %zu Sektionen, %u UV, %u Einfluesse, Skelett %s\n",
                 m.name.c_str(), m.numVerts, m.indices.size() / 3, m.skel.bones.size(), m.sections.size(), m.numUV, m.maxInf,
                 m.skeletonPath.c_str());
        log += b;
        aus.push_back(std::move(m));
    }
    return !aus.empty();
}

std::vector<M34> GlobalPose(const RefSkeleton& sk) {
    std::vector<M34> g(sk.bones.size());
    for (size_t i = 0; i < sk.bones.size(); i++) {
        const Bone& b = sk.bones[i];
        M34 l = M34::From(b.rot, b.pos, b.scale);
        g[i] = (b.parent >= 0 && b.parent < (int)i) ? g[b.parent] * l : l;
    }
    return g;
}

bool BaueFigur(std::vector<SkelMesh>& teile, Figur& f, std::string& log) {
    if (teile.empty()) return false;
    // Hauptteil: dessen Wurzel bei keinem anderen Teil UNTER dessen Wurzel haengt
    // (Kopf/Haare beginnen bei spineC, das beim Koerper unter origin liegt).
    // Unter mehreren solchen gewinnt das mit den meisten Knochen.
    auto haengtUnter = [&](size_t a, size_t b) {
        if (teile[a].skel.bones.empty() || teile[b].skel.bones.empty()) return false;
        int i = teile[b].skel.Find(teile[a].skel.bones[0].name);
        return i > 0 && teile[b].skel.bones[i].parent >= 0;
    };
    size_t haupt = 0;
    int besterRang = -1;
    for (size_t i = 0; i < teile.size(); i++) {
        bool unter = false;
        for (size_t j = 0; j < teile.size() && !unter; j++) if (j != i && haengtUnter(i, j)) unter = true;
        int rang = (unter ? 0 : 1 << 20) + (int)teile[i].skel.bones.size();
        if (teile[i].hatAnhang) rang = -1;              // Waffen am Socket sind nie das Hauptteil
        if (rang > besterRang) { besterRang = rang; haupt = i; }
    }
    if (haupt != 0) std::swap(teile[0], teile[haupt]);
    f.skel = teile[0].skel;
    f.global = GlobalPose(f.skel);
    char b[512];
    for (size_t t = 0; t < teile.size(); t++) {
        SkelMesh& m = teile[t];
        if (!m.skeletonPath.empty() && std::find(f.skelette.begin(), f.skelette.end(), m.skeletonPath) == f.skelette.end())
            f.skelette.push_back(m.skeletonPath);
        if (t == 0) continue;
        const std::vector<M34> gTeil = GlobalPose(m.skel);
        std::vector<int> map(m.skel.bones.size(), -1);
        size_t gemeinsam = 0, neu = 0;
        // Am Socket befestigt (Waffe): eigene Knochen, die Wurzeln haengen am Socket-Knochen
        int anhang = -1;
        M34 anhangLage;
        if (m.hatAnhang) {
            anhang = f.skel.Find(m.anhangKnochen);
            for (int c = 0; c < 12; c++) anhangLage.m[c] = m.anhangLage[c];
            if (anhang < 0) {
                snprintf(b, sizeof b, "Teil %s: Socket-Knochen %s fehlt - an die Wurzel gehaengt\n", m.name.c_str(), m.anhangKnochen.c_str());
                log += b;
            }
        }
        for (size_t i = 0; i < m.skel.bones.size(); i++) {
            const Bone& bn = m.skel.bones[i];
            int idx = m.hatAnhang ? -1 : f.skel.Find(bn.name);
            if (idx >= 0) { map[i] = idx; gemeinsam++; continue; }
            Bone nb = bn;
            nb.parent = (bn.parent >= 0 && bn.parent < (int)i) ? map[bn.parent] : -1;
            M34 l = M34::From(nb.rot, nb.pos, nb.scale);
            if (nb.parent < 0 && m.hatAnhang) {
                l = anhangLage * l;
                nb.parent = anhang;
            }
            f.skel.bones.push_back(nb);
            f.global.push_back(nb.parent >= 0 ? f.global[nb.parent] * l : l);
            map[i] = (int)f.skel.bones.size() - 1;
            neu++;
        }
        // Skinning-Matrizen Teil-Bindepose -> Figur-Bindepose
        std::vector<M34> s(m.skel.bones.size());
        for (size_t i = 0; i < s.size(); i++) s[i] = f.global[map[i]] * gTeil[i].Inverse();
        double maxWeg = 0;
        for (uint32_t v = 0; v < m.numVerts; v++) {
            double p[3] = { 0, 0, 0 }, n[3] = { 0, 0, 0 }, ws = 0;
            for (uint32_t k = 0; k < m.maxInf; k++) {
                double w = m.infWeight[v * m.maxInf + k] / 255.0;
                if (w <= 0) continue;
                uint16_t bi = m.infBone[v * m.maxInf + k];
                if (bi >= s.size()) continue;
                double a[3];
                s[bi].Apply(&m.pos[v * 3], a);
                for (int c = 0; c < 3; c++) p[c] += w * a[c];
                if (!m.nrm.empty()) { s[bi].ApplyDir(&m.nrm[v * 3], a); for (int c = 0; c < 3; c++) n[c] += w * a[c]; }
                ws += w;
            }
            if (ws <= 0) continue;
            for (int c = 0; c < 3; c++) {
                p[c] /= ws;
                maxWeg = std::max(maxWeg, std::fabs(p[c] - m.pos[v * 3 + c]));
                m.pos[v * 3 + c] = (float)p[c];
            }
            if (!m.nrm.empty()) {
                double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (l > 1e-9) for (int c = 0; c < 3; c++) m.nrm[v * 3 + c] = (float)(n[c] / l);
            }
            for (uint32_t k = 0; k < m.maxInf; k++) {
                uint16_t& bi = m.infBone[v * m.maxInf + k];
                bi = bi < map.size() ? (uint16_t)map[bi] : 0;
            }
        }
        snprintf(b, sizeof b, "Teil %s: %zu gemeinsame Knochen, %zu neue, Vertices bis %.1f cm verschoben\n",
                 m.name.c_str(), gemeinsam, neu, maxWeg);
        log += b;
    }
    // Hauptteil: Einfluesse zeigen schon auf f.skel (gleiche Reihenfolge)
    snprintf(b, sizeof b, "Figur: %zu Knochen aus %zu Teilen (Hauptteil %s)\n", f.skel.bones.size(), teile.size(), teile[0].name.c_str());
    log += b;
    f.teile = std::move(teile);
    return true;
}

} // namespace ns

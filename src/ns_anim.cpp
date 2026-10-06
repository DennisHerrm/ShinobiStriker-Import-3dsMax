// ns_anim.cpp - AnimSequence (UE 4.16 cooked) mit der klassischen UE-Key-Kompression
//
// Nach den Eigenschaften (gezaehlt an Naruto_Run_Loop):
//   bHasGuid u32, RawDataGuid 16, StripFlags 2, bSerializeCompressedData u32,
//   KeyEncodingFormat u8, Translation-/Rotation-/ScaleCompressionFormat u8,
//   CompressedTrackOffsets (int32[]), CompressedScaleOffsets (int32[] + StripSize),
//   CompressedTrackToSkeletonMapTable (int32[]), CompressedCurveData (Eigenschaftsliste),
//   CompressedByteStream (int32 + Bytes), bUseRawDataOnly u32.
//
// Formate und Bit-Aufteilungen wie in der UE4-Engine (nachgelesen in UEViewer, nur die Fakten):
//   AKF_ConstantKeyLerp 0 / VariableKeyLerp 1: 4 Offsets je Spur, globale Formate
//   AKF_PerTrackCompression 2: 2 Offsets je Spur, jede Spur mit eigenem Kopf
//     Kopf u32: Format (4 Bit) | Komponentenmaske (4 Bit, 8 = Zeittabelle) | Keys (24 Bit)
//   Bei Skalenspuren liegen die Daten vor 4.23 hintereinander (Spur fuer Spur, T/R/S), die
//   Offsets meinen aber die Lage "wie geplant" - erst umsortieren (UE-Fehler, TransferPerTrackData).
#include "ns_anim.h"
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cmath>

namespace ns {

namespace {

enum Acf { ACF_None = 0, ACF_Float96NoW, ACF_Fixed48NoW, ACF_IntervalFixed32NoW, ACF_Fixed32NoW, ACF_Float32NoW, ACF_Identity };
const char* const kAcfName[] = { "None", "Float96NoW", "Fixed48NoW", "IntervalFixed32NoW", "Fixed32NoW", "Float32NoW", "Identity" };

struct Tr {
    std::string* t;
    void operator()(const char* fmt, ...) {
        if (!t) return;
        char b[512]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
        *t += b; *t += "\n";
    }
};

// Liest die Eigenschaften und springt an den Anfang der Binaerdaten.
bool Kopf(Game& g, const Package& pk, int e, AnimClip& a, Reader& r, std::vector<Prop>& ps) {
    const Export& ex = pk.exports[(size_t)e];
    a.name = ex.name; a.packageName = pk.name;
    if (!ReadProps(r, ps)) return false;
    if (const Prop* p = FindProp(ps, "Skeleton")) {
        std::string path = g.IndexToPath(pk, p->obj);
        size_t dot = path.find('.');
        a.skeletonPath = dot == std::string::npos ? path : path.substr(0, dot);
    }
    if (const Prop* p = FindProp(ps, "NumFrames")) a.numFrames = (int)p->num;
    if (const Prop* p = FindProp(ps, "SequenceLength")) a.length = (float)p->num;
    if (const Prop* p = FindProp(ps, "RateScale")) a.rateScale = (float)p->num;
    if (const Prop* p = FindProp(ps, "bEnableRootMotion")) a.rootMotion = p->num != 0;
    if (const Prop* p = FindProp(ps, "AdditiveAnimType")) if (p->str.find("AAT_None") == std::string::npos) a.additive = p->str;
    return true;
}

// Byte-Lesen im Datenstrom mit Grenzpruefung
struct Strom {
    const uint8_t* p; size_t n; size_t o = 0; bool bad = false;
    template<class T> T get() { T v{}; if (o + sizeof(T) > n) { bad = true; o = n; return v; } memcpy(&v, p + o, sizeof(T)); o += sizeof(T); return v; }
    void ausrichten() { o = (o + 3) & ~(size_t)3; if (o > n) { bad = true; o = n; } }
};

float W(float x, float y, float z) { const float w = 1.f - (x * x + y * y + z * z); return w > 0 ? std::sqrt(w) : 0.f; }

// Eine Spur (Keys + optionale Zeiten in Frames)
struct Spur {
    std::vector<float> v;        // 3 (Vektor) oder 4 (Quaternion) je Key
    std::vector<float> zeit;     // leer = gleichmaessig verteilt
    int n = 0;
};

void Zeiten(Strom& s, int numKeys, int numFrames, Spur& out) {
    out.zeit.clear();
    if (numKeys <= 1) return;
    out.zeit.resize((size_t)numKeys);
    for (int k = 0; k < numKeys; k++) out.zeit[(size_t)k] = numFrames < 256 ? (float)s.get<uint8_t>() : (float)s.get<uint16_t>();
}

// Quaternion-Key in einem Format (mask: Komponentenmaske bei PerTrack, sonst 7)
void QuatKey(Strom& s, int fmt, int mask, const float mins[3], const float ranges[3], float q[4]) {
    float x = 0, y = 0, z = 0;
    switch (fmt) {
    case ACF_None: x = s.get<float>(); y = s.get<float>(); z = s.get<float>(); q[0] = x; q[1] = y; q[2] = z; q[3] = s.get<float>(); return;
    case ACF_Float96NoW: x = s.get<float>(); y = s.get<float>(); z = s.get<float>(); break;
    case ACF_Fixed48NoW: {
        uint16_t X = 32767, Y = 32767, Z = 32767;
        if (mask & 1) X = s.get<uint16_t>();
        if (mask & 2) Y = s.get<uint16_t>();
        if (mask & 4) Z = s.get<uint16_t>();
        x = ((int)X - 32767) / 32767.f; y = ((int)Y - 32767) / 32767.f; z = ((int)Z - 32767) / 32767.f;
        break;
    }
    case ACF_Fixed32NoW: case ACF_IntervalFixed32NoW: {
        const uint32_t d = s.get<uint32_t>();
        // Z 10 Bit unten, Y 11 Bit, X 11 Bit oben
        x = (d >> 21) / 1023.f - 1.f; y = ((d >> 10) & 0x7FF) / 1023.f - 1.f; z = (d & 0x3FF) / 511.f - 1.f;
        if (fmt == ACF_IntervalFixed32NoW) { x = x * ranges[0] + mins[0]; y = y * ranges[1] + mins[1]; z = z * ranges[2] + mins[2]; }
        break;
    }
    case ACF_Float32NoW: {
        const uint32_t d = s.get<uint32_t>();
        const uint32_t X = d >> 21, Y = (d >> 10) & 0x7FF, Z = d & 0x3FF;
        // 11/11/10-Bit-Gleitkomma: 3 Bit Exponent (+123), Mantisse 7 bzw. 6 Bit, Vorzeichen oben
        uint32_t fx = ((((X >> 7) & 7) + 123) << 23) | (((X & 0x7F) | (32 * (X & 0xFFFFFC00u))) << 16);
        uint32_t fy = ((((Y >> 7) & 7) + 123) << 23) | (((Y & 0x7F) | (32 * (Y & 0xFFFFFC00u))) << 16);
        uint32_t fz = ((((Z >> 6) & 7) + 123) << 23) | (((Z & 0x3F) | (32 * (Z & 0xFFFFFE00u))) << 17);
        memcpy(&x, &fx, 4); memcpy(&y, &fy, 4); memcpy(&z, &fz, 4);
        break;
    }
    case ACF_Identity: default: q[0] = q[1] = q[2] = 0; q[3] = 1; return;
    }
    q[0] = x; q[1] = y; q[2] = z; q[3] = W(x, y, z);
}

// Vektor-Key (Verschiebung/Skalierung)
void VecKey(Strom& s, int fmt, int mask, bool perTrack, const float mins[3], const float ranges[3], float v[3]) {
    v[0] = v[1] = v[2] = 0;
    switch (fmt) {
    case ACF_None: case ACF_Float96NoW:
        if (perTrack && (mask & 7)) {
            if (mask & 1) v[0] = s.get<float>();
            if (mask & 2) v[1] = s.get<float>();
            if (mask & 4) v[2] = s.get<float>();
        } else { v[0] = s.get<float>(); v[1] = s.get<float>(); v[2] = s.get<float>(); }
        break;
    case ACF_Fixed48NoW:
        if (perTrack) {
            // Komponenten einzeln, Versatz 255, Faktor 1
            if (mask & 1) v[0] = (float)((int)s.get<uint16_t>() - 255);
            if (mask & 2) v[1] = (float)((int)s.get<uint16_t>() - 255);
            if (mask & 4) v[2] = (float)((int)s.get<uint16_t>() - 255);
        } else {
            const float k = 128.f / 32767.f;
            for (int c = 0; c < 3; c++) v[c] = ((int)s.get<uint16_t>() - 32767) * k;
        }
        break;
    case ACF_IntervalFixed32NoW: {
        const uint32_t d = s.get<uint32_t>();
        // X 10 Bit unten, Y 11 Bit, Z 11 Bit oben
        v[0] = ((d & 0x3FF) / 511.f - 1.f) * ranges[0] + mins[0];
        v[1] = (((d >> 10) & 0x7FF) / 1023.f - 1.f) * ranges[1] + mins[1];
        v[2] = ((d >> 21) / 1023.f - 1.f) * ranges[2] + mins[2];
        break;
    }
    case ACF_Identity: default: break;
    }
}

bool PerTrackSpur(Strom& s, bool quat, int numFrames, Spur& out, int& fmtOut) {
    const uint32_t kopf = s.get<uint32_t>();
    const int fmt = (int)(kopf >> 28), mask = (int)((kopf >> 24) & 0xF), n = (int)(kopf & 0xFFFFFF);
    fmtOut = fmt;
    if (fmt > ACF_Identity || n > 1000000 || s.bad) return false;
    float mins[3] = { 0, 0, 0 }, ranges[3] = { 0, 0, 0 };
    if (fmt == ACF_IntervalFixed32NoW)
        for (int c = 0; c < 3; c++) if (mask & (1 << c)) { mins[c] = s.get<float>(); ranges[c] = s.get<float>(); }
    const int dim = quat ? 4 : 3;
    out.n = n;
    out.v.resize((size_t)n * dim);
    for (int k = 0; k < n; k++) {
        if (quat) QuatKey(s, fmt, mask, mins, ranges, &out.v[(size_t)k * 4]);
        else VecKey(s, fmt, mask, true, mins, ranges, &out.v[(size_t)k * 3]);
    }
    s.ausrichten();
    if (mask & 8) Zeiten(s, n, numFrames, out);
    return !s.bad;
}

// Groesse einer PerTrack-Spur im Strom (fuer das Umsortieren)
size_t PerTrackGroesse(const uint8_t* d, size_t rest, int numFrames, size_t start) {
    if (rest < 4) return 0;
    uint32_t kopf; memcpy(&kopf, d, 4);
    const int fmt = (int)(kopf >> 28), mask = (int)((kopf >> 24) & 0xF), n = (int)(kopf & 0xFFFFFF);
    static const int komp[8] = { 3, 1, 1, 2, 1, 2, 2, 3 };
    const int nk = komp[mask & 7];
    size_t g = 4;
    if (fmt == ACF_IntervalFixed32NoW) g += (size_t)nk * 8;
    switch (fmt) {
    case ACF_None: case ACF_Float96NoW: g += (size_t)nk * 4 * n; break;
    case ACF_Fixed48NoW: g += (size_t)nk * 2 * n; break;
    case ACF_IntervalFixed32NoW: case ACF_Fixed32NoW: case ACF_Float32NoW: g += (size_t)4 * n; break;
    default: break;
    }
    auto ausr = [&](size_t x) { return ((start + x + 3) & ~(size_t)3) - start; };
    if (mask & 8) { g = ausr(g); g += (size_t)(numFrames < 256 ? 1 : 2) * n; }
    return ausr(g);
}

// Key-Position fuer Frame f (0..ausgabeFrames-1) - gleichmaessig ueber die Laenge verteilt
void Abtasten(const Spur& sp, int dim, int numFrames, double pos, float* out) {
    if (sp.n <= 0) return;
    if (sp.n == 1) { memcpy(out, sp.v.data(), dim * 4); return; }
    int a = 0, b = 0;
    double t = 0;
    if (sp.zeit.empty()) {
        // Keys gleichmaessig ueber die ganze Laenge: pos in [0, numFrames-1] -> Keyraum [0, n-1]
        const double kp = numFrames > 1 ? pos * (sp.n - 1) / (numFrames - 1) : 0.0;
        a = std::min(sp.n - 1, std::max(0, (int)std::floor(kp)));
        b = std::min(sp.n - 1, a + 1);
        t = kp - a;
    } else {
        // Zeittabelle in Frames
        b = 0;
        while (b < sp.n && sp.zeit[(size_t)b] < pos) b++;
        if (b == 0) a = 0;
        else if (b >= sp.n) { a = b = sp.n - 1; }
        else {
            a = b - 1;
            const double d = sp.zeit[(size_t)b] - sp.zeit[(size_t)a];
            t = d > 0 ? (pos - sp.zeit[(size_t)a]) / d : 0.0;
        }
    }
    const float* A = &sp.v[(size_t)a * dim];
    const float* B = &sp.v[(size_t)b * dim];
    if (dim == 4) {
        // Nlerp mit kuerzestem Weg (wie UE)
        const float dot = A[0] * B[0] + A[1] * B[1] + A[2] * B[2] + A[3] * B[3];
        const float sg = dot < 0 ? -1.f : 1.f;
        float l = 0;
        for (int c = 0; c < 4; c++) { out[c] = (float)(A[c] * (1 - t) + sg * B[c] * t); l += out[c] * out[c]; }
        l = std::sqrt(l);
        if (l > 1e-8f) for (int c = 0; c < 4; c++) out[c] /= l;
    } else for (int c = 0; c < dim; c++) out[c] = (float)(A[c] * (1 - t) + B[c] * t);
}

} // namespace

bool ReadAnimInfo(Game& g, const Package& pk, int e, AnimClip& a) {
    const Export& ex = pk.exports[(size_t)e];
    Reader r(pk.data.data() + ex.dataOffset, (size_t)ex.serialSize, &pk);
    std::vector<Prop> ps;
    return Kopf(g, pk, e, a, r, ps);
}

bool ReadAnimSequence(Game& g, const std::string& file, AnimClip& a, std::string& err, const RefSkeleton* skel, std::string* trace) {
    Tr tr{ trace };
    Package pk;
    if (!g.LoadPackage(file, pk, err)) return false;
    int e = pk.FindExport("AnimSequence");
    if (e < 0) { err = "kein AnimSequence-Export"; return false; }
    const Export& ex = pk.exports[(size_t)e];
    Reader r(pk.data.data() + ex.dataOffset, (size_t)ex.serialSize, &pk);
    std::vector<Prop> ps;
    if (!Kopf(g, pk, e, a, r, ps)) { err = "Eigenschaften kaputt"; return false; }
    std::vector<int32_t> trackToSkel;
    if (const Prop* p = FindProp(ps, "TrackToSkeletonMapTable"))
        for (const Prop& k : p->kids) { const Prop* b = k.Get("BoneTreeIndex"); trackToSkel.push_back(b ? (int32_t)b->num : -1); }
    tr("Eigenschaften bis @%zx, Skelett %s, %d Frames, %.4f s, RateScale %.2f, %zu Spuren, additiv '%s'", r.o, a.skeletonPath.c_str(),
       a.numFrames, a.length, a.rateScale, trackToSkel.size(), a.additive.c_str());
    if (r.b32()) r.skip(16);            // Objekt-GUID
    r.skip(16);                         // RawDataGuid
    uint16_t sf = r.u16();
    bool komprimiert = r.b32();
    tr("strip %04x, compressed %d @%zx", sf, komprimiert, r.o);
    if (!komprimiert) { err = "keine komprimierten Daten"; return false; }
    const int keyFmt = r.u8(), transFmt = r.u8(), rotFmt = r.u8(), scaleFmt = r.u8();
    int32_t nOff = r.i32();
    if (nOff < 0 || !r.ok((size_t)nOff * 4)) { err = "Spur-Offsets kaputt"; return false; }
    std::vector<int32_t> off((size_t)nOff);
    for (auto& v : off) v = r.i32();
    int32_t nSc = r.i32();
    if (nSc < 0 || !r.ok((size_t)nSc * 4)) { err = "Skalen-Offsets kaputt"; return false; }
    std::vector<int32_t> scOff((size_t)nSc);
    for (auto& v : scOff) v = r.i32();
    int32_t scStrip = r.i32();
    int32_t nMap = r.i32();
    if (nMap < 0 || !r.ok((size_t)nMap * 4)) { err = "Spurtabelle kaputt"; return false; }
    std::vector<int32_t> compMap((size_t)nMap);
    for (auto& v : compMap) v = r.i32();
    std::vector<Prop> kurven;
    ReadProps(r, kurven);
    int32_t nBytes = r.i32();
    tr("Key %d, Trans %s, Rot %s, Scale %s, %d Offsets, %d Skalen (Strip %d), %d Map, %zu Kurven-Eigenschaften, %d Bytes @%zx",
       keyFmt, transFmt <= 6 ? kAcfName[transFmt] : "?", rotFmt <= 6 ? kAcfName[rotFmt] : "?", scaleFmt <= 6 ? kAcfName[scaleFmt] : "?",
       nOff, nSc, scStrip, nMap, kurven.size(), nBytes, r.o);
    if (nBytes < 0 || !r.ok((size_t)nBytes)) { err = "Bytestrom kaputt"; return false; }
    std::vector<uint8_t> strom(r.p + r.o, r.p + r.o + nBytes);
    r.skip((size_t)nBytes);
    if (!compMap.empty()) trackToSkel = compMap;
    const int ntr = (int)trackToSkel.size();
    const int jeSpur = keyFmt == 2 ? 2 : 4;
    if (ntr == 0 || (int)off.size() != ntr * jeSpur) { err = "Offsets passen nicht zur Spuranzahl"; return false; }
    if (keyFmt > 2) { err = "Key-Format " + std::to_string(keyFmt) + " unbekannt"; return false; }
    const int numFrames = std::max(1, a.numFrames);

    // PerTrack mit Skalen: Daten liegen Spur fuer Spur hintereinander -> an die Offsets umsortieren
    if (keyFmt == 2 && scStrip > 0 && !scOff.empty()) {
        std::vector<uint8_t> neu(strom.size(), 0);
        size_t q = 0;
        for (int i = 0; i < ntr; i++)
            for (int art = 0; art < 3; art++) {
                const int32_t o = art == 0 ? off[(size_t)i * 2] : art == 1 ? off[(size_t)i * 2 + 1]
                                                                            : ((size_t)i * scStrip < scOff.size() ? scOff[(size_t)i * scStrip] : -1);
                if (o < 0) continue;
                const size_t gr = PerTrackGroesse(strom.data() + q, strom.size() - q, numFrames, (size_t)o);
                if (gr == 0 || q + gr > strom.size() || (size_t)o + gr > neu.size()) { err = "Umsortieren fehlgeschlagen"; return false; }
                memcpy(neu.data() + o, strom.data() + q, gr);
                q += gr;
            }
        strom.swap(neu);
        tr("PerTrack mit Skalen umsortiert");
    }

    RefSkeleton own;
    if (!skel) {
        if (!ReadSkeleton(g, a.skeletonPath, own, err)) return false;
        skel = &own;
    }
    // Ausgabe: gleichmaessig mit 30 Bildern/s ueber die Laenge (wie das Spiel abspielt)
    a.rate = 30.f;
    const int aus = a.length > 0 ? std::max(1, (int)std::lround(a.length * a.rate) + 1) : numFrames;
    a.tracks.resize((size_t)ntr);
    Strom s{ strom.data(), strom.size() };
    int formate[8] = {};
    for (int i = 0; i < ntr; i++) {
        AnimTrack& t = a.tracks[(size_t)i];
        t.skelIndex = trackToSkel[(size_t)i];
        const bool gueltig = t.skelIndex >= 0 && t.skelIndex < (int)skel->bones.size();
        t.bone = gueltig ? skel->bones[(size_t)t.skelIndex].name : "";
        t.wurzel = gueltig && skel->bones[(size_t)t.skelIndex].parent < 0;
        Spur pos, rot, scl;
        bool hatScl = false;
        if (keyFmt == 2) {
            const int32_t to = off[(size_t)i * 2], ro = off[(size_t)i * 2 + 1];
            const int32_t so = (scStrip > 0 && (size_t)i * scStrip < scOff.size()) ? scOff[(size_t)i * scStrip] : -1;
            int f = 0;
            if (to >= 0) { s.o = (size_t)to; if (!PerTrackSpur(s, false, numFrames, pos, f)) { err = "Translation kaputt (Spur " + std::to_string(i) + ")"; return false; } formate[f & 7]++; }
            else { pos.n = 1; pos.v = { 0, 0, 0 }; }
            if (ro >= 0) { s.o = (size_t)ro; if (!PerTrackSpur(s, true, numFrames, rot, f)) { err = "Rotation kaputt (Spur " + std::to_string(i) + ")"; return false; } formate[f & 7]++; }
            else { rot.n = 1; rot.v = { 0, 0, 0, 1 }; }
            if (so >= 0) { s.o = (size_t)so; if (!PerTrackSpur(s, false, numFrames, scl, f)) { err = "Skalierung kaputt (Spur " + std::to_string(i) + ")"; return false; } hatScl = true; }
        } else {
            const int32_t to = off[(size_t)i * 4], tn = off[(size_t)i * 4 + 1], ro = off[(size_t)i * 4 + 2], rn = off[(size_t)i * 4 + 3];
            const float null3[3] = { 0, 0, 0 };
            float mins[3], ranges[3];
            // Verschiebung
            if (tn > 0) {
                s.o = (size_t)to;
                const int fmt = tn == 1 ? ACF_None : transFmt;
                if (fmt == ACF_IntervalFixed32NoW) { for (auto& v : mins) v = s.get<float>(); for (auto& v : ranges) v = s.get<float>(); }
                pos.n = tn; pos.v.resize((size_t)tn * 3);
                for (int k = 0; k < tn; k++) VecKey(s, fmt, 7, false, fmt == ACF_IntervalFixed32NoW ? mins : null3, ranges, &pos.v[(size_t)k * 3]);
                s.ausrichten();
                if (keyFmt == 1) Zeiten(s, tn, numFrames, pos);
            } else { pos.n = 1; pos.v = { 0, 0, 0 }; }
            // Drehung
            if (rn > 0) {
                s.o = (size_t)ro;
                const int fmt = rn == 1 ? ACF_Float96NoW : rotFmt;
                if (fmt == ACF_IntervalFixed32NoW) { for (auto& v : mins) v = s.get<float>(); for (auto& v : ranges) v = s.get<float>(); }
                rot.n = rn; rot.v.resize((size_t)rn * 4);
                for (int k = 0; k < rn; k++) QuatKey(s, fmt, 7, mins, ranges, &rot.v[(size_t)k * 4]);
                if (keyFmt == 1) { s.ausrichten(); Zeiten(s, rn, numFrames, rot); }
            } else { rot.n = 1; rot.v = { 0, 0, 0, 1 }; }
            // Skalierung (StripSize 2: Offset, Keys)
            if (scStrip >= 2 && (size_t)i * scStrip + 1 < scOff.size() && scOff[(size_t)i * scStrip + 1] > 0) {
                const int32_t so = scOff[(size_t)i * scStrip], sn = scOff[(size_t)i * scStrip + 1];
                s.o = (size_t)so;
                const int fmt = sn == 1 ? ACF_None : scaleFmt;
                if (fmt == ACF_IntervalFixed32NoW) { for (auto& v : mins) v = s.get<float>(); for (auto& v : ranges) v = s.get<float>(); }
                scl.n = sn; scl.v.resize((size_t)sn * 3);
                for (int k = 0; k < sn; k++) VecKey(s, fmt, 7, false, fmt == ACF_IntervalFixed32NoW ? mins : null3, ranges, &scl.v[(size_t)k * 3]);
                s.ausrichten();
                if (keyFmt == 1) Zeiten(s, sn, numFrames, scl);
                hatScl = true;
            }
            if (s.bad) { err = "Keys ausserhalb des Datenstroms (Spur " + std::to_string(i) + ")"; return false; }
        }
        t.rot.resize((size_t)aus * 4);
        t.pos.resize((size_t)aus * 3);
        t.scl.assign((size_t)aus * 3, 1.f);
        for (int f = 0; f < aus; f++) {
            // Bild f -> Position im Key-Raster des Clips (0..numFrames-1)
            const double p = aus > 1 ? (double)f * (numFrames - 1) / (aus - 1) : 0.0;
            Abtasten(rot, 4, numFrames, p, &t.rot[(size_t)f * 4]);
            Abtasten(pos, 3, numFrames, p, &t.pos[(size_t)f * 3]);
            if (hatScl) Abtasten(scl, 3, numFrames, p, &t.scl[(size_t)f * 3]);
        }
        if (i < 6) tr("  Spur %d %s: %d T-Keys, %d R-Keys%s", i, t.bone.c_str(), pos.n, rot.n, hatScl ? ", Skalierung" : "");
    }
    a.numFrames = aus;
    tr("Formate je Spur: Float96 %d, Fixed48 %d, Interval32 %d, Fixed32 %d, Float32 %d, Identity %d -> %d Bilder @ %.0f Hz",
       formate[1] + formate[0], formate[2], formate[3], formate[4], formate[5], formate[6], aus, a.rate);
    return true;
}

} // namespace ns

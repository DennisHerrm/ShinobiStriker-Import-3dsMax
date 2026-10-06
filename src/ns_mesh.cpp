// ns_mesh.cpp - SkeletalMesh (UE 4.16 cooked, Shinobi Striker)
//
// Aufbau nach den Eigenschaften (an den Bytes von SK_CHR_Naruto nachgezaehlt):
//   bHasGuid u32, StripFlags 2, ImportedBounds 28, Materialien (Index, Slotname, UVChannelData 24),
//   RefSkeleton, LOD-Anzahl, je LOD FStaticLODModel:
//     StripFlags 2, Sektionen, Indizes (MultiSize), ActiveBones, Size, NumVertices, RequiredBones,
//     MeshToImportVertexMap, MaxImportVertex, NumTexCoords, GPU-Skin-Vertexpuffer,
//     SkinWeight-Puffer, [Farben], [Nachbarschaftsindizes], [Cloth]
#include "ns_mesh.h"
#include "ns_bp.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdarg>

namespace ns {

int RefSkeleton::Find(const std::string& n) const {
    for (size_t i = 0; i < bones.size(); i++) if (_stricmp(bones[i].name.c_str(), n.c_str()) == 0) return (int)i;
    return -1;
}

bool ReadRefSkeleton(Reader& r, RefSkeleton& sk) {
    int32_t n = r.i32();
    if (n < 0 || n > 65535) return false;
    sk.bones.resize((size_t)n);
    for (auto& b : sk.bones) { b.name = r.fname(); b.parent = r.i32(); }
    int32_t nt = r.i32();
    if (nt != n) return false;
    for (auto& b : sk.bones) {
        for (int k = 0; k < 4; k++) b.rot[k] = r.f32();
        for (int k = 0; k < 3; k++) b.pos[k] = r.f32();
        for (int k = 0; k < 3; k++) b.scale[k] = r.f32();
    }
    int32_t nm = r.i32();
    if (nm < 0 || nm > 65535) return false;
    r.skip((size_t)nm * 12);
    return !r.bad;
}

namespace {
struct Tr {
    std::string* t;
    void operator()(const char* fmt, ...) {
        if (!t) return;
        char b[512]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
        *t += b; *t += "\n";
    }
};

// BulkSerialize: int32 Elementgroesse, int32 Anzahl, Daten
bool BulkArray(Reader& r, std::vector<uint8_t>& out, uint32_t& elemSize, uint32_t& count) {
    elemSize = r.u32(); count = r.u32();
    uint64_t sz = (uint64_t)elemSize * count;
    if (r.bad || sz > 0x40000000ull || !r.ok((size_t)sz)) { r.bad = true; return false; }
    out.assign(r.p + r.o, r.p + r.o + sz); r.o += (size_t)sz;
    return true;
}

float Half(uint16_t h) {
    uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    float v;
    if (e == 0) v = std::ldexp((float)m, -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = std::ldexp((float)(m | 1024), (int)e - 25);
    return s ? -v : v;
}

// FMultiSizeIndexContainer: DataTypeSize u8, dann BulkSerialize
bool Indizes(Reader& r, std::vector<uint32_t>& out) {
    r.u8();
    std::vector<uint8_t> raw; uint32_t es, cnt;
    if (!BulkArray(r, raw, es, cnt)) return false;
    if (es != 2 && es != 4 && cnt) return false;
    out.resize(cnt);
    for (uint32_t i = 0; i < cnt; i++) {
        if (es == 2) { uint16_t v; memcpy(&v, &raw[(size_t)i * 2], 2); out[i] = v; }
        else memcpy(&out[i], &raw[(size_t)i * 4], 4);
    }
    return true;
}

// Ein FSkelMeshSection (4.16, mit APEX-Cloth-Feldern)
bool Sektion(Reader& r, MeshSection& s, Tr& tr, int i) {
    size_t st = r.o;
    uint16_t sf = r.u16();
    s.material = r.u16();
    s.baseIndex = r.u32();
    s.numTris = r.u32();
    uint8_t sort = r.u8();
    s.disabled = r.b32();
    int16_t clothSec = (int16_t)r.u16();
    r.u8();                                  // bEnableClothLOD_DEPRECATED
    bool recomp = r.b32();
    bool shadow = r.b32();
    s.baseVertex = r.u32();
    int32_t nbm = r.i32();
    if (nbm < 0 || nbm > 65535 || !r.ok((size_t)nbm * 2)) return false;
    s.boneMap.resize((size_t)nbm);
    for (auto& b : s.boneMap) b = r.u16();
    s.numVerts = r.u32();
    uint32_t maxInf = r.u32();
    int32_t nApex = r.i32(); r.skip((size_t)std::max(0, nApex) * 64);   // FApexClothPhysToRenderVertData
    int32_t nPv = r.i32(); r.skip((size_t)std::max(0, nPv) * 12);       // PhysicalMeshVertices
    int32_t nPn = r.i32(); r.skip((size_t)std::max(0, nPn) * 12);       // PhysicalMeshNormals
    int16_t clothAsset = (int16_t)r.u16();
    r.skip(16 + 4);                          // ClothingData (Guid, LodIndex)
    tr("  Sektion %d @%zx: strip %04x mat %d base %u tris %u sort %u disabled %d clothSec %d recomp %d shadow %d bv %u bones %d verts %u maxInf %u apex %d/%d/%d clothAsset %d -> @%zx",
       i, st, sf, s.material, s.baseIndex, s.numTris, sort, s.disabled, clothSec, recomp, shadow, s.baseVertex, nbm, s.numVerts, maxInf,
       nApex, nPv, nPn, clothAsset, r.o);
    return !r.bad;
}

std::string OhnePunkt(const std::string& p) {
    size_t d = p.find('.');
    return d == std::string::npos ? p : p.substr(0, d);
}
} // namespace

bool FindeSocket(Game& g, const SkelMesh& m, const std::string& name, std::string& knochen, float rot[4], float pos[3], float scale[3]) {
    for (const std::string& paket : { m.packageName, m.skeletonPath }) {
        if (paket.empty()) continue;
        Package pk;
        std::string e;
        if (!g.LoadPackage(paket, pk, e)) continue;
        for (const Export& ex : pk.exports) {
            if (ex.className != "SkeletalMeshSocket") continue;
            Reader r(pk.data.data() + ex.dataOffset, (size_t)ex.serialSize, &pk);
            std::vector<Prop> ps;
            if (!ReadProps(r, ps)) continue;
            const Prop* sn = FindProp(ps, "SocketName");
            if (!sn || _stricmp(sn->str.c_str(), name.c_str()) != 0) continue;
            const Prop* bn = FindProp(ps, "BoneName");
            if (!bn) continue;
            knochen = bn->str;
            rot[0] = rot[1] = rot[2] = 0; rot[3] = 1;
            pos[0] = pos[1] = pos[2] = 0;
            scale[0] = scale[1] = scale[2] = 1;
            if (const Prop* p = FindProp(ps, "RelativeLocation")) if (p->vec.size() >= 3) for (int c = 0; c < 3; c++) pos[c] = (float)p->vec[c];
            if (const Prop* p = FindProp(ps, "RelativeRotation")) if (p->vec.size() >= 3) RotatorZuQuat(p->vec.data(), rot);
            if (const Prop* p = FindProp(ps, "RelativeScale")) if (p->vec.size() >= 3) for (int c = 0; c < 3; c++) scale[c] = (float)p->vec[c];
            return true;
        }
    }
    return false;
}

bool ReadSkeleton(Game& g, const std::string& fileOrName, RefSkeleton& sk, std::string& err) {
    Package pk;
    if (!g.LoadPackage(fileOrName, pk, err)) return false;
    int e = pk.FindExport("Skeleton");
    if (e < 0) { err = "kein Skeleton-Export"; return false; }
    const Export& ex = pk.exports[(size_t)e];
    Reader r(pk.data.data() + ex.dataOffset, (size_t)ex.serialSize, &pk);
    std::vector<Prop> ps;
    if (!ReadProps(r, ps)) { err = "Skeleton-Eigenschaften kaputt"; return false; }
    if (r.b32()) r.skip(16);
    if (!ReadRefSkeleton(r, sk)) { err = "RefSkeleton kaputt"; return false; }
    return true;
}

bool ReadSkeletalMesh(Game& g, const std::string& file, SkelMesh& m, std::string& err, std::string* trace) {
    Tr tr{ trace };
    Package pk;
    if (!g.LoadPackage(file, pk, err)) return false;
    int e = pk.FindExport("SkeletalMesh");
    if (e < 0) { err = "kein SkeletalMesh-Export"; return false; }
    const Export& ex = pk.exports[(size_t)e];
    m.name = ex.name; m.packageName = pk.name;
    Reader r(pk.data.data() + ex.dataOffset, (size_t)ex.serialSize, &pk);
    std::vector<Prop> ps;
    if (!ReadProps(r, ps)) { err = "Eigenschaften kaputt"; return false; }
    if (const Prop* p = FindProp(ps, "Skeleton")) m.skeletonPath = OhnePunkt(g.IndexToPath(pk, p->obj));
    bool hasColors = false;
    if (const Prop* p = FindProp(ps, "bHasVertexColors")) hasColors = p->num != 0;
    tr("Eigenschaften bis @%zx, Skeleton %s, Farben %d", r.o, m.skeletonPath.c_str(), hasColors);
    if (r.b32()) r.skip(16);
    r.u16();                // StripFlags
    r.skip(28);             // ImportedBounds
    int32_t nmat = r.i32();
    if (nmat < 0 || nmat > 1000) { err = "Materialanzahl kaputt"; return false; }
    for (int32_t i = 0; i < nmat; i++) {
        int32_t mi = r.i32();
        std::string slot = r.fname();
        r.skip(24);                   // UVChannelData
        m.materials.push_back(OhnePunkt(g.IndexToPath(pk, mi)));
        m.slotNames.push_back(slot);
    }
    tr("%d Materialien -> @%zx", nmat, r.o);
    if (!ReadRefSkeleton(r, m.skel)) { err = "RefSkeleton kaputt"; return false; }
    tr("%zu Knochen -> @%zx", m.skel.bones.size(), r.o);
    int32_t nlod = r.i32();
    m.lodCount = nlod;
    tr("%d LODs @%zx", nlod, r.o);
    if (nlod <= 0 || nlod > 16) { err = "LOD-Anzahl kaputt"; return false; }

    // LOD 0
    uint16_t sf = r.u16();
    int32_t nsec = r.i32();
    tr("LOD0: strip %04x, %d Sektionen @%zx", sf, nsec, r.o);
    if (nsec < 0 || nsec > 1000) { err = "Sektionsanzahl kaputt"; return false; }
    for (int32_t i = 0; i < nsec; i++) {
        MeshSection s;
        if (!Sektion(r, s, tr, i)) { err = "Sektion kaputt"; return false; }
        m.sections.push_back(std::move(s));
    }
    if (!Indizes(r, m.indices)) { err = "Indizes kaputt"; return false; }
    tr("%zu Indizes -> @%zx", m.indices.size(), r.o);
    int32_t nact = r.i32(); r.skip((size_t)std::max(0, nact) * 2);
    int32_t size = r.i32();
    uint32_t numVerts = r.u32();
    int32_t nreq = r.i32(); r.skip((size_t)std::max(0, nreq) * 2);
    int32_t nimp = r.i32(); r.skip((size_t)std::max(0, nimp) * 4);
    int32_t maxImp = r.i32();
    uint32_t numUV = r.u32();
    tr("%d ActiveBones, Size %d, %u Verts, %d RequiredBones, ImportMap %d, MaxImport %d, %u UV -> @%zx",
       nact, size, numVerts, nreq, nimp, maxImp, numUV, r.o);
    if (r.bad || numUV == 0 || numUV > 8 || numVerts == 0) { err = "LOD-Kopf kaputt"; return false; }

    // GPU-Skin-Vertexpuffer
    uint16_t vsf = r.u16();
    uint32_t vNumUV = r.u32();
    bool fullUV = r.b32();
    float ext[3], ori[3];
    for (auto& f : ext) f = r.f32();
    for (auto& f : ori) f = r.f32();
    std::vector<uint8_t> raw; uint32_t es, cnt;
    if (!BulkArray(r, raw, es, cnt)) { err = "Vertexpuffer kaputt"; return false; }
    tr("VertexBuffer: strip %04x, %u UV, fullUV %d, ext %.1f %.1f %.1f, origin %.1f %.1f %.1f, elem %u x %u -> @%zx",
       vsf, vNumUV, fullUV, ext[0], ext[1], ext[2], ori[0], ori[1], ori[2], es, cnt, r.o);
    const uint32_t uvBytes = (fullUV ? 8u : 4u) * numUV;
    if (cnt != numVerts || es != 8 + 12 + uvBytes) { err = "Vertexformat unbekannt (elem " + std::to_string(es) + ")"; return false; }
    m.numVerts = cnt; m.numUV = numUV;
    m.pos.resize((size_t)cnt * 3);
    m.nrm.resize((size_t)cnt * 3);
    m.uv.resize((size_t)cnt * numUV * 2);
    for (uint32_t v = 0; v < cnt; v++) {
        const uint8_t* q = &raw[(size_t)v * es];
        // TangentX (4), TangentZ (4, FPackedNormal ohne Vorzeichen: x/127.5-1), Position, UVs
        float n[3];
        for (int k = 0; k < 3; k++) n[k] = q[4 + k] / 127.5f - 1.f;
        float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l > 1e-6f) for (int k = 0; k < 3; k++) n[k] /= l;
        memcpy(&m.nrm[(size_t)v * 3], n, 12);
        memcpy(&m.pos[(size_t)v * 3], q + 8, 12);
        for (uint32_t k = 0; k < numUV * 2; k++) {
            float f;
            if (fullUV) memcpy(&f, q + 20 + k * 4, 4);
            else { uint16_t h; memcpy(&h, q + 20 + k * 2, 2); f = Half(h); }
            m.uv[((size_t)v * numUV) * 2 + k] = f;
        }
    }
    // Skin-Gewichte (eigener Puffer)
    uint16_t wsf = r.u16();
    bool extra = r.b32();
    uint32_t wNum = r.u32();
    if (!BulkArray(r, raw, es, cnt)) { err = "Skinpuffer kaputt"; return false; }
    tr("Skin: strip %04x, extra %d, %u Verts, elem %u x %u -> @%zx", wsf, extra, wNum, es, cnt, r.o);
    const uint32_t maxInf = extra ? 8u : 4u;
    if (cnt != m.numVerts || es != maxInf * 2) { err = "Skinformat unbekannt"; return false; }
    m.maxInf = maxInf;
    m.infBone.assign((size_t)m.numVerts * maxInf, 0);
    m.infWeight.assign((size_t)m.numVerts * maxInf, 0);
    for (uint32_t v = 0; v < m.numVerts; v++)
        for (uint32_t k = 0; k < maxInf; k++) {
            m.infBone[(size_t)v * maxInf + k] = raw[(size_t)v * es + k];
            m.infWeight[(size_t)v * maxInf + k] = raw[(size_t)v * es + maxInf + k];
        }
    if (hasColors) {
        uint16_t csf = r.u16(); uint32_t stride = r.u32(), cn = r.u32();
        tr("Farben: strip %04x stride %u n %u @%zx", csf, stride, cn, r.o);
        if (cn > 0) {
            if (!BulkArray(r, raw, es, cnt)) { err = "Farbpuffer kaputt"; return false; }
            if (es == 4 && cnt == m.numVerts) { m.color.resize(cnt); memcpy(m.color.data(), raw.data(), raw.size()); }
        }
    }
    tr("Ende LOD0-Puffer @%zx", r.o);
    // Einflussknochen von Sektions-BoneMap auf Skelett umrechnen
    for (auto& s : m.sections) {
        for (uint32_t v = s.baseVertex; v < s.baseVertex + s.numVerts && v < m.numVerts; v++)
            for (uint32_t k = 0; k < m.maxInf; k++) {
                uint16_t& b = m.infBone[(size_t)v * m.maxInf + k];
                b = b < s.boneMap.size() ? s.boneMap[b] : 0;
            }
    }
    m.lodUsed = 0;
    return true;
}

} // namespace ns

# objbild.py - OBJ schnell als Bild (Vorder- + Seitenansicht, flach schattiert) zur Sichtpruefung
import sys
import numpy as np
from PIL import Image, ImageDraw

def lade(p):
    v, f = [], []
    for line in open(p):
        if line.startswith('v '):
            v.append([float(x) for x in line.split()[1:4]])
        elif line.startswith('f '):
            f.append([int(t.split('/')[0]) - 1 for t in line.split()[1:4]])
    return np.array(v), np.array(f)

def bild(v, f, achse, groesse=700):
    # achse 0: Blick entlang -Y (vorn), 1: entlang +X (Seite); OBJ hier: x, y=oben, z
    if achse == 0: P = v[:, [0, 1]]; D = v[:, 2]
    else: P = v[:, [2, 1]]; D = -v[:, 0]
    mn, mx = P.min(0), P.max(0)
    s = (groesse - 20) / max(mx - mn)
    Q = (P - mn) * s + 10
    Q[:, 1] = groesse - Q[:, 1]
    tri = v[f]
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    n /= np.linalg.norm(n, axis=1)[:, None] + 1e-9
    licht = np.array([0.3, 0.5, 0.8]) if achse == 0 else np.array([0.8, 0.5, 0.3])
    hell = np.abs(n @ (licht / np.linalg.norm(licht)))
    tiefe = D[f].mean(1)
    img = Image.new('RGB', (groesse, groesse), (40, 40, 48))
    d = ImageDraw.Draw(img)
    for i in np.argsort(tiefe):
        c = int(60 + 190 * hell[i])
        d.polygon([tuple(Q[k]) for k in f[i]], fill=(c, c, c))
    return img

v, f = lade(sys.argv[1])
a, b = bild(v, f, 0), bild(v, f, 1)
out = Image.new('RGB', (1400, 700)); out.paste(a, (0, 0)); out.paste(b, (700, 0))
out.save(sys.argv[2])
print(len(v), 'Verts', len(f), 'Tris')

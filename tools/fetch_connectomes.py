#!/usr/bin/env python3
"""Download real connectomes, so Khora's graph can be compared against measured
nervous systems rather than against numbers copied out of papers.

WHY THIS EXISTS. The three sources the operator pointed at do not hand over a
connectivity matrix:

  humanconnectome.org  a portal; the data is behind ConnectomeDB with
                       registration and a data use agreement, and it is raw and
                       preprocessed IMAGING, not connectivity.
  openneuro.org        raw BIDS imaging. Turning any of it into a connectome
                       means running a full preprocessing pipeline for hours per
                       subject to arrive at a matrix somebody already published.
  FreeSurfer           surface reconstruction and labelling. It ships three
                       cortical parcellations (Desikan-Killiany, Destrieux,
                       DKT40) carrying ANATOMICAL LABELS ONLY -- no connectivity.

Connectivity lives in connectome repositories instead, and these are free,
small, and directly downloadable. What arrives is the actual wiring of six
nervous systems, including the only complete one ever mapped.

    python tools/fetch_connectomes.py     # writes data/connectomes/

WHAT THIS IS NOT FOR. Not for copying connection probabilities into Khora. A
connectome gives topology and nothing else -- Shiu et al. 2024 had the COMPLETE
fly wiring diagram and still had to guess every synaptic sign and weight.
Structure constrains a hypothesis space; the objective and the learning rule do
the work. Copying a matrix in would be decoration.

Using it as a MEASURING STICK is a different thing and a legitimate one, and it
only works if both sides go through the same code. That is the whole point:
`topology_bench` computes every number in its table itself, so a disagreement
with a published figure is a definitional difference rather than a finding.
"""

import io
import os
import sys
import tarfile
import urllib.request
import zipfile

OUT = os.path.join("data", "connectomes")
UA = {"User-Agent": "Mozilla/5.0 (khora topology bench)"}

# (name, url, kind). Every one of these is a NEURAL network -- KONECT and
# networkrepository both also carry social and metabolic graphs under similar
# names, which are not what this is for.
SOURCES = [
    ("dimacs10-celegansneural",
     "http://konect.cc/files/download.tsv.dimacs10-celegansneural.tar.bz2",
     "tar.bz2",
     "C. elegans nervous system, 297 neurons (White et al. 1986) -- the only "
     "complete connectome of any animal"),
    ("fly-medulla",
     "https://nrvis.com/download/data/bn/bn-fly-drosophila_medulla_1.zip",
     "zip",
     "Drosophila medulla, a visual-system column reconstruction"),
    ("mouse-visual",
     "https://nrvis.com/download/data/bn/bn-mouse_visual-cortex_2.zip",
     "zip",
     "Mouse visual cortex"),
    ("cat-cortex",
     "https://nrvis.com/download/data/bn/bn-cat-mixed-species_brain_1.zip",
     "zip",
     "Cat cortical areas (Scannell/Young tract tracing)"),
    ("macaque-brain",
     "https://nrvis.com/download/data/bn/bn-macaque-rhesus_brain_1.zip",
     "zip",
     "Macaque whole brain"),
    ("macaque-cortex",
     "https://nrvis.com/download/data/bn/bn-macaque-rhesus_cerebral-cortex_1.zip",
     "zip",
     "Macaque cerebral cortex"),
]


def fetch(url):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=180) as r:
        return r.read()


def main():
    os.makedirs(OUT, exist_ok=True)
    ok = 0
    for name, url, kind, desc in SOURCES:
        dest = os.path.join(OUT, name)
        try:
            blob = fetch(url)
        except Exception as e:                       # noqa: BLE001
            print("  miss  %-26s %s" % (name, e))
            continue
        os.makedirs(dest, exist_ok=True)
        try:
            if kind == "zip":
                zipfile.ZipFile(io.BytesIO(blob)).extractall(dest)
            else:
                tarfile.open(fileobj=io.BytesIO(blob), mode="r:bz2").extractall(OUT)
        except Exception as e:                       # noqa: BLE001
            print("  bad   %-26s %s" % (name, e))
            continue
        print("  got   %-26s %7d bytes  %s" % (name, len(blob), desc))
        ok += 1

    print("\n%d of %d sources retrieved into %s" % (ok, len(SOURCES), OUT))
    print("data/ is gitignored, so this is generated rather than committed.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

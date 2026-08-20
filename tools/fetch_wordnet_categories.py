#!/usr/bin/env python3
"""Build the external answer key for category induction: WordNet 3.1 categories.

Khora's own benchmarks are written by Khora's author, which makes them worth
very little on their own. This fetches an answer key nobody involved had a hand
in -- WordNet, built by lexicographers at Princeton over three decades -- and
reduces it to (category, members) rows.

It is used ONLY for evaluation. Nothing from WordNet enters any Khora code,
graph, or representation; the word codes being scored come entirely from books
Khora read.

    python tools/fetch_wordnet_categories.py           # writes data/eval/wn_categories.tsv

Output format, one category per line:

    <name>\\t<member> <member> <member> ...

data/ is gitignored (multi-GB runtime archives), so the answer key is generated
rather than committed. This script is the record of how.
"""

import io
import os
import sys
import tarfile
import urllib.request

WORDNET_URL = "https://wordnetcode.princeton.edu/wn3.1.dict.tar.gz"
OUT = os.path.join("data", "eval", "wn_categories.tsv")
MIN_MEMBERS = 6


def parse_data_noun(text):
    """Return (synset_offset -> lemmas, synset_offset -> set(hyponym offsets)).

    data.noun line format:
        offset lex_file ss_type w_cnt (word lex_id)* p_cnt (ptr_sym offset pos src/tgt)* | gloss
    The '~' pointer means "this target is a KIND of me", which is the relation
    a category is made of.
    """
    synsets, hyponyms = {}, {}
    for line in text.splitlines():
        if line.startswith("  "):          # licence header
            continue
        head = line.split("|", 1)[0]
        parts = head.split()
        if len(parts) < 4:
            continue
        off = parts[0]
        try:
            w_cnt = int(parts[3], 16)
        except ValueError:
            continue
        lemmas, i = [], 4
        try:
            for _ in range(w_cnt):
                lemmas.append(parts[i].lower().replace("_", " "))
                i += 2
            synsets[off] = lemmas
            p_cnt = int(parts[i]); i += 1
            for _ in range(p_cnt):
                sym, tgt = parts[i], parts[i + 1]
                i += 4
                if sym == "~":
                    hyponyms.setdefault(off, set()).add(tgt)
        except (IndexError, ValueError):
            continue
    return synsets, hyponyms


def main():
    print("fetching", WORDNET_URL)
    with urllib.request.urlopen(WORDNET_URL, timeout=300) as r:
        blob = r.read()
    print("  %.1f MB" % (len(blob) / 1048576.0))

    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz") as tf:
        member = next(m for m in tf.getmembers() if m.name.endswith("data.noun"))
        text = tf.extractfile(member).read().decode("latin-1")

    synsets, hyponyms = parse_data_noun(text)

    cats = []
    for off, kids in hyponyms.items():
        name = synsets.get(off, ["?"])[0]
        members = {lem for k in kids for lem in synsets.get(k, [])
                   if lem.isalpha() and len(lem) >= 3}
        if len(members) >= MIN_MEMBERS:
            cats.append((name, sorted(members)))
    cats.sort(key=lambda c: -len(c[1]))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        for name, members in cats:
            f.write(name.replace(" ", "_") + "\t" + " ".join(members) + "\n")

    print("wrote %s: %d categories, %d member words"
          % (OUT, len(cats), sum(len(m) for _, m in cats)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

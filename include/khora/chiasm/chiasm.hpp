#pragma once

// CHIASM — one memory in which a picture, a sound and a word are the same kind
// of thing, and any of them retrieves the others.
//
// (The optic chiasm is where the sensory streams cross.)
//
// THIS IS THE ARGUMENT FOR THE WHOLE SUBSTRATE, and until now it was only an
// argument. Khora encodes an image to a 10,000-bit Glyph (retina), a sound to a
// 10,000-bit Glyph (akoe), and a word to a 10,000-bit Glyph (lexicon). Three
// faculties, three utterly different front ends, ONE type coming out. Nothing in
// the tree had ever put them in the same container, so the property that makes
// the design worth having had never been used or measured.
//
// What the mainstream buys, and at what price. Putting images and text in a
// shared space is what CLIP does, and it costs four hundred million paired
// examples and a training run. The joint space is LEARNED, it is specific to the
// pair of modalities it was trained on, and adding a third means training again.
// Here the shared space is not learned at all -- it is the same 10,000 bits by
// construction, and a third modality is a third encoder writing into a container
// that already accepts it.
//
// HOW A RECORD WORKS, and why it is three operations rather than a database.
//
//     record = bundle over fields of bind(role, value)
//
// bind is XOR, so it is its own inverse: bind(bind(role, value), role) = value
// exactly. bundle is elementwise majority, and a bundle stays SIMILAR to each of
// its components -- that is the whole trick. So unbinding a role out of a whole
// record returns the right value plus the crosstalk of every other field, and
// the result is close to the true value and far from everything else.
//
// "Close" is not "equal", which is why a cleanup memory is not optional. The
// unbound glyph is noisy and must be snapped to the nearest thing ever stored
// under that role. Without cleanup the retrieval degrades silently as records
// are added, and reports a glyph nobody can name.
//
// THE COST, stated plainly. Capacity is finite and it is the fundamental
// constant of this design: a bundle of k components holds each one at a
// similarity that falls roughly as 1/sqrt(k), so a record with too many fields,
// or a cleanup memory with too many candidates, stops resolving. substrate_bench
// measures where that is. Nothing here is free; what is free is the TRAINING.

// THIS IS NOT A PRIVACY MECHANISM. DO NOT USE IT AS ONE.
//
// A bound record looks like it hides its contents, and in the algebra it does.
// structure_bench put an adversary in front of one holding the record and the
// entire 128-value codebook but no role glyph: every attack it could mount --
// direct similarity, and unbinding with guessed roles -- landed exactly on
// chance, at every record width from 2 to 16 fields. Hand the same adversary the
// role glyph and it recovers the value 100% of the time against a 0.78% chance.
// The contrast is real and it is the whole of the property.
//
// Two things break it in practice, and both are properties of THIS CODE rather
// than of the mathematics.
//
//   THE SECRET IS AN ENGLISH WORD. role_glyph() is from_hash(role_name), so the
//   "key" is a public function of a dictionary entry. An adversary with a
//   24-word list of plausible field names recovered 100% of the roles present
//   and 100% of full (role, value) pairs, against chances of 12.5% and 0.10%.
//   from_hash was chosen so a saved archive stays readable in the next process,
//   and that is exactly why it cannot also be a secret: STABILITY ACROSS
//   PROCESSES AND SECRECY ARE THE SAME PROPERTY WITH THE SIGN FLIPPED. Fixing it
//   means a caller-supplied key and an archive that is worthless without it,
//   which is a different product decision, not a patch.
//
//   XOR IS A ONE-TIME PAD AND THE ROLE IS REUSED. Take two groups of records
//   that share a role and share a value, bundle each group and XOR the two: the
//   role cancels itself out and what is left is the pair of values. Searching
//   all 8,128 codebook pairs recovers them 100% of the time from a SINGLE record
//   per group at up to 8 fields, and from about 16 records per group at any
//   width. This one is inherent: any self-inverse bind with a repeated key has
//   it, and no amount of key entropy helps.
//
// So: opacity holds for ONE record against an adversary who genuinely lacks the
// role. It is not a property of a corpus, and in this implementation it is not a
// property of a role name either.

#include "khora/lattice/glyph.hpp"
#include "khora/lattice/lattice.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace khora::chiasm {

// One observation, across however many modalities saw it.
struct Field {
    std::string           role;    // "sight", "sound", "word", "program", ...
    std::string           label;   // what this value is called, for cleanup
    khora::lattice::Glyph value;
};

struct Recall {
    std::string label;                 // the retrieved value's name, "" if none
    double      confidence = 0.0;      // similarity after cleanup
    double      margin     = 0.0;      // gap to the runner-up; the honest number
};

class Chiasm {
public:
    // Store one cross-modal observation. Every field becomes retrievable from
    // every other field, in both directions, from this single example. There is
    // no training step and no second pass.
    void remember(const std::vector<Field>& fields);

    // Given a value in one role, produce the value in another.
    //
    // Two steps, and the second is the one people leave out: find the record
    // whose bound pair matches the cue, then unbind the wanted role and CLEAN UP
    // against everything ever seen in that role.
    Recall recall(const std::string& cue_role, const khora::lattice::Glyph& cue,
                  const std::string& want_role) const;

    // The same, skipping the record lookup: unbind straight out of a glyph the
    // caller already holds. Exposed because it is the operation the claim is
    // actually about, and a test should be able to isolate it.
    Recall unbind_and_clean(const khora::lattice::Glyph& record,
                            const std::string& want_role) const;

    // IT HAS TO SURVIVE A RESTART OR IT NEVER COMPOUNDS.
    //
    // This repository already has the failure on record: the learned programming
    // library was built, filled and discarded at process exit, so nothing about
    // programming accumulated across runs and the fix was worth ten tasks on a
    // fixed bar. A cross-modal memory that forgets everything when the process
    // ends is the same mistake with a different noun.
    //
    // Role glyphs are NOT written: they come from from_hash(name) and are
    // therefore already stable across processes, which is why from_hash was used
    // for them instead of a counter.
    void save(const std::filesystem::path& dir) const;
    void load(const std::filesystem::path& dir);

    std::size_t records() const noexcept { return records_.size(); }
    std::size_t roles()   const noexcept { return role_glyph_.size(); }
    std::size_t known(const std::string& role) const;

private:
    // A stable glyph per role name, so the same role means the same thing in
    // every process and across a restart.
    const khora::lattice::Glyph& role_glyph(const std::string& role);
    const khora::lattice::Glyph* role_glyph_if(const std::string& role) const;

    std::unordered_map<std::string, khora::lattice::Glyph> role_glyph_;
    // Cleanup memory, one per role: every value ever stored under it.
    std::unordered_map<std::string, khora::lattice::Lattice> cleanup_;
    std::vector<khora::lattice::Glyph> records_;
};

} // namespace khora::chiasm

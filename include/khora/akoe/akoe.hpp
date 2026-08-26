#pragma once

// AKOE — hearing. (ἀκοή.)
//
// The last named absent sense. A capability audit found perception missing
// entirely and `retina` closed the visual half: an image encodes into the
// 10,000-bit Lattice as `bundle over cells of bind(place, level)`, four shapes
// at 75.4% against 49.2% for nearest-class-mean on raw pixels. Sound was left,
// and the whole tree contains no reference to a sample, a waveform, a frequency
// or a channel.
//
// IT NEEDS NO NEW SUBSTRATE EITHER, and that is the interesting part. The same
// argument that made vision work makes hearing work, because the Lattice does
// not care what the axes mean. An image is a value at a place in two dimensions;
// a sound is a value at a place in two dimensions as well, once you stop looking
// at the waveform and look at the SPECTROGRAM -- energy at a frequency at a time.
// Bind a frequency-band glyph to a level glyph, bundle over the band, permute by
// the time frame so order survives, bundle over frames.
//
// WHY A SPECTROGRAM AND NOT THE WAVEFORM. A waveform is the wrong
// representation for recognition and would make this look harder than it is: two
// recordings of the same note starting a millisecond apart are nearly orthogonal
// sample by sample, and identical in their frequency content. Every hearing
// system biological or otherwise does this transform first; the cochlea IS a
// filter bank. So the front end is a bank of band energies and the encoder sits
// behind it, exactly where the retina sits behind a pixel grid.
//
// THE DFT IS THE HONEST MINIMUM. A naive O(n*k) band energy over k bands, not an
// FFT. For the frame sizes here that is a few hundred thousand multiply-adds per
// second of audio, which is nothing, and an FFT is a page of index arithmetic
// that would need its own test to be trustworthy. If frames ever get large
// enough for it to matter this is the place to change and the test above it will
// not move.
//
// WHAT THIS IS NOT: no microphone, no file decoding, no MFCC, no pitch tracking,
// no onset detection. It takes samples and returns a Glyph, and everything the
// Lattice can already do then applies to sound unchanged -- which is the claim
// worth making and the one the test checks.

#include "khora/lattice/lattice.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace khora::akoe {

// A block of mono samples in [-1, 1] at a stated rate. Deliberately the dumbest
// possible container: this module's job is the encoding, not audio I/O.
struct Sound {
    std::vector<double> samples;
    double              rate = 16000.0;   // Hz
};

// Energy per frequency band per time frame -- the thing recognition actually
// operates on. Rows are frames, columns are bands.
struct Spectrogram {
    std::size_t         frames = 0, bands = 0;
    std::vector<double> energy;           // frames x bands, row-major, normalised 0..1

    double at(std::size_t f, std::size_t b) const { return energy[f * bands + b]; }
};

class Ear {
public:
    // `bands` filter channels spaced logarithmically between `lo` and `hi`,
    // because pitch is perceived logarithmically and linear spacing spends most
    // of its resolution on frequencies that carry almost nothing. `levels` is how
    // finely energy is quantised, exactly as the retina quantises intensity.
    explicit Ear(std::size_t bands = 24, std::size_t levels = 8,
                 double lo = 80.0, double hi = 6000.0,
                 std::size_t frame_samples = 512);

    Spectrogram analyse(const Sound& s) const;

    // The encoding: for each frame, bundle over bands of bind(band, level); then
    // permute each frame by its index and bundle, so that a rising tone and a
    // falling one do not collapse to the same glyph. That permutation is the only
    // reason time survives at all -- bundling is commutative, so without it the
    // representation is a bag of frames.
    khora::lattice::Glyph encode(const Sound& s) const;
    khora::lattice::Glyph encode(const Spectrogram& sp) const;

    std::size_t bands()  const noexcept { return bands_; }
    std::size_t levels() const noexcept { return levels_; }

private:
    std::size_t bands_, levels_, frame_;
    double      lo_, hi_;
};

// Nearest-prototype recognition over the encoded glyphs, the same shape as
// retina::Recogniser: a class is the bundle of everything labelled with it.
class Listener {
public:
    void learn(const khora::lattice::Glyph& g, int label);
    // The label and its similarity, or {-1, 0} if nothing has been learned.
    std::pair<int, double> classify(const khora::lattice::Glyph& g) const;
    std::size_t classes() const noexcept { return protos_.size(); }

private:
    std::vector<int>                        labels_;
    std::vector<std::vector<khora::lattice::Glyph>> examples_;
    std::vector<khora::lattice::Glyph>      protos_;
    void rebuild_(std::size_t i);
};

} // namespace khora::akoe

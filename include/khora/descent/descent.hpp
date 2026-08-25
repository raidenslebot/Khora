#pragma once

// DESCENT — the paradigm this system was built to avoid, implemented so the
// avoidance can be checked instead of assumed.
//
// Khora's banner reads "LLM-free", its substrate is 10,000-bit binary
// hypervectors, and a capability audit found no BLAS, no matmul, no tensors, no
// autograd and no floating-point linear algebra anywhere in the tree. That is a
// design position, and a design position that has never been measured against
// the thing it rejects is a preference.
//
// So: dense float matrices, a two-layer perceptron, cross-entropy, and
// stochastic gradient descent. Perhaps two hundred lines, no dependencies, and
// enough to answer the only question worth asking -- ON THE SAME TASK, WITH THE
// SAME DATA, which of the two paradigms actually does better?
//
// The retina classifies four synthetic shapes at 75.4% by encoding them into the
// hypervector algebra and taking the nearest class bundle. That number now has
// something to be compared against, and descent_test runs both on the identical
// generator and the identical disjoint test stream. Whatever comes out, the
// answer stops being an assertion about substrates.
//
// WHY BACKPROP BY HAND rather than a general autodiff graph. A tape-based
// reverse-mode engine is the right thing for arbitrary architectures and it is
// several times this much code, most of which would be untested here because
// nothing in this repository needs arbitrary architectures. Two layers with
// hand-written gradients is small enough to read, and the test checks those
// gradients against finite differences -- which is the only way to know a
// backward pass is right, and the check most hand-rolled ones skip.
//
// WHAT THIS IS NOT: no GPU, no convolution, no batching beyond a minibatch loop,
// no adaptive optimiser. It is the honest minimum required to make the
// comparison, and it is labelled as such rather than as a deep learning stack.

#include <cstddef>
#include <string>
#include <vector>

namespace khora::descent {

// Row-major dense matrix. Small enough that the layout matters less than being
// able to see what every index means.
struct Matrix {
    std::size_t         rows = 0, cols = 0;
    std::vector<double> v;

    Matrix() = default;
    Matrix(std::size_t r, std::size_t c) : rows(r), cols(c), v(r * c, 0.0) {}

    double&       at(std::size_t r, std::size_t c)       { return v[r * cols + c]; }
    const double& at(std::size_t r, std::size_t c) const { return v[r * cols + c]; }
};

// A two-layer perceptron: in -> hidden (ReLU) -> out (softmax).
class Mlp {
public:
    Mlp(std::size_t n_in, std::size_t n_hidden, std::size_t n_out,
        std::uint64_t seed = 0xD35CE27ULL);

    // Class scores for one input. `hidden_out` receives the post-ReLU
    // activations when non-null, because the backward pass needs them and
    // recomputing the forward pass to get them is how they drift apart.
    std::vector<double> forward(const std::vector<double>& x,
                                std::vector<double>* hidden_out = nullptr) const;

    // Cross-entropy of one example, and the gradients it implies, accumulated
    // into the supplied buffers. Returns the loss.
    double backward(const std::vector<double>& x, std::size_t label,
                    Matrix& dW1, std::vector<double>& db1,
                    Matrix& dW2, std::vector<double>& db2) const;

    // One epoch of minibatch SGD over the given examples. Returns mean loss.
    double train_epoch(const std::vector<std::vector<double>>& xs,
                       const std::vector<std::size_t>& ys,
                       double lr, std::size_t batch, std::uint64_t& seed);

    std::size_t predict(const std::vector<double>& x) const;

    // Exposed so the finite-difference check can perturb one weight at a time.
    // A backward pass nobody has checked against numerical gradients is a
    // backward pass that is probably wrong somewhere quiet.
    Matrix&              W1() noexcept { return w1_; }
    Matrix&              W2() noexcept { return w2_; }
    std::vector<double>& b1() noexcept { return b1_; }
    std::vector<double>& b2() noexcept { return b2_; }

    std::size_t inputs()  const noexcept { return n_in_; }
    std::size_t outputs() const noexcept { return n_out_; }

private:
    std::size_t         n_in_, n_hid_, n_out_;
    Matrix              w1_, w2_;
    std::vector<double> b1_, b2_;
};

} // namespace khora::descent

#include "khora/descent/descent.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace khora::descent {

namespace {

std::uint64_t nxt(std::uint64_t& s) {
    s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s;
}
double unit(std::uint64_t& s) {
    return static_cast<double>(nxt(s) >> 11) / 9007199254740992.0;
}

// Softmax in the numerically stable form. Subtracting the max is not an
// optimisation: without it, scores above ~710 overflow exp() to infinity and the
// whole vector becomes NaN, which shows up as a network that trains for a while
// and then silently stops learning.
std::vector<double> softmax(const std::vector<double>& z) {
    const double m = *std::max_element(z.begin(), z.end());
    std::vector<double> p(z.size());
    double sum = 0.0;
    for (std::size_t i = 0; i < z.size(); ++i) { p[i] = std::exp(z[i] - m); sum += p[i]; }
    for (double& x : p) x /= sum;
    return p;
}

} // namespace

Mlp::Mlp(std::size_t n_in, std::size_t n_hidden, std::size_t n_out, std::uint64_t seed)
    : n_in_(n_in), n_hid_(n_hidden), n_out_(n_out),
      w1_(n_hidden, n_in), w2_(n_out, n_hidden),
      b1_(n_hidden, 0.0), b2_(n_out, 0.0) {
    // He initialisation: variance 2/fan_in, which is the scaling ReLU wants.
    // Zero-initialising the weights instead would make every hidden unit compute
    // the same thing forever, since they would all receive identical gradients.
    std::uint64_t s = seed ? seed : 1;
    const double s1 = std::sqrt(2.0 / static_cast<double>(std::max<std::size_t>(n_in, 1)));
    const double s2 = std::sqrt(2.0 / static_cast<double>(std::max<std::size_t>(n_hidden, 1)));
    for (double& w : w1_.v) w = (unit(s) * 2.0 - 1.0) * s1;
    for (double& w : w2_.v) w = (unit(s) * 2.0 - 1.0) * s2;
}

std::vector<double> Mlp::forward(const std::vector<double>& x,
                                 std::vector<double>* hidden_out) const {
    std::vector<double> h(n_hid_, 0.0);
    for (std::size_t j = 0; j < n_hid_; ++j) {
        double a = b1_[j];
        for (std::size_t i = 0; i < n_in_; ++i) a += w1_.at(j, i) * x[i];
        h[j] = a > 0.0 ? a : 0.0;                      // ReLU
    }
    if (hidden_out) *hidden_out = h;
    std::vector<double> z(n_out_, 0.0);
    for (std::size_t k = 0; k < n_out_; ++k) {
        double a = b2_[k];
        for (std::size_t j = 0; j < n_hid_; ++j) a += w2_.at(k, j) * h[j];
        z[k] = a;
    }
    return z;
}

double Mlp::backward(const std::vector<double>& x, std::size_t label,
                     Matrix& dW1, std::vector<double>& db1,
                     Matrix& dW2, std::vector<double>& db2) const {
    std::vector<double> h;
    const std::vector<double> z = forward(x, &h);
    const std::vector<double> p = softmax(z);

    // Cross-entropy with softmax collapses to (p - onehot) at the logits, which
    // is why the two are always written together: the exp and the log cancel and
    // the gradient is a subtraction.
    std::vector<double> dz(n_out_);
    for (std::size_t k = 0; k < n_out_; ++k) dz[k] = p[k] - (k == label ? 1.0 : 0.0);

    for (std::size_t k = 0; k < n_out_; ++k) {
        db2[k] += dz[k];
        for (std::size_t j = 0; j < n_hid_; ++j) dW2.at(k, j) += dz[k] * h[j];
    }
    std::vector<double> dh(n_hid_, 0.0);
    for (std::size_t j = 0; j < n_hid_; ++j) {
        double g = 0.0;
        for (std::size_t k = 0; k < n_out_; ++k) g += w2_.at(k, j) * dz[k];
        dh[j] = h[j] > 0.0 ? g : 0.0;                  // ReLU derivative
    }
    for (std::size_t j = 0; j < n_hid_; ++j) {
        db1[j] += dh[j];
        for (std::size_t i = 0; i < n_in_; ++i) dW1.at(j, i) += dh[j] * x[i];
    }
    const double eps = 1e-12;
    return -std::log(std::max(p[label], eps));
}

double Mlp::train_epoch(const std::vector<std::vector<double>>& xs,
                        const std::vector<std::size_t>& ys,
                        double lr, std::size_t batch, std::uint64_t& seed) {
    if (xs.empty()) return 0.0;
    std::vector<std::size_t> order(xs.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    for (std::size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[nxt(seed) % i]);

    double total = 0.0;
    for (std::size_t start = 0; start < order.size(); start += batch) {
        const std::size_t end = std::min(start + batch, order.size());
        Matrix dW1(n_hid_, n_in_), dW2(n_out_, n_hid_);
        std::vector<double> db1(n_hid_, 0.0), db2(n_out_, 0.0);
        for (std::size_t q = start; q < end; ++q) {
            total += backward(xs[order[q]], ys[order[q]], dW1, db1, dW2, db2);
        }
        // Average over the minibatch, so the step size means the same thing
        // whatever the batch size is.
        const double scale = lr / static_cast<double>(end - start);
        for (std::size_t i = 0; i < w1_.v.size(); ++i) w1_.v[i] -= scale * dW1.v[i];
        for (std::size_t i = 0; i < w2_.v.size(); ++i) w2_.v[i] -= scale * dW2.v[i];
        for (std::size_t j = 0; j < n_hid_; ++j)      b1_[j]    -= scale * db1[j];
        for (std::size_t k = 0; k < n_out_; ++k)      b2_[k]    -= scale * db2[k];
    }
    return total / static_cast<double>(xs.size());
}

std::size_t Mlp::predict(const std::vector<double>& x) const {
    const std::vector<double> z = forward(x);
    return static_cast<std::size_t>(
        std::max_element(z.begin(), z.end()) - z.begin());
}

} // namespace khora::descent

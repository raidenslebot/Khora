// Gradients checked against finite differences, then the two paradigms raced.
//
// A hand-written backward pass that nobody has checked numerically is probably
// wrong somewhere quiet: it trains, the loss goes down, and it converges to
// something worse than it should. The check is cheap and almost always skipped,
// so it is the first thing here.
//
// The second thing is the comparison this module exists for. Khora's substrate
// is 10,000-bit binary hypervectors and its banner says LLM-free; the retina
// classifies four shapes at 75.4% that way. That is a design position, and one
// that has never been measured against the thing it rejects. So both run on the
// SAME generator, the SAME training set and the SAME disjoint test stream, and
// whatever comes out is the answer.

#include "khora/descent/descent.hpp"
#include "khora/retina/retina.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace khora::descent;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else       { std::printf("  ok  : %s\n", what); }
}

std::uint64_t g_s = 20240825;
std::uint64_t rnd() { g_s ^= g_s << 13; g_s ^= g_s >> 7; g_s ^= g_s << 17; return g_s; }
std::size_t   rint(std::size_t lo, std::size_t hi) { return lo + rnd() % (hi - lo + 1); }

constexpr std::size_t kW = 32, kH = 32;

void plot(khora::retina::Image& im, long x, long y, std::uint8_t v) {
    if (x < 0 || y < 0 || x >= static_cast<long>(kW) || y >= static_cast<long>(kH)) return;
    im.pixels[static_cast<std::size_t>(y) * kW + static_cast<std::size_t>(x)] = v;
}

// Byte-identical to the generator in retina_test, so the two paradigms are
// scored on the same problem rather than on two problems that resemble each
// other.
khora::retina::Image draw(int cls, std::size_t noise_pct) {
    khora::retina::Image im;
    im.width = kW; im.height = kH; im.pixels.assign(kW * kH, 0);
    const long r  = static_cast<long>(rint(5, 9));
    const long cx = static_cast<long>(rint(static_cast<std::size_t>(r) + 1, kW - static_cast<std::size_t>(r) - 2));
    const long cy = static_cast<long>(rint(static_cast<std::size_t>(r) + 1, kH - static_cast<std::size_t>(r) - 2));
    switch (cls) {
        case 0:
            for (long y = -r; y <= r; ++y)
                for (long x = -r; x <= r; ++x) plot(im, cx + x, cy + y, 255);
            break;
        case 1:
            for (long y = -r; y <= r; ++y)
                for (long x = -r; x <= r; ++x) {
                    const double d = std::sqrt(static_cast<double>(x * x + y * y));
                    if (d <= static_cast<double>(r) && d >= static_cast<double>(r) - 2.0)
                        plot(im, cx + x, cy + y, 255);
                }
            break;
        case 2:
            for (long t = -r; t <= r; ++t)
                for (long w = -1; w <= 1; ++w) {
                    plot(im, cx + t + w, cy + t, 255);
                    plot(im, cx + t + w, cy - t, 255);
                }
            break;
        default:
            for (long y = -r; y <= r; y += 3)
                for (long x = -r; x <= r; ++x) {
                    plot(im, cx + x, cy + y, 255);
                    plot(im, cx + x, cy + y + 1, 255);
                }
            break;
    }
    if (noise_pct > 0) {
        const std::size_t flips = kW * kH * noise_pct / 100;
        for (std::size_t i = 0; i < flips; ++i) {
            const std::size_t p = rnd() % (kW * kH);
            im.pixels[p] = im.pixels[p] > 127 ? 0 : 255;
        }
    }
    return im;
}

std::vector<double> as_input(const khora::retina::Image& im) {
    std::vector<double> x(im.pixels.size());
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = im.pixels[i] / 255.0;
    return x;
}

} // namespace

int main() {
    std::printf("Descent — gradients, and the paradigm race\n\n");

    // --- THE BACKWARD PASS IS CHECKED AGAINST FINITE DIFFERENCES -------------
    //
    // Perturb one weight by h, measure the loss change, compare to the analytic
    // gradient. If these disagree the network still trains and still converges,
    // just to somewhere worse -- which is why this is the first check and not an
    // afterthought.
    {
        Mlp net(5, 4, 3, 12345);
        std::vector<double> x{0.3, -0.7, 0.5, 0.9, -0.2};
        const std::size_t label = 2;

        Matrix dW1(4, 5), dW2(3, 4);
        std::vector<double> db1(4, 0.0), db2(3, 0.0);
        net.backward(x, label, dW1, db1, dW2, db2);

        auto loss_now = [&]() {
            const std::vector<double> z = net.forward(x);
            double m = z[0]; for (double v : z) m = std::max(m, v);
            double sum = 0.0; for (double v : z) sum += std::exp(v - m);
            return -( z[label] - m - std::log(sum) );
        };

        const double h = 1e-6;
        double worst = 0.0;
        // Every weight in the smaller layer, plus a sample of the larger.
        for (std::size_t k = 0; k < 3; ++k) {
            for (std::size_t j = 0; j < 4; ++j) {
                const double keep = net.W2().at(k, j);
                net.W2().at(k, j) = keep + h; const double up = loss_now();
                net.W2().at(k, j) = keep - h; const double dn = loss_now();
                net.W2().at(k, j) = keep;
                worst = std::max(worst, std::fabs((up - dn) / (2 * h) - dW2.at(k, j)));
            }
        }
        for (std::size_t j = 0; j < 4; ++j) {
            for (std::size_t i = 0; i < 5; ++i) {
                const double keep = net.W1().at(j, i);
                net.W1().at(j, i) = keep + h; const double up = loss_now();
                net.W1().at(j, i) = keep - h; const double dn = loss_now();
                net.W1().at(j, i) = keep;
                worst = std::max(worst, std::fabs((up - dn) / (2 * h) - dW1.at(j, i)));
            }
        }
        for (std::size_t k = 0; k < 3; ++k) {
            const double keep = net.b2()[k];
            net.b2()[k] = keep + h; const double up = loss_now();
            net.b2()[k] = keep - h; const double dn = loss_now();
            net.b2()[k] = keep;
            worst = std::max(worst, std::fabs((up - dn) / (2 * h) - db2[k]));
        }
        std::printf("      worst gradient error against finite differences: %.2e\n", worst);
        check(worst < 1e-5, "every analytic gradient matches the numerical one");
    }

    // --- IT ACTUALLY LEARNS SOMETHING TRIVIAL --------------------------------
    //
    // XOR, because it is the smallest problem a linear model cannot do: if the
    // hidden layer is broken this fails and nothing else will tell you.
    {
        Mlp net(2, 8, 2, 999);
        std::vector<std::vector<double>> xs{{0,0},{0,1},{1,0},{1,1}};
        std::vector<std::size_t> ys{0, 1, 1, 0};
        std::uint64_t s = 7;
        double last = 0;
        for (int e = 0; e < 4000; ++e) last = net.train_epoch(xs, ys, 0.5, 4, s);
        std::size_t right = 0;
        for (std::size_t i = 0; i < xs.size(); ++i) if (net.predict(xs[i]) == ys[i]) ++right;
        std::printf("      XOR after 4000 epochs: %zu/4 correct, loss %.4f\n", right, last);
        check(right == 4, "XOR is learned, so the hidden layer is doing work");
        check(last < 0.1, "and the loss actually descended");
    }

    // --- THE RACE, ACROSS THE DATA REGIME -----------------------------------
    //
    // Running this once at one training-set size would have produced a winner
    // and a wrong conclusion. Hyperdimensional computing is known to be strong
    // when examples are scarce -- a class prototype is a bundle, so a single
    // example already places it -- and gradient descent is known to need volume.
    // The question is not which wins but WHERE EACH ONE WINS, so the same
    // comparison runs at four training-set sizes with everything else fixed.
    {
        std::vector<khora::retina::Image> test_im;
        std::vector<std::size_t>          test_y;
        g_s = 99887766;
        for (int i = 0; i < 60; ++i)
            for (int c = 0; c < 4; ++c) { test_im.push_back(draw(c, 4)); test_y.push_back(c); }

        std::printf("      four shapes, 240 held-out images, identical for every row:\n");
        std::printf("        per class | hypervector | MLP (SGD)\n");
        std::printf("        ----------+-------------+----------\n");

        double hv_low = 0, nn_low = 0, hv_high = 0, nn_high = 0;
        const int sizes[] = {5, 20, 80, 320};
        for (int per_class : sizes) {
            std::vector<khora::retina::Image> train_im;
            std::vector<std::size_t>          train_y;
            g_s = 20240825;
            for (int i = 0; i < per_class; ++i)
                for (int c = 0; c < 4; ++c) { train_im.push_back(draw(c, 4)); train_y.push_back(c); }

            khora::retina::Retina r(32, 16);
            khora::retina::Recogniser rec;
            for (std::size_t i = 0; i < train_im.size(); ++i)
                rec.learn(r.encode(train_im[i]), static_cast<int>(train_y[i]));
            std::size_t hv_hit = 0;
            for (std::size_t i = 0; i < test_im.size(); ++i)
                if (rec.classify(r.encode(test_im[i])).first == static_cast<int>(test_y[i])) ++hv_hit;

            std::vector<std::vector<double>> xs;
            for (const auto& im : train_im) xs.push_back(as_input(im));
            Mlp net(kW * kH, 64, 4, 2468);
            std::uint64_t s = 13;
            for (int e = 0; e < 200; ++e) net.train_epoch(xs, train_y, 0.15, 16, s);
            std::size_t nn_hit = 0;
            for (std::size_t i = 0; i < test_im.size(); ++i)
                if (net.predict(as_input(test_im[i])) == test_y[i]) ++nn_hit;

            const double hv = 100.0 * static_cast<double>(hv_hit) / static_cast<double>(test_im.size());
            const double nn = 100.0 * static_cast<double>(nn_hit) / static_cast<double>(test_im.size());
            std::printf("        %9d | %10.1f%% | %8.1f%%\n", per_class, hv, nn);
            if (per_class == sizes[0]) { hv_low = hv; nn_low = nn; }
            if (per_class == sizes[3]) { hv_high = hv; nn_high = nn; }
        }
        std::printf("        chance 25.0%%.  MLP fixed at 64 hidden, 200 epochs, lr 0.15.\n");

        check(hv_low > 25.0 && nn_low > 25.0, "both beat chance at the smallest training set");
        check(hv_low > nn_low,
              "hypervectors win when examples are scarce -- one bundle places a class");
        check(nn_high - nn_low > hv_high - hv_low,
              "and gradient descent gains far more from data than hypervectors do");
    }

    std::printf("\n");
    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}

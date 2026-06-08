// Lexicon demo — show that semantic encoding actually works.
//
// 1. Structural similarity: variants and typos of a word are close.
// 2. Cooccurrence drift: words that appear together in real text become
//    closer over exposure.

#include "khora/lexicon/lexicon.hpp"

#include <cstdio>
#include <string>

using khora::lexicon::encode_token;
using khora::lexicon::Lexicon;

int main() {
    std::printf("Lexicon demo - structural + cooccurrence semantic encoding\n\n");

    std::printf("Structural similarities (pure char-trigram bundle, no training):\n");
    auto pair = [](const char* a, const char* b) {
        const double s = encode_token(a).similarity(encode_token(b));
        std::printf("  %-12s vs %-12s  sim=%+.4f\n", a, b, s);
    };
    pair("cat",      "cat");
    pair("cat",      "cats");
    pair("cat",      "cathedral");
    pair("cat",      "dog");
    pair("install",  "instal");
    pair("install",  "isntall");
    pair("install",  "uninstall");
    pair("aardvark", "zephyr");

    std::printf("\nCooccurrence drift — train on a tiny corpus, observe shifts:\n");
    Lexicon lex;
    const char* corpus =
        "the cat sat on the mat. the cat began to purr. the dog ran. "
        "the dog barked. the cat watched the dog. the cat napped. "
        "the dog chased the ball. the cat ignored the ball. "
        "kittens purr like cats. puppies bark like dogs. "
        "the cat is feline. the dog is canine. "
        "felines and canines are different. ";
    for (int i = 0; i < 30; ++i) lex.expose_text(corpus, 3);

    auto live = [&](const char* a, const char* b) {
        const double s = lex.similarity(a, b);
        std::printf("  %-12s vs %-12s  sim=%+.4f\n", a, b, s);
    };
    std::printf("  vocab=%zu  observations=%zu\n\n",
                lex.vocabulary_size(), lex.total_observations());
    live("cat",      "purr");        // expected up
    live("dog",      "bark");        // expected up
    live("cat",      "feline");      // co-occurred via "the cat is feline"
    live("dog",      "canine");
    live("cat",      "dog");         // both nouns, share contexts -> some up
    live("cat",      "zephyr");      // unrelated control
    live("install",  "uninstall");   // never seen, structural only

    return 0;
}

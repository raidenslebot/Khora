#pragma once

// Distillation — turn raw acquired bytes into clean canonical text with
// zero artifacts. The Reservoir admits nothing that has not been distilled.
//
// Removes: Project Gutenberg license header/footer, HTML tags + common
// entities, carriage returns, stray control bytes, trailing line
// whitespace, and runs of blank lines. Preserves paragraph structure and
// valid UTF-8 multibyte content.

#include <cstddef>
#include <string>
#include <string_view>

namespace khora::reservoir {

struct DistillStats {
    std::size_t input_bytes  = 0;
    std::size_t output_bytes = 0;
    bool        stripped_gutenberg_header = false;
    bool        stripped_gutenberg_footer = false;
    std::size_t html_tags_removed = 0;
    std::size_t control_bytes_dropped = 0;
};

// Distill raw text. Returns clean canonical text; fills stats if non-null.
std::string distill(std::string_view raw, DistillStats* stats = nullptr);

} // namespace khora::reservoir

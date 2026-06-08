#pragma once

// The Aqueduct — channels external knowledge into the Reservoir.
//
// Fetches public-domain texts (Project Gutenberg) over HTTPS using the
// Windows-native WinHTTP stack (no external dependency), and admits them
// to the Reservoir through the full distill -> compress -> verify pipeline.
//
// A curated seed catalog gives Khora a starting set to forage from by
// topic. Acquisition is autonomous in the sense that the runtime decides
// *when* and *what* to forage based on its measured knowledge needs; this
// module is the hands that fetch.

#include "khora/reservoir/reservoir.hpp"

#include <optional>
#include <string>
#include <vector>

namespace khora::reservoir {

struct HttpResult {
    bool          ok = false;
    long          status = 0;
    std::string   body;
    std::string   error;
};

// Blocking HTTPS/HTTP GET via WinHTTP. Returns the response body.
HttpResult http_get(const std::string& url, int timeout_ms = 30000);

struct Source {
    std::string title;
    std::string topic;
    std::string url;
};

// A curated set of clean public-domain sources, grouped by topic.
const std::vector<Source>& seed_catalog();

class Aqueduct {
public:
    explicit Aqueduct(Reservoir& reservoir) : reservoir_(reservoir) {}

    // Fetch a specific source and admit it. Returns the admit result.
    AdmitResult acquire(const Source& src);

    // Forage: pick the first seed in `topic` not already in the Reservoir,
    // fetch it, and admit it. If topic is empty, consider all topics.
    // Returns nullopt if nothing left to forage in that topic.
    std::optional<AdmitResult> forage(const std::string& topic = "");

    std::size_t acquisitions() const noexcept { return acquisitions_; }
    std::size_t failures()     const noexcept { return failures_; }

private:
    Reservoir&  reservoir_;
    std::size_t acquisitions_ = 0;
    std::size_t failures_     = 0;
};

} // namespace khora::reservoir

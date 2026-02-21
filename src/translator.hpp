#pragma once

#include "segment.hpp"

#include <memory>
#include <string>

class Translator {
public:
    virtual ~Translator() = default;

    // Per-thread isolation point: each worker gets its own translator clone.
    virtual std::unique_ptr<Translator> clone() const = 0;
    virtual std::string translate(const Segment& segment) = 0;
};

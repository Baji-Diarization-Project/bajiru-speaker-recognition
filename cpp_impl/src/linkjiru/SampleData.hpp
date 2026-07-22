#pragma once

// Bundled 48 kHz mono test samples, embedded at build time as float32 in
// [-1, 1] (exactly what the model expects). The big arrays live in
// SampleData.cpp so they compile once, not per translation unit.

namespace linkjiru
{
namespace samples
{

extern const float baji[];
extern const int bajiCount;

extern const float ru[];
extern const int ruCount;

} // namespace samples
} // namespace linkjiru

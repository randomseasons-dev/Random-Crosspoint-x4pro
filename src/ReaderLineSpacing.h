#pragma once

#include <I18n.h>

#include <iterator>

#include "CrossPointSettings.h"

// Labels and multipliers follow the persisted LINE_COMPRESSION order.
inline constexpr StrId LINE_SPACING_IDS[] = {
    StrId::STR_SPACING_MINUS_10_PERCENT, StrId::STR_SPACING_MINUS_5_PERCENT,         StrId::STR_SPACING_0_PERCENT,
    StrId::STR_SPACING_PLUS_10_PERCENT,  StrId::STR_SPACING_PLUS_20_PERCENT_DEFAULT, StrId::STR_SPACING_PLUS_30_PERCENT,
    StrId::STR_SPACING_PLUS_40_PERCENT,  StrId::STR_SPACING_PLUS_50_PERCENT};
inline constexpr float LINE_SPACING_MULTIPLIERS[] = {0.9f, 0.95f, 1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f};
static_assert(std::size(LINE_SPACING_IDS) == CrossPointSettings::LINE_COMPRESSION_COUNT);
static_assert(std::size(LINE_SPACING_MULTIPLIERS) == CrossPointSettings::LINE_COMPRESSION_COUNT);

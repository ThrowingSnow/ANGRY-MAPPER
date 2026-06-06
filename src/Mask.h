#pragma once
#include <vector>

enum class MaskShape { Circle, Rect };

struct Mask {
    MaskShape shape   = MaskShape::Circle;
    float     cx      = 0.5f;    // center in screen UV [0,1]
    float     cy      = 0.5f;
    float     rx      = 0.15f;   // radius (circle) or half-width (rect), in UV height units
    float     ry      = 0.15f;   // same as rx for circle, half-height for rect
    float     feather = 0.03f;   // soft edge width (inward)
};

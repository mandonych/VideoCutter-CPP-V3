
#include "Theme.h"

namespace Theme
{
    HFONT CreateUIFont(int height, int weight)
    {
        return CreateFontW(
            height,
            0,
            0,
            0,
            weight,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            FontFamily
        );
    }

    HBRUSH CreateThemeBrush(COLORREF color)
    {
        return CreateSolidBrush(color);
    }
}



#include "config.h"
#include "RenderThemeChromiumLinux.h"

#include "CSSValueKeywords.h"
#include "Color.h"
#include "RenderObject.h"
#include "UserAgentStyleSheets.h"

namespace WebCore {

unsigned RenderThemeChromiumLinux::m_thumbInactiveColor = 0xf0ebe5;
unsigned RenderThemeChromiumLinux::m_thumbActiveColor = 0xfaf8f5;
unsigned RenderThemeChromiumLinux::m_trackColor = 0xe3ddd8;
unsigned RenderThemeChromiumLinux::m_activeSelectionBackgroundColor =
    0xff1e90ff;
unsigned RenderThemeChromiumLinux::m_activeSelectionForegroundColor =
    Color::black;
unsigned RenderThemeChromiumLinux::m_inactiveSelectionBackgroundColor =
    0xffc8c8c8;
unsigned RenderThemeChromiumLinux::m_inactiveSelectionForegroundColor =
    0xff323232;

double RenderThemeChromiumLinux::m_caretBlinkInterval;

PassRefPtr<RenderTheme> RenderThemeChromiumLinux::create()
{
    return adoptRef(new RenderThemeChromiumLinux());
}

PassRefPtr<RenderTheme> RenderTheme::themeForPage(Page* page)
{
    static RenderTheme* rt = RenderThemeChromiumLinux::create().releaseRef();
    return rt;
}

RenderThemeChromiumLinux::RenderThemeChromiumLinux()
{
    m_caretBlinkInterval = RenderTheme::caretBlinkInterval();
}

RenderThemeChromiumLinux::~RenderThemeChromiumLinux()
{
}

Color RenderThemeChromiumLinux::systemColor(int cssValueId) const
{
    static const Color linuxButtonGrayColor(0xffdddddd);

    if (cssValueId == CSSValueButtonface)
        return linuxButtonGrayColor;
    return RenderTheme::systemColor(cssValueId);
}

String RenderThemeChromiumLinux::extraDefaultStyleSheet()
{
    return RenderThemeChromiumSkia::extraDefaultStyleSheet() +
           String(themeChromiumLinuxUserAgentStyleSheet, sizeof(themeChromiumLinuxUserAgentStyleSheet));
}

bool RenderThemeChromiumLinux::controlSupportsTints(const RenderObject* o) const
{
    return isEnabled(o);
}

Color RenderThemeChromiumLinux::activeListBoxSelectionBackgroundColor() const
{
    return Color(0x28, 0x28, 0x28);
}

Color RenderThemeChromiumLinux::activeListBoxSelectionForegroundColor() const
{
    return Color::black;
}

Color RenderThemeChromiumLinux::inactiveListBoxSelectionBackgroundColor() const
{
    return Color(0xc8, 0xc8, 0xc8);
}

Color RenderThemeChromiumLinux::inactiveListBoxSelectionForegroundColor() const
{
    return Color(0x32, 0x32, 0x32);
}

Color RenderThemeChromiumLinux::platformActiveSelectionBackgroundColor() const
{
    return m_activeSelectionBackgroundColor;
}

Color RenderThemeChromiumLinux::platformInactiveSelectionBackgroundColor() const
{
    return m_inactiveSelectionBackgroundColor;
}

Color RenderThemeChromiumLinux::platformActiveSelectionForegroundColor() const
{
    return m_activeSelectionForegroundColor;
}

Color RenderThemeChromiumLinux::platformInactiveSelectionForegroundColor() const
{
    return m_inactiveSelectionForegroundColor;
}

void RenderThemeChromiumLinux::adjustSliderThumbSize(RenderObject* o) const
{
    // These sizes match the sizes in Chromium Win.
    const int sliderThumbAlongAxis = 11;
    const int sliderThumbAcrossAxis = 21;
    if (o->style()->appearance() == SliderThumbHorizontalPart) {
        o->style()->setWidth(Length(sliderThumbAlongAxis, Fixed));
        o->style()->setHeight(Length(sliderThumbAcrossAxis, Fixed));
    } else if (o->style()->appearance() == SliderThumbVerticalPart) {
        o->style()->setWidth(Length(sliderThumbAcrossAxis, Fixed));
        o->style()->setHeight(Length(sliderThumbAlongAxis, Fixed));
    } else
        RenderThemeChromiumSkia::adjustSliderThumbSize(o);
}

bool RenderThemeChromiumLinux::supportsControlTints() const
{
    return true;
}

void RenderThemeChromiumLinux::setCaretBlinkInterval(double interval)
{
    m_caretBlinkInterval = interval;
}

double RenderThemeChromiumLinux::caretBlinkIntervalInternal() const
{
    return m_caretBlinkInterval;
}

void RenderThemeChromiumLinux::setSelectionColors(
    unsigned activeBackgroundColor,
    unsigned activeForegroundColor,
    unsigned inactiveBackgroundColor,
    unsigned inactiveForegroundColor)
{
    m_activeSelectionBackgroundColor = activeBackgroundColor;
    m_activeSelectionForegroundColor = activeForegroundColor;
    m_inactiveSelectionBackgroundColor = inactiveBackgroundColor;
    m_inactiveSelectionForegroundColor = inactiveForegroundColor;
}

void RenderThemeChromiumLinux::setScrollbarColors(
    SkColor inactiveColor, SkColor activeColor, SkColor trackColor)
{
    m_thumbInactiveColor = inactiveColor;
    m_thumbActiveColor = activeColor;
    m_trackColor = trackColor;
}

} // namespace WebCore

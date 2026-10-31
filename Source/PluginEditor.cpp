// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#include "PluginEditor.h"
#include "BinaryData.h"
#include "VoiceModel.h"

namespace palette
{
const juce::Colour bgTop    { 0xff121d2a };
const juce::Colour bgBot    { 0xff090e15 };
const juce::Colour card     { 0xff111b27 };
const juce::Colour cardHi   { 0xff172433 };
const juce::Colour line     { 0xff223245 };
const juce::Colour lineHi   { 0xff33495f };
const juce::Colour text     { 0xffe8eef4 };
const juce::Colour dim      { 0xff8a9cae };
const juce::Colour faint    { 0xff55677a };
const juce::Colour cyan     { 0xff2ee6d6 };
const juce::Colour cyanDeep { 0xff1aa9bd };
const juce::Colour onCyan   { 0xff04212a };
const juce::Colour red      { 0xffff5a5f };
const juce::Colour amber    { 0xffffb547 };
}

namespace
{
constexpr float kBaseW = 880.0f, kBaseH = 600.0f;

// Embedded font: Inter with softened (rounded) corners, see Resources/Inter-LICENSE.txt
juce::Font makeFont (float height, int style = juce::Font::plain)
{
    // looked up by name at run time: if the font files are missing from the build the plugin still compiles
    // and simply uses the default font
    auto load = [] (const char* name)
    {
        int size = 0;
        const char* data = BinaryData::getNamedResource (name, size);
        return data != nullptr && size > 0 ? juce::Typeface::createSystemTypefaceFor (data, (size_t) size) : juce::Typeface::Ptr();
    };
    static const juce::Typeface::Ptr regular = load ("ScribaSans_Regular_ttf");
    static const juce::Typeface::Ptr bold = load ("ScribaSans_Bold_ttf");
    const auto& face = (style & juce::Font::bold) != 0 ? bold : regular;
    if (face != nullptr)
        return juce::Font (juce::FontOptions (face).withHeight (height));
    return juce::Font (juce::FontOptions (height, style));
}

juce::Font labelFont (float s) { return makeFont (10.5f * s, juce::Font::bold).withExtraKerningFactor (0.16f); }

juce::String formatTime (double seconds)
{
    const int s = juce::jmax (0, (int) seconds);
    return juce::String::formatted ("%d:%02d", s / 60, s % 60);
}

float textWidth (const juce::Font& f, const juce::String& t)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, t, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

void fillCard (juce::Graphics& g, juce::Rectangle<float> r, float s)
{
    const float corner = 14.0f * s;
    g.setGradientFill (juce::ColourGradient (palette::cardHi, r.getX(), r.getY(), palette::card, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (palette::line);
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

void drawLabel (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float s,
                juce::Justification j = juce::Justification::centredLeft, juce::Colour c = palette::dim)
{
    g.setColour (c);
    g.setFont (labelFont (s));
    g.drawText (t, r, j, false);
}

const juce::String kMinus (juce::CharPointer_UTF8 ("\xe2\x88\x92"));   // real minus sign
const juce::String kArrow (juce::CharPointer_UTF8 ("\xe2\x86\x92"));   // right arrow
const juce::String kDot (juce::CharPointer_UTF8 ("\xc2\xb7"));         // middle dot
const juce::String kCent (juce::CharPointer_UTF8 ("\xc2\xa2"));        // cent sign

juce::String signedSemis (int v)
{
    if (v > 0) return "+" + juce::String (v);
    if (v < 0) return kMinus + juce::String (-v);
    return "0";
}

juce::String centsText (double cents)
{
    const int c = (int) std::lround (cents);
    if (std::abs (c) < 5)
        return {};
    return (c > 0 ? "+" : kMinus) + juce::String (std::abs (c)) + kCent;
}

// small icons, drawn in a square
void drawInstrumentIcon (juce::Graphics& g, int which, juce::Rectangle<float> r, juce::Colour c, juce::Colour bg)
{
    const float w = r.getWidth();
    const float st = juce::jmax (1.2f, w * 0.06f);
    g.setColour (c);
    switch (which)
    {
        case 0:   // bass: 4 strings over a fretboard
        {
            for (int i = 0; i < 4; ++i)
            {
                const float y = r.getY() + w * (0.26f + 0.16f * (float) i);
                g.fillRoundedRectangle (r.getX() + w * 0.08f, y - st * (0.9f - 0.12f * (float) i) * 0.5f, w * 0.84f,
                                        st * (0.9f - 0.12f * (float) i) * 1.4f, st);
            }
            for (int i = 0; i < 3; ++i)
            {
                const float x = r.getX() + w * (0.30f + 0.22f * (float) i);
                g.fillRect (x, r.getY() + w * 0.18f, st * 0.5f, w * 0.64f);
            }
            break;
        }
        case 1:   // guitar: body, sound hole, neck
        {
            juce::Path body;
            body.addEllipse (r.getX() + w * 0.10f, r.getY() + w * 0.42f, w * 0.48f, w * 0.48f);
            body.addEllipse (r.getX() + w * 0.20f, r.getY() + w * 0.26f, w * 0.34f, w * 0.34f);
            g.fillPath (body);
            juce::Path neck;
            neck.addRoundedRectangle (-w * 0.05f, -w * 0.42f, w * 0.10f, w * 0.48f, w * 0.02f);
            neck.applyTransform (juce::AffineTransform::rotation (0.78f).translated (r.getX() + w * 0.52f, r.getY() + w * 0.46f));
            g.fillPath (neck);
            g.setColour (bg);
            g.fillEllipse (r.getX() + w * 0.27f, r.getY() + w * 0.53f, w * 0.14f, w * 0.14f);
            break;
        }
        case 2:   // piano: keys
        {
            auto k = juce::Rectangle<float> (r.getX() + w * 0.08f, r.getY() + w * 0.22f, w * 0.84f, w * 0.56f);
            g.drawRoundedRectangle (k, w * 0.05f, st);
            for (int i = 1; i < 4; ++i)
                g.fillRect (k.getX() + k.getWidth() * (float) i / 4.0f - st * 0.4f, k.getY(), st * 0.8f, k.getHeight());
            for (int i : { 1, 2, 3 })
                g.fillRoundedRectangle (k.getX() + k.getWidth() * (float) i / 4.0f - w * 0.06f, k.getY(), w * 0.12f, k.getHeight() * 0.58f, w * 0.02f);
            break;
        }
        case 3:   // kick: bass drum seen from the front
        {
            auto d = r.reduced (w * 0.12f);
            g.drawEllipse (d, st * 1.1f);
            g.drawEllipse (d.reduced (w * 0.13f), st * 0.8f);
            g.fillEllipse (d.withSizeKeepingCentre (w * 0.14f, w * 0.14f));
            break;
        }
        case 4:   // snare: drum with two sticks
        {
            const float x0 = r.getX() + w * 0.12f, x1 = r.getRight() - w * 0.12f;
            const float top = r.getY() + w * 0.44f, bot = r.getY() + w * 0.78f, eh = w * 0.18f;
            juce::Path p;
            p.addEllipse (x0, top - eh * 0.5f, x1 - x0, eh);
            g.strokePath (p, juce::PathStrokeType (st));
            g.drawLine (x0, top, x0, bot, st);
            g.drawLine (x1, top, x1, bot, st);
            juce::Path b;
            b.addCentredArc ((x0 + x1) * 0.5f, bot, (x1 - x0) * 0.5f, eh * 0.5f, 0.0f, juce::MathConstants<float>::halfPi,
                             juce::MathConstants<float>::pi + juce::MathConstants<float>::halfPi, true);
            g.strokePath (b, juce::PathStrokeType (st));
            g.drawLine (r.getX() + w * 0.26f, r.getY() + w * 0.10f, r.getX() + w * 0.52f, r.getY() + w * 0.40f, st);
            g.drawLine (r.getRight() - w * 0.26f, r.getY() + w * 0.10f, r.getRight() - w * 0.52f, r.getY() + w * 0.40f, st);
            break;
        }
        case 5:   // vocals: microphone
        {
            auto cap = juce::Rectangle<float> (r.getCentreX() - w * 0.15f, r.getY() + w * 0.08f, w * 0.30f, w * 0.46f);
            g.fillRoundedRectangle (cap, w * 0.15f);
            juce::Path arc;
            arc.addCentredArc (r.getCentreX(), r.getY() + w * 0.40f, w * 0.26f, w * 0.26f, 0.0f,
                               juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi + juce::MathConstants<float>::halfPi, true);
            g.strokePath (arc, juce::PathStrokeType (st, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillRect (r.getCentreX() - st * 0.5f, r.getY() + w * 0.66f, st, w * 0.18f);
            g.fillRoundedRectangle (r.getCentreX() - w * 0.17f, r.getY() + w * 0.83f, w * 0.34f, st, st * 0.5f);
            break;
        }
        default: break;
    }
}
} // namespace

//==============================================================================
ScribaLookAndFeel::ScribaLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, palette::cardHi);
    setColour (juce::PopupMenu::textColourId, palette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, palette::cyan.withAlpha (0.16f));
    setColour (juce::PopupMenu::highlightedTextColourId, palette::cyan);
    setColour (juce::ComboBox::textColourId, palette::text);
    setColour (juce::ComboBox::arrowColourId, palette::dim);
    setColour (juce::TooltipWindow::backgroundColourId, palette::cardHi);
    setColour (juce::TooltipWindow::textColourId, palette::text);
    setColour (juce::TooltipWindow::outlineColourId, palette::lineHi);
}

void ScribaLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    const float corner = (float) h * 0.28f;
    g.setColour (palette::bgBot.withAlpha (0.55f));
    g.fillRoundedRectangle (r, corner);
    const bool hot = box.isMouseOver (true) || box.isPopupActive();
    g.setColour (hot ? palette::cyan.withAlpha (0.7f) : palette::lineHi);
    g.drawRoundedRectangle (r, corner, 1.0f);

    juce::Path arrow;
    const float cx = (float) w - (float) h * 0.5f, cy = (float) h * 0.5f, a = (float) h * 0.12f;
    arrow.startNewSubPath (cx - a, cy - a * 0.5f);
    arrow.lineTo (cx, cy + a * 0.5f);
    arrow.lineTo (cx + a, cy - a * 0.5f);
    g.setColour (hot ? palette::cyan : palette::dim);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ScribaLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (12, 1, box.getWidth() - box.getHeight() - 12, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font ScribaLookAndFeel::getComboBoxFont (juce::ComboBox& box) { return makeFont (juce::jlimit (12.0f, 17.0f, (float) box.getHeight() * 0.42f)); }
juce::Font ScribaLookAndFeel::getPopupMenuFont()                    { return makeFont (14.0f); }

void ScribaLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (palette::cardHi);
    g.setColour (palette::lineHi);
    g.drawRect (0, 0, width, height, 1);
}

void ScribaLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                          float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    const float cy = (float) y + (float) height * 0.5f;
    const float r = juce::jlimit (6.0f, 10.0f, (float) height * 0.36f);
    const float x0 = (float) x + r, x1 = (float) (x + width) - r;
    const float th = juce::jmax (3.0f, r * 0.5f);

    g.setColour (palette::line);
    g.fillRoundedRectangle (x0, cy - th * 0.5f, x1 - x0, th, th * 0.5f);

    const float px = juce::jlimit (x0, x1, sliderPos);
    g.setGradientFill (juce::ColourGradient (palette::cyanDeep, x0, cy, palette::cyan, px, cy, false));
    g.fillRoundedRectangle (x0, cy - th * 0.5f, juce::jmax (th, px - x0), th, th * 0.5f);

    if (s.isMouseOverOrDragging())
    {
        g.setColour (palette::cyan.withAlpha (0.16f));
        g.fillEllipse (px - r * 1.8f, cy - r * 1.8f, r * 3.6f, r * 3.6f);
    }
    g.setColour (palette::text);
    g.fillEllipse (px - r, cy - r, r * 2.0f, r * 2.0f);
    g.setColour (palette::cyan);
    g.fillEllipse (px - r * 0.42f, cy - r * 0.42f, r * 0.84f, r * 0.84f);
}

juce::Rectangle<int> ScribaLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const auto f = makeFont (13.0f);
    const int w = (int) std::ceil (juce::jmin (320.0f, textWidth (f, tipText) + 24.0f));
    const int lines = (int) std::ceil ((textWidth (f, tipText) + 24.0f) / 320.0f);
    const int h = 10 + 18 * juce::jmax (1, lines);
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void ScribaLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (palette::cardHi);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (palette::lineHi);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (palette::text);
    g.setFont (makeFont (13.0f));
    g.drawFittedText (text, r.reduced (12.0f, 5.0f).toNearestInt(), juce::Justification::centredLeft, 4);
}

//==============================================================================
void PrimaryButton::setMode (Mode m, const juce::String& label, float p)
{
    if (m != mode || label != getButtonText() || std::abs (p - progress) > 0.002f || m == Mode::Waiting || m == Mode::Recording)
    {
        mode = m;
        progress = p;
        setButtonText (label);
        repaint();
    }
}

void PrimaryButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float corner = r.getHeight() * 0.26f;
    const float h = r.getHeight();

    juce::Colour fg = palette::onCyan;
    switch (mode)
    {
        case Mode::Ready:
        {
            auto top = palette::cyan.brighter (over ? 0.12f : 0.0f), bot = palette::cyanDeep.brighter (over ? 0.12f : 0.0f);
            if (down) { top = top.darker (0.15f); bot = bot.darker (0.15f); }
            g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bot, r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, corner);
            break;
        }
        case Mode::Waiting:
        {
            const float a = 0.5f + 0.5f * std::sin (phase * 1.3f);
            g.setColour (palette::cyan.withAlpha (0.07f + 0.06f * a + (over ? 0.05f : 0.0f)));
            g.fillRoundedRectangle (r, corner);
            g.setColour (palette::cyan.withAlpha (0.55f + 0.4f * a));
            g.drawRoundedRectangle (r.reduced (0.75f), corner, 1.5f);
            fg = palette::cyan;
            break;
        }
        case Mode::Recording:
        {
            auto c = palette::red.brighter (over ? 0.1f : 0.0f);
            if (down) c = c.darker (0.15f);
            g.setGradientFill (juce::ColourGradient (c, r.getX(), r.getY(), c.darker (0.25f), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, corner);
            fg = juce::Colours::white;
            break;
        }
        case Mode::Analysing:
        {
            g.setColour (palette::bgBot.withAlpha (0.6f));
            g.fillRoundedRectangle (r, corner);
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip;
            clip.addRoundedRectangle (r, corner);
            g.reduceClipRegion (clip);
            g.setGradientFill (juce::ColourGradient (palette::cyanDeep.withAlpha (0.55f), r.getX(), 0.0f,
                                                     palette::cyan.withAlpha (0.55f), r.getX() + r.getWidth() * progress, 0.0f, false));
            g.fillRect (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, progress)));
            fg = palette::text;
            break;
        }
    }
    if (mode == Mode::Analysing)
    {
        g.setColour (palette::lineHi);
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    }

    // icon + text, centred together
    const auto font = makeFont (h * 0.30f, juce::Font::bold).withExtraKerningFactor (0.10f);
    const float tw = textWidth (font, getButtonText());
    const float ic = h * 0.30f;
    const bool hasIcon = mode != Mode::Analysing;
    const float total = tw + (hasIcon ? ic + h * 0.22f : 0.0f);
    float x = r.getCentreX() - total * 0.5f;
    const float cy = r.getCentreY();

    g.setColour (fg);
    if (hasIcon)
    {
        auto ir = juce::Rectangle<float> (x, cy - ic * 0.5f, ic, ic);
        if (mode == Mode::Ready)
            g.fillEllipse (ir);
        else if (mode == Mode::Waiting)
        {
            juce::Path tri;   // "play" triangle: waiting for the DAW to play
            tri.addTriangle (ir.getX() + ic * 0.12f, ir.getY(), ir.getX() + ic * 0.12f, ir.getBottom(), ir.getRight(), cy);
            g.fillPath (tri);
        }
        else
            g.fillRoundedRectangle (ir.reduced (ic * 0.06f), ic * 0.18f);
        x += ic + h * 0.22f;
    }
    g.setFont (font);
    g.drawText (getButtonText(), juce::Rectangle<float> (x, r.getY(), tw + 4.0f, r.getHeight()), juce::Justification::centredLeft, false);
}

void ActionButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight();
    const float corner = h * 0.26f;
    const bool on = isEnabled();

    g.setColour (on ? palette::cardHi.brighter (down ? 0.18f : over ? 0.10f : 0.0f) : palette::card);
    g.fillRoundedRectangle (r, corner);
    g.setColour (on ? (over ? palette::cyan.withAlpha (0.8f) : palette::lineHi) : palette::line);
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);

    const juce::Colour c = on ? (over ? palette::cyan : palette::text) : palette::faint;
    const auto font = makeFont (h * 0.30f, juce::Font::bold).withExtraKerningFactor (0.08f);
    const float tw = textWidth (font, getButtonText());
    const float ic = h * 0.40f;
    const float total = ic + h * 0.22f + tw;
    float x = r.getCentreX() - total * 0.5f;
    auto ir = juce::Rectangle<float> (x, r.getCentreY() - ic * 0.5f, ic, ic);
    const float st = juce::jmax (1.2f, ic * 0.09f);

    g.setColour (c);
    if (icon == Icon::Text)
    {
        // a sheet with lines
        auto sheet = ir.withSizeKeepingCentre (ic * 0.74f, ic);
        g.drawRoundedRectangle (sheet, ic * 0.10f, st);
        for (int i = 0; i < 3; ++i)
            g.fillRect (sheet.getX() + ic * 0.16f, sheet.getY() + ic * (0.26f + 0.2f * (float) i), sheet.getWidth() - ic * 0.32f - (i == 2 ? ic * 0.16f : 0.0f), st);
    }
    else
    {
        // MIDI connector: circle with 5 pins
        g.drawEllipse (ir.reduced (st * 0.5f), st);
        const auto cc = ir.getCentre();
        const float rr = ic * 0.26f, pin = ic * 0.13f;
        for (int i = 0; i < 5; ++i)
        {
            const float a = juce::MathConstants<float>::pi * (0.0f + (float) i * 0.25f);
            g.fillEllipse (cc.x - rr * std::cos (a) - pin * 0.5f, cc.y + rr * std::sin (a) * 0.9f - pin * 0.5f - ic * 0.06f, pin, pin);
        }
    }
    x += ic + h * 0.22f;
    g.setFont (font);
    g.drawText (getButtonText(), juce::Rectangle<float> (x, r.getY(), tw + 4.0f, h), juce::Justification::centredLeft, false);
}

//==============================================================================
void ChipButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float corner = r.getHeight() * 0.3f;
    const bool on = isEnabled();
    const bool lit = accent || getToggleState();

    if (lit && on)
    {
        g.setColour (accent ? palette::cyan.brighter (over ? 0.1f : 0.0f).darker (down ? 0.15f : 0.0f)
                            : palette::cyan.withAlpha (over ? 0.24f : 0.16f));
        g.fillRoundedRectangle (r, corner);
        g.setColour (palette::cyan);
        g.drawRoundedRectangle (r, corner, 1.2f);
    }
    else
    {
        g.setColour (palette::cardHi.brighter (on ? (down ? 0.18f : over ? 0.10f : 0.0f) : 0.0f));
        g.fillRoundedRectangle (r, corner);
        g.setColour (on ? (over ? palette::cyan.withAlpha (0.7f) : palette::lineHi) : palette::line);
        g.drawRoundedRectangle (r, corner, 1.0f);
    }
    const juce::Colour c = ! on ? palette::faint : accent ? palette::onCyan : lit ? palette::cyan : (over ? palette::cyan : palette::text);
    g.setColour (c);
    const bool symbol = getButtonText().length() <= 1;
    g.setFont (makeFont (r.getHeight() * (symbol ? 0.55f : 0.36f), juce::Font::bold).withExtraKerningFactor (symbol ? 0.0f : 0.08f));
    g.drawText (getButtonText(), r, juce::Justification::centred, false);
}

//==============================================================================
void ChordPad::set (const scriba::ChordDef& c, bool isTonic, bool used)
{
    if (c.name == chord.name && c.roman == chord.roman && isTonic == tonic && used == inSong)
        return;
    chord = c;
    tonic = isTonic;
    inSong = used;
    setTooltip ("Click to listen to " + juce::String (c.name) + ", drag it onto a track of your DAW to drop its MIDI");
    repaint();
}

void ChordPad::mouseDown (const juce::MouseEvent& e)
{
    dragged = false;
    juce::Button::mouseDown (e);
    if (onPress)
        onPress (chord);
}

void ChordPad::mouseDrag (const juce::MouseEvent& e)
{
    juce::Button::mouseDrag (e);
    if (! dragged && e.getDistanceFromDragStart() > 8 && onDragOut)
    {
        dragged = true;
        onDragOut (chord);
    }
}

void ChordPad::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float h = r.getHeight();
    const float corner = juce::jmin (h * 0.22f, 10.0f * h / 50.0f);

    juce::Colour fill = tonic ? palette::card.interpolatedWith (palette::cyan, 0.16f) : palette::cardHi;
    fill = fill.brighter (down ? 0.22f : over ? 0.10f : 0.0f);
    g.setGradientFill (juce::ColourGradient (fill.brighter (0.06f), r.getX(), r.getY(), fill.darker (0.10f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (tonic ? palette::cyan : over ? palette::cyan.withAlpha (0.75f) : palette::lineHi);
    g.drawRoundedRectangle (r.reduced (0.5f), corner, tonic || over ? 1.4f : 1.0f);

    // chord name, big; the degree under it
    g.setColour (tonic ? palette::cyan : palette::text);
    auto nameFont = makeFont (h * 0.36f, juce::Font::bold);
    const juce::String name (juce::CharPointer_UTF8 (chord.name.c_str()));
    const float maxW = r.getWidth() - h * 0.22f;
    if (textWidth (nameFont, name) > maxW)
        nameFont = makeFont (h * 0.36f * maxW / textWidth (nameFont, name), juce::Font::bold);
    g.setFont (nameFont);
    g.drawText (name, r.withTrimmedBottom (h * 0.30f).withTrimmedTop (h * 0.06f), juce::Justification::centred, false);
    g.setColour (tonic ? palette::cyan.withAlpha (0.8f) : palette::dim);
    g.setFont (makeFont (h * 0.20f, juce::Font::bold).withExtraKerningFactor (0.06f));
    g.drawText (juce::String (juce::CharPointer_UTF8 (chord.roman.c_str())), r.withTrimmedTop (h * 0.58f).withTrimmedBottom (h * 0.10f),
                juce::Justification::centred, false);

    // amber dot: this chord is played in the song
    if (inSong)
    {
        const float d = h * 0.11f;
        g.setColour (palette::amber);
        g.fillEllipse (r.getRight() - d - h * 0.12f, r.getY() + h * 0.12f, d, d);
    }
    // grip (six dots) on hover: it can be dragged
    if (over)
    {
        g.setColour (palette::cyan.withAlpha (0.7f));
        const float d = juce::jmax (1.5f, h * 0.045f), gap = d * 1.9f;
        const float x0 = r.getX() + h * 0.12f, y0 = r.getY() + h * 0.13f;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 2; ++j)
                g.fillEllipse (x0 + (float) j * gap, y0 + (float) i * gap, d, d);
    }
}

//==============================================================================
InstrumentSelector::InstrumentSelector (juce::RangedAudioParameter& param)
    : attachment (param, [this] (float v) { selected = juce::jlimit (0, 5, (int) std::lround (v)); repaint(); })
{
    attachment.sendInitialUpdate();
    setRepaintsOnMouseActivity (true);
}

int InstrumentSelector::indexAt (juce::Point<float> p) const
{
    if (! getLocalBounds().toFloat().contains (p))
        return -1;
    return juce::jlimit (0, 5, (int) (p.x / ((float) getWidth() / 6.0f)));
}

void InstrumentSelector::mouseMove (const juce::MouseEvent& e)
{
    const int h = indexAt (e.position);
    if (h != hover)
    {
        hover = h;
        repaint();
    }
}

void InstrumentSelector::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void InstrumentSelector::mouseDown (const juce::MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i >= 0 && i != selected)
        attachment.setValueAsCompleteGesture ((float) i);
}

void InstrumentSelector::paint (juce::Graphics& g)
{
    static const char* names[6] = { "BASS", "GUITAR", "PIANO", "KICK", "SNARE", "VOCALS" };
    auto r = getLocalBounds().toFloat();
    const float h = r.getHeight();
    const float corner = h * 0.2f;
    const float gap = h * 0.08f;
    const float w = (r.getWidth() - gap * 5.0f) / 6.0f;

    for (int i = 0; i < 6; ++i)
    {
        auto b = juce::Rectangle<float> (r.getX() + (float) i * (w + gap), r.getY(), w, h).reduced (0.5f);
        const bool sel = i == selected, hot = i == hover;
        g.setColour (sel ? palette::cyan.withAlpha (0.13f) : palette::cardHi.brighter (hot ? 0.08f : 0.0f));
        g.fillRoundedRectangle (b, corner);
        g.setColour (sel ? palette::cyan : (hot ? palette::lineHi.brighter (0.3f) : palette::line));
        g.drawRoundedRectangle (b, corner, sel ? 1.5f : 1.0f);

        const juce::Colour c = sel ? palette::cyan : (hot ? palette::text : palette::dim);
        const float ic = h * 0.42f;
        drawInstrumentIcon (g, i, juce::Rectangle<float> (b.getCentreX() - ic * 0.5f, b.getY() + h * 0.12f, ic, ic), c,
                            sel ? palette::card.interpolatedWith (palette::cyan, 0.13f) : palette::cardHi);
        g.setColour (sel ? palette::cyan : (hot ? palette::text : palette::text.withAlpha (0.75f)));
        g.setFont (makeFont (h * 0.165f, juce::Font::bold).withExtraKerningFactor (0.06f));
        g.drawText (names[i], b.withTop (b.getY() + h * 0.62f).withHeight (h * 0.26f), juce::Justification::centred, false);
    }
}

//==============================================================================
ScribaEditor::ScribaEditor (ScribaProcessor& p)
    : juce::AudioProcessorEditor (&p),
      proc (p),
      instrumentSelector (*p.apvts.getParameter ("instrument"))
{
    setLookAndFeel (&lnf);
    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);

    addAndMakeVisible (instrumentSelector);

    sensitivitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    sensitivitySlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    sensitivitySlider.setTooltip ("Higher = more notes or hits (some doubtful). Lower = fewer, more reliable. Changing it re-analyzes the same recording.");
    addAndMakeVisible (sensitivitySlider);
    sensitivityAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, "sensitivity", sensitivitySlider);
    // set after the attachment: its first value must not start a new analysis every time the window opens
    sensitivitySlider.onValueChange = [this] { reanalyseCountdown = 12; };

    gateSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gateSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gateSlider.setTooltip ("Gate threshold. Left = it opens on quieter hits too, right = only the loud hits. "
                           "You hear it live with LISTEN on, and the MIDI has exactly these hits.");
    addChildComponent (gateSlider);
    gateAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, "gate", gateSlider);

    listenButton.setClickingTogglesState (true);
    listenButton.setTooltip ("On: you hear the track through the gate (and the pitch). Off: you hear the original track.");
    addChildComponent (listenButton);
    listenAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "gateListen", listenButton);

    pitchDown.setTooltip ("Snare pitch: one semitone lower");
    pitchUp.setTooltip ("Snare pitch: one semitone higher");
    pitchZero.setTooltip ("Snare pitch back to the original");
    pitchSet.setTooltip ("Set the pitch to the suggested value: the snare is in tune with the song");
    pitchSet.accent = true;
    pitchDown.onClick = [this] { setPitch (std::round (proc.getPitch() * 100.0f) / 100.0f - 1.0f); };
    pitchUp.onClick = [this] { setPitch (std::round (proc.getPitch() * 100.0f) / 100.0f + 1.0f); };
    pitchZero.onClick = [this] { setPitch (0.0f); };
    pitchSet.onClick = [this] { if (tuning.valid) setPitch ((float) tuning.main.shift); };
    for (auto* b : { &pitchDown, &pitchUp, &pitchZero, &pitchSet })
        addChildComponent (b);

    // the items must exist BEFORE the attachment, or the box stays empty
    if (auto* keyParam = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("songKey")))
        songKeyBox.addItemList (keyParam->choices, 1);
    songKeyBox.setTooltip ("Auto: the key comes from the Scriba on the bass / guitar / piano tracks. Or choose it by hand.");
    addChildComponent (songKeyBox);
    songKeyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "songKey", songKeyBox);

    if (auto* langParam = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("lyricsLang")))
        languageBox.addItemList (langParam->choices, 1);
    languageBox.setTooltip ("Language of the lyrics. Auto chooses between Italian and English.");
    addChildComponent (languageBox);
    languageAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "lyricsLang", languageBox);
    languageBox.onChange = [this] { if (selected == scriba::Instrument::Vocals) reanalyseCountdown = 4; };

    modelButton.accent = true;
    modelButton.onClick = []
    {
        auto& vm = scriba::VoiceModel::get();
        if (vm.getStatus() == scriba::VoiceModel::Status::Downloading)
            vm.cancelDownload();
        else
            vm.startDownload();
    };
    folderButton.setTooltip ("Opens the folder of the voice model (you can also put a whisper.cpp ggml-*.bin model there by hand)");
    folderButton.onClick = []
    {
        auto f = scriba::VoiceModel::folder();
        f.createDirectory();
        f.startAsProcess();
    };
    addChildComponent (modelButton);
    addChildComponent (folderButton);

    mainButton.onClick = [this] { mainButtonClicked(); };
    addAndMakeVisible (mainButton);

    txtButton.setButtonText ("SAVE TXT");
    txtButton.onClick = [this] { exportText(); };
    addAndMakeVisible (txtButton);

    midiButton.setButtonText ("DRAG MIDI");
    midiButton.setTooltip ("Drag onto the DAW timeline, or click to save the .mid file");
    midiButton.onClick = [this] { if (! midiButton.wasDragged) saveMidi(); };
    midiButton.onDragOut = [this] { dragMidi(); };
    addAndMakeVisible (midiButton);

    // bottom card: WAVE / CHORDS tabs
    waveTab.setTooltip ("The waveform and the notes (the gate meter for kick and snare)");
    chordsTab.setTooltip ("The chords of the key: click to listen, drag into your DAW");
    waveTab.onClick = [this] { setTab (false); };
    chordsTab.onClick = [this] { setTab (true); };
    addAndMakeVisible (waveTab);
    addAndMakeVisible (chordsTab);
    for (auto& pad : pads)
    {
        pad.onPress = [this] (const scriba::ChordDef& c) { proc.previewChord (c); };
        pad.onDragOut = [this, &pad] (const scriba::ChordDef& c) { dragChord (c, &pad); };
        addChildComponent (pad);
    }
    songButton.setTooltip ("All the chords found in the song, played in the style you chose. "
                           "Drop it at bar 1 of your project: it lines up with the song.");
    songButton.onDragOut = [this] { dragSong(); };
    songButton.onClick = [this] { if (! songButton.wasDragged) flash ("DRAG IT ONTO A TRACK"); };
    addChildComponent (songButton);
    {
        const char* ids[4] = { "chordType", "chordStyle", "chordVoicing", "chordLength" };
        juce::ComboBox* boxes[4] = { &chordTypeBox, &chordStyleBox, &chordVoicingBox, &chordLengthBox };
        const char* tips[4] = { "Triads, sevenths, sus2, sus4 or add9 chords",
                                "How the chord is played: held, strummed, repeated, arpeggios, Alberti bass, ballad (left + right hand)",
                                "Where the notes sit: close, inversions, open (wide) or with the bass note",
                                "Length of the dragged chord" };
        for (int i = 0; i < 4; ++i)
        {
            if (auto* prm = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (ids[i])))
                boxes[i]->addItemList (prm->choices, 1);
            boxes[i]->setTooltip (tips[i]);
            addChildComponent (boxes[i]);
            chordAttach.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ids[i], *boxes[i]));
        }
    }

    setResizable (true, true);
    setResizeLimits (704, 480, 1320, 900);
    getConstrainer()->setFixedAspectRatio (kBaseW / kBaseH);
    setSize ((int) kBaseW, (int) kBaseH);

    refresh();   // texts and buttons are right from the first frame
    startTimerHz (30);
}

ScribaEditor::~ScribaEditor()
{
    stopTimer();
    proc.stopPreview();
    setLookAndFeel (nullptr);
}

//==============================================================================
void ScribaEditor::resized()
{
    auto b = getLocalBounds().toFloat();
    const float s = scale = b.getWidth() / kBaseW;

    headerArea = b.removeFromTop (64.0f * s);
    b = b.reduced (24.0f * s, 0.0f);
    b.removeFromTop (14.0f * s);
    b.removeFromBottom (22.0f * s);

    waveCard = b.removeFromBottom (144.0f * s);
    b.removeFromBottom (14.0f * s);

    leftCard = b.removeFromLeft (372.0f * s);
    b.removeFromLeft (20.0f * s);
    rightArea = b;

    auto p = rightArea;
    instLabelArea = p.removeFromTop (14.0f * s);
    p.removeFromTop (8.0f * s);
    instrumentSelector.setBounds (p.removeFromTop (66.0f * s).toNearestInt());
    p.removeFromTop (16.0f * s);

    sensLabelArea = p.removeFromTop (14.0f * s);
    p.removeFromTop (2.0f * s);
    sliderRow = p.removeFromTop (26.0f * s);
    {
        auto row = sliderRow;
        if (scriba::isDrum (selected))
        {
            listenButton.setBounds (row.removeFromRight (86.0f * s).reduced (0.0f, 0.5f).toNearestInt());
            row.removeFromRight (14.0f * s);
        }
        sensitivitySlider.setBounds (row.toNearestInt().expanded ((int) (4.0f * s), 0));
        languageBox.setBounds (sliderRow.expanded (0.0f, 4.0f * s).toNearestInt());
        gateSlider.setBounds (row.toNearestInt().expanded ((int) (4.0f * s), 0));
    }
    laidOutFor = (int) selected;
    sensHintArea = p.removeFromTop (14.0f * s);
    p.removeFromTop (12.0f * s);

    keyRowArea = p.removeFromTop (54.0f * s);
    songKeyBox.setBounds (keyRowArea.withTrimmedTop (20.0f * s).toNearestInt());

    // vocals: model download buttons at the bottom of the lyrics card
    {
        auto lc = leftCard.reduced (20.0f * s, 16.0f * s);
        auto row = lc.removeFromBottom (38.0f * s);
        folderButton.setBounds (row.removeFromRight (120.0f * s).toNearestInt());
        row.removeFromRight (8.0f * s);
        modelButton.setBounds (row.toNearestInt());
    }

    // snare pitch row, inside the snare card above the song key line
    {
        auto lc = leftCard.reduced (20.0f * s, 16.0f * s);
        lc.removeFromBottom (34.0f * s);
        pitchRowArea = lc.removeFromBottom (48.0f * s);
        auto pr = pitchRowArea.withTrimmedTop (6.0f * s).withTrimmedBottom (8.0f * s);
        pitchSet.setBounds (pr.removeFromRight (104.0f * s).toNearestInt());
        pr.removeFromRight (8.0f * s);
        pitchZero.setBounds (pr.removeFromRight (34.0f * s).toNearestInt());
        pr.removeFromRight (5.0f * s);
        pitchUp.setBounds (pr.removeFromRight (34.0f * s).toNearestInt());
        pr.removeFromRight (5.0f * s);
        pitchDown.setBounds (pr.removeFromRight (34.0f * s).toNearestInt());
    }

    // bottom card: tabs on the right of the top row, chord pads, chord options
    {
        auto top = waveCard.reduced (16.0f * s, 0.0f).removeFromTop (34.0f * s).withTrimmedTop (9.0f * s);
        chordsTab.setBounds (top.removeFromRight (74.0f * s).toNearestInt());
        top.removeFromRight (6.0f * s);
        waveTab.setBounds (top.removeFromRight (62.0f * s).toNearestInt());
        top.removeFromRight (12.0f * s);
        songButton.setBounds (top.removeFromRight (136.0f * s).toNearestInt());

        auto body = waveCard.reduced (16.0f * s, 0.0f).withTrimmedTop (42.0f * s).withTrimmedBottom (12.0f * s);
        auto opts = body.removeFromBottom (24.0f * s);
        body.removeFromBottom (8.0f * s);
        padsArea = body;
        const int n = juce::jmax (1, padCount);
        const float gap = 8.0f * s;
        const float w = (padsArea.getWidth() - gap * (float) (n - 1)) / (float) n;
        for (int i = 0; i < (int) pads.size(); ++i)
            pads[(size_t) i].setBounds (juce::Rectangle<float> (padsArea.getX() + (float) i * (w + gap), padsArea.getY(), w, padsArea.getHeight()).toNearestInt());

        juce::ComboBox* boxes[4] = { &chordTypeBox, &chordStyleBox, &chordVoicingBox, &chordLengthBox };
        const float labelW[4] = { 38.0f, 38.0f, 58.0f, 60.0f };
        const float boxW[4] = { 100.0f, 196.0f, 126.0f, 92.0f };
        float total = 0.0f;
        for (int i = 0; i < 4; ++i)
            total += (labelW[i] + boxW[i]) * s;
        const float spacing = (opts.getWidth() - total) / 3.0f;
        float x = opts.getX();
        for (int i = 0; i < 4; ++i)
        {
            chordLabelAreas[(size_t) i] = juce::Rectangle<float> (x, opts.getY(), labelW[i] * s, opts.getHeight());
            x += labelW[i] * s;
            boxes[i]->setBounds (juce::Rectangle<float> (x, opts.getY(), boxW[i] * s, opts.getHeight()).toNearestInt());
            x += boxW[i] * s + spacing;
        }
    }

    auto bottom = p.removeFromBottom (42.0f * s);
    auto left = bottom.removeFromLeft (bottom.getWidth() * 0.5f - 6.0f * s);
    bottom.removeFromLeft (12.0f * s);
    txtButton.setBounds (left.toNearestInt());
    midiButton.setBounds (bottom.toNearestInt());
    p.removeFromBottom (12.0f * s);
    mainButton.setBounds (p.removeFromBottom (54.0f * s).toNearestInt());
}

//==============================================================================
bool ScribaEditor::showResults() const
{
    return resultsOk && analysed == selected
           && (state == ScribaProcessor::State::Done || state == ScribaProcessor::State::Idle);
}

juce::String ScribaEditor::statusText() const
{
    if (flashText.isNotEmpty() && juce::Time::getMillisecondCounter() < flashUntil)
        return flashText;
    switch (state)
    {
        case ScribaProcessor::State::Idle:      return resultsOk ? "SAVED RESULT" : "READY";
        case ScribaProcessor::State::Armed:     return "WAITING FOR PLAY";
        case ScribaProcessor::State::Recording: return "REC  " + formatTime (proc.getRecordedSeconds());
        case ScribaProcessor::State::Analysing: return "ANALYZING  " + juce::String ((int) std::lround (proc.getProgress() * 100.0f)) + "%";
        case ScribaProcessor::State::Done:      return resultsOk ? "DONE" : "NO DATA";
    }
    return {};
}

juce::String ScribaEditor::baseFileName() const
{
    return juce::String ("Scriba_") + scriba::instrumentName (proc.getAnalysedInstrument());
}

void ScribaEditor::flash (const juce::String& message)
{
    flashText = message;
    flashUntil = juce::Time::getMillisecondCounter() + 2500;
}

void ScribaEditor::mainButtonClicked()
{
    switch (state)
    {
        case ScribaProcessor::State::Armed:
        case ScribaProcessor::State::Recording:  proc.stopCapture(); break;
        case ScribaProcessor::State::Analysing:  break;
        default:                                 proc.startCapture(); break;
    }
    refresh();
}

void ScribaEditor::exportText()
{
    chooser = std::make_unique<juce::FileChooser> ("Save the text file",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (baseFileName() + ".txt"),
                                                   "*.txt");
    juce::Component::SafePointer<ScribaEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (safe == nullptr || f == juce::File())
                                  return;
                              if (f.getFileExtension().isEmpty())
                                  f = f.withFileExtension ("txt");

                              // the String must live while it is written (toRawUTF8 points into it)
                              const juce::String text = safe->proc.buildTextReport();
                              // UTF-8 con BOM, cosi' il Blocco note mostra bene i simboli
                              juce::FileOutputStream os (f);
                              bool ok = os.openedOk();
                              if (ok)
                              {
                                  os.setPosition (0);
                                  os.truncate();
                                  ok = os.write ("\xEF\xBB\xBF", 3) && os.writeText (text, false, false, "\r\n");
                              }
                              safe->flash (ok ? "SAVED " + f.getFileName().toUpperCase() : "COULD NOT SAVE");
                          });
}

bool ScribaEditor::writeMidi (const juce::File& f)
{
    const auto bytes = proc.buildMidiBytes();
    juce::FileOutputStream os (f);
    if (! os.openedOk())
        return false;
    os.setPosition (0);
    os.truncate();
    return os.write (bytes.data(), bytes.size());
}

void ScribaEditor::saveMidi()
{
    chooser = std::make_unique<juce::FileChooser> ("Save the MIDI file",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (baseFileName() + ".mid"),
                                                   "*.mid");
    juce::Component::SafePointer<ScribaEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (safe == nullptr || f == juce::File())
                                  return;
                              if (f.getFileExtension().isEmpty())
                                  f = f.withFileExtension ("mid");
                              safe->flash (safe->writeMidi (f) ? "SAVED " + f.getFileName().toUpperCase() : "COULD NOT SAVE");
                          });
}

void ScribaEditor::dragMidi()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Scriba");
    dir.createDirectory();
    const auto f = dir.getChildFile (baseFileName() + ".mid");
    if (! writeMidi (f))
        return;

    juce::Component::SafePointer<MidiDragButton> btn (&midiButton);
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, &midiButton,
                                                                [btn] { if (btn != nullptr) btn->setState (juce::Button::buttonNormal); });
}

//==============================================================================
juce::Rectangle<float> ScribaEditor::meterArea() const
{
    return waveCard.reduced (16.0f * scale, 0.0f).withTrimmedTop (38.0f * scale).withTrimmedBottom (12.0f * scale);
}

namespace
{
constexpr float kMeterFloorDb = -60.0f;
float dbToY (float db, juce::Rectangle<float> r)
{
    return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db - kMeterFloorDb) / -kMeterFloorDb);
}
}

bool ScribaEditor::overThresholdLine (juce::Point<float> p) const
{
    const auto r = meterArea();
    return scriba::isDrum (selected) && ! chordsView && r.expanded (0.0f, 6.0f * scale).contains (p);
}

void ScribaEditor::setGateFromY (float y)
{
    const auto r = meterArea();
    const float db = kMeterFloorDb * (1.0f - juce::jlimit (0.0f, 1.0f, (r.getBottom() - y) / r.getHeight()));
    if (auto* prm = proc.apvts.getParameter ("gate"))
        prm->setValueNotifyingHost (prm->convertTo0to1 (std::round (db * 2.0f) / 2.0f));
}

void ScribaEditor::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (overThresholdLine (e.position) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void ScribaEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! overThresholdLine (e.position))
        return;
    draggingGate = true;
    if (auto* prm = proc.apvts.getParameter ("gate"))
        prm->beginChangeGesture();
    setGateFromY (e.position.y);
}

void ScribaEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingGate)
        setGateFromY (e.position.y);
}

void ScribaEditor::mouseUp (const juce::MouseEvent&)
{
    if (! draggingGate)
        return;
    draggingGate = false;
    if (auto* prm = proc.apvts.getParameter ("gate"))
        prm->endChangeGesture();
}

void ScribaEditor::setPitch (float semitones)
{
    if (auto* prm = proc.apvts.getParameter ("pitch"))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (-12.0f, 12.0f, semitones)));
        prm->endChangeGesture();
    }
}

//==============================================================================
void ScribaEditor::refresh()
{
    const auto newState = proc.getState();
    const bool stateChanged = newState != state;
    state = newState;
    selected = proc.getSelectedInstrument();

    const int version = proc.getResultsVersion();
    if (stateChanged || lastSelected < 0 || version != seenResultsVersion)
    {
        seenResultsVersion = version;
        resultsOk = proc.hasResults();
        analysed = proc.getAnalysedInstrument();
        audioKept = proc.hasAudio();
        if (state == ScribaProcessor::State::Done || state == ScribaProcessor::State::Idle)
        {
            result = proc.getResult();
            notes = proc.getNotes();
            overview = proc.getOverview();
            drum = proc.getDrumResult();
            lyricsShown = proc.getLyrics();
        }
        if (state == ScribaProcessor::State::Armed)
            history.clear();
    }

    // the song key of the snare can change at any moment (another Scriba finished its analysis)
    songKey = proc.getSongKey();
    tuning = (resultsOk && analysed == scriba::Instrument::Snare && drum.pitch.valid && songKey.valid)
                 ? scriba::computeSnareTuning (drum.pitch, songKey.key)
                 : scriba::SnareTuning();

    // main button
    using M = PrimaryButton::Mode;
    switch (state)
    {
        case ScribaProcessor::State::Idle:      mainButton.setMode (M::Ready, "ANALYZE", 0.0f); break;
        case ScribaProcessor::State::Armed:     mainButton.setMode (M::Waiting, "PRESS PLAY IN YOUR DAW", 0.0f); break;
        case ScribaProcessor::State::Recording: mainButton.setMode (M::Recording, "STOP & ANALYZE", 0.0f); break;
        case ScribaProcessor::State::Analysing: mainButton.setMode (M::Analysing, "ANALYZING  " + juce::String ((int) std::lround (proc.getProgress() * 100.0f)) + "%", proc.getProgress()); break;
        case ScribaProcessor::State::Done:      mainButton.setMode (M::Ready, "ANALYZE AGAIN", 0.0f); break;
    }
    mainButton.setEnabled (state != ScribaProcessor::State::Analysing);
    mainButton.setTooltip (state == ScribaProcessor::State::Armed ? "Recording starts with the DAW transport. Click to cancel."
                           : state == ScribaProcessor::State::Recording ? "Stop the DAW or click here to stop and analyze."
                                                                        : "Arm Scriba, then play the song in your DAW from the start.");

    const bool ready = showResults();
    const bool kick = analysed == scriba::Instrument::Kick;
    txtButton.setEnabled (ready && ! kick);
    txtButton.setTooltip (kick ? "Kick: MIDI only (no text file)"
                               : analysed == scriba::Instrument::Vocals ? "Save the lyrics (with the chords above the words) as a text file"
                               : analysed == scriba::Instrument::Snare ? "Save the snare tuning as a text file"
                                                                       : "Save the chords / notes and the song map as a text file");
    midiButton.setEnabled (ready && ! notes.empty());

    const bool snare = selected == scriba::Instrument::Snare;
    const bool drums = scriba::isDrum (selected);
    songKeyBox.setVisible (snare);
    const bool vocals = selected == scriba::Instrument::Vocals;
    sensitivitySlider.setVisible (! drums && ! vocals);
    languageBox.setVisible (vocals);
    {
        const auto st = scriba::VoiceModel::get().getStatus();
        const bool needModel = vocals && st != scriba::VoiceModel::Status::Ready;
        modelButton.setVisible (needModel);
        folderButton.setVisible (needModel);
        const juce::String mt = st == scriba::VoiceModel::Status::Downloading
                                    ? "CANCEL  " + juce::String ((int) std::lround (scriba::VoiceModel::get().getProgress() * 100.0f)) + "%"
                                    : st == scriba::VoiceModel::Status::Failed ? "TRY AGAIN" : "DOWNLOAD VOICE MODEL";
        if (modelButton.getButtonText() != mt)
            modelButton.setButtonText (mt);
        modelButton.accent = st != scriba::VoiceModel::Status::Downloading;
    }
    gateSlider.setVisible (drums);
    listenButton.setVisible (drums);
    for (auto* b : { &pitchDown, &pitchUp, &pitchZero, &pitchSet })
        b->setVisible (snare);
    pitchSet.setEnabled (tuning.valid);
    {
        juce::String t = "SET";
        if (tuning.valid)
        {
            const double v = std::round (tuning.main.shift * 100.0) / 100.0;
            t += " " + (v > 0 ? juce::String ("+") : v < 0 ? kMinus : juce::String()) + juce::String (std::abs (v), 2);
        }
        if (pitchSet.getButtonText() != t)
            pitchSet.setButtonText (t);
    }
    // chord buttons: the chords of the key found (here or by the other Scriba)
    {
        if (stateChanged && state == ScribaProcessor::State::Armed)
            setTab (false);   // while recording: the waveform

        if (--songScanCountdown <= 0)
        {
            songScanCountdown = 15;
            songChordShapes.clear();
            const auto m = scriba::SharedKeyRegistry::get().songMap (-1);
            const int k = proc.getChordKey();
            for (const auto& c : m.chords)
            {
                if (c.second.empty())
                    continue;
                const auto cd = m.fromBass ? scriba::chordOnBass (c.second, k) : scriba::parseChord (c.second);
                if (cd.intervals.size() >= 2)
                {
                    const std::pair<int, int> shape { cd.root, cd.intervals[1] };
                    if (std::find (songChordShapes.begin(), songChordShapes.end(), shape) == songChordShapes.end())
                        songChordShapes.push_back (shape);
                }
            }
            songHasChords = ! songChordShapes.empty();
        }

        const int ck = proc.getChordKey();
        const auto chords = proc.getKeyChords();
        const int oldCount = padCount;
        padCount = juce::jmin ((int) pads.size(), (int) chords.size());
        for (int i = 0; i < (int) pads.size(); ++i)
        {
            if (i < padCount)
            {
                const auto& c = chords[(size_t) i];
                bool used = false;
                for (const auto& sh : songChordShapes)
                    used = used || (sh.first == c.root && c.intervals.size() >= 2 && sh.second == c.intervals[1]);
                pads[(size_t) i].set (c, i == 0, used);
            }
            pads[(size_t) i].setVisible (chordsView && i < padCount);
        }
        if (ck != shownChordKey)
        {
            if (ck >= 0 && ! scriba::isDrum (selected) && state != ScribaProcessor::State::Recording
                && state != ScribaProcessor::State::Armed)
                setTab (true);   // a key was found: the chord buttons come out
            shownChordKey = ck;
        }
        for (auto* b : { &chordTypeBox, &chordStyleBox, &chordVoicingBox, &chordLengthBox })
            b->setVisible (chordsView);
        songButton.setVisible (chordsView);
        songButton.setEnabled (songHasChords);
        waveTab.setToggleState (! chordsView, juce::dontSendNotification);
        chordsTab.setToggleState (chordsView, juce::dontSendNotification);
        if (oldCount != padCount)
            laidOutFor = -1;
    }

    if (laidOutFor != (int) selected)
        resized();
}

void ScribaEditor::setTab (bool chords)
{
    if (chordsView == chords)
        return;
    chordsView = chords;
    if (! chords)
        proc.stopPreview();
    songScanCountdown = 0;
    refresh();
    repaint();
}

bool ScribaEditor::dragBytes (const std::vector<juce::uint8>& bytes, const juce::String& fileName, juce::Component* source)
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Scriba");
    dir.createDirectory();
    const auto f = dir.getChildFile (fileName);
    {
        juce::FileOutputStream os (f);
        if (! os.openedOk())
            return false;
        os.setPosition (0);
        os.truncate();
        if (! os.write (bytes.data(), bytes.size()))
            return false;
    }
    juce::Component::SafePointer<juce::Component> src (source);
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, source,
                                                                [src]
                                                                {
                                                                    if (auto* b = dynamic_cast<juce::Button*> (src.getComponent()))
                                                                        b->setState (juce::Button::buttonNormal);
                                                                });
    return true;
}

void ScribaEditor::dragChord (const scriba::ChordDef& c, juce::Component* source)
{
    proc.stopPreview();
    const juce::String name = juce::String (c.name).replace ("#", "sharp");
    if (! dragBytes (proc.buildChordMidi (c), "Scriba_" + name + ".mid", source))
        flash ("COULD NOT CREATE THE MIDI");
}

void ScribaEditor::dragSong()
{
    if (! dragBytes (proc.buildProgressionMidi(), "Scriba_Chords.mid", &songButton))
        flash ("COULD NOT CREATE THE MIDI");
}

void ScribaEditor::timerCallback()
{
    refresh();

    // cambio di strumento o sensibilita': rifa l'analisi sull'audio gia' registrato
    const int sel = (int) selected;
    if (lastSelected >= 0 && sel != lastSelected)
        reanalyseCountdown = 4;
    lastSelected = sel;
    if (reanalyseCountdown > 0 && --reanalyseCountdown == 0)
        proc.reanalyse();

    // moving the gate re-builds the hits from the same recording (fast)
    const float gateNow = proc.getGateDb();
    if (std::abs (gateNow - lastGate) > 0.01f)
    {
        if (lastGate < 999.0f && scriba::isDrum (selected) && ! draggingGate)
            reanalyseCountdown = 6;
        else if (draggingGate)
            reanalyseCountdown = 0;
        lastGate = gateNow;
    }
    if (! draggingGate && reanalyseCountdown == 0 && scriba::isDrum (selected) && showResults()
        && std::abs (drum.thresholdDb - gateNow) > 0.01f && audioKept)
        reanalyseCountdown = 6;

    const float det = proc.takeDetectorDb();
    if (scriba::isDrum (selected) && proc.isTransportPlaying())
    {
        detHistory.push_back (det);
        while (detHistory.size() > 300)
            detHistory.pop_front();
    }

    const float lvl = proc.getLevel();
    smoothLevel += (juce::jlimit (0.0f, 1.0f, lvl * 1.6f) - smoothLevel) * 0.25f;
    phase += 0.14f;
    mainButton.phase = phase;

    if (state == ScribaProcessor::State::Recording)
    {
        history.push_back (smoothLevel);
        while (history.size() > 300)
            history.pop_front();
    }

    // easing verso i valori mostrati
    const bool keyOn = showResults() && result.valid;
    float bestScore = 0.0f;
    if (keyOn && result.best >= 0)
        bestScore = result.scores[(size_t) result.best];

    float maxChroma = 0.0f;
    for (float c : result.chroma)
        maxChroma = juce::jmax (maxChroma, c);

    for (size_t i = 0; i < 24; ++i)
    {
        float target = 0.0f;
        if (keyOn && bestScore > 0.2f)
            target = std::pow (juce::jlimit (0.0f, 1.0f, (result.scores[i] - 0.2f) / (bestScore - 0.2f)), 3.0f);
        shown[i] += (target - shown[i]) * 0.14f;
    }
    for (size_t i = 0; i < 12; ++i)
    {
        const float target = (keyOn && maxChroma > 0.0f) ? result.chroma[i] / maxChroma : 0.0f;
        shownChroma[i] += (target - shownChroma[i]) * 0.14f;
    }

    repaint();
}

//==============================================================================
void ScribaEditor::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float s = scale;

    g.setGradientFill (juce::ColourGradient (palette::bgTop, b.getCentreX(), 0.0f, palette::bgBot, b.getCentreX(), b.getBottom(), false));
    g.fillAll();

    // soft glow behind the result card, stronger while recording
    const float glow = state == ScribaProcessor::State::Recording ? 0.07f + 0.08f * smoothLevel : showResults() ? 0.06f : 0.03f;
    const auto wc = leftCard.getCentre();
    g.setGradientFill (juce::ColourGradient (palette::cyan.withAlpha (glow), wc.x, wc.y,
                                             palette::cyan.withAlpha (0.0f), wc.x + leftCard.getWidth() * 0.9f, wc.y, true));
    g.fillRect (b);

    paintHeader (g);

    fillCard (g, leftCard, s);
    switch (selected)
    {
        case scriba::Instrument::Snare: paintSnareCard (g, leftCard); break;
        case scriba::Instrument::Kick:  paintKickCard (g, leftCard); break;
        case scriba::Instrument::Vocals: paintLyricsCard (g, leftCard); break;
        default:                        paintKeyCard (g, leftCard); break;
    }

    paintControls (g);
    paintWave (g);

    // copyright line under the waveform
   #ifdef JucePlugin_VersionString
    const juce::String version = " " + kDot + " v" JucePlugin_VersionString;
   #else
    const juce::String version;
   #endif
    g.setColour (palette::faint.withAlpha (0.8f));
    g.setFont (makeFont (10.0f * s));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc2\xa9")) + " 2026 Meteora Plugins " + kDot + " Scriba" + version,
                juce::Rectangle<float> (waveCard.getX(), waveCard.getBottom() + 4.0f * s, waveCard.getWidth(), 14.0f * s),
                juce::Justification::centredRight, false);

    // glow under the main button (drawn here: the button itself cannot paint outside its bounds)
    if (state != ScribaProcessor::State::Analysing && state != ScribaProcessor::State::Armed)
    {
        const auto mb = mainButton.getBounds().toFloat().reduced (2.0f);
        juce::Path p;
        p.addRoundedRectangle (mb, mb.getHeight() * 0.26f);
        const bool rec = state == ScribaProcessor::State::Recording;
        const auto c = rec ? palette::red.withAlpha (0.28f + 0.12f * std::sin (phase * 1.6f))
                           : palette::cyan.withAlpha (mainButton.isOver() ? 0.32f : 0.20f);
        juce::DropShadow (c, (int) (20.0f * s), { 0, (int) (4.0f * s) }).drawForPath (g, p);
    }
}

void ScribaEditor::paintHeader (juce::Graphics& g)
{
    const float s = scale;
    auto h = headerArea.reduced (24.0f * s, 0.0f);

    const float logoSize = 48.0f * s;
    if (logo.isValid())
    {
        // the logo is a 320 px image with thin lines: shrinking it on the fly makes the lines break up.
        // It is resized once, in steps of 2x with high quality, to the exact number of screen pixels.
        const float pixelScale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const int px = juce::jmax (8, (int) std::lround (logoSize * pixelScale));
        if (logoCache.getWidth() != px)
        {
            juce::Image img = logo;
            while (img.getWidth() / 2 >= px)
                img = img.rescaled (img.getWidth() / 2, img.getHeight() / 2, juce::Graphics::highResamplingQuality);
            logoCache = img.rescaled (px, px, juce::Graphics::highResamplingQuality);
        }
        const auto dest = juce::Rectangle<float> (h.getX() - 4.0f * s, h.getCentreY() - logoSize * 0.5f, logoSize, logoSize);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (logoCache, dest, juce::RectanglePlacement::centred);
    }
    auto t = h.withTrimmedLeft (logoSize + 8.0f * s);
    g.setColour (palette::text);
    g.setFont (makeFont (19.0f * s, juce::Font::bold).withExtraKerningFactor (0.32f));
    g.drawText ("SCRIBA", t.withTrimmedBottom (h.getHeight() * 0.46f).withTrimmedTop (h.getHeight() * 0.18f), juce::Justification::bottomLeft, false);
    g.setColour (palette::dim);
    g.setFont (makeFont (10.5f * s).withExtraKerningFactor (0.12f));
    g.drawText ("KEY " + kDot + " CHORDS " + kDot + " LYRICS " + kDot + " MIDI " + kDot + " DRUMS", t.withTrimmedTop (h.getHeight() * 0.56f).withTrimmedBottom (h.getHeight() * 0.14f),
                juce::Justification::topLeft, false);

    // status pill
    const auto text = statusText();
    const auto font = makeFont (11.5f * s, juce::Font::bold).withExtraKerningFactor (0.12f);
    const float tw = textWidth (font, text);
    const float pw = tw + 40.0f * s;
    auto pill = juce::Rectangle<float> (pw, 28.0f * s).withCentre ({ h.getRight() - pw * 0.5f, h.getCentreY() });

    const bool live = state == ScribaProcessor::State::Recording;
    const bool flashing = flashText.isNotEmpty() && juce::Time::getMillisecondCounter() < flashUntil;
    const juce::Colour c = live ? palette::red
                           : flashing || (resultsOk && (state == ScribaProcessor::State::Done || state == ScribaProcessor::State::Idle)) ? palette::cyan
                           : state == ScribaProcessor::State::Armed || state == ScribaProcessor::State::Analysing ? palette::amber
                                                                                                                 : palette::dim;
    g.setColour (c.withAlpha (0.10f));
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (c.withAlpha (0.7f));
    g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, 1.0f);

    const float dotA = live || state == ScribaProcessor::State::Armed ? 0.45f + 0.55f * (0.5f + 0.5f * std::sin (phase * 1.6f)) : 1.0f;
    g.setColour (c.withAlpha (dotA));
    g.fillEllipse (pill.getX() + 13.0f * s, pill.getCentreY() - 3.5f * s, 7.0f * s, 7.0f * s);

    g.setColour (c == palette::dim ? palette::text : c);
    g.setFont (font);
    g.drawText (text, pill.withTrimmedLeft (26.0f * s).withTrimmedRight (14.0f * s), juce::Justification::centred, false);

    g.setColour (palette::line);
    g.fillRect (headerArea.getX() + 24.0f * s, headerArea.getBottom() - 1.0f, headerArea.getWidth() - 48.0f * s, 1.0f);
}

//==============================================================================
void ScribaEditor::paintKeyCard (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float s = scale;
    auto r = area.reduced (18.0f * s, 16.0f * s);
    drawLabel (g, "KEY OF THIS TRACK", r.removeFromTop (14.0f * s), s);

    auto bottom = r.removeFromBottom (44.0f * s);
    paintWheel (g, r.reduced (0.0f, 4.0f * s));

    // the found key and the alternative
    const bool on = showResults() && result.valid;
    auto bar = bottom.removeFromTop (6.0f * s);
    g.setColour (palette::line);
    g.fillRoundedRectangle (bar, bar.getHeight() * 0.5f);
    bottom.removeFromTop (8.0f * s);
    if (! on)
    {
        g.setColour (palette::faint);
        g.setFont (makeFont (12.5f * s));
        g.drawText ("Key found  vs  alternative", bottom, juce::Justification::centredLeft, false);
        return;
    }
    const int pctBest = (int) std::lround (result.bestShare * 100.0f);
    g.setGradientFill (juce::ColourGradient (palette::cyanDeep, bar.getX(), 0.0f, palette::cyan, bar.getRight(), 0.0f, false));
    g.fillRoundedRectangle (bar.withWidth (juce::jmax (bar.getHeight(), bar.getWidth() * result.bestShare)), bar.getHeight() * 0.5f);

    g.setColour (palette::text);
    g.setFont (makeFont (13.5f * s, juce::Font::bold));
    g.drawText (juce::String (scriba::keyName (result.best)) + "  " + juce::String (pctBest) + "%", bottom, juce::Justification::centredLeft, false);
    g.setColour (palette::dim);
    g.setFont (makeFont (13.5f * s));
    g.drawText (juce::String (100 - pctBest) + "%  " + juce::String (scriba::keyName (result.second)), bottom, juce::Justification::centredRight, false);
}

void ScribaEditor::paintWheel (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float R = size * 0.5f * 0.82f;
    const float step = juce::MathConstants<float>::twoPi / 12.0f;
    const float gap = 0.012f;
    const bool done = showResults() && result.valid;

    auto segment = [&] (float rOut, float rIn, int i)
    {
        juce::Path p;
        p.addPieSegment (c.x - rOut, c.y - rOut, rOut * 2.0f, rOut * 2.0f,
                         ((float) i - 0.5f) * step + gap, ((float) i + 0.5f) * step - gap, rIn / rOut);
        return p;
    };

    // barre del cromagramma attorno alla ruota
    {
        const float maxH = R * 0.15f;
        const bool live = state == ScribaProcessor::State::Recording;
        for (int i = 0; i < 12; ++i)
        {
            const int pc = (i * 7) % 12;
            float amount = shownChroma[(size_t) pc];
            if (live)
                amount = smoothLevel * (0.45f + 0.55f * (0.5f + 0.5f * std::sin (phase + (float) i * 0.65f)));
            const float h = 3.0f + amount * maxH;
            const bool strong = ! live && amount > 0.62f;

            juce::Path bar;
            bar.addRoundedRectangle (c.x - 2.5f, c.y - R * 1.07f - h, 5.0f, h, 2.5f);
            bar.applyTransform (juce::AffineTransform::rotation ((float) i * step, c.x, c.y));

            if (strong)
                juce::DropShadow (palette::cyan.withAlpha (0.5f), (int) (R * 0.06f), {}).drawForPath (g, bar);

            const auto col = live ? palette::cyan.withAlpha (0.55f + 0.4f * amount)
                                  : palette::faint.interpolatedWith (palette::cyan, juce::jlimit (0.0f, 1.0f, (amount - 0.35f) * 2.6f))
                                        .withAlpha (0.4f + 0.6f * amount);
            g.setColour (col);
            g.fillPath (bar);
        }
    }

    g.setColour (palette::line);
    g.drawEllipse (c.x - R - 2.0f, c.y - R - 2.0f, (R + 2.0f) * 2.0f, (R + 2.0f) * 2.0f, 1.2f);

    // due anelli: maggiori (esterno) e minori (interno)
    for (int ring = 0; ring < 2; ++ring)
    {
        const bool minor = ring == 1;
        const float rOut = minor ? R * 0.665f : R;
        const float rIn  = minor ? R * 0.45f  : R * 0.685f;

        for (int i = 0; i < 12; ++i)
        {
            const int tonic = minor ? (i * 7 + 9) % 12 : (i * 7) % 12;
            const int idx = (minor ? 12 : 0) + tonic;
            const float t = shown[(size_t) idx];
            const bool isBest = done && idx == result.best;

            auto path = segment (rOut, rIn, i);
            if (isBest)
                juce::DropShadow (palette::cyan.withAlpha (0.45f), (int) (R * 0.14f), {}).drawForPath (g, path);

            g.setColour (palette::bgBot.interpolatedWith (palette::card, 0.6f));
            g.fillPath (path);
            if (t > 0.01f || isBest)
            {
                g.setColour (palette::cyan.withAlpha (isBest ? 0.30f : 0.13f * t));
                g.fillPath (path);
            }
            g.setColour (isBest ? palette::cyan : palette::line);
            g.strokePath (path, juce::PathStrokeType (isBest ? 1.8f : 1.0f));

            const float a = (float) i * step;
            const float rl = (rOut + rIn) * 0.5f;
            const juce::Point<float> pos (c.x + rl * std::sin (a), c.y - rl * std::cos (a));
            const auto label = juce::String (scriba::pitchClassName (tonic)) + (minor ? "m" : "");

            g.setColour (isBest ? palette::cyan : palette::text.withAlpha ((done ? 0.45f : 0.55f) + 0.45f * t));
            g.setFont (makeFont (R * (minor ? 0.098f : 0.115f), isBest ? juce::Font::bold : juce::Font::plain));
            g.drawText (label, juce::Rectangle<float> (R * 0.34f, R * 0.14f).withCentre (pos), juce::Justification::centred, false);
        }
    }

    // testo al centro
    auto centre = juce::Rectangle<float> (R * 0.84f, R * 0.66f).withCentre (c);
    if (done)
    {
        const bool minor = result.best >= 12;
        g.setColour (palette::text);
        g.setFont (makeFont (R * 0.25f, juce::Font::bold));
        g.drawText (scriba::pitchClassName (result.best % 12), centre.removeFromTop (centre.getHeight() * 0.52f), juce::Justification::centredBottom, false);
        g.setColour (palette::cyan);
        g.setFont (makeFont (R * 0.095f, juce::Font::bold).withExtraKerningFactor (0.18f));
        g.drawText (minor ? "MINOR" : "MAJOR", centre.removeFromTop (centre.getHeight() * 0.45f), juce::Justification::centred, false);
        g.setColour (palette::dim);
        g.setFont (makeFont (R * 0.082f));
        g.drawText (juce::String ("relative ") + scriba::keyName (result.relativeOfBest), centre, juce::Justification::centredTop, false);
        return;
    }

    juce::String msg;
    switch (state)
    {
        case ScribaProcessor::State::Idle:      msg = "Press ANALYZE,\nthen play the song"; break;
        case ScribaProcessor::State::Armed:     msg = "Press play\nin your DAW"; break;
        case ScribaProcessor::State::Recording: msg = formatTime (proc.getRecordedSeconds()); break;
        case ScribaProcessor::State::Analysing: msg = "Analyzing..."; break;
        case ScribaProcessor::State::Done:      msg = resultsOk ? (audioKept ? "Analyzing..." : "Press ANALYZE\nto record again") : "Too short\nor silent"; break;
    }
    if (state == ScribaProcessor::State::Recording)
    {
        g.setColour (palette::text);
        g.setFont (makeFont (R * 0.2f, juce::Font::bold));
    }
    else
    {
        g.setColour (palette::dim);
        g.setFont (makeFont (R * 0.088f));
    }
    g.drawFittedText (msg, centre.toNearestInt(), juce::Justification::centred, 2);
}

//==============================================================================
void ScribaEditor::paintSnareCard (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float s = scale;
    auto r = area.reduced (20.0f * s, 16.0f * s);
    drawLabel (g, "SNARE TUNING", r.removeFromTop (14.0f * s), s);

    const bool done = showResults();
    auto keyLine = r.removeFromBottom (34.0f * s);

    // song key line at the bottom of the card
    {
        g.setColour (palette::line);
        g.fillRect (keyLine.getX(), keyLine.getY(), keyLine.getWidth(), 1.0f);
        auto kl = keyLine.withTrimmedTop (10.0f * s);
        const bool ok = songKey.valid;
        g.setColour (ok ? palette::cyan : palette::amber);
        g.fillEllipse (kl.getX(), kl.getCentreY() - 3.5f * s, 7.0f * s, 7.0f * s);
        kl.removeFromLeft (14.0f * s);
        juce::String t;
        if (! ok)
            t = "Song key: not found yet";
        else
            t = "Song key: " + juce::String (scriba::keyName (songKey.key)) + "  " + kDot + "  "
                + (songKey.manual ? juce::String ("chosen by hand") : juce::String ("from ") + juce::String (songKey.source));
        g.setColour (ok ? palette::text : palette::amber);
        g.setFont (makeFont (12.5f * s));
        g.drawFittedText (t, kl.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
    }

    // snare pitch on Scriba (buttons are child components on the right of this row)
    {
        auto pr = r.removeFromBottom (48.0f * s).withTrimmedTop (6.0f * s).withTrimmedBottom (8.0f * s);
        pr = pr.withRight ((float) pitchDown.getX() - 10.0f * s);
        drawLabel (g, "SNARE PITCH", pr.removeFromTop (pr.getHeight() * 0.42f), s);
        const float pv = proc.getPitch();
        juce::String v = std::abs (pv) < 0.005f ? juce::String ("0 st  (off)")
                                                : (pv > 0 ? "+" : kMinus) + juce::String (std::abs (pv), 2) + " st";
        if (std::abs (pv) >= 0.005f && drum.pitch.valid && showResults())
        {
            const double nowMidi = 69.0 + 12.0 * std::log2 (drum.pitch.hz / 440.0) + (double) pv;
            const int nn = (int) std::lround (nowMidi);
            v += "  " + kArrow + "  " + juce::String (scriba::noteLabel (nn)) + " " + centsText ((nowMidi - nn) * 100.0);
        }
        g.setColour (std::abs (pv) < 0.005f ? palette::dim : palette::cyan);
        g.setFont (makeFont (14.5f * s, juce::Font::bold));
        g.drawFittedText (v.trimEnd(), pr.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }

    auto message = [&] (const juce::String& head, const juce::String& body, juce::Colour headColour)
    {
        auto m = r.reduced (0.0f, 10.0f * s);
        g.setColour (headColour);
        g.setFont (makeFont (17.0f * s, juce::Font::bold));
        g.drawFittedText (head, m.removeFromTop (30.0f * s).toNearestInt(), juce::Justification::centredLeft, 1);
        g.setColour (palette::dim);
        g.setFont (makeFont (13.5f * s));
        juce::AttributedString as;
        as.append (body, makeFont (13.5f * s), palette::dim);
        as.setLineSpacing (5.0f * s);
        as.draw (g, m.withTrimmedTop (6.0f * s));
    };

    if (! done)
    {
        if (state == ScribaProcessor::State::Recording)
            return message ("Listening to the snare...  " + formatTime (proc.getRecordedSeconds()), "Stop the DAW (or press STOP & ANALYZE) when the song is over.", palette::text);
        if (state == ScribaProcessor::State::Armed)
            return message ("Press play in your DAW", "Recording starts with the transport.", palette::text);
        if (state == ScribaProcessor::State::Analysing)
            return message ("Analyzing...", "", palette::text);
        if (resultsOk && analysed != selected && ! audioKept)
            return message ("Press ANALYZE", "The saved result is for another instrument: record the snare again.", palette::text);
        return message ("How it works",
                        "1.  Put another Scriba on the bass, guitar or piano track and analyze it: the song key arrives here by itself.\n"
                        "2.  Here press ANALYZE and play the song.\n"
                        "3.  Read how many semitones to move the snare.",
                        palette::text);
    }

    if (drum.hits.empty())
        return message ("No snare hits found", "Raise the SENSITIVITY, or use a track where the snare is clearly audible.", palette::amber);
    if (! drum.pitch.valid)
        return message ("Snare tone not measurable", "There is no clear tone in the hits. The MIDI file with the "
                        + juce::String ((int) drum.hits.size()) + " hits is still fine.", palette::amber);

    const auto& p = drum.pitch;
    const juce::String nowText = juce::String (scriba::noteLabel (p.midi)) + (centsText (p.cents).isEmpty() ? "" : " " + centsText (p.cents));

    if (! tuning.valid)
    {
        message ("Snare: " + nowText, "To know how much to tune it, the song key is needed: analyze the bass or guitar with "
                 "another Scriba, or choose the key in SONG KEY.", palette::text);
        return;
    }

    const auto& m = tuning.main;
    const bool inTune = tuning.alreadyInTune || m.semitones == 0;

    // big answer
    auto big = r.removeFromTop (r.getHeight() * 0.50f);
    const juce::String num = inTune ? "0" : signedSemis (m.semitones);
    const auto numFont = makeFont (big.getHeight() * 0.80f, juce::Font::bold);
    const float nw = textWidth (numFont, num);
    const juce::String l1 = inTune ? "IN TUNE" : (std::abs (m.semitones) == 1 ? "SEMITONE" : "SEMITONES");
    const juce::String l2 = inTune ? "leave it as it is" : (m.semitones > 0 ? "UP" : "DOWN");
    const auto l1Font = makeFont (15.0f * s, juce::Font::bold).withExtraKerningFactor (0.14f);
    const auto l2Font = makeFont (inTune ? 13.0f * s : 22.0f * s, juce::Font::bold).withExtraKerningFactor (0.10f);
    const float sideW = juce::jmax (textWidth (l1Font, l1), textWidth (l2Font, l2)) + 30.0f * s;
    float x = big.getCentreX() - (nw + 16.0f * s + sideW) * 0.5f;

    juce::DropShadow (palette::cyan.withAlpha (0.35f), (int) (24.0f * s), {}).drawForRectangle (g, juce::Rectangle<float> (x, big.getCentreY() - 6.0f * s, nw, 12.0f * s).toNearestInt());
    g.setColour (palette::cyan);
    g.setFont (numFont);
    g.drawText (num, juce::Rectangle<float> (x, big.getY(), nw + 4.0f, big.getHeight()), juce::Justification::centredLeft, false);
    x += nw + 16.0f * s;

    // arrow up / down
    if (! inTune)
    {
        juce::Path arrow;
        const float ax = x + 9.0f * s, ay = big.getCentreY() + 14.0f * s, ah = 12.0f * s;
        const float dir = m.semitones > 0 ? -1.0f : 1.0f;
        arrow.addTriangle (ax - 8.0f * s, ay - dir * ah * 0.5f, ax + 8.0f * s, ay - dir * ah * 0.5f, ax, ay + dir * ah * 0.5f);
        g.setColour (palette::text);
        g.fillPath (arrow);
    }
    g.setColour (palette::dim);
    g.setFont (l1Font);
    g.drawText (l1, juce::Rectangle<float> (x, big.getCentreY() - 26.0f * s, sideW, 20.0f * s), juce::Justification::centredLeft, false);
    g.setColour (palette::text);
    g.setFont (l2Font);
    g.drawText (l2, juce::Rectangle<float> (x + (inTune ? 0.0f : 24.0f * s), big.getCentreY() + 2.0f * s, sideW, 26.0f * s), juce::Justification::centredLeft, false);

    // now -> target
    auto row = r.removeFromTop (24.0f * s);
    {
        const auto f1 = makeFont (16.0f * s, juce::Font::bold);
        const juce::String a = nowText, arrowT = "   " + kArrow + "   ", b = scriba::noteLabel (m.midi);
        const float w1 = textWidth (f1, a), w2 = textWidth (f1, arrowT), w3 = textWidth (f1, b);
        float xx = row.getCentreX() - (w1 + w2 + w3) * 0.5f;
        g.setFont (f1);
        g.setColour (palette::dim);
        g.drawText (a, juce::Rectangle<float> (xx, row.getY(), w1 + 2.0f, row.getHeight()), juce::Justification::centredLeft, false);
        xx += w1;
        g.setColour (palette::faint);
        g.drawText (arrowT, juce::Rectangle<float> (xx, row.getY(), w2 + 2.0f, row.getHeight()), juce::Justification::centredLeft, false);
        xx += w2;
        g.setColour (palette::cyan);
        g.drawText (b, juce::Rectangle<float> (xx, row.getY(), w3 + 2.0f, row.getHeight()), juce::Justification::centredLeft, false);
    }
    g.setColour (palette::dim);
    g.setFont (makeFont (12.5f * s));
    juce::String sub = "the " + juce::String (m.role) + " of " + scriba::keyName (songKey.key);
    if (! inTune && std::abs (m.cents) >= 10)
        sub += "   " + kDot + "   fine: " + signedSemis (m.semitones) + " st " + (m.cents > 0 ? "+" : kMinus) + juce::String (std::abs (m.cents)) + kCent;
    g.drawText (sub, r.removeFromTop (18.0f * s), juce::Justification::centred, false);

    // ruler: one tick per semitone around the snare, the notes of the key chord in cyan
    auto rul = r.reduced (4.0f * s, 6.0f * s);
    const float cy = rul.getY() + rul.getHeight() * 0.40f;
    const float unit = rul.getWidth() / 13.0f;
    const float cx = rul.getCentreX();
    const double sm = tuning.snareMidi;
    const int tonic = songKey.key % 12;
    const bool minorKey = songKey.key >= 12;
    auto roleOf = [&] (int pc)
    {
        const int iv = (pc - tonic + 12) % 12;
        if (iv == 0 || iv == 7 || iv == (minorKey ? 3 : 4))
            return 2;
        static const int maj[7] = { 0, 2, 4, 5, 7, 9, 11 }, mnr[7] = { 0, 2, 3, 5, 7, 8, 10 };
        for (int k = 0; k < 7; ++k)
            if ((minorKey ? mnr[k] : maj[k]) == iv)
                return 1;
        return 0;
    };
    g.setColour (palette::line);
    g.fillRect (rul.getX(), cy, rul.getWidth(), 1.0f);
    for (int mm = (int) std::ceil (sm - 6.4); mm <= (int) std::floor (sm + 6.4); ++mm)
    {
        const float tx = cx + (float) ((double) mm - sm) * unit;
        if (tx < rul.getX() || tx > rul.getRight())
            continue;
        const int role = roleOf (mm % 12);
        const bool isTarget = mm == m.midi;
        const float th = role == 2 ? 9.0f * s : role == 1 ? 6.0f * s : 3.5f * s;
        g.setColour (isTarget ? palette::cyan : role == 2 ? palette::cyan.withAlpha (0.55f) : role == 1 ? palette::dim : palette::faint);
        g.fillRect (tx - 0.75f, cy - th, 1.5f, th * 2.0f);
        g.setFont (makeFont ((isTarget ? 11.5f : 10.0f) * s, isTarget || role == 2 ? juce::Font::bold : juce::Font::plain));
        g.setColour (isTarget ? palette::cyan : role == 2 ? palette::text.withAlpha (0.8f) : palette::faint);
        g.drawText (scriba::pitchClassName (mm % 12), juce::Rectangle<float> (unit * 1.4f, 14.0f * s).withCentre ({ tx, cy + 18.0f * s }),
                    juce::Justification::centred, false);
    }
    // the move, as an arrow from the snare to the target
    const float tx = cx + (float) m.shift * unit;
    if (! inTune)
    {
        g.setColour (palette::cyan.withAlpha (0.8f));
        const float ay = cy - 15.0f * s;
        g.drawLine (cx, ay, tx, ay, 1.6f * s);
        juce::Path head;
        const float d = tx > cx ? 1.0f : -1.0f;
        head.addTriangle (tx, ay, tx - d * 6.0f * s, ay - 4.0f * s, tx - d * 6.0f * s, ay + 4.0f * s);
        g.fillPath (head);
    }
    g.setColour (palette::text);
    g.fillEllipse (cx - 4.5f * s, cy - 4.5f * s, 9.0f * s, 9.0f * s);

    // where the snare sounds now with the pitch set on Scriba
    const float pv = proc.getPitch();
    if (std::abs (pv) >= 0.005f)
    {
        const float px = juce::jlimit (rul.getX(), rul.getRight(), cx + pv * unit);
        juce::Path tri;
        tri.addTriangle (px, cy - 8.0f * s, px - 6.0f * s, cy - 17.0f * s, px + 6.0f * s, cy - 17.0f * s);
        g.setColour (palette::amber);
        g.fillPath (tri);
        g.fillRect (px - 1.0f, cy - 8.0f * s, 2.0f, 16.0f * s);
    }
    g.setColour (palette::cyan);
    g.drawEllipse (tx - 6.0f * s, cy - 6.0f * s, 12.0f * s, 12.0f * s, 1.8f * s);
}

void ScribaEditor::paintLyricsCard (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float s = scale;
    auto r = area.reduced (20.0f * s, 16.0f * s);
    auto& vm = scriba::VoiceModel::get();
    const auto st = vm.getStatus();
    const bool ready = st == scriba::VoiceModel::Status::Ready;

    auto top = r.removeFromTop (14.0f * s);
    drawLabel (g, "LYRICS", top, s);
    if (showResults() && analysed == scriba::Instrument::Vocals && ! lyricsShown.language.empty())
        drawLabel (g, juce::String (lyricsShown.language).toUpperCase(), top, s, juce::Justification::centredRight, palette::cyan);

    if (! ready)
    {
        // the model is needed: explanation + download (buttons are child components at the bottom)
        r.removeFromBottom (38.0f * s);
        auto m = r.reduced (0.0f, 12.0f * s);
        g.setColour (palette::text);
        g.setFont (makeFont (17.0f * s, juce::Font::bold));
        g.drawText ("Voice model needed", m.removeFromTop (30.0f * s), juce::Justification::centredLeft, false);
        juce::AttributedString as;
        as.setLineSpacing (5.0f * s);
        as.append ("To write the lyrics Scriba uses Whisper large-v3-turbo, working on your computer, without internet.\n\n"
                   "It is downloaded only once (about 550 MB) and then it serves every Scriba and every project.\n\n"
                   "Without it Scriba on the voice still makes the MIDI of the melody.",
                   makeFont (13.0f * s), palette::dim);
        as.draw (g, m.withTrimmedTop (4.0f * s));

        if (st == scriba::VoiceModel::Status::Downloading)
        {
            auto bar = r.withTop (r.getBottom() - 14.0f * s).withHeight (5.0f * s);
            g.setColour (palette::line);
            g.fillRoundedRectangle (bar, bar.getHeight() * 0.5f);
            g.setColour (palette::cyan);
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (bar.getHeight(), bar.getWidth() * vm.getProgress())), bar.getHeight() * 0.5f);
        }
        else if (st == scriba::VoiceModel::Status::Failed)
        {
            g.setColour (palette::amber);
            g.setFont (makeFont (12.0f * s));
            g.drawFittedText (vm.getError(), r.withTop (r.getBottom() - 34.0f * s).toNearestInt(), juce::Justification::bottomLeft, 2);
        }
        return;
    }

    auto footer = r.removeFromBottom (26.0f * s);
    {
        g.setColour (palette::line);
        g.fillRect (footer.getX(), footer.getY(), footer.getWidth(), 1.0f);
        auto f = footer.withTrimmedTop (8.0f * s);
        g.setColour (palette::cyan);
        g.fillEllipse (f.getX(), f.getCentreY() - 3.5f * s, 7.0f * s, 7.0f * s);
        g.setColour (palette::dim);
        g.setFont (makeFont (12.0f * s));
        g.drawText ("Voice model ready  " + kDot + "  " + vm.modelFile().getFileName(), f.withTrimmedLeft (14.0f * s), juce::Justification::centredLeft, false);
    }

    auto body = r.reduced (0.0f, 10.0f * s);
    auto message = [&] (const juce::String& head, const juce::String& text, juce::Colour c)
    {
        g.setColour (c);
        g.setFont (makeFont (17.0f * s, juce::Font::bold));
        g.drawText (head, body.removeFromTop (30.0f * s), juce::Justification::centredLeft, false);
        juce::AttributedString as;
        as.setLineSpacing (5.0f * s);
        as.append (text, makeFont (13.5f * s), palette::dim);
        as.draw (g, body.withTrimmedTop (4.0f * s));
    };

    if (state == ScribaProcessor::State::Recording)
        return message ("Listening to the voice...  " + formatTime (proc.getRecordedSeconds()), "Stop the DAW (or press STOP & ANALYZE) when the song is over.", palette::text);
    if (state == ScribaProcessor::State::Armed)
        return message ("Press play in your DAW", "Recording starts with the transport.", palette::text);
    if (state == ScribaProcessor::State::Analysing)
        return message ("Writing the lyrics...  " + juce::String ((int) std::lround (proc.getProgress() * 100.0f)) + "%",
                        "Whisper is listening to the song. It takes from a few seconds (Apple Silicon) to a couple of minutes.", palette::text);
    if (! showResults() || analysed != scriba::Instrument::Vocals)
        return message ("How it works",
                        "1.  Put Scriba on the vocal track, before reverb and delay.\n"
                        "2.  Press ANALYZE and play the song.\n"
                        "3.  SAVE TXT gives the lyrics; with another Scriba on guitar or piano the chords are written above the words.",
                        palette::text);
    if (! lyricsShown.ok)
        return message ("No lyrics", juce::String (lyricsShown.error.empty() ? "The lyrics were not transcribed." : lyricsShown.error), palette::amber);
    if (lyricsShown.lines.empty())
        return message ("No sung words found", "Check that Scriba is on the vocal track, before the effects.", palette::amber);

    // preview: the first lines of the lyrics
    const auto f = makeFont (14.0f * s);
    const float lh = 22.0f * s;
    int shownLines = 0;
    for (const auto& line : lyricsShown.lines)
    {
        if (body.getHeight() < lh * 2.0f)
            break;
        auto row = body.removeFromTop (lh);
        g.setColour (palette::faint);
        g.setFont (makeFont (11.0f * s));
        g.drawText (formatTime (line.t0), row.removeFromLeft (40.0f * s), juce::Justification::centredLeft, false);
        g.setColour (palette::text);
        g.setFont (f);
        g.drawFittedText (juce::String::fromUTF8 (line.text().c_str()), row.toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
        ++shownLines;
    }
    const int more = (int) lyricsShown.lines.size() - shownLines;
    if (more > 0)
    {
        g.setColour (palette::dim);
        g.setFont (makeFont (12.0f * s));
        g.drawText ("+ " + juce::String (more) + " more lines in the text file", body.removeFromTop (lh), juce::Justification::centredLeft, false);
    }
}

void ScribaEditor::paintKickCard (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float s = scale;
    auto r = area.reduced (20.0f * s, 16.0f * s);
    drawLabel (g, "KICK", r.removeFromTop (14.0f * s), s);

    const bool done = showResults();
    auto mid = r.reduced (0.0f, 20.0f * s);
    auto bigArea = mid.removeFromTop (mid.getHeight() * 0.48f);
    if (done || state == ScribaProcessor::State::Recording)
    {
        g.setColour (done ? palette::cyan : palette::text);
        g.setFont (makeFont (mid.getHeight() * 0.62f, juce::Font::bold));
        g.drawText (done ? juce::String ((int) notes.size()) : formatTime (proc.getRecordedSeconds()), bigArea, juce::Justification::centredBottom, false);
    }
    else
    {
        const float ic = bigArea.getHeight() * 0.8f;
        drawInstrumentIcon (g, 3, juce::Rectangle<float> (ic, ic).withCentre ({ bigArea.getCentreX(), bigArea.getBottom() - ic * 0.5f }),
                            palette::faint, palette::card);
    }
    g.setColour (palette::dim);
    g.setFont (makeFont (14.0f * s, juce::Font::bold).withExtraKerningFactor (0.16f));
    g.drawText (done ? "KICK HITS" : state == ScribaProcessor::State::Recording ? "LISTENING" : "HITS", mid.removeFromTop (26.0f * s), juce::Justification::centred, false);
    mid.removeFromTop (14.0f * s);
    g.setFont (makeFont (13.0f * s));
    g.drawFittedText (done ? "Move the GATE until you hear only the kick: the MIDI follows.\nDrag it onto a drum sampler (note C1 / 36)."
                           : "Set the GATE while the song plays (LISTEN on),\nthen press ANALYZE: the MIDI has the hits you hear.",
                      mid.toNearestInt(), juce::Justification::centredTop, 3);
}

//==============================================================================
void ScribaEditor::paintControls (juce::Graphics& g)
{
    const float s = scale;
    const bool drums = scriba::isDrum (selected);

    drawLabel (g, "INSTRUMENT", instLabelArea, s);
    if (drums)
    {
        drawLabel (g, "GATE", sensLabelArea, s);
        // live LED: lit while the gate is open
        const bool lit = proc.isGateOpen() && proc.isTransportPlaying();
        const float lx = sensLabelArea.getX() + textWidth (labelFont (s), "GATE") + 10.0f * s;
        const auto led = juce::Rectangle<float> (lx, sensLabelArea.getCentreY() - 4.0f * s, 8.0f * s, 8.0f * s);
        if (lit)
            juce::DropShadow (palette::cyan.withAlpha (0.8f), (int) (8.0f * s), {}).drawForRectangle (g, led.toNearestInt());
        g.setColour (lit ? palette::cyan : palette::line);
        g.fillEllipse (led);
        const auto valueArea = sensLabelArea.withTrimmedRight (listenButton.isVisible() ? 100.0f * s : 0.0f);
        drawLabel (g, juce::String (gateSlider.getValue(), 1).replace ("-", kMinus) + " dB", valueArea, s,
                   juce::Justification::centredRight, palette::text);
    }
    else if (selected == scriba::Instrument::Vocals)
        drawLabel (g, "LANGUAGE", sensLabelArea, s);
    else
    {
        drawLabel (g, "SENSITIVITY", sensLabelArea, s);
        drawLabel (g, juce::String ((int) std::lround (sensitivitySlider.getValue() * 100.0)) + "%", sensLabelArea, s,
                   juce::Justification::centredRight, palette::text);
    }

    const bool vocalsMode = selected == scriba::Instrument::Vocals;
    g.setColour (palette::faint);
    g.setFont (makeFont (11.0f * s));
    if (vocalsMode)
        g.drawText ("the text file has the lyrics, the MIDI the melody", sensHintArea.translated (0.0f, 4.0f * s), juce::Justification::centredLeft, false);
    const auto hint = drums ? sensHintArea.withTrimmedRight (100.0f * s) : sensHintArea;
    if (! vocalsMode)
    {
        g.drawText (drums ? "more hits (ghost notes too)" : "fewer notes, more sure", hint, juce::Justification::centredLeft, false);
        g.drawText (drums ? "only loud hits" : "more notes", hint, juce::Justification::centredRight, false);
    }

    if (selected == scriba::Instrument::Snare)
    {
        drawLabel (g, "SONG KEY", keyRowArea.withHeight (14.0f * s), s);
        return;
    }

    // info row: what happens with the key
    auto r = keyRowArea.withTrimmedTop (14.0f * s).reduced (0.0f, 2.0f * s);
    g.setColour (palette::cardHi.withAlpha (0.6f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.28f);
    g.setColour (palette::line);
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.28f, 1.0f);

    auto ir = r.reduced (12.0f * s, 0.0f);
    const float ic = 14.0f * s;
    auto icon = ir.removeFromLeft (ic).withSizeKeepingCentre (ic, ic);
    ir.removeFromLeft (10.0f * s);
    // link icon: two rings
    g.setColour (drums ? palette::dim : palette::cyan);
    g.drawEllipse (icon.withWidth (ic * 0.62f).reduced (1.0f), 1.5f);
    g.drawEllipse (icon.withTrimmedLeft (ic * 0.38f).reduced (1.0f), 1.5f);

    juce::String t;
    if (selected == scriba::Instrument::Kick)
        t = "Kick: the MIDI has exactly the hits that open the gate.";
    else if (vocalsMode)
    {
        const auto sheet = proc.getLyricsSheetInfo();
        t = sheet.haveMap ? "Chords above the words: from " + juce::String (sheet.source) + "."
                          : "Analyze guitar or piano with another Scriba: its chords go above the words.";
    }
    else if (showResults() && result.valid)
        t = juce::String (scriba::keyName (result.best)) + " is shared with every Scriba on the snare.";
    else
        t = "The key found here is sent to the Scriba on the snare.";
    g.setColour (palette::text.withAlpha (0.85f));
    g.setFont (makeFont (12.5f * s));
    g.drawFittedText (t, ir.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
}

void ScribaEditor::paintWave (juce::Graphics& g)
{
    const float s = scale;
    fillCard (g, waveCard, s);
    if (chordsView)
    {
        paintChords (g);
        return;
    }

    // chips: tempo, meter, length, notes
    auto top = waveCard.reduced (16.0f * s, 0.0f).removeFromTop (34.0f * s).withTrimmedTop (10.0f * s);
    const bool drums = scriba::isDrum (selected);
    const double bpm = proc.getBpm();
    const bool haveTime = state == ScribaProcessor::State::Recording || state == ScribaProcessor::State::Analysing || state == ScribaProcessor::State::Done;
    struct Chip { juce::String label, value; };
    const Chip chips[4] = {
        { "BPM", bpm > 0.0 ? juce::String (bpm, std::abs (bpm - std::round (bpm)) < 0.05 ? 0 : 1) : kMinus },
        { "METER", proc.getTimeSigNumerator() > 0 ? juce::String (proc.getTimeSigNumerator()) + "/" + juce::String (proc.getTimeSigDenominator()) : kMinus },
        { "LENGTH", haveTime ? formatTime (proc.getRecordedSeconds()) : kMinus },
        { drums ? "HITS" : "NOTES", showResults() ? juce::String ((int) notes.size()) : kMinus } };
    float x = top.getX();
    for (const auto& c : chips)
    {
        const auto lf = labelFont (s);
        const auto vf = makeFont (13.0f * s, juce::Font::bold);
        const float lw = textWidth (lf, c.label), vw = textWidth (vf, c.value);
        g.setColour (palette::dim);
        g.setFont (lf);
        g.drawText (c.label, juce::Rectangle<float> (x, top.getY(), lw + 2.0f, top.getHeight()), juce::Justification::centredLeft, false);
        g.setColour (palette::text);
        g.setFont (vf);
        g.drawText (c.value, juce::Rectangle<float> (x + lw + 7.0f * s, top.getY(), vw + 2.0f, top.getHeight()), juce::Justification::centredLeft, false);
        x += lw + vw + 30.0f * s;
    }

    auto r = meterArea();
    const float midY = r.getCentreY();

    if (drums)
    {
        // gate meter: detector level (live while the DAW plays, or the whole song after the analysis),
        // the threshold as a line you can drag, and the hits
        const float gateDb = proc.getGateDb();
        const float ty = dbToY (gateDb, r);
        const bool live = proc.isTransportPlaying() && ! detHistory.empty();
        const bool offline = ! live && showResults() && ! drum.detectorDb.empty();

        auto drawLevel = [&] (const std::vector<float>& db, bool scroll)
        {
            const int n = (int) db.size();
            if (n < 2)
                return;
            const int slots = scroll ? 300 : n;
            juce::Path fill;
            fill.startNewSubPath (r.getX() + r.getWidth() * (float) (slots - n) / (float) (slots - 1), r.getBottom());
            for (int i = 0; i < n; ++i)
            {
                const float x = r.getX() + r.getWidth() * (float) (slots - n + i) / (float) (slots - 1);
                fill.lineTo (x, dbToY (db[(size_t) i], r));
            }
            fill.lineTo (r.getRight(), r.getBottom());
            fill.closeSubPath();
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (r.withTop (ty).toNearestInt());
                g.setColour (palette::faint.withAlpha (0.45f));
                g.fillPath (fill);
            }
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (r.withBottom (ty).toNearestInt());
                g.setColour (palette::cyan.withAlpha (0.75f));
                g.fillPath (fill);
            }
        };

        if (live)
            drawLevel (std::vector<float> (detHistory.begin(), detHistory.end()), true);
        else if (offline)
        {
            drawLevel (drum.detectorDb, false);
            double endT = proc.getRecordedSeconds();
            for (const auto& h : drum.hits)
                endT = juce::jmax (endT, h.time);
            const bool stale = std::abs (drum.thresholdDb - gateDb) > 0.01f;
            g.setColour (stale ? palette::faint : palette::text);
            if (endT > 0.0)
                for (const auto& h : drum.hits)
                {
                    const float xx = r.getX() + r.getWidth() * (float) (h.time / endT);
                    g.fillRect (xx - 0.6f, r.getY(), 1.2f, 4.0f * s);
                }
        }
        else
        {
            g.setColour (palette::dim);
            g.setFont (makeFont (12.0f * s));
            g.drawText (state == ScribaProcessor::State::Analysing ? "Processing the recording..."
                                                                   : "Play the song: you see the hits here, drag the line to set the gate",
                        r.withTrimmedTop (r.getHeight() * 0.45f), juce::Justification::centred, false);
        }

        // threshold line + handle
        const bool hot = draggingGate || overThresholdLine (getMouseXYRelative().toFloat());
        g.setColour (palette::amber.withAlpha (hot ? 1.0f : 0.85f));
        g.fillRect (r.getX(), ty - (hot ? 1.0f : 0.5f), r.getWidth(), hot ? 2.0f : 1.0f);
        const auto tag = juce::String (gateDb, 1).replace ("-", kMinus) + " dB";
        const auto tf = makeFont (10.5f * s, juce::Font::bold);
        auto tr = juce::Rectangle<float> (textWidth (tf, tag) + 14.0f * s, 16.0f * s);
        tr = tr.withPosition (r.getRight() - tr.getWidth(), juce::jlimit (r.getY() - 6.0f * s, r.getBottom() - tr.getHeight(), ty - tr.getHeight() * 0.5f));
        g.setColour (palette::amber);
        g.fillRoundedRectangle (tr, tr.getHeight() * 0.5f);
        g.setColour (palette::onCyan);
        g.setFont (tf);
        g.drawText (tag, tr, juce::Justification::centred, false);
        return;
    }

    auto drawWave = [&] (const std::vector<float>& vals, float lineAlpha)
    {
        if (vals.size() < 2)
            return;
        const int n = (int) vals.size();
        juce::Path top2, bottom, fill;
        for (int i = 0; i < n; ++i)
        {
            const float xx = r.getX() + r.getWidth() * (float) i / (float) (n - 1);
            const float h = juce::jlimit (0.0f, 1.0f, vals[(size_t) i]) * r.getHeight() * 0.5f;
            if (i == 0) { top2.startNewSubPath (xx, midY - h); bottom.startNewSubPath (xx, midY + h); fill.startNewSubPath (xx, midY - h); }
            else        { top2.lineTo (xx, midY - h); bottom.lineTo (xx, midY + h); fill.lineTo (xx, midY - h); }
        }
        for (int i = n - 1; i >= 0; --i)
        {
            const float xx = r.getX() + r.getWidth() * (float) i / (float) (n - 1);
            fill.lineTo (xx, midY + juce::jlimit (0.0f, 1.0f, vals[(size_t) i]) * r.getHeight() * 0.5f);
        }
        fill.closeSubPath();
        g.setColour (palette::cyan.withAlpha (0.10f * lineAlpha));
        g.fillPath (fill);
        g.setColour (palette::cyan.withAlpha (lineAlpha));
        g.strokePath (top2, juce::PathStrokeType (1.1f));
        g.strokePath (bottom, juce::PathStrokeType (1.1f));
    };

    if (state == ScribaProcessor::State::Recording)
    {
        drawWave (std::vector<float> (history.begin(), history.end()), 0.95f);
        return;
    }
    if (showResults() && ! overview.empty())
    {
        float mx = 0.0f;
        for (float v : overview)
            mx = juce::jmax (mx, v);
        std::vector<float> norm (overview.size());
        for (size_t i = 0; i < overview.size(); ++i)
            norm[i] = mx > 0.0f ? std::pow (overview[i] / mx, 0.8f) : 0.0f;

        const bool hasNotes = ! notes.empty();
        drawWave (norm, hasNotes ? 0.30f : 0.9f);
        if (! hasNotes)
            return;

        double endT = proc.getRecordedSeconds();
        for (const auto& n : notes)
            endT = juce::jmax (endT, n.end);

        if (scriba::isDrum (analysed))
        {
            // hits: one tick each, taller when louder
            g.setColour (palette::cyan);
            for (const auto& n : notes)
            {
                const float xx = r.getX() + r.getWidth() * (float) (n.start / endT);
                const float h = r.getHeight() * (0.25f + 0.7f * (float) n.velocity / 127.0f);
                g.fillRect (xx - 0.6f, midY - h * 0.5f, 1.2f, h);
            }
            return;
        }

        // piano roll: ogni nota trovata e' un trattino alla sua altezza
        int lo = 127, hi = 0;
        for (const auto& n : notes)
        {
            lo = juce::jmin (lo, n.midi);
            hi = juce::jmax (hi, n.midi);
        }
        const int span = juce::jmax (18, hi - lo + 3);
        const int base = lo - (span - (hi - lo + 1)) / 2;
        const float rowH = r.getHeight() / (float) span;
        g.setColour (palette::cyan);
        for (const auto& n : notes)
        {
            const float x0 = r.getX() + r.getWidth() * (float) (n.start / endT);
            const float x1 = r.getX() + r.getWidth() * (float) (n.end / endT);
            const float y = r.getBottom() - rowH * (float) (n.midi - base + 1);
            g.fillRoundedRectangle (x0, y, juce::jmax (1.5f, x1 - x0), juce::jmax (1.8f, rowH * 0.85f), 1.0f);
        }
        return;
    }

    g.setColour (palette::line);
    g.drawLine (r.getX(), midY, r.getRight(), midY, 1.0f);
    juce::String msg = drums ? "The hits will appear here" : "The waveform and the notes will appear here";
    if (state == ScribaProcessor::State::Analysing)
        msg = "Processing the recording...";
    else if (state == ScribaProcessor::State::Armed)
        msg = "Waiting for the DAW to play";
    else if (resultsOk && analysed != selected)
        msg = audioKept ? "Analyzing..." : "Saved result is for " + juce::String (scriba::instrumentName (analysed)) + ": press ANALYZE to record again";
    auto tr = juce::Rectangle<float> (textWidth (makeFont (12.0f * s), msg) + 28.0f * s, 22.0f * s).withCentre (r.getCentre());
    g.setColour (palette::card);
    g.fillRoundedRectangle (tr, tr.getHeight() * 0.5f);
    g.setColour (palette::dim);
    g.setFont (makeFont (12.0f * s));
    g.drawText (msg, tr, juce::Justification::centred, false);
}

void ScribaEditor::paintChords (juce::Graphics& g)
{
    const float s = scale;
    auto top = waveCard.reduced (16.0f * s, 0.0f).removeFromTop (34.0f * s).withTrimmedTop (9.0f * s);
    top.setRight ((float) songButton.getX() - 10.0f * s);

    // option labels
    const char* labels[4] = { "TYPE", "PLAY", "VOICING", "LENGTH" };
    for (int i = 0; i < 4; ++i)
        drawLabel (g, labels[i], chordLabelAreas[(size_t) i], s);

    const int key = proc.getChordKey();
    if (key < 0 || padCount == 0)
    {
        drawLabel (g, "CHORDS", top, s);
        g.setColour (palette::dim);
        g.setFont (makeFont (12.5f * s));
        const juce::String msg = scriba::isDrum (selected) || selected == scriba::Instrument::Vocals
                                     ? "Analyze a bass, guitar or piano track with Scriba (or choose the song key): its chords appear here"
                                     : "Press ANALYZE: when the key is found, its chords appear here";
        g.drawFittedText (msg, padsArea.toNearestInt(), juce::Justification::centred, 2, 0.9f);
        return;
    }

    // "CHORDS IN A MINOR  ·  click to listen, drag into your DAW"
    const juce::String title = "CHORDS IN " + juce::String (scriba::keyName (key)).toUpperCase();
    const auto lf = labelFont (s);
    const float tw = textWidth (lf, title);
    drawLabel (g, title, top, s, juce::Justification::centredLeft, palette::cyan);
    auto hint = top.withTrimmedLeft (tw + 12.0f * s);
    g.setColour (palette::dim);
    g.setFont (makeFont (11.5f * s));
    juce::String h = kDot + "  click to listen, drag onto a track";
    if (songHasChords)
        h += "   " + kDot + "  ";
    g.drawText (h, hint, juce::Justification::centredLeft, true);
    if (songHasChords)
    {
        const float hw = textWidth (makeFont (11.5f * s), h);
        if (hw + 120.0f * s < hint.getWidth())
        {
            const float d = 6.0f * s;
            g.setColour (palette::amber);
            g.fillEllipse (hint.getX() + hw + 2.0f * s, hint.getCentreY() - d * 0.5f, d, d);
            g.setColour (palette::dim);
            g.drawText ("in the song", hint.withTrimmedLeft (hw + 2.0f * s + d + 5.0f * s), juce::Justification::centredLeft, true);
        }
    }

}

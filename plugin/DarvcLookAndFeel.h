#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Shared palette (previously a private copy in PluginEditor.cpp). */
namespace DarvcColours
{
    inline const juce::Colour bg      { 0xff050914 };
    inline const juce::Colour panel   { 0xff080f20 };
    inline const juce::Colour panel2  { 0xff0b1428 };
    inline const juce::Colour border  { 0xff1a3158 };
    inline const juce::Colour cyan    { 0xff23d9ff };
    inline const juce::Colour blue    { 0xff287bff };
    inline const juce::Colour purple  { 0xffbd43ff };
    inline const juce::Colour text    { 0xffedf5ff };
    inline const juce::Colour muted   { 0xff8ca5ca };
    inline const juce::Colour success { 0xff43e6a8 };
    inline const juce::Colour warning { 0xfff5c66b };
}

/** Bullet separator built from its code point. Embedding the literal
    character in source made the UI render mojibake (the sequence passed
    through compilers with different source encodings); a numeric code
    point cannot be corrupted that way. */
inline juce::String darvcBullet()
{
    return juce::String::charToString(static_cast<juce::juce_wchar>(0x2022));
}

inline juce::Font darvcFont(float height, int styleFlags = juce::Font::plain)
{
    return juce::Font(juce::FontOptions(height, styleFlags));
}

/** Rounded dark-neon styling so buttons/combo boxes/sliders match the
    hand-painted panels instead of JUCE's default look (whose pale focus
    outlines and flat fills looked out of place in the reference design). */
class DarvcLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    DarvcLookAndFeel()
    {
        using namespace DarvcColours;
        setColour(juce::TextButton::buttonColourId, panel2);
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff164c86));
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::TextButton::textColourOnId, text);
        setColour(juce::ComboBox::backgroundColourId, panel2);
        setColour(juce::ComboBox::textColourId, text);
        setColour(juce::ComboBox::outlineColourId, border);
        setColour(juce::ComboBox::arrowColourId, muted);
        setColour(juce::ComboBox::buttonColourId, panel2);
        setColour(juce::PopupMenu::backgroundColourId, panel);
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff164c86));
        setColour(juce::PopupMenu::highlightedTextColourId, text);
        setColour(juce::Label::textColourId, text);
        setColour(juce::TextEditor::backgroundColourId, panel2);
        setColour(juce::TextEditor::textColourId, text);
        setColour(juce::TextEditor::outlineColourId, border);
        setColour(juce::TextEditor::focusedOutlineColourId, blue);
        setColour(juce::ListBox::backgroundColourId, panel2);
        setColour(juce::ListBox::outlineColourId, border);
        setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff27457c));
        setColour(juce::ScrollBar::trackColourId, panel2);
        setColour(juce::TooltipWindow::backgroundColourId, panel);
        setColour(juce::TooltipWindow::textColourId, text);
        setColour(juce::TooltipWindow::outlineColourId, border);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return darvcFont(juce::jmin(15.0f, static_cast<float>(buttonHeight) * 0.55f));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override { return darvcFont(14.0f); }
    juce::Font getLabelFont(juce::Label&) override { return darvcFont(13.0f); }
    juce::Font getPopupMenuFont() override { return darvcFont(14.0f); }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        const auto corner = 8.0f;
        const auto edges = button.getConnectedEdgeFlags();

        juce::Path path;
        path.addRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                                 corner, corner,
                                 (edges & juce::Button::ConnectedOnLeft) == 0,
                                 (edges & juce::Button::ConnectedOnRight) == 0,
                                 (edges & juce::Button::ConnectedOnLeft) == 0,
                                 (edges & juce::Button::ConnectedOnRight) == 0);

        const bool isPrimary = button.getName() == "primary";
        if (isPrimary)
        {
            juce::ColourGradient gradient(shouldDrawButtonAsDown
                                              ? juce::Colour(0xff1a55c4) : juce::Colour(0xff1f66e0),
                                          bounds.getX(), bounds.getY(),
                                          juce::Colour(0xff123a8f),
                                          bounds.getX(), bounds.getBottom(), false);
            if (shouldDrawButtonAsHighlighted)
                gradient = juce::ColourGradient(juce::Colour(0xff2a74ee), bounds.getX(), bounds.getY(),
                                                juce::Colour(0xff1647a8), bounds.getX(), bounds.getBottom(), false);
            g.setGradientFill(gradient);
        }
        else
        {
            auto base = backgroundColour;
            if (shouldDrawButtonAsDown)
                base = base.brighter(0.25f);
            else if (shouldDrawButtonAsHighlighted)
                base = base.brighter(0.12f);
            g.setColour(base);
        }
        g.fillPath(path);

        const bool focused = button.hasKeyboardFocus(true);
        g.setColour(isPrimary ? DarvcColours::cyan.withAlpha(0.55f)
                              : DarvcColours::border.brighter(focused ? 0.6f : 0.15f));
        g.strokePath(path, juce::PathStrokeType(1.0f));
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
                      juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(box.hasKeyboardFocus(true) ? DarvcColours::blue
                                               : box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(bounds, 8.0f, 1.0f);

        const float arrowZone = 28.0f;
        const float cx = bounds.getRight() - arrowZone * 0.5f;
        const float cy = bounds.getCentreY();
        juce::Path arrow;
        arrow.addTriangle(cx - 5.0f, cy - 3.0f, cx + 5.0f, cy - 3.0f, cx, cy + 4.0f);
        g.setColour(box.findColour(juce::ComboBox::arrowColourId));
        g.fillPath(arrow);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(10, 1, box.getWidth() - 38, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                                   minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const auto track = juce::Rectangle<float>(static_cast<float>(x) + 2.0f,
                                                  static_cast<float>(y + height / 2) - 3.0f,
                                                  static_cast<float>(width) - 4.0f, 6.0f);
        g.setColour(juce::Colour(0xff14223b));
        g.fillRoundedRectangle(track, 3.0f);

        const float fraction = juce::jlimit(0.0f, 1.0f,
                                             (sliderPos - minSliderPos)
                                                 / juce::jmax(1.0f, maxSliderPos - minSliderPos));
        auto filled = track.withWidth(track.getWidth() * fraction);
        juce::ColourGradient gradient(DarvcColours::blue, track.getX(), track.getY(),
                                      DarvcColours::cyan, track.getRight(), track.getY(), false);
        g.setGradientFill(gradient);
        g.fillRoundedRectangle(filled, 3.0f);

        const float thumbR = 8.0f;
        const float thumbX = juce::jlimit(track.getX(), track.getRight(), sliderPos);
        const float thumbY = track.getCentreY();
        g.setColour(DarvcColours::cyan.withAlpha(0.35f));
        g.fillEllipse(thumbX - thumbR - 4.0f, thumbY - thumbR - 4.0f,
                      (thumbR + 4.0f) * 2.0f, (thumbR + 4.0f) * 2.0f);
        g.setColour(DarvcColours::cyan);
        g.fillEllipse(thumbX - thumbR, thumbY - thumbR, thumbR * 2.0f, thumbR * 2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.fillEllipse(thumbX - 3.0f, thumbY - 3.0f, 6.0f, 6.0f);
    }
};

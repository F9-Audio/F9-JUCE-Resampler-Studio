#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
 * Custom LookAndFeel for F9 Batch Resampler
 * Matches the bright, clean macOS aesthetic of the original Swift version
 */
class F9LookAndFeel : public juce::LookAndFeel_V4
{
public:
    F9LookAndFeel()
    {
        // Color scheme matching original Swift UI
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xfff5f5f7)); // Light gray background

        // Sidebar colors
        setColour(juce::GroupComponent::outlineColourId, juce::Colour(0xffe5e5e7));
        setColour(juce::GroupComponent::textColourId, juce::Colour(0xff1d1d1f));

        // ComboBox (dropdown) colors - matching macOS style
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xffffffff));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xffd1d1d6));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xff1d1d1f));
        setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff86868b));

        // Button colors - macOS blue accent
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff007aff)); // Apple blue
        setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff0051d5)); // Darker blue when pressed

        // TextEditor colors
        setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xffffffff));
        setColour(juce::TextEditor::outlineColourId, juce::Colour(0xffd1d1d6));
        setColour(juce::TextEditor::textColourId, juce::Colour(0xff1d1d1f));
        setColour(juce::TextEditor::highlightColourId, juce::Colour(0xff007aff).withAlpha(0.3f));

        // Label colors
        setColour(juce::Label::textColourId, juce::Colour(0xff1d1d1f));

        // Slider colors
        setColour(juce::Slider::thumbColourId, juce::Colour(0xffffffff));
        setColour(juce::Slider::trackColourId, juce::Colour(0xff007aff));
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xffe5e5e7));

        // ToggleButton colors
        setColour(juce::ToggleButton::textColourId, juce::Colour(0xff1d1d1f));
        setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff007aff));
        setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xffc7c7cc));

        // Progress bar colors
        setColour(juce::ProgressBar::backgroundColourId, juce::Colour(0xffe5e5e7));
        setColour(juce::ProgressBar::foregroundColourId, juce::Colour(0xff007aff));
    }

    // Custom button drawing for rounded macOS style
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                             const juce::Colour& backgroundColour,
                             bool shouldDrawButtonAsHighlighted,
                             bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f, 0.5f);
        auto cornerSize = 6.0f;

        auto baseColour = backgroundColour;

        if (shouldDrawButtonAsDown)
            baseColour = baseColour.darker(0.1f);
        else if (shouldDrawButtonAsHighlighted)
            baseColour = baseColour.brighter(0.05f);

        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, cornerSize);

        // Subtle border
        g.setColour(baseColour.darker(0.2f));
        g.drawRoundedRectangle(bounds, cornerSize, 1.0f);
    }

    // Custom ComboBox drawing for macOS style
    void drawComboBox(juce::Graphics& g, int width, int height,
                     bool isButtonDown,
                     int buttonX, int buttonY, int buttonW, int buttonH,
                     juce::ComboBox& box) override
    {
        auto cornerSize = 6.0f;
        juce::Rectangle<int> boxBounds(0, 0, width, height);

        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(boxBounds.toFloat(), cornerSize);

        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(boxBounds.toFloat().reduced(0.5f, 0.5f), cornerSize, 1.0f);

        // Draw arrow
        juce::Rectangle<int> arrowZone(width - 20, 0, 20, height);
        juce::Path path;
        path.startNewSubPath(arrowZone.getX() + 6, arrowZone.getCentreY() - 2);
        path.lineTo(arrowZone.getCentreX(), arrowZone.getCentreY() + 3);
        path.lineTo(arrowZone.getRight() - 6, arrowZone.getCentreY() - 2);

        g.setColour(box.findColour(juce::ComboBox::arrowColourId));
        g.strokePath(path, juce::PathStrokeType(2.0f));
    }

    // Custom TextEditor drawing
    void fillTextEditorBackground(juce::Graphics& g, int width, int height,
                                  juce::TextEditor& textEditor) override
    {
        auto cornerSize = 6.0f;
        g.setColour(textEditor.findColour(juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle(0, 0, width, height, cornerSize);
    }

    void drawTextEditorOutline(juce::Graphics& g, int width, int height,
                               juce::TextEditor& textEditor) override
    {
        auto cornerSize = 6.0f;
        g.setColour(textEditor.findColour(juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle(0, 0, width, height, cornerSize, 1.0f);
    }

    // Custom slider drawing - macOS style
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos,
                         float minSliderPos, float maxSliderPos,
                         const juce::Slider::SliderStyle style,
                         juce::Slider& slider) override
    {
        if (slider.isBar())
        {
            g.setColour(slider.findColour(juce::Slider::trackColourId));
            g.fillRect(slider.isHorizontal() ? juce::Rectangle<float>(static_cast<float>(x), y + 0.5f, sliderPos - x, height - 1.0f)
                                             : juce::Rectangle<float>(x + 0.5f, sliderPos, width - 1.0f, y + (height - sliderPos)));
        }
        else
        {
            auto trackWidth = juce::jmin(6.0f, slider.isHorizontal() ? height * 0.25f : width * 0.25f);

            juce::Point<float> startPoint(slider.isHorizontal() ? x : x + width * 0.5f,
                                         slider.isHorizontal() ? y + height * 0.5f : height + y);

            juce::Point<float> endPoint(slider.isHorizontal() ? width + x : startPoint.x,
                                       slider.isHorizontal() ? startPoint.y : y);

            juce::Path backgroundTrack;
            backgroundTrack.startNewSubPath(startPoint);
            backgroundTrack.lineTo(endPoint);
            g.setColour(slider.findColour(juce::Slider::backgroundColourId));
            g.strokePath(backgroundTrack, juce::PathStrokeType(trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            juce::Path valueTrack;
            juce::Point<float> minPoint, maxPoint;

            if (slider.isHorizontal())
            {
                minPoint = startPoint;
                maxPoint = {sliderPos, height * 0.5f};
            }
            else
            {
                minPoint = {width * 0.5f, sliderPos};
                maxPoint = endPoint;
            }

            valueTrack.startNewSubPath(minPoint);
            valueTrack.lineTo(maxPoint);
            g.setColour(slider.findColour(juce::Slider::trackColourId));
            g.strokePath(valueTrack, juce::PathStrokeType(trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Draw thumb
            auto thumbWidth = 16.0f;
            g.setColour(slider.findColour(juce::Slider::thumbColourId));
            g.fillEllipse(juce::Rectangle<float>(thumbWidth, thumbWidth).withCentre(slider.isHorizontal() ? maxPoint : minPoint));

            // Thumb shadow
            g.setColour(juce::Colours::black.withAlpha(0.1f));
            g.fillEllipse(juce::Rectangle<float>(thumbWidth, thumbWidth).withCentre(slider.isHorizontal() ? maxPoint : minPoint).translated(0, 1));
        }
    }

    // Custom ToggleButton for checkbox/switch style
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        auto tickBounds = bounds.removeFromLeft(bounds.getHeight()).reduced(4);

        // Draw checkbox
        auto cornerSize = 4.0f;

        if (button.getToggleState())
        {
            g.setColour(button.findColour(juce::ToggleButton::tickColourId));
            g.fillRoundedRectangle(tickBounds, cornerSize);

            // Draw checkmark
            juce::Path tick;
            tick.startNewSubPath(tickBounds.getX() + tickBounds.getWidth() * 0.25f,
                                tickBounds.getCentreY());
            tick.lineTo(tickBounds.getX() + tickBounds.getWidth() * 0.45f,
                       tickBounds.getY() + tickBounds.getHeight() * 0.7f);
            tick.lineTo(tickBounds.getRight() - tickBounds.getWidth() * 0.25f,
                       tickBounds.getY() + tickBounds.getHeight() * 0.3f);

            g.setColour(juce::Colours::white);
            g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setColour(juce::Colour(0xffe5e5e7));
            g.fillRoundedRectangle(tickBounds, cornerSize);

            g.setColour(juce::Colour(0xffd1d1d6));
            g.drawRoundedRectangle(tickBounds, cornerSize, 1.0f);
        }
    }

    // Font settings
    juce::Font getLabelFont(juce::Label&) override
    {
        return juce::Font(juce::FontOptions().withHeight(13.0f));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::Font(juce::FontOptions().withHeight(13.0f));
    }

    juce::Font getTextButtonFont(juce::TextButton&, int) override
    {
        return juce::Font(juce::FontOptions(13.0f, juce::Font::bold));
    }
};

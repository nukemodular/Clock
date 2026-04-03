// -------------------------------------------------------------------------------------------------
// Restored, de-corrupted PluginEditor.cpp
// -------------------------------------------------------------------------------------------------

#include <utility>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "GumroadLicenseValidator.h"
#include "LicenseDialog.h"
#include "build_info.h"
#include "BinaryData.h"
#include "UiTheme.h"
#include "UiComponents.h"
#include "HitRouting.h"
#include <array>
#include <vector>
#include <optional>


namespace
{
    const auto kFixedHeaderBaseColour = UiThemeColours::fixedBaseColour();
    constexpr auto kGumroadProductId = "eaWDZgauhj9vCNxpyiBoIQ==";

    // Simple rectangular button
    class SimpleRectButton : public juce::Button
    {
    public:
        SimpleRectButton(const juce::String& name, UiThemeColours& t) : juce::Button(name), theme(t) {}
        void paintButton(juce::Graphics& g, bool shouldDrawButtonAsMouseOver, bool shouldDrawButtonAsDown) override
        {
            auto bounds = getLocalBounds().toFloat();
            auto cyanClicked = theme.cyan();
            auto cyan = cyanClicked.withAlpha(0.5f);

            if (shouldDrawButtonAsDown)
            {
                g.setColour(cyanClicked);
                g.fillRect(bounds);
                g.setColour(cyanClicked);   
                g.drawRect(bounds, 1.0f);
            }
            else
            {
                g.setColour(cyan);
                g.fillRect(bounds);
                g.setColour(cyanClicked);   
                g.drawRect(bounds, 1.0f);
            }
        }
    private:
        UiThemeColours& theme;
    };

    // LNF for the MIDI channel numberbox: Arial Bold and accent text.
    class MidiChannelLNF : public juce::LookAndFeel_V4
    {
    public:
        MidiChannelLNF(UiThemeColours& t) : theme(t)
        {
            setColour(juce::Slider::textBoxTextColourId, theme.accent());
            setColour(juce::Slider::textBoxOutlineColourId, theme.accent());
            setColour(juce::Slider::textBoxHighlightColourId, theme.accent().withAlpha(0.35f));
        }
        juce::Font getLabelFont(juce::Label&) override
        {
            return juce::Font(juce::FontOptions("Arial", 14.0f, juce::Font::bold));
        }
        juce::Label* createSliderTextBox(juce::Slider& slider) override
        {
            auto* l = new juce::Label();
            l->setJustificationType(juce::Justification::centredLeft);
            l->setColour(juce::Label::textColourId, slider.findColour(juce::Slider::textBoxTextColourId));
            l->setColour(juce::Label::outlineColourId, slider.findColour(juce::Slider::textBoxOutlineColourId));
            l->setFont(getLabelFont(*l));
            return l;
        }
    private:
        UiThemeColours& theme;
    };

    // Custom LNF for the slider text to ensure Arial Bold
    class MidiRowLNF : public juce::LookAndFeel_V4
    {
    public:
        MidiRowLNF(UiThemeColours& t) : theme(t)
        {
            setColour(juce::Slider::textBoxTextColourId, theme.cyan());
            setColour(juce::Slider::textBoxOutlineColourId, theme.cyan());
            setColour(juce::Slider::textBoxHighlightColourId, theme.accent().withAlpha(0.5f));
        }
        juce::Font getLabelFont(juce::Label&) override
        {
            return juce::Font(juce::FontOptions("Arial", 14.0f, juce::Font::bold));
        }
        juce::Label* createSliderTextBox(juce::Slider& slider) override
        {
            auto* l = new juce::Label();
            l->setJustificationType(juce::Justification::centred);
            l->setColour(juce::Label::textColourId, slider.findColour(juce::Slider::textBoxTextColourId));
            l->setColour(juce::Label::outlineColourId, slider.findColour(juce::Slider::textBoxOutlineColourId));
            l->setFont(getLabelFont(*l));
            return l;
        }
    private:
        UiThemeColours& theme;
    };

    class SyncLatchLNF : public ThemeLNF
    {
    public:
        SyncLatchLNF(UiThemeColours& t) : ThemeLNF(t), theme(t) {}
        
        void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                  bool isHighlighted, bool isDown) override
        {
            auto r = b.getLocalBounds().toFloat();
            const float corner = 2.0f;
            
            juce::Colour fill;
            if (b.getToggleState() || isDown)
                fill = theme.cyan();
            else
                fill = theme.cyan().withAlpha(0.4f);

            g.setColour(fill);
            g.fillRoundedRectangle(r, corner);
        }

        void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            g.setColour(theme.accent().darker(1.0f));
            g.setFont(juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold)));
            g.drawFittedText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
        }
        
    private:
        UiThemeColours& theme;
    };

    class MidiRemoteRow : public juce::Component
    {
    public:
        enum Type { Note, CC, Channel };
        enum LayoutMode { Full, Compact };

        MidiRemoteRow(const juce::String& labelText,
                      Type type,
                      std::atomic<int>& valueRef,
                      UiThemeColours& t,
                      LayoutMode mode = Full,
                      std::atomic<bool>* typeIsCcFlag = nullptr)
            : value(valueRef), layoutMode(mode), theme(t), lnf(t), decBtn("-", t), incBtn("+", t), fixedType(type), typeIsCc(typeIsCcFlag)
        {
            label.setText(labelText, juce::dontSendNotification);
            label.setFont(juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold)));
            label.setColour(juce::Label::textColourId, theme.cyan());
            label.setJustificationType(juce::Justification::centredRight);
            // Avoid "squashed" text when space is tight: don't allow horizontal scaling.
            label.setMinimumHorizontalScale(1.0f);
            addAndMakeVisible(label);

            if (typeIsCc != nullptr && (fixedType == Note || fixedType == CC))
            {
                typeToggle = std::make_unique<HeaderSwitchToggle>(theme);
                typeToggle->setSize(10, 20);
                typeToggle->setToggleState(typeIsCc->load(std::memory_order_relaxed));
                typeToggle->onToggle = [this](bool on) {
                    if (typeIsCc)
                        typeIsCc->store(on, std::memory_order_relaxed);
                    applyTextMapping();
                    valueSlider.updateText();
                    repaint();
                };
                addAndMakeVisible(*typeToggle);
            }

            // Value Box (using Slider in TextBoxOnly mode for display/entry)
            valueSlider.setSliderStyle(juce::Slider::LinearBar); 
            // Fixed size 40x20 requested
            valueSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 20);
            valueSlider.setLookAndFeel(&lnf);
            
            if (type == Channel)
            {
                valueSlider.setRange(0, 16, 1);
                valueSlider.textFromValueFunction = [](double val) {
                    if (val == 0) return juce::String("ALL");
                    return juce::String((int)val);
                };
            }
            else
            {
                valueSlider.setRange(-1, 127, 1);
                applyTextMapping();
            }
            
            // Ensure initial text is correct
            valueSlider.setValue(value.load(), juce::dontSendNotification);
            
            valueSlider.setColour(juce::Slider::textBoxTextColourId, theme.cyan());
            // Cyan outline requested
            valueSlider.setColour(juce::Slider::textBoxOutlineColourId, theme.cyan());
            // Greyish accent fill
            valueSlider.setColour(juce::Slider::textBoxBackgroundColourId, theme.accent().withAlpha(0.5f));
            valueSlider.setColour(juce::Slider::trackColourId, theme.accent().withAlpha(0.5f));
            valueSlider.setColour(juce::Slider::backgroundColourId, theme.accent().withAlpha(0.5f));
            
            // Force update text
            valueSlider.updateText();

            valueSlider.onValueChange = [this]() {
                value.store((int)valueSlider.getValue());
            };
            addAndMakeVisible(valueSlider);

            decBtn.onClick = [this] { valueSlider.setValue(valueSlider.getValue() - 1.0); };
            addAndMakeVisible(decBtn);

            incBtn.onClick = [this] { valueSlider.setValue(valueSlider.getValue() + 1.0); };
            addAndMakeVisible(incBtn);
        }

        ~MidiRemoteRow() override
        {
            valueSlider.setLookAndFeel(nullptr);
        }
        juce::Rectangle<int> getValueBoxBounds() const { return valueSlider.getBounds(); }
        juce::Rectangle<int> getTypeToggleBounds() const
        {
            return typeToggle ? typeToggle->getBounds() : juce::Rectangle<int>();
        }

        void resized() override
        {
            auto area = getLocalBounds();
            
            // Fixed dimensions
            const int btnW = 10;
            const int btnH = 20;
            const int boxW = 40;
            const int boxH = 20;
            
            // Center vertically
            int y = (area.getHeight() - boxH) / 2;
            
            // Right align the controls with 4px overlap (closer)
            // Total width calculation:
            // DecBtn: 0..10
            // Box:    6..46 (starts at 6, width 40) -> Overlaps DecBtn by 4px
            // IncBtn: 42..52 (starts at 42, width 10) -> Overlaps Box by 4px
            // Total width needed = 52
            
            const bool hasTypeToggle = (typeToggle != nullptr);
            // Keep the control cluster compact but ensure it doesn't overflow (switch must fit).
            // NOTE: For rows without the NOTE/CC toggle, the +/- cluster needs 57px
            // (incBtn ends at x + 57). Using 55px clips the '+' by ~2px.
            auto controlsArea = area.removeFromRight(hasTypeToggle ? 70 : 57);
            int x = controlsArea.getX();

            // Controls order: '-', value, '+', then NOTE/CC switch (requested)
            decBtn.setBounds(x - 1, y, btnW, btnH);
            valueSlider.setBounds(x + 8, y, boxW, boxH);
            incBtn.setBounds(x + 47, y, btnW, btnH);

            if (hasTypeToggle)
            {
                // Place directly after '+' with a small gap
                typeToggle->setBounds(x + 60, y, 10, btnH);
            }
            
            // Label takes the rest
            label.setBounds(area);
        }

    private:
        void applyTextMapping()
        {
            if (fixedType == Channel)
                return;

            const bool asCc = (typeIsCc != nullptr) ? typeIsCc->load(std::memory_order_relaxed) : (fixedType == CC);
            if (!asCc)
            {
                valueSlider.textFromValueFunction = [](double val) {
                    if (val < 0) return juce::String("OFF");
                    return juce::MidiMessage::getMidiNoteName((int)val, true, true, 3);
                };
            }
            else
            {
                valueSlider.textFromValueFunction = [](double val) {
                    if (val < 0) return juce::String("OFF");
                    return juce::String((int)val);
                };
            }
        }

        UiThemeColours& theme;
        MidiRowLNF lnf;
        juce::Label label;
        juce::Slider valueSlider;
        SimpleRectButton decBtn, incBtn;
        std::atomic<int>& value;
        LayoutMode layoutMode;
        Type fixedType;
        std::atomic<bool>* typeIsCc { nullptr };
        std::unique_ptr<HeaderSwitchToggle> typeToggle;
    };

    class MidiRemoteSetupComponent : public juce::Component
    {
    public:
        MidiRemoteSetupComponent(ClockSyncAudioProcessor& p,
                                 const std::vector<juce::MidiDeviceInfo>& cachedRemoteInputs)
            : processor(p), theme(p.theme),
                            startRow("START", MidiRemoteRow::Note, p.midiRemoteStart, p.theme, MidiRemoteRow::Compact, &p.midiRemoteStartIsCC),
                            stopRow("STOP", MidiRemoteRow::Note, p.midiRemoteStop, p.theme, MidiRemoteRow::Compact, &p.midiRemoteStopIsCC),
              offsetRow("OFFSET", MidiRemoteRow::CC, p.midiRemoteOffset, p.theme),
              shuffleRow("SHUFFLE", MidiRemoteRow::CC, p.midiRemoteShuffle, p.theme),
              clockDivRow("CLK DIV.", MidiRemoteRow::CC, p.midiRemoteClockDiv, p.theme),
                            triggerRow("TRIGGER", MidiRemoteRow::Note, p.midiRemoteTrigger, p.theme, MidiRemoteRow::Compact, &p.midiRemoteTriggerIsCC),
                            resyncRow("RESYNC", MidiRemoteRow::Note, p.midiRemoteResync, p.theme, MidiRemoteRow::Compact, &p.midiRemoteResyncIsCC),
                            gatedSyncRow("LATCH", MidiRemoteRow::Note, p.midiRemoteGatedSync, p.theme, MidiRemoteRow::Compact, &p.midiRemoteGatedSyncIsCC),
              autoFillRow("AUTOFILL", MidiRemoteRow::CC, p.midiRemoteAutoFill, p.theme),
                            channelValue(p.midiRemoteChannel),
                            channelLnf(p.theme)
        {
            titleLabel.setText("MIDI CONTROL  CHANNEL", juce::dontSendNotification);
            titleLabel.setFont(juce::Font(juce::FontOptions("Arial", 14.0f, juce::Font::bold)));
            titleLabel.setColour(juce::Label::textColourId, theme.cyan());
            titleLabel.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(titleLabel);

            // Channel Slider
            channelSlider.setSliderStyle(juce::Slider::LinearBar);
            channelSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 24, 20);
            channelSlider.setRange(1, 16, 1);
            channelSlider.setLookAndFeel(&channelLnf);
            channelSlider.setColour(juce::Slider::textBoxTextColourId, theme.accent());
            channelSlider.setColour(juce::Slider::trackColourId, juce::Colours::transparentBlack);
            channelSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            channelSlider.setValue(channelValue.load());
            channelSlider.onValueChange = [this]() { channelValue.store((int)channelSlider.getValue()); };
            addAndMakeVisible(channelSlider);

            addAndMakeVisible(startRow);
            addAndMakeVisible(stopRow);
            addAndMakeVisible(offsetRow);
            addAndMakeVisible(shuffleRow);
            addAndMakeVisible(clockDivRow);
            addAndMakeVisible(triggerRow);
            addAndMakeVisible(resyncRow);
            addAndMakeVisible(gatedSyncRow);
            addAndMakeVisible(autoFillRow);

            // Dedicated LNF: no arrow/button width + middle-ellipsis text.
            remotePortLnf = std::make_unique<RemotePortComboLNF>(theme);

            // REMOTE PORT selector (embedded here; replaces the old popup window)
            remotePortBox.setJustificationType(juce::Justification::centred);
            remotePortBox.setColour(juce::ComboBox::outlineColourId, theme.cyan());
            remotePortBox.setColour(juce::ComboBox::backgroundColourId, theme.fixedBase());
            // Hide the internal label text; we draw it ourselves in the LNF.
            remotePortBox.setColour(juce::ComboBox::textColourId, juce::Colours::transparentBlack);
            // No arrow (and no reserved arrow space).
            remotePortBox.setColour(juce::ComboBox::arrowColourId, juce::Colours::transparentBlack);
            remotePortBox.setLookAndFeel(remotePortLnf.get());
            addAndMakeVisible(remotePortBox);

            remotePortLabel.setText("REMOTE PORT", juce::dontSendNotification);
            remotePortLabel.setFont(juce::Font(juce::FontOptions("Arial", 10.0f, juce::Font::bold)));
            remotePortLabel.setColour(juce::Label::textColourId, theme.cyan());
            remotePortLabel.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(remotePortLabel);

            noteCcLabel.setText("NOTE/CC", juce::dontSendNotification);
            noteCcLabel.setFont(juce::Font(juce::FontOptions("Arial", 10.0f, juce::Font::bold)));
            noteCcLabel.setColour(juce::Label::textColourId, theme.cyan());
            noteCcLabel.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(noteCcLabel);

            addAndMakeVisible(noteCcTriangle);

            remotePortBox.onChange = [this]()
            {
                const int idx = remotePortBox.getSelectedItemIndex();
                if (idx < 0)
                    return;

                auto& st = processor.getAPVTS().state;

                // Index 0 is "Host/Track MIDI"
                if (idx == 0)
                {
                    st.setProperty("ui.midiRemoteInDeviceId", juce::String(), nullptr);
                    return;
                }

                const int devIdx = idx - 1;
                if (devIdx >= 0 && devIdx < (int) remoteDevices.size())
                    st.setProperty("ui.midiRemoteInDeviceId", remoteDevices[(size_t) devIdx].identifier, nullptr);
            };

            // IMPORTANT: Do not enumerate CoreMIDI devices here.
            // On macOS, device enumeration can briefly stall MIDI I/O and cause
            // external MIDI clock hiccups. Use the editor-provided cached list.
            setRemoteDevices(cachedRemoteInputs);
        }

        ~MidiRemoteSetupComponent() override
        {
            channelSlider.setLookAndFeel(nullptr);
            remotePortBox.setLookAndFeel(nullptr);
        }

        void paint(juce::Graphics& g) override
        {
            // Semi-transparent background with rounded corners
            g.setColour(theme.fixedBase().withAlpha(0.9f));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
            g.setColour(theme.cyan());
            g.drawRoundedRectangle(getLocalBounds().toFloat(), 6.0f, 2.0f);

            g.setColour(theme.cyan().withAlpha(0.72f));
            g.setFont(juce::Font(juce::FontOptions("Arial", 10.0f, juce::Font::plain)));
            const auto licensedUser = processor.getLicenseUserUI().trim();
            const auto licenseLine = licensedUser.isNotEmpty()
                                        ? ("licensed to " + licensedUser)
                                        : juce::String("licensed to customer@email.com");
            g.drawFittedText(licenseLine,
                             juce::Rectangle<int>(10, 187, getWidth() - 20, 12),
                             juce::Justification::centred,
                             1);
        }

        void resized() override
        {
            // Keep padding minimal so labels/buttons don't clip.
            auto area = getLocalBounds().reduced(6, 8);
            
            // Header row (Title + Channel)
            auto headerRow = area.removeFromTop(24);
            
            // Center the label + slider combo
            juce::Font f(juce::FontOptions("Arial", 14.0f, juce::Font::bold));
            juce::GlyphArrangement ga;
            ga.addLineOfText(f, titleLabel.getText(), 0, 0);
            int labelW = (int)std::ceil(ga.getBoundingBox(0, -1, true).getWidth()) + 5;
            int sliderW = 30;
            int totalW = labelW + sliderW;
            int startX = (headerRow.getWidth() - totalW) / 2;
            
            titleLabel.setBounds(headerRow.getX() + startX + 10, headerRow.getY(), labelW, headerRow.getHeight());
            // Requested: nudge the channel number box left.
            channelSlider.setBounds(titleLabel.getRight() - 5, headerRow.getY(), sliderW, headerRow.getHeight());
            
            // Keep the title/channel row fixed, but move the rest of the setup content up by 10px.
            area.removeFromTop(10);
            area = area.translated(0, -5);

            // 2 Columns (fixed widths in base coordinates)
            // Prefer a wider right column (it was getting clipped on the right).
            constexpr int kLeftColW  = 150;
            constexpr int kRightColW = 150;
            constexpr int kMinGapW   = 4;

            auto cols = area;
            const int availW = cols.getWidth();

            // If there isn't enough width for both target columns, shrink the left column first.
            constexpr int kMinLeftColW = 120;
            const int rightW = juce::jlimit(0, availW,
                                            juce::jmin(kRightColW, juce::jmax(0, availW - kMinGapW - kMinLeftColW)));
            const int leftW  = juce::jlimit(0, availW, juce::jmin(kLeftColW, juce::jmax(0, availW - kMinGapW - rightW)));

            auto rightCol = cols.removeFromRight(rightW);
            cols.removeFromRight(juce::jmin(kMinGapW, cols.getWidth()));
            auto leftCol  = cols.removeFromLeft(leftW);

            // Column nudges (requested): left column +5 relative to previous; right column -5 additional.
            leftCol = leftCol.translated(0, 0);
            rightCol = rightCol.translated(-10, 0);
            
            int h = 24;
            int gap = 4;
            
            // Left Column: Start, Stop, Trigger, Resync, Gated Sync
            startRow.setBounds(leftCol.removeFromTop(h));
            leftCol.removeFromTop(gap);
            stopRow.setBounds(leftCol.removeFromTop(h));
            leftCol.removeFromTop(gap);
            triggerRow.setBounds(leftCol.removeFromTop(h));
            leftCol.removeFromTop(gap);
            resyncRow.setBounds(leftCol.removeFromTop(h));
            leftCol.removeFromTop(gap);
            gatedSyncRow.setBounds(leftCol.removeFromTop(h));
            
            // Right Column: Offset, Shuffle, Clock Div, Auto Fill
            offsetRow.setBounds(rightCol.removeFromTop(h));
            rightCol.removeFromTop(gap);
            shuffleRow.setBounds(rightCol.removeFromTop(h));
            rightCol.removeFromTop(gap);
            clockDivRow.setBounds(rightCol.removeFromTop(h));
            rightCol.removeFromTop(gap);
            autoFillRow.setBounds(rightCol.removeFromTop(h));

            // REMOTE PORT combobox: under AUTOFILL
            rightCol.removeFromTop(6);
            // Match GATE numfield height (MidiRemoteRow value box is 20px tall)
            const int comboH = 20;
            remotePortBox.setBounds(rightCol.removeFromTop(comboH));
            // Trim 20px from the left side (gain space alignment as requested)
            {
                auto r = remotePortBox.getBounds();
                const int trimL = 20;
                const int newW = juce::jmax(1, r.getWidth() - trimL);
                remotePortBox.setBounds(r.getX() + trimL, r.getY(), newW, r.getHeight());
            }
            const auto labelLine = rightCol.removeFromTop(12);
            remotePortLabel.setBounds(labelLine);

            // Under the NOTE/CC switches:
            // - small up-pointing triangle (same line as REMOTE PORT label)
            // - NOTE/CC label below the triangle (next line)
            {
                const auto gateToggle = gatedSyncRow.getTypeToggleBounds()
                                             .translated(gatedSyncRow.getX(), gatedSyncRow.getY());
                if (! gateToggle.isEmpty())
                {
                    const int triW = 10;
                    const int triH = 6;
                    const int cx = gateToggle.getCentreX();
                    noteCcTriangle.setBounds(cx - triW / 2,
                                             labelLine.getY() + (labelLine.getHeight() - triH) / 2,
                                             triW,
                                             triH);
                    noteCcTriangle.setVisible(true);

                    juce::Font f(juce::FontOptions("Arial", 10.0f, juce::Font::bold));
                    juce::GlyphArrangement ga;
                    ga.addLineOfText(f, noteCcLabel.getText(), 0.0f, 0.0f);
                    const int textW = (int) std::ceil(ga.getBoundingBox(0, -1, true).getWidth());
                    const int labelW = juce::jlimit(24, 64, textW + 6);
                    noteCcLabel.setBounds(cx - labelW  - 6,
                                          labelLine.getY(),
                                          labelW,
                                          labelLine.getHeight());
                    noteCcLabel.setVisible(true);
                }
                else
                {
                    noteCcTriangle.setVisible(false);
                    noteCcLabel.setVisible(false);
                }
            }
        }

    private:
        struct RemotePortComboLNF final : public juce::LookAndFeel_V4
        {
            explicit RemotePortComboLNF(UiThemeColours& t) : theme(t)
            {
                setColour(juce::PopupMenu::backgroundColourId, theme.fixedBase());
                setColour(juce::PopupMenu::textColourId, theme.cyan());
                setColour(juce::PopupMenu::highlightedBackgroundColourId, theme.cyan());
                setColour(juce::PopupMenu::highlightedTextColourId, theme.fixedBase());
            }

            void positionComboBoxText (juce::ComboBox& box, juce::Label& labelToPosition) override
            {
                // Fill the full component width so JUCE's internal "button" area becomes 0px.
                labelToPosition.setBounds (box.getLocalBounds());
                labelToPosition.setFont (getComboBoxFont (box));
            }

            static juce::String middleEllipsize(const juce::String& s, const juce::Font& font, int maxWidth)
            {
                const auto stringWidthPx = [&font](const juce::String& str) -> int
                {
                    juce::GlyphArrangement ga;
                    ga.addLineOfText(font, str, 0.0f, 0.0f);
                    return (int) std::ceil(ga.getBoundingBox(0, -1, true).getWidth());
                };

                if (maxWidth <= 0)
                    return {};

                if (stringWidthPx(s) <= maxWidth)
                    return s;

                const juce::String dots("...");
                if (stringWidthPx(dots) >= maxWidth)
                    return dots;

                const int len = s.length();
                for (int keep = len; keep > 0; --keep)
                {
                    const int prefix = (keep + 1) / 2;
                    const int suffix = keep / 2;
                    const int suffixStart = juce::jmax(0, len - suffix);
                    const juce::String cand = s.substring(0, prefix) + dots + s.substring(suffixStart);
                    if (stringWidthPx(cand) <= maxWidth)
                        return cand;
                }

                return dots;
            }

            void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
            {
                auto bounds = juce::Rectangle<int>(0, 0, w, h);

                g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
                g.fillRoundedRectangle(bounds.toFloat(), 3.0f);

                g.setColour(box.findColour(juce::ComboBox::outlineColourId));
                g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.5f);

                const auto textBounds = bounds.reduced(4);
                const auto font = getComboBoxFont(box);
                const auto txt = middleEllipsize(box.getText(), font, textBounds.getWidth());

                g.setColour(theme.cyan());
                g.setFont(font);
                g.drawText(txt, textBounds, juce::Justification::centred, false);
            }

            juce::Font getComboBoxFont(juce::ComboBox&) override
            {
                return juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold));
            }

            juce::Font getPopupMenuFont() override
            {
                return juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold));
            }

            void drawPopupMenuBackground(juce::Graphics& g, int w, int h) override
            {
                g.fillAll(theme.fixedBase());
                g.setColour(theme.cyan());
                g.drawRect(juce::Rectangle<int>(0, 0, w, h));
            }

            void drawPopupMenuItem(juce::Graphics& g,
                                   const juce::Rectangle<int>& area,
                                   const bool isSeparator,
                                   const bool isActive,
                                   const bool isHighlighted,
                                   const bool isTicked,
                                   const bool /*hasSubMenu*/,
                                   const juce::String& text,
                                   const juce::String& /*shortcutKeyText*/,
                                   const juce::Drawable* /*icon*/,
                                   const juce::Colour* /*textColour*/) override
            {
                if (isSeparator)
                {
                    const int y = area.getCentreY();
                    g.setColour(theme.cyan().withAlpha(0.6f));
                    g.drawLine((float)area.getX() + 6.0f, (float)y, (float)area.getRight() - 6.0f, (float)y, 1.0f);
                    return;
                }

                auto r = area.reduced(2, 0);
                if (isHighlighted)
                {
                    g.setColour(theme.cyan());
                    g.fillRect(r);
                }

                const auto font = getPopupMenuFont();
                g.setFont(font);

                juce::Colour fg = isHighlighted ? theme.fixedBase() : theme.cyan();
                if (! isActive)
                    fg = fg.withAlpha(0.45f);
                g.setColour(fg);

                auto textArea = r.reduced(8, 0);

                // Optional tick indicator (kept minimal; only base/cyan).
                if (isTicked)
                {
                    const int box = 6;
                    const int cx = textArea.getX();
                    const int cy = textArea.getCentreY() - box / 2;
                    juce::Rectangle<int> tickBox(cx, cy, box, box);
                    g.drawRect(tickBox);
                    textArea.removeFromLeft(box + 6);
                }

                g.drawText(text, textArea, juce::Justification::centredLeft, true);
            }

            UiThemeColours& theme;
        };

        struct UpTriangle final : public juce::Component
        {
            explicit UpTriangle(UiThemeColours& t) : theme(t) {}

            void paint(juce::Graphics& g) override
            {
                auto b = getLocalBounds().toFloat();
                juce::Path p;
                p.startNewSubPath(b.getCentreX(), b.getY());
                p.lineTo(b.getRight(), b.getBottom());
                p.lineTo(b.getX(), b.getBottom());
                p.closeSubPath();
                g.setColour(theme.cyan().withAlpha(0.9f));
                g.fillPath(p);
            }

            UiThemeColours& theme;
        };

    public:
        void setRemoteDevices(const std::vector<juce::MidiDeviceInfo>& devices)
        {
            remoteDevices = devices;
            rebuildRemotePortList();
        }

    private:
        void rebuildRemotePortList()
        {
            remotePortBox.clear();

            // Always include Host/Track MIDI at index 0.

            int itemId = 1;
            remotePortBox.addItem("Host / Track MIDI", itemId++);
            for (const auto& d : remoteDevices)
                remotePortBox.addItem(d.name, itemId++);

            const auto wantId = processor.getMidiRemoteInDeviceId();
            if (wantId.isEmpty())
            {
                remotePortBox.setSelectedItemIndex(0, juce::dontSendNotification);
                return;
            }

            int selectIndex = 0;
            for (size_t i = 0; i < remoteDevices.size(); ++i)
            {
                if (remoteDevices[i].identifier == wantId)
                {
                    selectIndex = 1 + (int) i;
                    break;
                }
            }
            remotePortBox.setSelectedItemIndex(selectIndex, juce::dontSendNotification);
        }

        ClockSyncAudioProcessor& processor;
        UiThemeColours& theme;
        juce::Label titleLabel;
        std::atomic<int>& channelValue;
        juce::Slider channelSlider;
        MidiChannelLNF channelLnf;
        MidiRemoteRow startRow, stopRow, offsetRow, shuffleRow, clockDivRow, triggerRow, resyncRow, gatedSyncRow, autoFillRow;

        juce::ComboBox remotePortBox;
        juce::Label remotePortLabel;
        juce::Label noteCcLabel;
        UpTriangle noteCcTriangle { theme };
        std::vector<juce::MidiDeviceInfo> remoteDevices;
        std::unique_ptr<RemotePortComboLNF> remotePortLnf;
    };
}


class ClockEditorPaintLayer final : public juce::Component
{
public:
    enum class Kind { Backdrop, OverlayBg, HeaderBg, OverlayFg };

    ClockEditorPaintLayer(ClockSyncAudioProcessorEditor& e, Kind k) : editor(e), kind(k)
    {
        setInterceptsMouseClicks(false, false);
        setOpaque(false);
    }

    void paint(juce::Graphics& g) override
    {
        switch (kind)
        {
            case Kind::Backdrop:  editor.paintBackdropLayer(g); break;
            case Kind::OverlayBg: editor.paintOverlayBackgroundLayer(g); break;
            case Kind::HeaderBg:  editor.paintHeaderBackgroundLayer(g); break;
            case Kind::OverlayFg: editor.paintOverlayForegroundLayer(g); break;
        }
    }

private:
    ClockSyncAudioProcessorEditor& editor;
    Kind kind;
};

namespace
{
    static constexpr double kMinScaleFactor = 0.75;
    static constexpr int kMaxScaleFactor = 3;
}



ClockSyncAudioProcessorEditor::ClockSyncAudioProcessorEditor(ClockSyncAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p), 
      deviceBox(p.theme), nameBox(p.theme),
      clickButton(p.theme), idleClockToggle(p.theme), helpToggle(p.theme),
    colorPaletteToggle(p.theme)
{
    // Scalable editor: allow resizing but keep a fixed aspect ratio (no stretching).
    // Keep scaling, but remove the bottom-right corner resizer graphic.
    setResizable(true, false);
    setResizeLimits((int) std::round((double) kBaseW * kMinScaleFactor),
                    (int) std::round((double) kBaseH * kMinScaleFactor),
                    kBaseW * kMaxScaleFactor,
                    kBaseH * kMaxScaleFactor);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double) kBaseW / (double) kBaseH);

    const auto initialScale = 1.0;
    setSize((int) std::round((double) kBaseW * initialScale),
            (int) std::round((double) kBaseH * initialScale));
    startTimerHz(60);

    // Root container: all UI children are reparented into this so we can scale uniformly.
    addAndMakeVisible(uiRoot);
    uiRoot.setInterceptsMouseClicks(false, true);
    uiRoot.setBounds(0, 0, kBaseW, kBaseH);

    // Scalable paint layers (inside uiRoot)
    backdropLayer  = std::make_unique<ClockEditorPaintLayer>(*this, ClockEditorPaintLayer::Kind::Backdrop);
    overlayBgLayer = std::make_unique<ClockEditorPaintLayer>(*this, ClockEditorPaintLayer::Kind::OverlayBg);
    headerBgLayer  = std::make_unique<ClockEditorPaintLayer>(*this, ClockEditorPaintLayer::Kind::HeaderBg);
    overlayFgLayer = std::make_unique<ClockEditorPaintLayer>(*this, ClockEditorPaintLayer::Kind::OverlayFg);
    uiRoot.addAndMakeVisible(*backdropLayer);
    uiRoot.addAndMakeVisible(*overlayBgLayer);
    uiRoot.addAndMakeVisible(*headerBgLayer);
    uiRoot.addAndMakeVisible(*overlayFgLayer);

    themeLNF = std::make_unique<ThemeLNF>(processor.theme);
    syncLatchLNF = std::make_unique<SyncLatchLNF>(processor.theme);

    // Assign theme LNF
    nameBox.setLookAndFeel(themeLNF.get());
    nameMidiSwitch.setLookAndFeel(themeLNF.get());
    setupButton.setLookAndFeel(themeLNF.get());
    idleModeButton.setLookAndFeel(themeLNF.get());
    legacyModernButton.setLookAndFeel(themeLNF.get());
    sppButton.setLookAndFeel(themeLNF.get());
    refreshButton.setLookAndFeel(themeLNF.get());
    // Enable toggle state
    nameMidiSwitch.setClickingTogglesState(true);
    setupButton.setClickingTogglesState(true);
    idleModeButton.setClickingTogglesState(true);
    legacyModernButton.setClickingTogglesState(true);
    sppButton.setClickingTogglesState(true);

    // APVTS alias
    auto& apvts = processor.getAPVTS();
    if (auto* rp = apvts.getRawParameterValue(ClockSyncAudioProcessor::paramRun))
        runParamCached = rp->load() > 0.5f;
    rateParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(ClockSyncAudioProcessor::paramClockRateIndex));
    if (rateParam) rateIndexCached = rateParam->getIndex();

    // Load persisted UI-only flags from APVTS state (if present). These are
    // non-parameter values we store in the same ValueTree so the editor
    // appearance (pattern-edit, name/midi mode, selected instrument, submenu)
    // is restored when a project is reloaded.
    {
        auto& st = apvts.state;
        // NAME/MIDI toggle (default: current widget state)
        if (st.hasProperty("ui.showNameMode"))
            nameMidiSwitch.setToggleState((bool) st.getProperty("ui.showNameMode"), juce::dontSendNotification);
        // Pattern edit mode (default: false)
        if (st.hasProperty("ui.patternEditMode"))
        {
            patternEditMode = (bool) st.getProperty("ui.patternEditMode");
            if (playgroundComp) playgroundComp->setPatternEditButtonState(patternEditMode);
            if (playgroundComp) playgroundComp->setInterceptsMouseClicks(!patternEditMode, !patternEditMode);
        }
        // Setup submenu open state (default: false)
        if (st.hasProperty("ui.setupSubmenuOn"))
        {
            setupSubmenuTargetOn = (bool) st.getProperty("ui.setupSubmenuOn");
            setupButton.setToggleState(setupSubmenuTargetOn, juce::dontSendNotification);
            setupSubmenuProgress = setupSubmenuTargetOn ? 1.0f : 0.0f;
            setupSubmenuAnimatingHide = ! setupSubmenuTargetOn;
        }
        // Load instrument names early so populateNameBox can restore selection
        loadInstrumentNamesFromState();
    }



    // Create playground component (owns popups & ring visuals)
    playgroundComp = std::make_unique<PlaygroundComponent>(processor.theme);
    addAndMakeVisible(*playgroundComp);
    playgroundComp->setBounds(0, 0, getWidth(), getHeight());
    playgroundComp->setHeaderHeight(30);
    playgroundComp->toBack();

    // Ensure playground reflects any persisted pattern-edit state loaded earlier
    if (playgroundComp)
    {
        playgroundComp->setPatternEditButtonState(patternEditMode);
        playgroundComp->setInterceptsMouseClicks(!patternEditMode, !patternEditMode);
    }

    // Help toggle ("?") - restore visibility and behaviour so users can enable/disable tooltips.
    // Wire directly to the playground's hover text API rather than a separate applyTooltips helper.
    addAndMakeVisible(helpToggle);
    addAndMakeVisible(colorPaletteToggle);
    colorPaletteToggle.onColorChanged = [this] { 
        sendLookAndFeelChange(); 
        updateSetupButtonImages();
        if (svgDancer) svgDancer->setTint(processor.theme.accent().withAlpha(1.0f));
        
        // Update manual colors
        if (pulseWidthSlider) {
            pulseWidthSlider->setColour(juce::Slider::textBoxTextColourId, processor.theme.cyan());
            pulseWidthSlider->setColour(juce::Slider::thumbColourId, processor.theme.cyan());
            pulseWidthSlider->setColour(juce::Slider::trackColourId, processor.theme.cyan().darker(0.5f));
        }
        if (pulseWidthValueLabel) {
            pulseWidthValueLabel->setColour(juce::Label::textColourId, processor.theme.cyan());
        }
        deviceBox.setColour(FullWidthComboBox::overlayTextColourId, processor.theme.cyan());
        nameBox.setColour(FullWidthComboBox::overlayTextColourId, processor.theme.cyan());

        if (playgroundComp) playgroundComp->repaint();
        repaint(); 
    };
    // Register callback for state restoration
    processor.onThemeChanged = colorPaletteToggle.onColorChanged;

    helpToggle.setClickingTogglesState(true);
    helpToggle.setColour(juce::TextButton::textColourOffId, processor.theme.cyan().darker(0.45f));
    helpToggle.setColour(juce::TextButton::textColourOnId, processor.theme.cyan());
    helpToggle.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    helpToggle.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    // Initialize toggle from playground state if available
    if (playgroundComp)
        helpToggle.setToggleState(playgroundComp->getHoverTextEnabled(), juce::dontSendNotification);
    helpToggle.onClick = [this]() {
        const bool on = helpToggle.getToggleState();
        if (playgroundComp) playgroundComp->setHoverTextEnabled(on);
        repaint(0, 217, getWidth(), 22);
    };

    // Restore selected instrument index if present in state
    {
        auto& st = processor.getAPVTS().state;
        int sel = (int) st.getProperty("ui.selectedInstrument", 0);
        if (sel > 0 && sel <= (int) instrumentNames.size())
            nameBox.setSelectedId(sel, juce::dontSendNotification);
    }

    // Pattern start fine-tune slider removed

    // Initialize SVG Dancer
    svgDancer = std::make_unique<SvgDancerComponent>();
    svgDancer->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(svgDancer.get());
    svgDancer->setTint(processor.theme.accent().withAlpha(1.0f));
    svgDancer->toBack(); // Ensure it's behind other controls

    // Status bar (LED + status string) – painted above playground
    statusBar = std::make_unique<StatusBarComponent>(processor.theme);
    addAndMakeVisible(*statusBar);
    // Initialize visible content (bounds set centrally in resized()).
    {
        juce::String st = idleModeButton.getToggleState() ? "IDLE" : "STOP";
        statusBar->setStatusText(st);
        statusBar->setLedLevel(ledLevel);
    }

    // Header widgets on top
   
    addAndMakeVisible(refreshButton);
    addAndMakeVisible(setupButton);
    addAndMakeVisible(idleModeButton);
    // Create attachment for idleModeButton
    idleClockAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getAPVTS(), ClockSyncAudioProcessor::paramClockWhileStopped, idleModeButton);
    addAndMakeVisible(legacyModernButton);
    addAndMakeVisible(sppButton);
    addAndMakeVisible(deviceBox);
    addAndMakeVisible(nameBox);
    addAndMakeVisible(nameMidiSwitch);
    
    // Sync Latch Button
    addAndMakeVisible(syncLatchButton);
    syncLatchButton.setClickingTogglesState(true);
    syncLatchButton.setButtonText("LATCH");
    syncLatchButton.setLookAndFeel(syncLatchLNF.get());
    
    // Initial colors (OFF = SYNC = Accent background, Base text)
    syncLatchButton.setColour(juce::TextButton::buttonColourId, processor.theme.cyan().withAlpha(0.7f));
    syncLatchButton.setColour(juce::TextButton::textColourOffId, processor.theme.accent().darker(0.3f));
    
    // ON colors (ON = LATCH = Cyan background, Base text)
    syncLatchButton.setColour(juce::TextButton::buttonOnColourId, processor.theme.cyan());
    syncLatchButton.setColour(juce::TextButton::textColourOnId, processor.theme.accent().darker(0.3f));

    syncLatchButton.onClick = [this] {
        bool on = syncLatchButton.getToggleState();
        syncLatchButton.setButtonText(on ? "LATCH" : "LATCH");
    };
    // Initial state update
    {
        bool on = false;
        if (auto* p = processor.getAPVTS().getRawParameterValue(ClockSyncAudioProcessor::paramSyncLatchEnabled))
            on = p->load() > 0.5f;
        syncLatchButton.setToggleState(on, juce::dontSendNotification);
        syncLatchButton.onClick(); // update text/color
    }
    syncLatchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.getAPVTS(), ClockSyncAudioProcessor::paramSyncLatchEnabled, syncLatchButton);


    // Header switch toggle (created once)
    headerSwitchToggle = std::make_unique<HeaderSwitchToggle>(processor.theme);
    addAndMakeVisible(*headerSwitchToggle);
    headerSwitchToggle->setSize(12, 20);
    headerSwitchToggle->setVisible(true);
    // Geometry set centrally in resized(); keep interaction flags here.
    headerSwitchToggle->setInterceptsMouseClicks(true, true);
    headerSwitchToggle->toFront(true);
    headerSwitchToggle->setAlwaysOnTop(true);
    // Toggle behaviour: adjust editor canvas height and arrow visibility
    headerSwitchToggle->onToggle = [this](bool on){
        canvasExpanded = on;

        // Keep visual affordances consistent
        if (arrowDown)
            arrowDown->setVisible((setupSubmenuTargetOn || setupSubmenuProgress > 0.0f) && on);

        // Show/hide canvas-heavy content when compact
        if (playgroundComp) playgroundComp->setVisible(on);
        if (svgDancer) svgDancer->setVisible(on);
        syncLatchButton.setVisible(on);
        idleClockToggle.setVisible(on);

        const bool submenuActive = (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f);
        const int effectiveBaseH = on ? kBaseH : (kHeaderBaseH + (submenuActive ? kCompactExtraH : 0));

        // Lock aspect ratio per-mode: expanded uses 300x240, compact uses 300x(30 [+25])
        if (auto* c = getConstrainer())
        {
            c->setFixedAspectRatio((double) kBaseW / (double) effectiveBaseH);
            // Update size limits per-mode so compact doesn't force a super-wide window.
            c->setSizeLimits(kBaseW,
                             effectiveBaseH,
                             kBaseW * kMaxScaleFactor,
                             effectiveBaseH * kMaxScaleFactor);
        }

        // Preserve the current width; compute height from ratio so the host doesn't stretch
        const double scaleFromWidth = (double) getWidth() / (double) kBaseW;
        const int targetH = (int) std::round((double) effectiveBaseH * scaleFromWidth);
        setSize(getWidth(), juce::jmax(1, targetH));

        resized();
        repaint();
    };
    // Header visibility handled centrally via updateHeaderVisibility()
    // Ensure pulseWidthValueLabel is always constructed before use
    if (!pulseWidthValueLabel)
    {
        pulseWidthValueLabel = std::make_unique<juce::Label>();
        pulseWidthValueLabel->setFont(juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold)));
        pulseWidthValueLabel->setColour(juce::Label::textColourId, processor.theme.cyan());
        pulseWidthValueLabel->setJustificationType(juce::Justification::centredLeft);
    }
    addAndMakeVisible(*pulseWidthValueLabel);

    // Ensure pulseWidthSlider is always constructed before use
    if (!pulseWidthSlider)
    {
        pulseWidthSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        pulseWidthSlider->setRange(1, 20, 1);
        pulseWidthSlider->setTextValueSuffix(" ms");
        pulseWidthSlider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        pulseWidthSlider->setColour(juce::Slider::textBoxTextColourId, processor.theme.cyan());
        pulseWidthSlider->setColour(juce::Slider::thumbColourId, processor.theme.cyan());
        pulseWidthSlider->setColour(juce::Slider::trackColourId, processor.theme.cyan().darker(0.5f));
        pulseWidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.getAPVTS(), "pulseWidthMs", *pulseWidthSlider);
        addAndMakeVisible(*pulseWidthSlider);
        pulseWidthSlider->setVisible(false);
    }

    // Ensure arrowDown is always constructed before use
    if (!arrowDown)
    {
        arrowDown = std::make_unique<ArrowDownComponent>(processor.theme);
        addAndMakeVisible(*arrowDown);
        arrowDown->setVisible(false);
    }
    refreshButton.toFront(true);
    deviceBox.toFront(true);
    nameBox.toFront(true);
    nameMidiSwitch.toFront(true);
    statusBar->toFront(true);

    // Initialise pattern ring visual state from persisted parameter
        if (auto* psi = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(ClockSyncAudioProcessor::paramPatternSteps)))
        {
            int v = psi->get();
            patternParamCached = v;
            // Always update UI after parameter load
            if (playgroundComp) playgroundComp->setPatternBitmask((uint16_t) v);
            else pattern.setBitmask((uint16_t) v);
        }
        // (Parameter listener code removed: handled elsewhere)

    // --- Wire PlaygroundComponent callbacks to APVTS ---
    if (playgroundComp)
    {
        playgroundComp->onResyncStepRequested = [this](int step){
            if (auto* pParam = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep))
            {
                const auto& range = pParam->getNormalisableRange();
                pParam->beginChangeGesture();
                pParam->setValueNotifyingHost(range.convertTo0to1((float) juce::jlimit(1, 16, step)));
                pParam->endChangeGesture();
            }
            processor.notifyResyncOffsetChanged();
        };
        playgroundComp->onRunToggleRequested = [this](bool on){
            if (auto* pParam = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramRun))
            {
                pParam->beginChangeGesture();
                pParam->setValueNotifyingHost(on ? 1.0f : 0.0f);
                pParam->endChangeGesture();
            }
            // Schedule a resync immediately when Run is toggled ON at the currently selected OFFSET step
            if (on && playgroundComp && playgroundComp->onResyncStepRequested)
            {
                int offsetStep = 1;
                if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep)))
                    offsetStep = juce::jlimit(1, 16, pi->get());
                playgroundComp->onResyncStepRequested(offsetStep);
            }
        };
        playgroundComp->onTriggerOnceRequested = [this](){
            ledPulseTarget = 1.0f; ledAnimator.start(); processor.requestTriggerOnce();
            manualTriggerOffsetActive = true;
            manualTriggerRelativeStepAtTrigger = juce::jlimit(1,16, relativeStepCached);
            manualTriggerPlayheadDelta = (selectedResyncStepCached > 0)
                ? (selectedResyncStepCached - manualTriggerRelativeStepAtTrigger + 16) % 16 : 0;
            if (playgroundComp) playgroundComp->flashSegmentLogical(manualTriggerRelativeStepAtTrigger);
            // Shift dancer phase by one 16th (3 frames) on manual trigger
            dancerFrameOffset = (dancerFrameOffset + 3) % 24;
            repaint(ringArea);
        };
        // Automatic pattern playback should NOT call into `processor.requestTriggerOnce()`
        // which would arm processor state. Map auto playback to a no-op/visual-only
        // callback so the editor handles preview without mutating processor flags.
        playgroundComp->onAutoTriggerRequested = [this]() {
            // Visual preview only: flash the logical segment but do not arm processor.
            int step = juce::jlimit(1,16, (int) (playgroundComp ? playgroundComp->getPatternBitmask() : 1));
            // flashSegmentLogical expects 1..16 logical step; caller already triggers visual elsewhere
            // Keep this intentionally minimal to avoid side-effects.
        };
        playgroundComp->onClockRateIndexRequested = [this](int val){
            int idx = 1; if (val == 32) idx = 0; else if (val == 16) idx = 1; else if (val == 8) idx = 2; else if (val == 4) idx = 3;
            if (auto* pParam = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramClockRateIndex))
                pParam->setValueNotifyingHost(pParam->getNormalisableRange().convertTo0to1((float) idx));
            // Persist main circle 6 / clock-rate UI choice so it is restored on reload
            processor.getAPVTS().state.setProperty("ui.mainCircle6Value", val, nullptr);
        };
        playgroundComp->onShuffleStepRequested = [this](int v){
            if (auto* pParam = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramShuffleStep))
            {
                const auto& range = pParam->getNormalisableRange();
                pParam->beginChangeGesture();
                pParam->setValueNotifyingHost(range.convertTo0to1((float) juce::jlimit(1,7,v)));
                pParam->endChangeGesture();
                // Remember last selected shuffle (visual) so we can restore it
                processor.getAPVTS().state.setProperty("ui.selectedShuffle", v, nullptr);
            }
        };
        playgroundComp->onClockWhileStoppedRequested = [this](bool on){
            if (auto* pParam = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramClockWhileStopped))
            {
                pParam->beginChangeGesture();
                pParam->setValueNotifyingHost(on ? 1.0f : 0.0f);
                pParam->endChangeGesture();
            }
        };
        playgroundComp->onClickRateRequested = [this](int v){
            if (auto* pParam = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramClickRate))
            {
                pParam->beginChangeGesture();
                pParam->setValueNotifyingHost(pParam->getNormalisableRange().convertTo0to1((float) juce::jlimit(0,4,v)));
                pParam->endChangeGesture();
            }
        };
        playgroundComp->onClickPulseRequested = [this](bool on){
            if (auto* pParam = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramClickPulse))
            {
                pParam->beginChangeGesture(); pParam->setValueNotifyingHost(on ? 1.0f : 0.0f); pParam->endChangeGesture();
            }
        };
        playgroundComp->onTriggerModeRequested = [this](bool on){
            if (auto* pParam = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramTriggerModeEnabled))
            {
                pParam->beginChangeGesture(); pParam->setValueNotifyingHost(on ? 1.0f : 0.0f); pParam->endChangeGesture();
            }
        };
        playgroundComp->onPopup3Selected = [this](int idx){ updatePatternParamFromPopup3(idx); };
        playgroundComp->onPatternEditToggled = [this](bool on){
            patternEditMode = on;
            if (playgroundComp) playgroundComp->setPatternEditButtonState(on);
            if (on)
            {
                if (auto* psi = dynamic_cast<juce::AudioParameterInt*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramPatternSteps))) {
                    uint16_t m = (uint16_t) juce::jlimit(0, 65535, psi->get());
                    if (playgroundComp) playgroundComp->setPatternBitmask(m);
                    else pattern.setBitmask(m);
                }
            }
            // Persist editor-only pattern edit flag
            processor.getAPVTS().state.setProperty("ui.patternEditMode", on, nullptr);
            repaint(ringArea);
        };
        playgroundComp->onPatternChanged = [this](uint16_t m){
            patternParamCached = m;
            pushPatternStateToProcessor();
        };
        // Initialise playground visual state
        playgroundComp->setRunState(runParamCached);
        if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep)))
        {
            const int offsetStep = juce::jlimit(1, 16, pi->get());
            playgroundComp->setResyncStepSelected(offsetStep);
            // Schedule a resync at the next bar using the selected OFFSET step
            if (playgroundComp->onResyncStepRequested)
                playgroundComp->onResyncStepRequested(offsetStep);
        }
        int displayed = 16; switch (rateIndexCached){ case 0: displayed = 32; break; case 1: displayed = 16; break; case 2: displayed = 8; break; case 3: displayed = 4; break; default: break; }
        playgroundComp->setClockRateIndexValue(displayed);
        if (auto* cp = apvts.getRawParameterValue(ClockSyncAudioProcessor::paramClickPulse))
            playgroundComp->setClickPulseState(cp->load() > 0.5f);
        if (auto* tm = apvts.getRawParameterValue(ClockSyncAudioProcessor::paramTriggerModeEnabled))
            playgroundComp->setTriggerModeState(tm->load() > 0.5f);

        // Restore popup3 (autofill interval) visual label from the parameter
        if (auto* pchoice = dynamic_cast<juce::AudioParameterChoice*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramPatternBars)))
        {
            // paramPatternBars maps: 0=OFF,1=1,2=2,3=4,4=8,5=16,6=32,7=64,8=RND
            // Playground popup ordering is 64(0),32(1),16(2),8(3),4(4),2(5),1(6),OFF(7)
            int paramIdx = pchoice->getIndex();
            int popupIndex = 7;
            switch (paramIdx)
            {
                case 7: popupIndex = 0; break;
                case 6: popupIndex = 1; break;
                case 5: popupIndex = 2; break;
                case 4: popupIndex = 3; break;
                case 3: popupIndex = 4; break;
                case 2: popupIndex = 5; break;
                case 1: popupIndex = 6; break;
                case 0:
                case 8:
                default: popupIndex = 7; break;
            }
            playgroundComp->setPopup3Index(popupIndex);
            processor.getAPVTS().state.setProperty("ui.popup3Index", popupIndex, nullptr);
        }
        // Persist linear shuffle toggle and wire amount changes immediately
        playgroundComp->onLinearShuffleModeChanged = [this](bool on){
            processor.getAPVTS().state.setProperty("ui.linearShuffleMode", on, nullptr);
        };
        playgroundComp->onLinearShuffleAmountChanged = [this](float amount) {
            // Map 0.5..0.75 to 0..1 for param `shuffleLinear`
            float t01 = juce::jlimit(0.0f, 1.0f, (amount - 0.5f) / 0.25f);
            if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramShuffleLinear)) {
                p->beginChangeGesture();
                p->setValueNotifyingHost(t01);
                p->endChangeGesture();
            }
        };
        // Restore shuffle visual selection from parameter (if present)
        if (auto* sh = dynamic_cast<juce::AudioParameterInt*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramShuffleStep)))
        {
            playgroundComp->setSelectedShuffle(sh->get());
            processor.getAPVTS().state.setProperty("ui.selectedShuffle", sh->get(), nullptr);
        }
        // Restore linear shuffle UI-only toggle (persisted in state)
        {
            juce::var v = processor.getAPVTS().state.getProperty("ui.linearShuffleMode", juce::var(false));
            if (v.isBool()) playgroundComp->setLinearShuffleModeState((bool) v);
        }
        // Restore main circle6 value (click / extra6) if saved
        {
            juce::var v = processor.getAPVTS().state.getProperty("ui.mainCircle6Value", juce::var());
            if (! v.isVoid() && v.isDouble()) playgroundComp->setMainCircle6Value((int) v);
        }
    }
    // --- Pulse Width Slider (1–20 ms, no label, visible in setup submenu) ---
    // Pulse Width Slider (1–20 ms, no label, visible in setup submenu)
    if (!pulseWidthSlider)
    {
        pulseWidthSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        pulseWidthSlider->setRange(1, 20, 1);
        pulseWidthSlider->setTextValueSuffix(" ms");
        pulseWidthSlider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        pulseWidthSlider->setColour(juce::Slider::textBoxTextColourId, processor.theme.cyan());
        pulseWidthSlider->setColour(juce::Slider::thumbColourId, processor.theme.cyan());
        pulseWidthSlider->setColour(juce::Slider::trackColourId, processor.theme.cyan().darker(0.5f));
        pulseWidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.getAPVTS(), "pulseWidthMs", *pulseWidthSlider);
        addAndMakeVisible(*pulseWidthSlider);
        pulseWidthSlider->setVisible(false);
    }
    // Device list
    refreshButton.onClick = [this]{ refreshDeviceList(); };
    deviceBox.setLookAndFeel(themeLNF.get());
    deviceBox.setJustificationType(juce::Justification::centred);
    deviceBox.setColour(juce::ComboBox::arrowColourId, juce::Colours::transparentBlack);
    deviceBox.setEditableText(false);
    // Ensure default ComboBox label doesn't draw; use FullWidthComboBox overlay colour instead
    deviceBox.setColour(juce::ComboBox::textColourId, juce::Colours::transparentBlack);
    deviceBox.setColour(FullWidthComboBox::overlayTextColourId, processor.theme.cyan());
    deviceBox.setTextWhenNothingSelected("select midi-out...");

    // Move refreshButton to the right of deviceBox
    // const int deviceBoxW = 140;
    // const int refreshW = 28;
    // const int deviceBoxH = 24;
    // const int deviceBoxY = 36;
    // const int deviceBoxX = 16;
    // deviceBox.setBounds(deviceBoxX, deviceBoxY, deviceBoxW, deviceBoxH);
    // refreshButton.setBounds(deviceBoxX + deviceBoxW + 4, deviceBoxY, refreshW, deviceBoxH);

    nameBox.setJustificationType(juce::Justification::centred);
    nameBox.setEditableText(false);
    nameBox.setColour(juce::ComboBox::arrowColourId, juce::Colours::transparentBlack);
    // Ensure default ComboBox label doesn't draw; use FullWidthComboBox overlay colour instead
    nameBox.setColour(juce::ComboBox::textColourId, juce::Colours::transparentBlack);
    nameBox.setColour(FullWidthComboBox::overlayTextColourId, processor.theme.cyan());
    // Defer combo/header/submenu visibility to centralized helpers
    // NAME/MIDI toggle initial appearance
    {
        const bool showName = nameMidiSwitch.getToggleState();
        nameMidiSwitch.setButtonText(showName ? "MIDI" : "NAME");
        auto txtCol = (showName ? processor.theme.accent() : processor.theme.cyan());
        nameMidiSwitch.setColour(juce::TextButton::textColourOffId, txtCol);
        nameMidiSwitch.setColour(juce::TextButton::textColourOnId,  txtCol);
    }
    if (nameMidiSwitch.getToggleState()) { populateNameBox(); }
    // Persist NAME/MIDI toggle into APVTS state so it survives project save/load
    nameMidiSwitch.onClick = [this]{
        // If submenu is open, close it by toggling setupButton and setting animation flags
        // if (setupButton.getToggleState())
        // {   
            // Linear shuffle wiring done during initialisation; no header/NAME toggle dependency
        //     setupSubmenuAnimatingHide = true;
        //     setupButton.setToggleState(false, juce::sendNotification);
        //     //  updateSetupSubmenuLayout(); repaint(); return;
        //     if (setupAnimator)
        //         setupAnimator->start();
               
                // Sync playground handle from parameter value (once here too in case of user toggling NAME/MIDI)
                if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramShuffleLinear)) {
                    float t01 = p->getValue();
                    float amount = 0.5f + 0.25f * juce::jlimit(0.0f, 1.0f, t01);
                    if (playgroundComp)
                        playgroundComp->setLinearShuffleModeState((bool) processor.getAPVTS().state.getProperty("ui.linearShuffleMode", false));
                    if (playgroundComp->onLinearShuffleAmountChanged)
                        playgroundComp->onLinearShuffleAmountChanged(amount);
                }
        // }
        const bool showName = nameMidiSwitch.getToggleState();
        nameMidiSwitch.setButtonText(showName ? "MIDI" : "NAME");
        auto txtCol = (showName ? processor.theme.accent() : processor.theme.cyan());
        nameMidiSwitch.setColour(juce::TextButton::textColourOffId, txtCol);
        nameMidiSwitch.setColour(juce::TextButton::textColourOnId,  txtCol);
        // Centralized header visibility update
        updateHeaderVisibility();
        // setupButton and refreshButton are always visible, do not change their visibility here
        if (showName) { loadInstrumentNamesFromState(); populateNameBox(); nameBox.toFront(true); }
        else { deviceBox.toFront(true); }
        if (playgroundComp) playgroundComp->setHeaderHeight(30);
        resized(); repaint(0,0,getWidth(), 34);
        processor.getAPVTS().state.setProperty("ui.showNameMode", showName, nullptr);
    };
        if (auto* sh = dynamic_cast<juce::AudioParameterInt*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramShuffleStep)))

    // Setup submenu animator
    setupAnimator = std::make_unique<juce::Animator>(juce::ValueAnimatorBuilder{}
        .withDurationMs(166.0f)
        .withValueChangedCallback([this](float progress){
            const float p = juce::jlimit(0.0f, 1.0f, progress);
            setupSubmenuProgress = setupSubmenuAnimatingHide ? (1.0f - p) : p;
            updateSetupSubmenuLayout();
            const bool active = (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f);
            idleModeButton.setVisible(active);
            legacyModernButton.setVisible(active);
            sppButton.setVisible(active);
            pulseWidthSlider->setVisible(active);
            if (pulseWidthValueLabel) pulseWidthValueLabel->setVisible(active);
            idleModeButton.setInterceptsMouseClicks(active, active);
            legacyModernButton.setInterceptsMouseClicks(active, active);
            sppButton.setInterceptsMouseClicks(active, active);
            pulseWidthSlider->setInterceptsMouseClicks(active, active);
            if (playgroundComp) { playgroundComp->setExternalHoverBlocked(active); if (!active) playgroundComp->clearForcedHoverIndex(); }
            // Arrow visibility depends on header toggle: hide arrowDown when headerSwitchToggle is OFF
            if (arrowDown) arrowDown->setVisible(active && (!headerSwitchToggle || headerSwitchToggle->getToggleState()));
            repaint();
        }).build());
    vblankUpdater = std::make_unique<juce::VBlankAnimatorUpdater>(this);
    vblankUpdater->addAnimator(*setupAnimator);
    vblankUpdater->addAnimator(ledAnimator);




    setupButton.onClick = [this]{
        setupSubmenuTargetOn = setupButton.getToggleState();
        setupSubmenuAnimatingHide = ! setupSubmenuTargetOn;
        setupAnimator->start();
        // Persist submenu open state
        processor.getAPVTS().state.setProperty("ui.setupSubmenuOn", setupSubmenuTargetOn, nullptr);
        // canvas resizing is handled by the setupAnimator animation callback
    };
    auto configureSetupToggle = [this](juce::TextButton& b){
        b.setColour(juce::TextButton::textColourOffId, processor.theme.cyan());
        b.setColour(juce::TextButton::textColourOnId, processor.theme.cyan());
    };
    configureSetupToggle(idleModeButton); configureSetupToggle(legacyModernButton); configureSetupToggle(sppButton);
    // Restore legacy mode from state (default true -> Legacy)
    {
        bool legacy = true;
        if (processor.getAPVTS().state.hasProperty("ui.legacyMode"))
            legacy = (bool) processor.getAPVTS().state.getProperty("ui.legacyMode");
        // Button state is inverse of legacy (True=Modern, False=Legacy)
        legacyModernButton.setToggleState(!legacy, juce::dontSendNotification);
        processor.setLegacyMode(legacy);
    }
    legacyModernButton.setButtonText(legacyModernButton.getToggleState() ? "MODERN" : "LEGACY");
    
    // Restore SPP mode
    {
        bool spp = (bool) processor.getAPVTS().state.getProperty("ui.sppMode", false);
        sppButton.setToggleState(spp, juce::dontSendNotification);
        sppButton.setButtonText(spp ? "S.P.P. ON" : "S.P.P. OFF");
    }

    idleModeButton.onClick = [this]{ const bool on = idleModeButton.getToggleState(); idleModeButton.setButtonText(on ? "IDLE ON" : "IDLE OFF"); };
    legacyModernButton.onClick = [this]{ const bool on = legacyModernButton.getToggleState(); legacyModernButton.setButtonText(on ? "MODERN" : "LEGACY"); processor.setLegacyMode(!on); };
    sppButton.onClick = [this]{ 
        const bool on = sppButton.getToggleState(); 
        sppButton.setButtonText(on ? "S.P.P. ON" : "S.P.P. OFF"); 
        processor.getAPVTS().state.setProperty("ui.sppMode", on, nullptr);
    };
    nameBox.onEditCurrentRequest = [this]
    {
        toggleNameEditorOrCommit();
    };

    nameBox.onChange = [this]
    {
        const int id = nameBox.getSelectedId();

        if (id >= 1)
            processor.getAPVTS().state.setProperty("ui.selectedInstrument", id, nullptr);
    };

    refreshDeviceList();

    // When the user selects an item from the device combo, map selection id -> device identifier
    // Selection id mapping: 1 => no device selected (placeholder), 2.. => midiOutputs[selId-2]
    deviceBox.onChange = [this]() {
        const int selId = deviceBox.getSelectedId();
        if (selId <= 1)
        {
            processor.setExternalDeviceId({});
        }
        else if (selId - 2 >= 0 && selId - 2 < (int) midiOutputs.size())
        {
            processor.setExternalDeviceId(midiOutputs[(size_t) (selId - 2)].identifier);
        }
        // Schedule a resync when a MIDI port is selected
        if (playgroundComp && playgroundComp->onResyncStepRequested)
            playgroundComp->onResyncStepRequested(1); // step 1 = bar start
    };

    // Backdrop animators (decorative circles)
    for (size_t i = 0; i < backdropAnimators.size(); ++i)
    {
        backdropAnimators[i] = std::make_unique<juce::Animator>(juce::ValueAnimatorBuilder{}
            .withDurationMs((float) UiLayout::kBackdropPulseDurationMs)
            .withValueChangedCallback([this,i](float progress){ backdropPulseProgress[i] = 1.0f - juce::jlimit(0.0f,1.0f,progress); repaint(); })
            .build());
        vblankUpdater->addAnimator(*backdropAnimators[i]);
    }

    // loadDancerFrames(); // Removed

    // Ensure submenu controls are laid out and visibility is correct after construction
    updateSetupSubmenuLayout();

    // Create bottom-right setup corner button with SVG icon (setup.svg).
    // For now it's just clickable (no wiring yet).
    if (! setupCornerButton)
    {
        // Use ImageFitted so JUCE does not paint a default grey button background
        setupCornerButton = std::make_unique<juce::DrawableButton>("setupCorner", juce::DrawableButton::ImageFitted);
        addAndMakeVisible(*setupCornerButton);
        setupCornerButton->setInterceptsMouseClicks(true, true);
        setupCornerButton->setAlwaysOnTop(true);

        auto loadSvgDrawable = []() -> std::unique_ptr<juce::Drawable>
        {
            // Load embedded setup.svg from BinaryData to avoid host-dependent file paths
            if (auto d = juce::Drawable::createFromImageData(BinaryData::setup_svg, BinaryData::setup_svgSize))
                return d;
            return nullptr;
        };

        setupCornerDrawable = loadSvgDrawable();
        setupSvgLoaded = (setupCornerDrawable != nullptr);
        
        // Use the helper to generate tinted images
        updateSetupButtonImages();

        if (setupCornerDrawable)
        {
            setupCornerButton->setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
            setupCornerButton->setColour(juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentBlack);
            setupCornerButton->setOpaque(false);
            setupCornerButton->setWantsKeyboardFocus(false);
            setupCornerButton->setColour(juce::DrawableButton::textColourId, juce::Colours::transparentBlack);
            // Make it toggle visually (no behavior wiring yet)
            setupCornerButton->setClickingTogglesState(true);
            // Draw icon half-sized within the button bounds
            setupCornerButton->setEdgeIndent(7);
        }
        // Independent toggle button: open/close a canvas overlay (not linked to header submenu)
        setupCornerButton->onClick = [this]() {
            if (! setupCornerButton) return;
            setupOverlayVisible = setupCornerButton->getToggleState();
            
            // Hide help toggle when overlay is open
            if (helpToggle.isVisible() != !setupOverlayVisible)
                helpToggle.setVisible(!setupOverlayVisible);

            // Create/remove a transparent component to consume mouse inside overlay bounds
            if (setupOverlayVisible)
            {
                if (! setupOverlayComp)
                {
                    setupOverlayComp = std::make_unique<MidiRemoteSetupComponent>(processor, remoteMidiInputs);
                    setupOverlayComp->setInterceptsMouseClicks(true, true); // consume clicks
                    uiRoot.addAndMakeVisible(*setupOverlayComp);
                    setupOverlayComp->setAlwaysOnTop(true);
                }
                // Position overlay comp to match visual overlay rect
                const int overlayW = 295;
                const int overlayH = 205;
                const int headerH = 30;
                const int canvasH = juce::jmax(0, kBaseH - headerH);
                const int x = (kBaseW - overlayW) / 2;
                const int y = headerH + (canvasH - overlayH) / 2;
                setupOverlayComp->setBounds(x, y, overlayW, overlayH);
                setupOverlayComp->toFront(true);
                if (setupCornerButton) setupCornerButton->toFront(true);
            }
            else
            {
                setupOverlayComp.reset();
            }
            repaint();
        };
    }
    // Setup corner button placement handled in resized(); visibility centralized.
    if (setupCornerButton) setupCornerButton->toFront(true);

    // Reparent our UI children into uiRoot (keep the resizer corner component as a direct child).
    {
        std::vector<juce::Component*> children;
        children.reserve((size_t) getNumChildComponents());
        for (int i = 0; i < getNumChildComponents(); ++i)
            children.push_back(getChildComponent(i));

        for (auto* c : children)
        {
            if (c == nullptr) continue;
            if (c == &uiRoot) continue;
            if (dynamic_cast<juce::ResizableCornerComponent*>(c) != nullptr) continue;
            if (dynamic_cast<juce::ResizableBorderComponent*>(c) != nullptr) continue;
            uiRoot.addAndMakeVisible(*c);
        }

        // Ensure paint layers are ordered correctly:
        // Back -> Front: backdrop, playground, submenuBg, help/?/palette (behind), submenu controls,
        // headerBg (covers submenu under header), header controls, overlayFg, gear.
        if (overlayBgLayer && playgroundComp)
            playgroundComp->toBehind(overlayBgLayer.get());
        if (backdropLayer && playgroundComp)
            backdropLayer->toBehind(playgroundComp.get());
        if (headerBgLayer)
            headerBgLayer->toFront(false);
        if (overlayFgLayer)
            overlayFgLayer->toFront(false);

        // Things that should be covered by the submenu when it's out:
        if (overlayBgLayer)
        {
            helpToggle.toBehind(overlayBgLayer.get());
            colorPaletteToggle.toBehind(overlayBgLayer.get());
            syncLatchButton.toBehind(overlayBgLayer.get());
        }

        // Submenu controls must be above submenu background but behind the header background.
        if (headerBgLayer)
        {
            idleModeButton.toBehind(headerBgLayer.get());
            legacyModernButton.toBehind(headerBgLayer.get());
            sppButton.toBehind(headerBgLayer.get());
            if (pulseWidthSlider) pulseWidthSlider->toBehind(headerBgLayer.get());
            if (pulseWidthValueLabel) pulseWidthValueLabel->toBehind(headerBgLayer.get());
            if (arrowDown) arrowDown->toBehind(headerBgLayer.get());
        }

        // Header controls should be on top of the header background.
        refreshButton.toFront(true);
        deviceBox.toFront(true);
        nameBox.toFront(true);
        nameMidiSwitch.toFront(true);
        setupButton.toFront(true);
        if (statusBar) statusBar->toFront(true);
        if (headerSwitchToggle) headerSwitchToggle->toFront(true);
        if (setupCornerButton) setupCornerButton->toFront(true);
    }

    // Centralize initial visibility and layout.
    updateHeaderVisibility();
    resized();
    setVisible(true);

    if (! processor.isLicensedUI())
    {
        toolboy_license::LicenseDialog::Config dialogConfig;
        dialogConfig.title = "Register Clock v3";
        dialogConfig.overlayColour = processor.theme.fixedBase().withAlpha(0.92f);
        dialogConfig.panelColour = processor.theme.base().withAlpha(0.985f);
        dialogConfig.textColour = processor.theme.cyan();
        dialogConfig.accentColour = processor.theme.accent();

        toolboy_license::GumroadLicenseValidator::Config validatorConfig;
        validatorConfig.productId = kGumroadProductId;

        licenseDialog = std::make_unique<toolboy_license::LicenseDialog>(
            dialogConfig,
            toolboy_license::GumroadLicenseValidator::makeValidator(std::move(validatorConfig)),
            [this](const juce::String& licensedUser, const juce::String& providerKey)
            {
                return processor.saveLicenseUI(licensedUser, providerKey);
            },
            [this]()
            {
                repaint();
                if (setupOverlayComp)
                    setupOverlayComp->repaint();
            });

        addAndMakeVisible(*licenseDialog);
        licenseDialog->setBounds(getLocalBounds());
        licenseDialog->toFront(true);
    }
}

void ClockSyncAudioProcessorEditor::paint(juce::Graphics& g)
{
    // All custom painting is handled by scalable paint layers inside uiRoot.
    g.fillAll(processor.theme.accent().darker(0.9f));
}

// --- Unified HitRouting helpers ---
HitContext ClockSyncAudioProcessorEditor::buildHitContext() const
{
    HitContext hc; hc.playground = playgroundComp.get(); hc.pattern = const_cast<PatternRing*>(&pattern); hc.ringArea = ringArea; hc.patternEditMode = patternEditMode; hc.submenuActive = (nameMidiSwitch.getToggleState() && (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f)); hc.outerDiameter = kRingOuterD; hc.innerDiameter = kRingInnerD; return hc;
}

InteractionMode ClockSyncAudioProcessorEditor::getInteractionMode() const
{
    if (patternEditMode) return InteractionMode::PatternEdit;
    if (playgroundComp)
    {
        if (playgroundComp->isPopup3Active()) return InteractionMode::Popup3Open;
        if (playgroundComp->isPopup6Active()) return InteractionMode::Popup6Open;
    }
    if (nameMidiSwitch.getToggleState() && (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f)) return InteractionMode::SubmenuActive;
    return InteractionMode::Normal;
}

HitResult ClockSyncAudioProcessorEditor::routeHit(const juce::MouseEvent& e, bool isHover)
{
    auto hc = buildHitContext();
    auto mode = getInteractionMode();
    return performHitTest(e.position.toFloat(), hc, mode);
}

void ClockSyncAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    // Scalable overlay painting is handled by uiRoot layers.

   #if CLOCKV3_DEMO
    if (processor.isDemoExpiredUI())
    {
        auto bounds = getLocalBounds().reduced((int) std::round(16.0 * uiScale),
                                               (int) std::round(36.0 * uiScale));
        g.setColour(processor.theme.fixedBase().withAlpha(0.9f));
        g.fillRoundedRectangle(bounds.toFloat(), 10.0f * (float) uiScale);
        g.setColour(processor.theme.cyan().withAlpha(0.85f));
        g.drawRoundedRectangle(bounds.toFloat(), 10.0f * (float) uiScale, 1.5f * (float) uiScale);

        auto headline = bounds.removeFromTop((int) std::round(42.0 * uiScale));
        auto detail = bounds.reduced((int) std::round(12.0 * uiScale), (int) std::round(8.0 * uiScale));

        g.setColour(processor.theme.cyan());
        g.setFont(juce::Font(juce::FontOptions("Arial", 15.0f * (float) uiScale, juce::Font::bold)));
        g.drawFittedText("DEMO EXPIRED", headline, juce::Justification::centred, 1);

        g.setColour(processor.theme.cyan().withAlpha(0.72f));
        g.setFont(juce::Font(juce::FontOptions("Arial", 10.0f * (float) uiScale, juce::Font::plain)));
        g.drawFittedText("This demo runs for 30 minutes.\nOpen the full version for unrestricted runtime.",
                         detail,
                         juce::Justification::centred,
                         2);
    }
   #endif

    // Debug stack (left side): show last clicked stored step, logical mapping, host bar and scheduled targets
    // {
    //     const int leftX = 6;
    //     int y = 40;
    //     const int lineH = 14;
    //     g.setColour(UiThemeColours::cyan().withAlpha(0.95f));
    //     g.setFont(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::plain)));

    //     int clicked = lastClickedStoredIndex;
    //     const int visualRotation = 4;
    //     juce::String clickedLine;
    //     if (clicked >= 0)
    //     {
    //         int logical = ((clicked - visualRotation) & 15) + 1; // 1..16
    //         clickedLine = "Clicked stored: " + juce::String(clicked) + "  (logical " + juce::String(logical) + ")";
    //     }
    //     else clickedLine = "Clicked stored: N/A";
    //     g.drawFittedText(clickedLine, juce::Rectangle<int>(leftX, y, 200, lineH), juce::Justification::left, 1);
    //     y += lineH + 2;

    //     int hostBar = processor.getUiExternalBarNumber();
    //     juce::String hostLine = "Host bar: "; hostLine += (hostBar >= 0) ? juce::String(hostBar) : juce::String("N/A");
    //     g.drawFittedText(hostLine, juce::Rectangle<int>(leftX, y, 200, lineH), juce::Justification::left, 1);
    //     y += lineH + 2;

    //     long long rtBar = processor.getResyncTargetBar();
    //     int rtStep = processor.getResyncTargetStep();
    //     juce::String resyncLine = "Resync target: ";
    //     if (rtBar >= 0) resyncLine += "bar " + juce::String(rtBar) + " step " + juce::String(rtStep);
    //     else resyncLine += "none";
    //     g.drawFittedText(resyncLine, juce::Rectangle<int>(leftX, y, 240, lineH), juce::Justification::left, 1);
    //     y += lineH + 2;

    //     long long pendingBar = processor.getPendingPatternRestartTargetBar();
    //     juce::String pendingLine = "Pending pat restart bar: "; pendingLine += (pendingBar >= 0) ? juce::String(pendingBar) : juce::String("N/A");
    //     g.drawFittedText(pendingLine, juce::Rectangle<int>(leftX, y, 240, lineH), juce::Justification::left, 1);
    // }

}

void ClockSyncAudioProcessorEditor::resized()
{
    processor.getAPVTS().state.setProperty("ui.editorWidth", getWidth(), nullptr);
    processor.getAPVTS().state.setProperty("ui.editorScale",
                                           (double) getWidth() / (double) kBaseW,
                                           nullptr);

    // Compute uniform scale to fit the current editor size while keeping aspect ratio.
    const bool submenuActive = (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f);
    const int effectiveBaseH = canvasExpanded ? kBaseH : (kHeaderBaseH + (submenuActive ? kCompactExtraH : 0));

    const double sx = (double) getWidth()  / (double) kBaseW;
    const double sy = (double) getHeight() / (double) effectiveBaseH;
    uiScale = juce::jlimit(kMinScaleFactor, (double) kMaxScaleFactor, std::min(sx, sy));

    const int scaledW = (int) std::round((double) kBaseW * uiScale);
    const int scaledH = (int) std::round((double) effectiveBaseH * uiScale);
    uiOffset = { (getWidth() - scaledW) / 2, (getHeight() - scaledH) / 2 };

    uiRoot.setBounds(0, 0, kBaseW, kBaseH);
    uiRoot.setTransform(juce::AffineTransform::scale((float) uiScale));
    uiRoot.setTopLeftPosition(uiOffset);
    colorPaletteToggle.setPopupScale((float) uiScale);

    if (licenseDialog)
    {
        licenseDialog->setBounds(getLocalBounds());
        if (licenseDialog->isVisible())
            licenseDialog->toFront(true);
    }

    if (backdropLayer)  backdropLayer->setBounds(0, 0, kBaseW, kBaseH);
    if (overlayBgLayer) overlayBgLayer->setBounds(0, 0, kBaseW, kBaseH);
    if (headerBgLayer)  headerBgLayer->setBounds(0, 0, kBaseW, kBaseH);
    if (overlayFgLayer) overlayFgLayer->setBounds(0, 0, kBaseW, kBaseH);

    // Ensure shared playground component occupies the full base canvas.
    if (playgroundComp)
    {
        playgroundComp->setBounds(0, 0, kBaseW, kBaseH);
        playgroundComp->setHeaderHeight(30);
    }
    // Header area is painted, no components there (top 28px)

    {
        const bool showName = nameMidiSwitch.getToggleState();
        // Painted header height (base 30 + optional 20 for Setup row)
        const int headerH = 30; // fixed header height now
        const int marginX = 5;
        const int marginY = 3;
        // First-row content height is fixed (decoupled from Setup expansion)
        const int baseHeaderH = 30;
        const int contentH = baseHeaderH - marginY * 2; // stay constant at 24px
        const int gap = 4;
        const int buttonW = std::max(16, contentH - 6); // square-ish refresh button (width ~= height)
        const int toggleW = 14; // same width for name/midi toggle
        const int fullComboOriginal = kBaseW - marginX * 2 - buttonW - gap - toggleW - gap; // account for toggle + refresh
        int comboW = fullComboOriginal - 90; // keep prior shrink
        // Reduce all combo widths by 10px as requested, but keep a sensible minimum
        comboW = std::max(60, comboW - 10);
        const int centerX = kBaseW / 2;
        const int comboX = centerX - comboW / 2;
        // place refresh button immediately left of the combo
        refreshButton.setBounds(comboX + comboW -1, marginY + 5, toggleW, contentH - 10);
        nameMidiSwitch.setBounds(comboX - 44, marginY + 5, 40, contentH - 10);
        // place setupButton left of nameMidiSwitch (moved further left to avoid clipping)
        const int extraLeft = 17; // nudge left
        setupButton.setBounds(nameMidiSwitch.getX() - (toggleW + 4), marginY + 5, toggleW, contentH - 10);
        // comboboxes share same bounds; only one visible at a time
        deviceBox.setBounds(comboX, marginY + 1 , comboW, contentH - 2);
        nameBox.setBounds(comboX, marginY + 1, comboW, contentH - 2);
        // Centralize header visibility each layout pass
        updateHeaderVisibility();

        // Setup submenu layout now handled in paintOverChildren via animation; initial hidden position set here.
        updateSetupSubmenuLayout();
        
        // Combo remains interactive (double-click to edit).
        // make refresh button small single-letter
        refreshButton.setButtonText("R");
        setupButton.setButtonText("S");
    }

    updateInlineNameEditorBounds();

    // Position imported components exactly as authored in LayoutPlayground (use canonical baseCircles)
    if (playgroundComp && playgroundComp->getCircleCount() >= 9)
    {
        // Use canonical positions from Main.cpp (baseCircles). Do NOT apply
        // any vertical shift; the header is a Z-overlay.

        // idx7 (click rate rotary + inner click-to-pulse button) is fully handled
        // by the PlaygroundComponent. Keep legacy slider/button hidden and do not
        // lay them out when the playground is active.

        // circle 0 -> ring area
        {
            float x0,y0,r0; if (playgroundComp->getCircleInfo(0,x0,y0,r0))
            {
                const int d = (int) std::round(r0 * 2.0f);
                ringAreaBase = juce::Rectangle<int>((int)std::round(x0 - r0), (int)std::round(y0 - r0), d, d);
            }
        }
        if (svgDancer) svgDancer->setBounds(ringAreaBase);

        // Map base ring area to editor coords for hit-testing / repaint rectangles.
        {
            const float x = (float) uiOffset.x + ringAreaBase.getX() * (float) uiScale;
            const float y = (float) uiOffset.y + ringAreaBase.getY() * (float) uiScale;
            const float w = ringAreaBase.getWidth() * (float) uiScale;
            const float h = ringAreaBase.getHeight() * (float) uiScale;
            ringArea = juce::Rectangle<float>(x, y, w, h).toNearestInt();
        }

        // circle 1 handled by playground visuals.

        // circle 2 -> idleClockToggle
        {
            float x2,y2,r2; if (playgroundComp->getCircleInfo(2,x2,y2,r2))
            {
                const int w = (int) std::round(r2 * 2.0f);
                idleClockToggle.setBounds((int)std::round(x2 - r2), (int)std::round(y2 - r2), w, w);
            }
        }

        // circle 5: shuffle / clockWhileStopped visuals.

        // Resync step selection handled by ring segments.

        const int setupSize = 30;
        const int paletteW = 40;
        const int paletteH = 61;
        const int paletteX = 5;
        const int paletteY = (ringAreaBase.getCentreY() - (paletteH / 2)) - 75;
        colorPaletteToggle.setBounds(paletteX, paletteY, paletteW, paletteH);

        const int helpSize = 24;
        const int helpX = paletteX + (ColorPaletteToggle::kDotSize - helpSize) / 2;
        const int helpY = 210 + (setupSize - helpSize) / 2;
        helpToggle.setBounds(helpX, helpY, helpSize, helpSize);

        if (setupCornerButton){
            setupCornerButton->setBounds(kBaseW - setupSize, 210, setupSize, setupSize);
            setupCornerButton->setEdgeIndent(setupSize / 4);
        }
    }
    else
    {
        // Fallback positions if playground absent; keep clickButton hidden.
        ringAreaBase = juce::Rectangle<int>(150 - 70, 130 - 70, 140, 140);
        if (svgDancer) svgDancer->setBounds(ringAreaBase);
        {
            const float x = (float) uiOffset.x + ringAreaBase.getX() * (float) uiScale;
            const float y = (float) uiOffset.y + ringAreaBase.getY() * (float) uiScale;
            const float w = ringAreaBase.getWidth() * (float) uiScale;
            const float h = ringAreaBase.getHeight() * (float) uiScale;
            ringArea = juce::Rectangle<float>(x, y, w, h).toNearestInt();
        }
        //idleClockToggle.setBounds(234 - 14, 132 - 14, 28, 28);
        // shuffleScaleToggle removed.
        // no legacy triggerRect fallback
        // stepOffsetMenu no longer present.
        const int setupSize = 30;
        colorPaletteToggle.setBounds(5, 25, 40, 61);

        const int helpSize = 24;
        const int helpX = 5 + (ColorPaletteToggle::kDotSize - helpSize) / 2;
        const int helpY = 210 + (setupSize - helpSize) / 2;
        helpToggle.setBounds(helpX, helpY, helpSize, helpSize);
        
        if (setupCornerButton){
            setupCornerButton->setBounds(kBaseW - setupSize, 210, setupSize, setupSize);
            setupCornerButton->setEdgeIndent(setupSize / 4);
        }
    }

    // Position status bar inside header (right-aligned region)
    if (statusBar)
    {
        // Provide area matching previous manual drawing region (right segment of header minus margins)
        const int headerH = 30;
        const int w =
           #if CLOCKV3_DEMO
            64;
           #else
            50;
           #endif
        statusBar->setBounds(kBaseW - w - 9, 0, w, headerH);
    }

    // Sync Latch Button moved to the old palette position.
    syncLatchButton.setBounds(124, 32, 52, 16);


    // Force absolute placement to keep toggle at exact design coordinates.
    if (headerSwitchToggle) {
        headerSwitchToggle->setBounds(286, 5, 10, 20);
        headerSwitchToggle->setInterceptsMouseClicks(true, true);
        headerSwitchToggle->toFront(true);
        headerSwitchToggle->setAlwaysOnTop(true);
    }

    // Keep the status bar LED/shadow above nearby header widgets.
    if (statusBar)
        statusBar->toFront(false);

    // Keep the gear button always front-most.
    if (setupCornerButton)
        setupCornerButton->toFront(true);
}

void ClockSyncAudioProcessorEditor::paintBackdropLayer(juce::Graphics& g)
{
    g.fillAll(processor.theme.accent().darker(0.9f));
    auto bounds = uiRoot.getLocalBounds().toFloat();
    g.setColour(processor.theme.accent().darker(0.9f));
    g.fillRect(bounds);

    {
        g.setColour(processor.theme.accent().darker(0.33f).withAlpha(0.33f));
        g.setFont(juce::Font(juce::FontOptions("Arial", 10.0f, juce::Font::bold)));
        juce::String buildText = juce::String(PLUGIN_VERSION_WITH_BUILD);
        g.drawFittedText(buildText, juce::Rectangle<int>(210, 31, 82, 14), juce::Justification::centredRight, 1);
    }

    // Decorative backdrop circles
    auto centre = bounds.getCentre();
    const auto& sizes = UiLayout::kBackdropSizes;
    for (size_t i = 0; i < sizes.size(); ++i)
    {
        const float baseSz = (float) sizes[i];
        const float alphaFactor = 0.1f * (float) i;
        const float progress = backdropPulseProgress[i];
        const float extraScale = backdropPulseScale[i] * progress;
        const float sz = baseSz * (UiLayout::kBackdropMultiplier + extraScale);
        juce::Colour col = processor.theme.base().withAlpha(alphaFactor);
        juce::Rectangle<float> rc(centre.x - sz * 0.5f, centre.y - sz * 0.5f + 10.0f, sz, sz);
        g.setColour(col);
        g.fillEllipse(rc);
    }
}

void ClockSyncAudioProcessorEditor::paintOverlayBackgroundLayer(juce::Graphics& g)
{
    // Submenu background (behind controls)
    const bool submenuActive = (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f);
    if (submenuActive)
    {
        const float hiddenTop = -10.0f;
        const float shownTop  = 25.0f;
        const float submenuH  = 30.0f;
        const float topY = hiddenTop + (shownTop - hiddenTop) * setupSubmenuProgress;
        juce::Rectangle<float> submenuRect(0.0f, topY, (float) kBaseW, submenuH);
        {
            juce::DropShadow ds(juce::Colours::black.withAlpha(0.45f), 14, juce::Point<int>(0, 6));
            auto shadowInt = submenuRect.toNearestInt().expanded(-2, 2);
            ds.drawForRectangle(g, shadowInt);
        }
        g.setColour(kFixedHeaderBaseColour);
        g.fillRect(submenuRect);
    }
}

void ClockSyncAudioProcessorEditor::paintHeaderBackgroundLayer(juce::Graphics& g)
{
    // Header background (covers submenu content when it slides under the header)
    const int headerPaintH = 30;
    juce::Rectangle<float> headerRect(0.0f, 0.0f, (float) kBaseW, (float) headerPaintH);
    {
        juce::DropShadow ds(juce::Colours::black.withAlpha(0.45f), 14, juce::Point<int>(0, 6));
        auto shadowInt = headerRect.toNearestInt().withWidth((int) headerRect.getWidth() - 2);
        ds.drawForRectangle(g, shadowInt);
    }
    g.setColour(kFixedHeaderBaseColour);
    g.fillRect(headerRect);
}

void ClockSyncAudioProcessorEditor::paintOverlayForegroundLayer(juce::Graphics& g)
{
    // Submenu hover tooltips
    const bool submenuActive = (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f);
    if (submenuActive && hoveredSetupIndex >= 0)
    {
        juce::Font vf(juce::Font(juce::FontOptions("Arial", 11.0f, juce::Font::bold)));
        g.setColour(processor.theme.accent());
        g.setFont(vf);
        auto drawTip = [&](const juce::String& text, const juce::Component& c){
            juce::Rectangle<int> r = c.getBounds();
            juce::Rectangle<int> tipR(r.getX(), r.getY() - 12, r.getWidth(), r.getHeight());
            if (tipR.getBottom() < 30) return;
            g.drawFittedText(text, tipR, juce::Justification::centred, 1);
        };
        if (hoveredSetupIndex == 0) drawTip("send midi-clocks when idle/stopped", idleModeButton);
        else if (hoveredSetupIndex == 1) drawTip("legacy sends always stop before start", legacyModernButton);
        else if (hoveredSetupIndex == 2) drawTip("send song position pointer", sppButton);
    }

    if (!setupOverlayVisible && helpToggle.getToggleState())
    {
        const int h = 16;
        const int y = helpToggle.getBounds().getCentreY() - (h / 2) + 5;

        juce::String hoverText = editorHoverText;
        if (hoverText.isEmpty() && playgroundComp && playgroundComp->getHoverTextEnabled())
            hoverText = playgroundComp->getCurrentHoverText();

        if (hoverText.isNotEmpty())
        {
            g.setColour(processor.theme.cyan());
            g.setFont(juce::Font(juce::FontOptions("Arial", 12.0f, juce::Font::bold)));
            const int textX = helpToggle.getRight() + 4;
            g.drawFittedText(hoverText, juce::Rectangle<int>(textX, y, kBaseW - textX - 8, h), juce::Justification::centredLeft, 1);
        }
    }

    // Diagnostics: SVG missing indicator
    if (!setupSvgLoaded)
    {
        g.setColour(juce::Colours::red);
        g.setFont(juce::Font(juce::FontOptions("Arial", 10.0f, juce::Font::bold)));
        g.drawFittedText("SVG MISSING", juce::Rectangle<int>(kBaseW - 100, kBaseH - 40, 96, 16), juce::Justification::centredRight, 1);
    }
}


bool ClockSyncAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    if (setupOverlayVisible && key == juce::KeyPress(juce::KeyPress::escapeKey))
    {
        if (setupCornerButton)
            setupCornerButton->setToggleState(false, juce::sendNotification);
        
        // Force close in case button callback didn't run or button is missing
        if (setupOverlayVisible)
        {
            setupOverlayVisible = false;
            setupOverlayComp.reset();
            helpToggle.setVisible(true);
            repaint();
        }
        return true;
    }
    return false;
}

// Centralized header visibility management
void ClockSyncAudioProcessorEditor::updateHeaderVisibility()
{
    const bool showName = nameMidiSwitch.getToggleState();
    nameBox.setVisible(showName);
    deviceBox.setVisible(!showName);
    nameMidiSwitch.setVisible(true);
    refreshButton.setVisible(true);
    setupButton.setVisible(true);
    if (statusBar) statusBar->setVisible(true);
    if (setupCornerButton) setupCornerButton->setVisible(true);
}

// Update setup submenu button positions based on current animation progress (without repaint).
void ClockSyncAudioProcessorEditor::updateSetupSubmenuLayout()
{
    // Defensive: check all required pointers before use
    if (!pulseWidthSlider || !pulseWidthValueLabel || !arrowDown)
        return;

    // Only show and position controls if submenu is open or animating
    if (!(setupSubmenuTargetOn || setupSubmenuProgress > 0.0f)) {
        idleModeButton.setVisible(false);
        legacyModernButton.setVisible(false);
        sppButton.setVisible(false);
        pulseWidthSlider->setVisible(false);
        arrowDown->setVisible(false);
        if (pulseWidthValueLabel) pulseWidthValueLabel->setVisible(false);
        return;
    }
    const bool active = (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f);
    // Layout: slider on the left, then buttons, all horizontally aligned
    const float hiddenTop = -10.0f;
    const float shownTop  = 25.0f;
    const float topY = hiddenTop + (shownTop - hiddenTop) * setupSubmenuProgress;
    const float submenuH = 30.0f;
    const int sliderW = 46;
    const int btnW = 60;
    const int btnH = 14;
    const int gap = 4;
    const int totalW = sliderW + btnW * 3 + gap * 4;
    const int x0 = (kBaseW - totalW) / 2;
    // Move slider down a bit and make it taller so value box is not hidden
    const int yButtons = (int) std::round(topY + 10); // 6px padding from top of submenu

    int x = x0;
    pulseWidthSlider->setBounds(x - 20, yButtons, sliderW, btnH);
    if (themeLNF)
        pulseWidthSlider->setLookAndFeel(themeLNF.get());

    // Place the value label immediately to the right of the slider
    x += sliderW ;
    const int valueLabelW = 40;
    pulseWidthValueLabel->setBounds(x - 25, yButtons, valueLabelW, btnH);
    pulseWidthValueLabel->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    pulseWidthValueLabel->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    pulseWidthValueLabel->setVisible(true);

    // Position the arrow centered below the value label
    const int arrowW = 22;
    const int arrowH = 15;
    int arrowX = pulseWidthValueLabel->getX() + (pulseWidthValueLabel->getWidth() - arrowW) / 2;
    int arrowY = pulseWidthValueLabel->getBottom() + 2;
    arrowDown->setBounds(arrowX, arrowY, arrowW, arrowH);
    arrowDown->setVisible(active && (!headerSwitchToggle || headerSwitchToggle->getToggleState()));

    x += gap + 20 ;

    idleModeButton.setBounds(x, yButtons, btnW, btnH);
    x += btnW + gap;
    legacyModernButton.setBounds(x, yButtons, btnW, btnH);
    x += btnW + gap;
    sppButton.setBounds(x, yButtons, btnW, btnH);
    pulseWidthSlider->setVisible(true);
    pulseWidthValueLabel->setVisible(true);
        arrowDown->setVisible(active && (!headerSwitchToggle || headerSwitchToggle->getToggleState()));
    idleModeButton.setVisible(true);
    legacyModernButton.setVisible(true);
    sppButton.setVisible(true);

    // Keep label in sync with slider value
    if (pulseWidthSlider)
    {
        pulseWidthSlider->onValueChange = [this]()
        {
            if (pulseWidthValueLabel && pulseWidthSlider)
                pulseWidthValueLabel->setText(juce::String((int)pulseWidthSlider->getValue()) + " ms", juce::dontSendNotification);
        };
        // Set initial value
        pulseWidthSlider->onValueChange();
    }

    // --- Adjust layout: slider + label + legacyModernButton + idleModeButton + sppButton ---
    // (updateSetupSubmenuLayout handles positioning)
    // Add label to layout after slider, before legacyModernButton

}

// Removed duplicate legacy mouseMove (#if 0 block) – only one active handler remains.

// Tooltips handled by PlaygroundComponent hoverText.

void ClockSyncAudioProcessorEditor::timerCallback()
{
    // Check for hover over editor-level components
    juce::String newHoverText;
    if (colorPaletteToggle.isMouseOver())
        newHoverText = "Colorize or Shift-Click for Default";
    else if (setupCornerButton && setupCornerButton->isMouseOver())
        newHoverText = "Setup MIDI Remote";
    
    if (editorHoverText != newHoverText)
    {
        editorHoverText = newHoverText;
        repaint(0, 214, getWidth(), 22);
    }

    bool needAll = false;
    bool needRing = false;
    bool needTrigger = false;
    // Process any scheduled backdrop animator starts (staggered starts placed by beat detection).
    const double nowMsTop = juce::Time::getMillisecondCounterHiRes();
    constexpr double kPerBackdropDelayMs = 88.0; // ~66ms stagger between successive backdrop circles
    for (size_t si = 0; si < backdropScheduledStartMs.size(); ++si)
    {
        double s = backdropScheduledStartMs[si];
        if (s > 0.0 && s <= nowMsTop)
        {
            if (si < backdropAnimators.size() && backdropAnimators[si])
                backdropAnimators[si]->start();
            backdropScheduledStartMs[si] = 0.0;
            needAll = true; // scheduled starts affect full-canvas backdrop
        }
    }
    
    // LED update: do NOT set ledLevel on every MIDI clock tick (uiClockCounter)
    // — that was causing the LED to blink at clock resolution. Instead we
    // allow the quarter-beat logic below to set `ledLevel` on beat boundaries
    // (so the LED blinks in step with the backdrop pulses). We still perform
    // decay each timer tick so the LED falls off between beats.
    const auto counter = processor.getUiClockCounter();
    if (counter != lastSeenClockCounter)
    {
        lastSeenClockCounter = counter;
        // Ensure dancer advances every 24PPQ pulse by repainting ring area
        needRing = true;
        // When a new MIDI clock tick arrives, check whether the 1/16 step
        // has advanced and drive any step-aligned UI updates (LED pulse,
        // backdrop sequencing) from this branch. This is more robust than
        // relying on a separate read later in the function where races can
        // cause missed updates.
        const int step16_now = juce::jlimit(1, 16, processor.getUiStep16());
        if (step16_now != stepNumberCached)
        {
            stepNumberCached = step16_now;
            needTrigger = true;
            const int beatIndex = (step16_now - 1) / 4; // 0..3

            // Decide LED pulse level for this 16th step and start the animator
            const bool hasNext = processor.getUiNextRestartPending();
            const bool armed = processor.getUiPendingStart();
            if (hasNext || armed)
                ledPulseTarget = 1.0f;
            else if (! runParamCached && idleModeButton.getToggleState())
                ledPulseTarget = 0.5f;
            else if (runParamCached)
                ledPulseTarget = 1.0f;
            else
                ledPulseTarget = 0.0f;

            if (ledPulseTarget > 0.0f)
            {
                // If we're in PENDING/ARMED mode, only pulse once per quarter-note
                // (beatIndex reflects quarter boundaries). For normal running mode
                // pulse on every 16th-step.
                if (hasNext || armed)
                {
                    if (beatIndex != lastLedBeatIndex)
                    {
                        lastLedBeatIndex = beatIndex;
                        ledAnimator.start();
                    }
                }
                else
                {
                    ledAnimator.start();
                }
            }

            // Backdrop pulse sequencing: on quarter-note boundaries schedule staggered starts
            if (beatIndex != lastBackdropBeatIndex)
            {
                lastBackdropBeatIndex = beatIndex;
                const double nowMs = juce::Time::getMillisecondCounterHiRes();
                // Invert pulse sequence: previously index 0 pulsed first. Now highest index
                // starts immediately and lower indices are staggered later so the visual
                // ripple direction reverses.
                const size_t total = backdropScheduledStartMs.size();
                for (size_t i = 0; i < total; ++i)
                {
                    const size_t reversedIdx = total  - 2 - i; // total-1 -> 0
                    backdropScheduledStartMs[i] = nowMs + (double)reversedIdx * kPerBackdropDelayMs;
                }
                // request full repaint when pulses begin
                needAll = true;
                // Also ask the playground to schedule its per-circle pulses so
                // the UI circles (not just the decorative backdrop) pulse in
                // sync with the editor-driven beat. Playground exposes a
                // wrapper that schedules pulses for all circles.
                // Do NOT request playground pulses: only decorative backdrop
                // animators in the editor should pulse on beats. Avoid invoking
                // playground scheduling so UI elements do not animate on the beat.
            }
        }
    }

    // Run param / engine flags / pending start
    if (auto* rp = processor.getAPVTS().getRawParameterValue(ClockSyncAudioProcessor::paramRun))
    {
        const bool runNow = rp->load() > 0.5f;
        if (runNow != runParamCached) {
            runParamCached = runNow;
            needRing = true;
            // Visual feedback: pulse LED when Run toggles on/off so user sees state change
            if (runParamCached)
                ledPulseTarget = 1.0f;
            else
                ledPulseTarget = 0.6f; // softer pulse for stop
            if (ledPulseTarget > 0.0f) ledAnimator.start();
        }
    }
    // Mirror trigger mode param into playground idx8 button so automation updates UI
    if (playgroundComp)
    {
        if (auto* tm = processor.getAPVTS().getRawParameterValue(ClockSyncAudioProcessor::paramTriggerModeEnabled))
        {
            playgroundComp->setTriggerModeState(tm->load() > 0.5f);
        }
    }
    // Cache NEXT state to detect restart application (edge: true -> false)
    const bool uiNextPendingNow = processor.getUiNextRestartPending();
    const bool running = processor.getUiIsRunning();
    if (running != engineRunningCached) {
        engineRunningCached = running;
        needAll = true;
        if (playgroundComp) playgroundComp->setRunState(running, false); // host-driven: don't toggle user Run parameter
            // Ensure ring internal progression uses host tempo + rate multiplier (clear external playhead usage implicitly)
            if (playgroundComp && running) {
                playgroundComp->setHostTempo(processor.getUiBpm());
            }
        // When transport/run starts, initialise cycle start using selected offset so
        // visual relative step reflects offset immediately (selected step becomes relative target)
        if (running && cycleStartStepAbsolute < 0) {
            if (selectedResyncStepCached > 1) {
                // Shift cycle start backwards so the first visual relative step aligns with selected offset.
                int stepNow = juce::jlimit(1,16, processor.getUiStep16());
                int cs = stepNow - (selectedResyncStepCached - 1);
                while (cs <= 0) cs += 16;
                cycleStartStepAbsolute = cs; // 1..16
            }
        }
    }
    const bool armed = processor.getUiPendingStart();
    if (armed != pendingStartCached) {
        pendingStartCached = armed;
        needAll = true;
        // pulse LED when armed status changes
        ledPulseTarget = armed ? 1.0f : 0.5f;
        if (ledPulseTarget > 0.0f) ledAnimator.start();
    }

    // Rate choice
    if (rateParam)
    {
        const int idx = rateParam->getIndex();
        if (idx != rateIndexCached)
        {
            rateIndexCached = idx;
            // Keep Playground idx6 (rate popup) in sync with APVTS, same as GridScaleMenu
            if (playgroundComp)
            {
                int displayed = 16; // map index -> division label
                switch (idx)
                {
                    case 0: displayed = 32; break;
                    case 1: displayed = 16; break;
                    case 2: displayed = 8;  break;
                    case 3: displayed = 4;  break;
                    default: displayed = 16; break;
                }
                playgroundComp->setClockRateIndexValue(displayed);
                    // Update host tempo each rate change to ensure internal progression uses latest BPM
                    playgroundComp->setHostTempo(processor.getUiBpm());
            }
            needRing = true;
        }
    }

    // Mirror Shuffle and Offset parameters to UI (Playground)
    if (playgroundComp)
    {
        if (auto* p = processor.getAPVTS().getRawParameterValue(ClockSyncAudioProcessor::paramShuffleStep))
        {
            int val = (int)p->load();
            if (val != shuffleValue)
            {
                shuffleValue = val;
                playgroundComp->setSelectedShuffle(shuffleValue);
                needRing = true;
            }
        }
        if (auto* p = processor.getAPVTS().getRawParameterValue(ClockSyncAudioProcessor::paramResyncOffsetStep))
        {
            int val = (int)p->load();
            if (val != selectedResyncStepCached)
            {
                selectedResyncStepCached = val;
                playgroundComp->setResyncStepSelected(selectedResyncStepCached);
                needRing = true;
            }
        }
        // Mirror Linear Shuffle Amount
        if (auto* p = processor.getAPVTS().getRawParameterValue(ClockSyncAudioProcessor::paramShuffleLinear))
        {
            float t01 = p->load();
            // Convert 0..1 param to 0.5..0.75 amount
            float amount = 0.5f + (t01 * 0.25f);
            if (std::abs(amount - linearShuffleAmountCached) > 0.001f)
            {
                linearShuffleAmountCached = amount;
                playgroundComp->setLinearShuffleAmount(amount);
            }
        }
        // Mirror Linear Shuffle Mode (from state)
        {
             bool mode = (bool) processor.getAPVTS().state.getProperty("ui.linearShuffleMode", false);
             if (mode != linearShuffleModeCached)
             {
                 linearShuffleModeCached = mode;
                 playgroundComp->setLinearShuffleModeState(mode);
             }
        }
        // Mirror Auto-Fill (Pattern Bars)
        if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramPatternBars))
        {
            float v = p->getValue(); // 0..1
            int idx = (int)std::round(v * 8.0f);
            if (idx != autoFillIndexCached)
            {
                autoFillIndexCached = idx;
                // Map param index (0=OFF,1=1,2=2,3=4,4=8,5=16,6=32,7=64,8=RND)
                // to popup index (0=64,1=32,2=16,3=8,4=4,5=2,6=1,7=OFF).
                int popupIndex = 7;
                switch (idx)
                {
                    case 7: popupIndex = 0; break;
                    case 6: popupIndex = 1; break;
                    case 5: popupIndex = 2; break;
                    case 4: popupIndex = 3; break;
                    case 3: popupIndex = 4; break;
                    case 2: popupIndex = 5; break;
                    case 1: popupIndex = 6; break;
                    case 0:
                    case 8:
                    default: popupIndex = 7; break;
                }
                playgroundComp->setPopup3Index(popupIndex);
            }
        }
    }

    // Update trigger fade animator (drives repaint via value changed callback)
    // Trigger fade animator is now driven by the VBlankAnimatorUpdater; the
    // value-changed callback repaints the trigger area when necessary.

    // Update LED animator so pulses decay (ledAnimator callbacks repaint header)
    // LED animator is driven by the VBlankAnimatorUpdater; its callback
    // repaints the header when active.

    

    // (Previously backdrop sequencing and LED start logic ran here based on
    // reading `getUiStep16()`. That could miss updates due to timing races;
    // the step-driven logic now lives in the clock-counter branch above.)

    // Fallback: if for any reason the MIDI clock counter didn't advance but
    // the reported 1/16 `uiStep16` has changed (e.g. host/processor timing),
    // perform the same step-aligned updates here to avoid missing pulses.
    {
        const int step16_now = juce::jlimit(1, 16, processor.getUiStep16());
        if (step16_now != stepNumberCached)
        {
            stepNumberCached = step16_now;
            needTrigger = true;
            const int beatIndex = (step16_now - 1) / 4; // 0..3

            const bool hasNext = processor.getUiNextRestartPending();
            const bool armed = processor.getUiPendingStart();
            if (hasNext || armed)
                ledPulseTarget = 1.0f;
            else if (! runParamCached && idleModeButton.getToggleState())
                ledPulseTarget = 0.5f;
            else if (runParamCached)
                ledPulseTarget = 1.0f;
            else
                ledPulseTarget = 0.0f;

            if (ledPulseTarget > 0.0f)
            {
                // If pending/armed, only pulse once per quarter
                if (hasNext || armed)
                {
                    if (beatIndex != lastLedBeatIndex)
                    {
                        lastLedBeatIndex = beatIndex;
                        ledAnimator.start();
                    }
                }
                else
                {
                    ledAnimator.start();
                }
            }

            if (beatIndex != lastBackdropBeatIndex)
            {
                lastBackdropBeatIndex = beatIndex;
                const double nowMs = juce::Time::getMillisecondCounterHiRes();
                constexpr double kPerBackdropDelayMs = 22.0;
                for (size_t i = 0; i < backdropScheduledStartMs.size(); ++i)
                    backdropScheduledStartMs[i] = nowMs + (double)i * kPerBackdropDelayMs;
                needAll = true;
            }
        }
    }

    // Decay backdrop pulse progress (frame-based) so pulses scale/fade back over time
    // Backdrop pulse decay is now driven by per-ring animators (VBlank-driven)

    // Step number update tied to 16th-note changes (no fade).
    // The numeric step is tracked internally, but the visual playhead
    // wedge (`visualStepCached`) should advance ONLY while actually running
    // (no fallback 120 BPM chaser when idle). This mirrors the playground’s
    // canonical behaviour.
        {
            const int stepNow = juce::jlimit(1, 16, processor.getUiStep16());
            // Forward host bar information when available so playground's
            // auto-trigger interval counting is driven by explicit bar
            // boundaries (more robust than relying purely on step==0).
            if (playgroundComp)
            {
                // Use cached bar number written by the audio thread in processBlock
                // to avoid calling playHead->getPosition() from the message thread
                // (the host playhead wrapper is only valid during audio callbacks).
                int barNumber = processor.getUiExternalBarNumber();
                // If the audio thread just detected a host START edge, ask the
                // playground to reset its external-bar tracking first so the
                // next forwarded bar number is treated as a fresh first bar.
                if (processor.consumeUiHostStartPending())
                {
                    playgroundComp->setExternalBarNumber(-1);
                }
                // Forward cached bar number (may be -1 when host transport is unavailable)
                // so the playground can reset its autofill counters when the DAW stops.
                playgroundComp->setExternalBarNumber(barNumber);

                // Always forward DAW step to the playground after bar info so
                // `patternBarActive` is set correctly before onStepChanged runs.
                playgroundComp->setExternalPlayheadStep(stepNow);
            }

            if (stepNow != stepNumberCached)
        {
            stepNumberCached = stepNow;
            needTrigger = true;

            // Decide LED pulse level for this 16th step and start the animator
            const bool hasNext = processor.getUiNextRestartPending();
            const bool armed = processor.getUiPendingStart();
            if (hasNext || armed)
                ledPulseTarget = 1.0f;
            else if (! runParamCached && idleModeButton.getToggleState())
                ledPulseTarget = 0.5f;
            else if (runParamCached)
                ledPulseTarget = 1.0f;
            else
                ledPulseTarget = 0.0f;

            if (ledPulseTarget > 0.0f)
            {
                // When we reach this 16th-step, determine whether we should
                // start the LED animator now. For pending/armed, only start
                // at quarter-note boundaries to achieve a slower beat-rate blink.
                const int beatIndex = (stepNow - 1) / 4;
                if (hasNext || armed)
                {
                    if (beatIndex != lastLedBeatIndex)
                    {
                        lastLedBeatIndex = beatIndex;
                        ledAnimator.start();
                    }
                }
                else
                {
                    ledAnimator.start();
                }
            }
        }
        // Advance visual wedge only when the engine is actually running; do not
        // advance on the idle 120 BPM fallback. This ensures the chaselights
        // are driven by the real playhead (visualStepCached), consistent with
        // the segment colouring.
        if (engineRunningCached && stepNow != visualStepCached)
        {
            // Keep visual wedge loosely in sync with real step (no rate scaling here; ring handles scaled progression).
            visualStepCached = ((stepNow % 16) + 1);
            needRing = true; // ring contains the wedge
            // External playhead sync deferred until relativeStepCached updated below.
        }
        // Detect restart application: either NEXT just cleared or pendingStart just cleared at a running state.
        // Restart applied when NEXT indicator clears (true -> false). PendingStart edge is already handled by processor flags.
        const bool restartApplied = (nextRestartPendingCached && ! uiNextPendingNow);
        // If NEXT just cleared and we're running, capture the absolute step as cycle start.
        if (restartApplied && engineRunningCached)
        {
            // If a manual trigger with an offset preview was active, adjust the
            // recorded cycle start so the relative step matches the selected
            // resync offset. Otherwise use the reported absolute step.
            if (manualTriggerOffsetActive)
            {
                int cs = stepNow - manualTriggerPlayheadDelta; // may underflow
                while (cs <= 0) cs += 16;
                cycleStartStepAbsolute = cs; // 1..16
            }
            else
            {
                cycleStartStepAbsolute = stepNow; // absolute bar step where restart applied
            }
            // Clear manual trigger offset state – the real restart just applied.
            manualTriggerOffsetActive = false;
            manualTriggerPlayheadDelta = 0;
            // Full ring flash to acknowledge restart application (intensity boost if offset selected)
            if (playgroundComp) playgroundComp->flashFullRing(selectedResyncStepCached > 1 ? 1.5f : 1.0f);
            // Commit any pending speed multiplier change now so visual chase speed updates ONLY on restart.
            if (playgroundComp) playgroundComp->commitPendingSpeedMultiplier();
        }
        // Initialise cycle start if not set yet once running
        if (cycleStartStepAbsolute < 0 && engineRunningCached)
            cycleStartStepAbsolute = stepNow;
        // Compute relative step counting from cycleStartStepAbsolute (1..16)
        if (cycleStartStepAbsolute > 0)
        {
            relativeStepCached = ((stepNow - cycleStartStepAbsolute + 16) % 16) + 1; // 1..16
        }
        nextRestartPendingCached = uiNextPendingNow;
        // Do not override the playground's running playhead here. The chase-light
        // must continue running uninterrupted. Manual-trigger previews flash a
        // single segment via flashSegmentLogical without setting the external
        // playhead, and the actual restart alignment is applied when the restart
        // event is seen (cycleStartStepAbsolute adjusted above).
    }

    // Cache selected resync offset step from parameter.
    {
        int paramStep = -1;
        if (auto* pi = dynamic_cast<juce::AudioParameterInt*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep)))
            paramStep = juce::jlimit(1, 16, pi->get());
        if (paramStep > 0)
            selectedResyncStepCached = paramStep;
    }

    // Keep pattern visual in sync with the parameter in case it changed externally
    if (auto* psi = dynamic_cast<juce::AudioParameterInt*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramPatternSteps)))
    {
        int v = (int) juce::jlimit(0, 65535, psi->get());
        if (v != patternParamCached)
        {
            patternParamCached = v;
            if (playgroundComp)
                playgroundComp->setPatternBitmask((uint16_t) v);
            else
                pattern.setBitmask((uint16_t) v);
            needRing = true;
        }
    }

    // Playground reflects shuffle amount visually.

    // Dispatch minimal repaints
    // Update popup animations (driven from editor timer so they pause when editor is suspended)
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        double dtMs = 16.6667;
        if (lastUiUpdateMs > 0.0) dtMs = nowMs - lastUiUpdateMs;
        lastUiUpdateMs = nowMs;
        // Decay segment fade trail (~600ms to fully fade)
        ///const float decayMs = 600.0f;
        // Slow the decay slightly to improve visibility of the cyan trail.
        // (Previous value 600ms made fades too brief; new 900ms extends trail.)
        // NOTE: retain variable name for minimal change; adjust value only.
        // (We keep original line for context; override below.)
        
    }
    // Update status bar text + LED level each timer tick if changed
    if (statusBar)
    {
        juce::String st;

       #if CLOCKV3_DEMO
        const int remainingSeconds = processor.getDemoRemainingSecondsUI();
        const int minutes = remainingSeconds / 60;
        const int seconds = remainingSeconds % 60;
        st = juce::String::formatted("%02d:%02d", minutes, seconds);
       #else
        const bool isRunning = processor.getUiIsRunning();
        const bool isArmed = processor.getUiPendingStart();
        const bool hasNext = processor.getUiNextRestartPending();
        if (hasNext)
        {
            if (processor.syncLatchEnabled.load(std::memory_order_relaxed))
                st = "GATE";
            else
                st = "WAIT";
        }
        else if (isRunning) st = "LOCK";
        else if (isArmed) st = "ARM'D";
        else st = idleModeButton.getToggleState() ? "IDLE" : "STOP";
       #endif

        statusBar->setStatusText(st);
        statusBar->setLedLevel(ledLevel);
    }
    // Consume pattern-triggered idx1 blink requests and mirror manual click visuals
    {
        const int blinkStep = processor.consumeUiIdx1BlinkStep(); // 1..16 or 0 if none
        if (blinkStep > 0)
        {
            // Flash idx1 like manual click: pulse LED and trigger playground visuals
            ledPulseTarget = 1.0f; ledAnimator.start();
            if (playgroundComp)
            {
                (void) playgroundComp->handleExternalClickIndex(1);
                playgroundComp->flashSegmentLogical(blinkStep);
            }
            // Shift dancer by one step (3 frames) on pattern Start
            dancerFrameOffset = (dancerFrameOffset + 3) % 24;
            needRing = true;
        }
    }
    
    // Update SVG Dancer Frame
    if (svgDancer)
    {
        svgDancer->setTint(processor.theme.accent().withAlpha(1.0f));
        svgDancer->setVisible(!patternEditMode);

        if (!patternEditMode)
        {
            int frameIdx = 0;
            const int totalFrames = 24;
            const unsigned long long pulses = processor.getUiClockCounter();
            const bool isRunning = processor.getUiIsRunning();
            
            if (isRunning)
            {
                int pulsesInQuarter = 24; 
                switch (rateIndexCached)
                {
                    case 0: pulsesInQuarter = 24; break; 
                    case 1: pulsesInQuarter = 12; break; 
                    case 2: pulsesInQuarter = 6;  break; 
                    case 3: pulsesInQuarter = 6;  break; 
                    default: pulsesInQuarter = 24; break;
                }
                const int scaledCycle = pulsesInQuarter * 4; 
                const int pInCycle = (int) (pulses % (unsigned long long) scaledCycle);
                frameIdx = (pInCycle * totalFrames) / scaledCycle;
                frameIdx = juce::jlimit(0, totalFrames - 1, frameIdx);
                
                int off = dancerFrameOffset % totalFrames;
                if (off < 0) off += totalFrames;
                frameIdx = (frameIdx + off) % totalFrames;
            }
            else
            {
                const double bpm = juce::jmax(1.0, processor.getUiBpm());
                const double quarterMs = (60.0 / bpm) * 1000.0;
                const double nowMs = juce::Time::getMillisecondCounterHiRes();
                const double tInQuarter = std::fmod(nowMs, quarterMs);
                const int frames3 = 3;
                double x = (tInQuarter / quarterMs) * 2.0; 
                if (x > 2.0) x -= std::floor(x);
                double tri = 1.0 - std::fabs(x - 1.0); 
                int localIdx = (int) std::floor(tri * (double) (frames3 - 1) + 0.5);
                localIdx = juce::jlimit(0, frames3 - 1, localIdx);
                
                int off = dancerFrameOffset % totalFrames;
                if (off < 0) off += totalFrames;
                int blockStart = (off / 3) * 3; 
                frameIdx = (blockStart + localIdx) % totalFrames;
            }
            svgDancer->setFrame(frameIdx);
        }
    }

    if (needAll) { repaint(); return; }
    if (needRing) repaint(ringArea);
    // Trigger updates can affect the ring drawing (visual slice near trigger). Ensure
    // we repaint both the trigger rect and the ring area to avoid leftover artefacts.
    if (needTrigger)
    {
        // Trigger updates can affect the ring drawing; repaint ring.
        repaint(ringArea);
    }
}

void ClockSyncAudioProcessorEditor::updateSetupButtonImages()
{
    if (!setupCornerButton || !setupCornerDrawable) return;

    auto makeTintedDrawable = [&](juce::Colour tint) -> std::unique_ptr<juce::Drawable> {
        const int imgSz = 64;
        juce::Image img(juce::Image::ARGB, imgSz, imgSz, true);
        {
            juce::Graphics gg(img);
            juce::Rectangle<float> area(0, 0, (float) imgSz, (float) imgSz);
            setupCornerDrawable->drawWithin(gg, area, juce::RectanglePlacement::centred, 1.0f);
        }
        // Recolour all non-transparent pixels to the given tint, preserving alpha
        juce::Image::BitmapData bd(img, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < imgSz; ++y)
            for (int x = 0; x < imgSz; ++x)
            {
                auto col = bd.getPixelColour(x, y);
                if (col.getAlpha() > 0)
                    bd.setPixelColour(x, y, tint.withAlpha(col.getFloatAlpha() * tint.getFloatAlpha()));
            }
        auto di = std::make_unique<juce::DrawableImage>();
        di->setImage(img);
        return di;
    };

    setupCornerOffDrawable = makeTintedDrawable(processor.theme.cyan().withAlpha(0.5f));
    setupCornerOnDrawable  = makeTintedDrawable(processor.theme.cyan());

    setupCornerButton->setImages(
        setupCornerOffDrawable.get(), setupCornerOffDrawable.get(), setupCornerOnDrawable.get(), setupCornerOffDrawable.get(),
        setupCornerOnDrawable.get(),  setupCornerOnDrawable.get(),  setupCornerOnDrawable.get(),  setupCornerOnDrawable.get());
}

// ---------------- Dancer PNG sequence ----------------
// void ClockSyncAudioProcessorEditor::loadDancerFrames()
// {
//     // Removed in favor of SvgDancerComponent
// }

// void ClockSyncAudioProcessorEditor::drawDancer(juce::Graphics& g)
// {
//     // Removed in favor of SvgDancerComponent
// }

// OverlayTooltip handlers unused.

ClockSyncAudioProcessorEditor::~ClockSyncAudioProcessorEditor()
{
    processor.onThemeChanged = nullptr;
    // Ensure vblank updater is destroyed before member animators go away
    vblankUpdater.reset();
    arrowDown.reset();
    deviceBox.setLookAndFeel(nullptr);
    refreshButton.setLookAndFeel(nullptr);
    nameBox.setLookAndFeel(nullptr);
    nameMidiSwitch.setLookAndFeel(nullptr);
    syncLatchButton.setLookAndFeel(nullptr);
    // Clear help button look-and-feel
    helpToggle.setLookAndFeel(nullptr);
}

// Existing mouseUp earlier (original stub) removed; consolidated into final mouseUp below.

void ClockSyncAudioProcessorEditor::mouseMove(const juce::MouseEvent& e)
{
    juce::Point<float> pf((float) e.x, (float) e.y);
    bool did = false;
    const bool submenuActive = (setupSubmenuTargetOn || setupSubmenuProgress > 0.0f);
    if (submenuActive)
    {
        // Use editor-relative coordinates when testing child bounds, because events may
        // arrive from nested components with their own local coordinate spaces.
        auto ep = e.getEventRelativeTo(this);
        juce::Point<int> posEditor = ep.getPosition();

        int forced = -1;
        int newHover = -1;
        if (idleModeButton.isVisible() && idleModeButton.getBounds().contains(posEditor)) { forced = 9; newHover = 0; }
        else if (legacyModernButton.isVisible() && legacyModernButton.getBounds().contains(posEditor)) { forced = 10; newHover = 1; }
        else if (sppButton.isVisible() && sppButton.getBounds().contains(posEditor)) { forced = 11; newHover = 2; }

        if (newHover != hoveredSetupIndex)
        {
            hoveredSetupIndex = newHover;
            // Repaint the header/submenu region only
            repaint(0, 0, getWidth(), 80);
        }
        if (playgroundComp)
        {
            if (forced >= 0)
            {
                playgroundComp->setForcedHoverIndex(forced);
                // When hovering submenu controls, don't forward circle hover.
                return;
            }
            // Not over submenu control: clear forced label and allow normal circle hover forwarding below.
            playgroundComp->clearForcedHoverIndex();
        }
    }
    else if (playgroundComp)
    {
        playgroundComp->clearForcedHoverIndex();
        playgroundComp->setExternalHoverBlocked(false);
    }
    // If either popup menu (idx3 or idx6) is active (visible or animating),
    // don't override Playground's own hover mapping. This ensures that hovering
    // expanded popup items keeps the correct tooltip (3/6) visible and that
    // per-item hover animations are fed reliably.
    if (playgroundComp)
    {
        const bool popup3Active = playgroundComp->isPopup3Active();
        const bool popup6Active = playgroundComp->isPopup6Active();
        if (popup3Active || popup6Active)
        {
            // Feed popup hover directly so expanded items get proper hoverText even if overlays are on top.
            bool consumedPopupHover = false;
            if (popup6Active && playgroundComp->getCircleCount() > 6)
            {
                float x6,y6,r6; if (! playgroundComp->getCircleInfo(6,x6,y6,r6)) {}
                const int hi6 = playgroundComp->popup6Hit(pf, x6, y6, r6);
                if (hi6 >= 0)
                {
                    playgroundComp->setHoverIndexFromEditor(6);
                    repaint(juce::Rectangle<int>((int) std::round(x6 - 90.0f), (int) std::round(y6 - 90.0f), 180, 180));
                    return; // item hover mapped to circle 6
                }
                // No item: if pointer over base circle, still show 6
                const float dx6 = pf.x - x6, dy6 = pf.y - y6;
                if (dx6*dx6 + dy6*dy6 <= r6 * r6)
                {
                    playgroundComp->setHoverIndexFromEditor(6);
                    repaint(juce::Rectangle<int>((int) std::round(x6 - 60.0f), (int) std::round(y6 - 60.0f), 120, 120));
                    return;
                }
            }
            if (popup3Active && playgroundComp->getCircleCount() > 3)
            {
                float x3,y3,r3; if (! playgroundComp->getCircleInfo(3,x3,y3,r3)) {}
                const int hi3 = playgroundComp->popup3Hit(pf, x3, y3, r3);
                if (hi3 >= 0)
                {
                    playgroundComp->setHoverIndexFromEditor(3);
                    repaint(juce::Rectangle<int>((int) std::round(x3 - 90.0f), (int) std::round(y3 - 90.0f), 180, 180));
                    return; // item hover mapped to circle 3
                }
                const float dx3 = pf.x - x3, dy3 = pf.y - y3;
                if (dx3*dx3 + dy3*dy3 <= r3 * r3)
                {
                    playgroundComp->setHoverIndexFromEditor(3);
                    repaint(juce::Rectangle<int>((int) std::round(x3 - 60.0f), (int) std::round(y3 - 60.0f), 120, 120));
                    return;
                }
            }
            // Not over popup items or base circles: clear editor-driven hover; let generic logic continue below
            playgroundComp->clearHoverFromEditor();
        }
    }
    // Playground provides hover/tooltips; skip deprecated popup-ring hover handling.
    if (! playgroundComp)
    {
        auto checkHoverComp = [&](const std::unique_ptr<PopupMenuRing>& pr, const juce::Component& c) -> int
        {
            if (! pr) return 0;
            auto b = c.getBounds().toFloat();
            const float baseX = b.getCentreX();
            const float baseY = b.getCentreY();
            const float baseR = std::min(b.getWidth(), b.getHeight()) * 0.5f;
            return pr->handleMouseMove(pf, baseX, baseY, baseR);
        };

        
    }
    // Legacy popupRing0 (run-offset selection) was superseded by PlaygroundComponent's
    // internal ring + popup menus. This no-op check is a leftover and has been
    // removed to avoid confusion.

    

    // Improved hover hit-test: prefer the ring donut (idx0) when pointer is inside
    // the annulus, unless the pointer is truly inside a smaller circle by a margin.
    // This avoids the ring edge showing idx6/others just because their disks overlap
    // the donut slightly. Also provide explicit hover for idx7's tiny inner button.
    if (playgroundComp && playgroundComp->getCircleCount() > 0)
    {
        const int pgx = playgroundComp->getX();
        const int pgy = playgroundComp->getY();
        const int n = playgroundComp->getCircleCount();
        int bestIdx = -1;
        float bestR = 1e9f;
        juce::Point<int> pos = e.getPosition(); // (x,y) public members
        // Precompute ring annulus state using the same geometry we paint with
        bool inDonut = false;
        float dx = 0.0f, dy = 0.0f, dist2 = 0.0f, innerR = 0.0f, outerR = 0.0f;
        if (ringArea.contains(pos))
        {
            const int cx = ringArea.getCentreX();
            const int cy = ringArea.getCentreY();
            dx = (float) pos.x - (float) cx;
            dy = (float) pos.y - (float) cy;
            dist2 = dx*dx + dy*dy;
            outerR = (float) kRingOuterD * 0.5f;
            innerR = (float) kRingInnerD * 0.5f;
            inDonut = (dist2 <= outerR*outerR && dist2 >= innerR*innerR);

            // All code using dx, dy, dist2, innerR, outerR must be inside this block
            for (int i = 0; i < n; ++i)
            {
                float cx,cy,cr; if (! playgroundComp->getCircleInfo(i,cx,cy,cr)) continue; const float gx = cx + (float) pgx; const float gy = cy + (float) pgy; const float dx = (float) pos.x - gx; const float dy = (float) pos.y - gy; if (dx*dx + dy*dy <= cr*cr + 0.0001f) { if (cr < bestR) { bestR = cr; bestIdx = i; } }
            }

            bool consumed = false;
            // Special-case idx7 tiny inner button hover so its label shows when pointer is
            // inside the small dot even if overlays block Playground's own mouseMove.
            bool innerBtnHover = false;
            if (playgroundComp->getCircleCount() > 7)
            {
                float cx7,cy7,cr7; if (playgroundComp->getCircleInfo(7,cx7,cy7,cr7))
                {
                    const float smallR = 8.0f; const float dx7 = (float) pos.x - (cx7 + (float) pgx); const float dy7 = (float) pos.y - (cy7 + (float) pgy); innerBtnHover = (dx7*dx7 + dy7*dy7) <= (smallR*smallR);
                }
                playgroundComp->setClickToPulseHoverFromEditor(innerBtnHover);
                if (innerBtnHover)
                {
                    playgroundComp->setHoverIndexFromEditor(7);
                    consumed = true;
                }
            }

            if (!consumed && bestIdx >= 0)
            {
                // Smallest containing circle wins; only fall back to ring when no circle matches.
                playgroundComp->setHoverIndexFromEditor(bestIdx);
                consumed = true;
            }
            else if (inDonut)
            {
                // Ring donut fallback: pointer in annulus (between inner & outer radii) -> idx0 hover.
                playgroundComp->setHoverIndexFromEditor(0);
                consumed = true;
            }

            if (! consumed)
            {
                // Clear circle/ring hover if nothing matched.
                playgroundComp->clearHoverFromEditor();
            }

            // If popup menus are visible, propagate their internal item hover by forcing the parent
            // circle index (3 or 6) so tooltip stays active even when pointer is over expanded items.
            // We only do this when the popup itself reports a hover >= 0. (Requires PopupMenuRing API.)
            // Access underlying rings through playgroundComp public members (popup3/popup6).
            // NOTE: We rely on PopupMenuRing::getHoverIndex(); if not hovered returns -1.
            // Popup item hover mapping handled internally in PlaygroundComponent; avoid forcing here
            // to prevent overriding a smaller circle selection.
        }
    }
}



void ClockSyncAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    // Route clicks to any visible popup rings first.
    juce::Point<float> pf((float) e.x, (float) e.y);
    // If pattern edit is active and user clicks circle idx3 region, close edit mode immediately
    // so subsequent click can open its menu without interference.
    if (patternEditMode && playgroundComp && playgroundComp->getCircleCount() > 3)
    {
        float cx3,cy3,cr3; if (playgroundComp->getCircleInfo(3,cx3,cy3,cr3))
        {
            juce::Rectangle<float> r3(cx3 - cr3, cy3 - cr3, cr3 * 2.0f, cr3 * 2.0f);
            if (r3.contains(pf))
            {
                patternEditMode = false; playgroundComp->setInterceptsMouseClicks(true, true); playgroundComp->setPatternEditButtonState(false); patternHoverIndex = -1; repaint(ringArea); return; }
        }
    }
    auto handlePopupClickAtComp = [&](const std::unique_ptr<PopupMenuRing>& pr, const juce::Component& c)->int
    {
        if (! pr) return -1;
        auto b = c.getBounds().toFloat();
        const float baseX = b.getCentreX();
        const float baseY = b.getCentreY();
        const float baseR = std::min(b.getWidth(), b.getHeight()) * 0.5f;
        return pr->handleMouseDown(pf, baseX, baseY, baseR);
    };

    // Check popups in a reasonable visual order. If any handles the click,
    // apply the corresponding parameter change and collapse the popup.
    // Skip popup-ring click handling when playground active.
    if (! playgroundComp)
    {

        
    }

    // Defer ring clicks until after small circle click handling to avoid accidental
    // idx0 toggles when clicking overlapping small circles (e.g., idx5).

    // If no popup consumed the click, allow clicks on the base components to toggle their popups.
    // This mirrors the playground behaviour: clicking a small circle opens the associated popup.
    // Forward clicks that land on the playground's visual circles (indices 3 and 6)
    // to the playground so its internal popups (`popup3` and `popup6`) can
    // expand/collapse. The playground is rendered underneath native controls,
    // so clicks may be intercepted — forward them explicitly.
    if (playgroundComp)
    {
        bool inAnySmallCircle = false; // track if click is within any small circle disk (excludes idx0)
        // Pattern edit toggle circle (idx2) should still be clickable while in edit mode to exit.
        if (playgroundComp->getCircleCount() > 2)
        {
            float c2x,c2y,c2r; if (playgroundComp->getCircleInfo(2,c2x,c2y,c2r))
            {
                juce::Rectangle<int> r2((int)std::round(c2x - c2r), (int)std::round(c2y - c2r), (int)std::round(c2r * 2.0f), (int)std::round(c2r * 2.0f));
                if (r2.contains(e.getPosition()))
                {
                    bool next = ! patternEditMode;
                    patternEditMode = next;
                    playgroundComp->setInterceptsMouseClicks(!next, !next);
                    playgroundComp->setPatternEditButtonState(next);
                    if (next)
                        if (auto* psi = dynamic_cast<juce::AudioParameterInt*>(processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramPatternSteps)))
                        {
                            uint16_t m = (uint16_t) juce::jlimit(0, 65535, psi->get());
                            if (playgroundComp)
                                playgroundComp->setPatternBitmask(m);
                            else
                                pattern.setBitmask(m);
                        }
                    repaint(ringArea);
                    return;
                }
                else if (r2.contains(e.getPosition())) inAnySmallCircle = true;
            }
        }
        // Popup3 (idx3)
        if (playgroundComp->getCircleCount() > 3)
        {
            float c3x,c3y,c3r; if (playgroundComp->getCircleInfo(3,c3x,c3y,c3r))
            {
                juce::Rectangle<int> r3((int)std::round(c3x - c3r), (int)std::round(c3y - c3r), (int)std::round(c3r * 2.0f), (int)std::round(c3r * 2.0f));
                if (r3.contains(e.getPosition())) { playgroundComp->togglePopup3(); repaint(); return; }
                else if (r3.contains(e.getPosition())) inAnySmallCircle = true;
            }
        }
        // Trigger circle (idx1)
        if (playgroundComp->getCircleCount() > 1)
        {
            float c1x,c1y,c1r; if (playgroundComp->getCircleInfo(1,c1x,c1y,c1r))
            {
                juce::Rectangle<int> r1((int)std::round(c1x - c1r), (int)std::round(c1y - c1r), (int)std::round(c1r * 2.0f), (int)std::round(c1r * 2.0f));
                if (r1.contains(e.getPosition()))
                {
                    if (patternEditMode)
                    {
                        int logicalStep = juce::jlimit(1,16, relativeStepCached);
                        constexpr int visualRotation = 4;
                        int storedIndex = ((logicalStep - 1) + visualRotation) & 15;
                        uint16_t mask = playgroundComp ? playgroundComp->getPatternBitmask() : pattern.getBitmask();
                        if ((mask & (1u << storedIndex)) == 0)
                        {
                            mask |= (uint16_t)(1u << storedIndex);
                            if (playgroundComp) playgroundComp->setPatternBitmask(mask);
                            else pattern.setStep(storedIndex, true);
                            lastClickedStoredIndex = storedIndex;
                            pushPatternStateToProcessor();
                            repaint(ringArea);
                        }
                        playgroundComp->flashSegmentLogical(logicalStep);
                    }
                    processor.requestTriggerOnce();
                    (void) playgroundComp->handleExternalClickIndex(1);
                    manualTriggerOffsetActive = true;
                    manualTriggerRelativeStepAtTrigger = juce::jlimit(1,16, relativeStepCached);
                    if (selectedResyncStepCached > 0)
                        manualTriggerPlayheadDelta = (selectedResyncStepCached - manualTriggerRelativeStepAtTrigger + 16) % 16;
                    else
                        manualTriggerPlayheadDelta = 0;
                    playgroundComp->flashSegmentLogical(manualTriggerRelativeStepAtTrigger);
                    repaint();
                    return;
                }
                else if (r1.contains(e.getPosition())) inAnySmallCircle = true;
            }
        }
        // Popup6 (idx6) guarded against ring wedge clicks
        if (playgroundComp->getCircleCount() > 6)
        {
            const float outerRGuard = (float) kRingOuterD * 0.5f;
            const float innerRGuard = (float) kRingInnerD * 0.5f;
            const int rcx = ringArea.getCentreX();
            const int rcy = ringArea.getCentreY();
            const float gdx = (float) e.x - (float) rcx;
            const float gdy = (float) e.y - (float) rcy;
            const float gd2 = gdx*gdx + gdy*gdy;
            // Only toggle Run when pointer is well inside the centre (apply margin so near-boundary clicks don't toggle).
            // Reduce run-toggle radius while in pattern edit mode so wedge clicks are not misinterpreted.
            const float innerToggleR = patternEditMode ? (innerRGuard * 0.60f) : (innerRGuard * 0.82f);
            if (gd2 <= innerToggleR * innerToggleR)
            {
                if (patternEditMode) return; // Block run toggle while editing pattern
                if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramRun))
                {
                    if (auto* rp = processor.getAPVTS().getRawParameterValue(ClockSyncAudioProcessor::paramRun))
                    {
                        const bool current = rp->load() > 0.5f;
                        const bool next = ! current;
                        p->beginChangeGesture();
                        p->setValueNotifyingHost(next ? 1.0f : 0.0f);
                        p->endChangeGesture();
                        runParamCached = next;
                        ledPulseTarget = next ? 1.0f : 0.6f;
                        ledAnimator.start();
                        repaint(ringArea);
                    }
                }
                return;
            }
            if (gd2 <= outerRGuard * outerRGuard && gd2 >= innerRGuard * innerRGuard)
            {
                const float angle = std::atan2(gdy, gdx); // -pi..pi
                const float startAt12 = -juce::MathConstants<float>::halfPi;
                const float twoPi = juce::MathConstants<float>::twoPi;
                float rel = angle - startAt12;
                while (rel < 0.0f) rel += twoPi;
                const float slice = twoPi / 16.0f;
                int rawIdx = (int) std::floor(rel / slice); // 0..15
                // Use the raw angular sector as the logical step (0..15) so
                // clicking the top wedge maps to step 1. The PlaygroundComponent
                // already applies the visual rotation when rendering and when
                // scheduling pattern steps, so we should treat the ring sector
                // index as the canonical logical index here.
                int logical0 = rawIdx;
                int step = logical0 + 1; // 1..16
                if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramResyncOffsetStep))
                {
                    const auto& range = p->getNormalisableRange();
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(range.convertTo0to1((float) juce::jlimit(1, 16, step)));
                    p->endChangeGesture();
                }
                processor.notifyResyncOffsetChanged();
                selectedResyncStepCached = step;
                repaint(ringArea.expanded(120, 120));
                return;
            }
        }
    }
    
}

void ClockSyncAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (! patternEditMode || ! patternDragActive) return;
    if (! ringArea.contains(e.getPosition())) return;
    float innerHoleR = (float) kRingInnerD * 0.5f;
    float patternOuterR = innerHoleR - 8.0f;
    float patternInnerR = patternOuterR * 0.775f;
    juce::Point<float> centre((float) ringArea.getCentreX(), (float) ringArea.getCentreY());
    int pw = pattern.hitTest(e.position.toFloat(), centre, patternOuterR, patternInnerR);
    if (pw >= 0 && ! patternDragTouched[pw])
    {
        patternDragTouched[pw] = true;
        if (playgroundComp)
        {
            uint16_t mask = playgroundComp->getPatternBitmask();
            if (patternDragSetState)
                mask |= (uint16_t)(1u << pw);
            else
                mask &= (uint16_t)~(1u << pw);
            playgroundComp->setPatternBitmask(mask);
        }
        else
        {
            pattern.setStep(pw, patternDragSetState);
        }
        lastClickedStoredIndex = pw;
        pushPatternStateToProcessor();
        repaint(ringArea);
    }
}

void ClockSyncAudioProcessorEditor::mouseUp(const juce::MouseEvent& e)
{
    if (patternDragActive)
    {
        patternDragActive = false;
        patternDragSetState = false;
        std::fill(std::begin(patternDragTouched), std::end(patternDragTouched), false);
    }
}

// Map popup3 selection index -> patternBars parameter index (OFF,1,2,4,8,16,32,64)
void ClockSyncAudioProcessorEditor::updatePatternParamFromPopup3(int popupIndex)
{
    // popup order: 64(0),32(1),16(2),8(3),4(4),2(5),1(6),OFF(7)
    static const int map[8] = { 7,6,5,4,3,2,1,0 };
    if (popupIndex < 0 || popupIndex > 7) return;
    int paramIdx = map[popupIndex];
    if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramPatternBars))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1((float) paramIdx));
        p->endChangeGesture();
        // Persist the popup3 index (visual) so it can be restored even if the playground
        // visual state isn't created before parameters are applied by the host.
        int popupIndex = 8 - paramIdx;
        processor.getAPVTS().state.setProperty("ui.popup3Index", popupIndex, nullptr);
    }
}

void ClockSyncAudioProcessorEditor::pushPatternStateToProcessor()
{
    uint16_t mask = pattern.getBitmask();
    if (playgroundComp)
        mask = playgroundComp->getPatternBitmask();
    if (auto* p = processor.getAPVTS().getParameter(ClockSyncAudioProcessor::paramPatternSteps))
    {
        float norm = p->getNormalisableRange().convertTo0to1((float) mask);
        p->setValueNotifyingHost(norm);
    }
}

// PlaygroundComponent ring & animations are authoritative.

void ClockSyncAudioProcessorEditor::refreshDeviceList()
{
    midiOutputs.clear();
    auto arr = juce::MidiOutput::getAvailableDevices();
    for (auto& d : arr) midiOutputs.push_back(d);

    // Cache MIDI input devices for the "system settings" (gear) overlay.
    // We intentionally do this here (explicit refresh / editor init) and
    // avoid enumerating inputs when the overlay is opened while playing.
    remoteMidiInputs.clear();
    {
        auto inArr = juce::MidiInput::getAvailableDevices();
        remoteMidiInputs.reserve((size_t) inArr.size());
        for (const auto& d : inArr)
            remoteMidiInputs.push_back(d);
    }

    // If the overlay is currently open, update its combobox list from the cache.
    if (setupOverlayComp)
        if (auto* overlay = dynamic_cast<MidiRemoteSetupComponent*>(setupOverlayComp.get()))
            overlay->setRemoteDevices(remoteMidiInputs);

    deviceBox.clear(juce::dontSendNotification);
    int idx = 1;
    // First item acts as a placeholder; selection id 1 corresponds to 'no external port selected'.
    deviceBox.addItem("select midi-out...", idx++);
    int selectionToSet = 1;
    const auto currentId = processor.getExternalDeviceId();
    for (const auto& d : midiOutputs)
    {
        deviceBox.addItem(d.name, idx);
        if (currentId.isNotEmpty() && d.identifier == currentId)
            selectionToSet = idx;
        ++idx;
    }
    deviceBox.setSelectedId(selectionToSet, juce::dontSendNotification);
}

// Instrument name persistence: stored in APVTS state under property "instrumentNames"
void ClockSyncAudioProcessorEditor::loadInstrumentNamesFromState()
{
    instrumentNames.clear();
    auto& st = processor.getAPVTS().state;
    juce::var v = st.getProperty("instrumentNames", juce::var());
    if (v.isString())
    {
        juce::String s = v.toString();
        instrumentNames.addLines(s);
        instrumentNames.removeEmptyStrings(true);
    }
}

// Restored instrument name persistence helpers.
void ClockSyncAudioProcessorEditor::saveInstrumentNamesToState()
{
    juce::String s = instrumentNames.joinIntoString("\n");
    processor.getAPVTS().state.setProperty("instrumentNames", s, nullptr);
}

void ClockSyncAudioProcessorEditor::populateNameBox()
{
    nameBox.clear(juce::dontSendNotification);
    int id = 1;
    for (auto& n : instrumentNames)
        nameBox.addItem(n, id++);
    
    // Retrieve stored selection
    int storedSel = (int) processor.getAPVTS().state.getProperty("ui.selectedInstrument", 0);

    // preserve selection if possible; when there are no names show placeholder text
    if (instrumentNames.size() > 0)
    {
        // Prefer stored selection if valid
        if (storedSel >= 1 && storedSel <= instrumentNames.size())
            nameBox.setSelectedId(storedSel, juce::dontSendNotification);
        else
            nameBox.setSelectedId(1, juce::dontSendNotification);

        nameBox.setTextWhenNothingSelected(juce::String());
    }
    else
    {
        // Show actionable hint when empty
        nameBox.setSelectedId(0, juce::dontSendNotification);
        nameBox.setTextWhenNothingSelected("name...");
    }
    // Ensure the displayed text uses the cyan accent via overlay colour and is centred
    nameBox.setColour(FullWidthComboBox::overlayTextColourId, processor.theme.cyan());
    nameBox.setJustificationType(juce::Justification::centred);
    nameBox.toFront(true);
    // Ensure any internal editor uses the accent cyan, but always hide any
    // internal Label children so `FullWidthComboBox::paint()` is the single
    // source of visible text (prevents doubled rendering).
    for (auto* c : nameBox.getChildren())
    {
        if (auto* te = dynamic_cast<juce::TextEditor*>(c))
        {
            te->setColour(juce::TextEditor::textColourId, processor.theme.cyan());
            te->setBounds(nameBox.getLocalBounds().reduced(6, 0));
        }
        else if (auto* lb = dynamic_cast<juce::Label*>(c))
        {
            lb->setVisible(false);
            lb->toBack();
            lb->setJustificationType(juce::Justification::centred);
        }
    }
    // Additionally hide any sibling Label components that overlap the nameBox
    // — some hosts or look-and-feel implementations may create separate labels
    // that draw the selected text; hide them to avoid doubled rendering.
    if (auto* p = nameBox.getParentComponent())
    {
        for (auto* sc : p->getChildren())
        {
            if (sc == &nameBox) continue;
            if (auto* lb = dynamic_cast<juce::Label*>(sc))
            {
                if (lb->isVisible() && lb->getBounds().intersects(nameBox.getBounds()))
                {
                    lb->setVisible(false);
                    lb->toBack();
                }
            }
        }
    }
}

void ClockSyncAudioProcessorEditor::showNewNameDialog()
{
    if (nameEntryEditor) return;
    showInlineNameEditor({}, 0);
}

void ClockSyncAudioProcessorEditor::toggleNameEditorOrCommit()
{
    if (nameEntryEditor)
    {
        juce::String name = nameEntryEditor->getText().trim();
        if (name.isNotEmpty())
        {
            if (! instrumentNames.contains(name))
                instrumentNames.add(name);
            saveInstrumentNamesToState();
            populateNameBox();
            nameBox.setSelectedId(instrumentNames.indexOf(name) + 1, juce::dontSendNotification);
        }
        else
        {
            if (nameEntryEditingId >= 1 && nameEntryEditingId <= instrumentNames.size())
            {
                instrumentNames.remove(nameEntryEditingId - 1);
                saveInstrumentNamesToState();
            }

            processor.getAPVTS().state.setProperty("ui.selectedInstrument", 0, nullptr);
            populateNameBox();
        }

        nameEntryEditingId = 0;
        nameEntryEditor.reset();
        repaint();
        return;
    }

    // open editor for current selection
    const int sel = nameBox.getSelectedId();
    juce::String cur;
    if (sel >= 1 && sel <= instrumentNames.size()) cur = instrumentNames[(size_t) (sel - 1)];
    showInlineNameEditor(cur, sel);
}

void ClockSyncAudioProcessorEditor::showInlineNameEditor(const juce::String& initialText, int editingId)
{
    if (nameEntryEditor)
        return;

    nameEntryEditingId = editingId;
    nameEntryEditor = std::make_unique<juce::TextEditor>();
    nameEntryEditor->setText(initialText, juce::dontSendNotification);
    nameEntryEditor->setJustification(juce::Justification::centred);
    nameEntryEditor->setBorder(juce::BorderSize<int>(1));
    nameEntryEditor->setIndents(4, 0);
    nameEntryEditor->setColour(juce::TextEditor::textColourId, processor.theme.cyan());
    nameEntryEditor->setColour(juce::TextEditor::backgroundColourId, processor.theme.fixedBase());
    nameEntryEditor->setColour(juce::TextEditor::outlineColourId, processor.theme.accent());
    nameEntryEditor->setColour(juce::TextEditor::focusedOutlineColourId, processor.theme.cyan());
    nameEntryEditor->setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    nameEntryEditor->setColour(juce::TextEditor::highlightColourId, processor.theme.accent().withAlpha(0.35f));
    nameEntryEditor->setColour(juce::TextEditor::highlightedTextColourId, processor.theme.fixedBase());
    nameEntryEditor->setColour(juce::CaretComponent::caretColourId, processor.theme.cyan());

    if (auto* top = getTopLevelComponent())
    {
        top->addAndMakeVisible(*nameEntryEditor);
    }
    else
    {
        addAndMakeVisible(*nameEntryEditor);
    }

    updateInlineNameEditorBounds();

    nameEntryEditor->grabKeyboardFocus();
    nameEntryEditor->toFront(true);
    nameEntryEditor->selectAll();

    const auto commitNameEdit = [this]()
    {
        if (! nameEntryEditor)
            return;

        juce::String name = nameEntryEditor->getText().trim();
        if (name.isNotEmpty())
        {
            if (nameEntryEditingId >= 1 && nameEntryEditingId <= instrumentNames.size())
            {
                const int existingIndex = instrumentNames.indexOf(name);

                if (existingIndex < 0 || existingIndex == nameEntryEditingId - 1)
                    instrumentNames.set(nameEntryEditingId - 1, name);
            }
            else if (! instrumentNames.contains(name))
            {
                instrumentNames.add(name);
            }

            saveInstrumentNamesToState();
            populateNameBox();
            nameBox.setSelectedId(instrumentNames.indexOf(name) + 1, juce::dontSendNotification);
        }
        else
        {
            if (nameEntryEditingId >= 1 && nameEntryEditingId <= instrumentNames.size())
            {
                instrumentNames.remove(nameEntryEditingId - 1);
                saveInstrumentNamesToState();
            }

            processor.getAPVTS().state.setProperty("ui.selectedInstrument", 0, nullptr);
            populateNameBox();
        }

        nameEntryEditingId = 0;
        nameEntryEditor.reset();
        repaint();
    };

    nameEntryEditor->onReturnKey = commitNameEdit;

    nameEntryEditor->onEscapeKey = [this]()
    {
        nameEntryEditingId = 0;
        nameEntryEditor.reset();
        repaint();
    };

    nameEntryEditor->onFocusLost = commitNameEdit;
}

void ClockSyncAudioProcessorEditor::updateInlineNameEditorBounds()
{
    if (! nameEntryEditor)
        return;

    const float scaledFontHeight = 13.0f * (float) uiScale;
    const auto editorFont = juce::Font(juce::FontOptions("Arial", scaledFontHeight, juce::Font::bold));
    nameEntryEditor->setFont(editorFont);
    nameEntryEditor->applyFontToAllText(editorFont);
    nameEntryEditor->setColour(juce::TextEditor::textColourId, processor.theme.cyan());
    nameEntryEditor->setColour(juce::CaretComponent::caretColourId, processor.theme.cyan());

    if (auto* top = getTopLevelComponent())
    {
        auto editorBounds = top->getLocalArea(&nameBox, nameBox.getLocalBounds().reduced(1, 1));
        nameEntryEditor->setBounds(editorBounds);
    }
    else
    {
        nameEntryEditor->setBounds(nameBox.getBounds().reduced(1, 1));
    }
}

// Dancer frame loading no longer used.



#pragma once

#include "Page.h"

namespace snag
{

//==============================================================================
/** The built-in manual: docs/MANUAL.md (compiled into the plug-in), split into chapters.
    Only the small subset of Markdown the manual uses is understood - see parse(). */
namespace manual
{
    struct Block
    {
        enum class Type { heading, paragraph, bullet, step, note, tableHead, tableRow };
        Type type = Type::paragraph;
        juce::String text;       // may hold **bold**, *emphasis* and `code`
        juce::String text2;      // second cell of a table row
        int number = 0;          // step number
        juce::String copyText;   // a long `command` in the text that gets a COPY button
    };

    struct Chapter
    {
        juce::String title;
        std::vector<Block> blocks;
    };

    /** "## " starts a chapter, "### " a heading, "- " a bullet, "1. " a step, "> " a note,
        "| a | b |" a table row. Images and the "# " document title are skipped. */
    std::vector<Chapter> parse (const juce::String& markdown);

    /** The manual that ships inside Sample Snagger. */
    const std::vector<Chapter>& chapters();

    /** **bold**, *emphasis* and `code` as coloured runs. */
    juce::AttributedString styled (const juce::String& text, float size, juce::Colour base);

    /** The same text without the markup. */
    juce::String plain (const juce::String& text);
}

//==============================================================================
/** Overlay with the manual: chapters on the left, the open chapter on the right. */
class ManualPanel : public juce::Component
{
public:
    explicit ManualPanel (EditorContext&);
    ~ManualPanel() override;

    void open();
    void close();

    int getNumChapters() const;
    int getChapter() const noexcept { return chapter; }
    void showChapter (int index);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class ChapterList;
    class Body;

    void layoutBody();

    EditorContext& ctx;
    juce::Rectangle<int> card, listArea, bodyArea;
    IconButton closeBtn { "close", theme::icons::close(), {}, "icon" };
    juce::TextButton onlineBtn { "VIEW ONLINE" };
    std::unique_ptr<ChapterList> list;
    std::unique_ptr<Body> body;
    juce::Viewport viewport;
    int chapter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ManualPanel)
};

} // namespace snag

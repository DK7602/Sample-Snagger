#include "ManualPanel.h"
#include "SnaggerBinaryData.h"
#include "../PluginProcessor.h"

namespace snag
{
using namespace theme;

//==============================================================================
namespace manual
{

static bool isTableRule (const juce::StringArray& cells)
{
    for (auto& c : cells)
        if (c.isEmpty() || ! c.containsOnly ("-:"))
            return false;
    return ! cells.isEmpty();
}

/** "12. text" -> 12 (and the text), otherwise 0. */
static int stepNumber (const juce::String& line, juce::String& rest)
{
    const int dot = line.indexOf (". ");
    if (dot < 1 || dot > 2 || ! line.substring (0, dot).containsOnly ("0123456789"))
        return 0;
    rest = line.substring (dot + 2).trim();
    return line.substring (0, dot).getIntValue();
}

/** The longest `code` run, if it is long enough to be a command worth copying. */
static juce::String longCommand (const juce::String& text)
{
    juce::String best;
    auto parts = juce::StringArray::fromTokens (text, "`", "");
    for (int i = 1; i < parts.size(); i += 2)
        if (parts[i].length() > best.length())
            best = parts[i];
    return best.length() >= 40 ? best : juce::String();
}

std::vector<Chapter> parse (const juce::String& markdown)
{
    std::vector<Chapter> out;
    Block cur;
    bool open = false;

    auto push = [&out] (Block b)
    {
        if (out.empty() || b.text.isEmpty())
            return;                                   // the intro above the first chapter isn't shown
        if (b.type == Block::Type::note)
            b.copyText = longCommand (b.text);
        out.back().blocks.push_back (std::move (b));
    };
    auto flush = [&]
    {
        if (open) push (cur);
        open = false;
        cur = {};
    };
    auto start = [&] (Block::Type type, const juce::String& text, int number = 0)
    {
        flush();
        cur.type = type;
        cur.text = text;
        cur.number = number;
        open = true;
    };

    juce::StringArray lines;
    lines.addLines (markdown);
    for (auto& raw : lines)
    {
        const auto line = raw.trim();
        juce::String rest;

        if (line.isEmpty())                       { flush(); }
        else if (line.startsWith ("## "))         { flush(); out.push_back ({ line.substring (3).trim(), {} }); }
        else if (line.startsWith ("### "))        { start (Block::Type::heading, line.substring (4).trim()); flush(); }
        else if (line.startsWith ("# "))          { flush(); }
        else if (line.startsWith ("!["))          { flush(); }
        else if (line.startsWith ("|"))
        {
            flush();
            auto cells = juce::StringArray::fromTokens (line.substring (1).upToLastOccurrenceOf ("|", false, false), "|", "");
            cells.trim();
            if (isTableRule (cells))
            {
                // the row above the |---|---| rule is the table's heading
                if (! out.empty() && ! out.back().blocks.empty() && out.back().blocks.back().type == Block::Type::tableRow)
                    out.back().blocks.back().type = Block::Type::tableHead;
            }
            else if (! out.empty())
            {
                Block b;
                b.type = Block::Type::tableRow;
                b.text = cells[0];
                cells.remove (0);
                b.text2 = cells.joinIntoString ("  ");
                out.back().blocks.push_back (std::move (b));   // (a cell may be empty)
            }
        }
        else if (line.startsWith ("> "))
        {
            if (open && cur.type == Block::Type::note) cur.text << " " << line.substring (2).trim();
            else start (Block::Type::note, line.substring (2).trim());
        }
        else if (line.startsWith ("- "))          { start (Block::Type::bullet, line.substring (2).trim()); }
        else if (const int n = stepNumber (line, rest); n > 0) { start (Block::Type::step, rest, n); }
        else if (open)                            { cur.text << " " << line; }
        else                                      { start (Block::Type::paragraph, line); }
    }
    flush();
    return out;
}

const std::vector<Chapter>& chapters()
{
    static const auto all = parse (juce::String::fromUTF8 (SnaggerBinary::MANUAL_md, SnaggerBinary::MANUAL_mdSize));
    return all;
}

juce::AttributedString styled (const juce::String& text, float size, juce::Colour base)
{
    juce::AttributedString as;
    as.setWordWrap (juce::AttributedString::byWord);
    as.setLineSpacing (size * 0.3f);

    bool bold = false, em = false, code = false;
    juce::String run;
    auto emit = [&]
    {
        if (run.isEmpty()) return;
        if (code)      as.append (run, mono (size - 0.5f), col::goldLight);
        else if (bold) as.append (run, ui (size, true), col::goldPale);
        else           as.append (run, ui (size), em ? col::goldPale : base);
        run.clear();
    };

    for (auto p = text.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        if (c == '`')                { emit(); code = ! code; }
        else if (c == '*' && ! code)
        {
            emit();
            if (*p == '*') { ++p; bold = ! bold; }
            else           em = ! em;
        }
        else
            run << juce::String::charToString (c);
    }
    emit();
    return as;
}

juce::String plain (const juce::String& text)
{
    return styled (text, 12.0f, juce::Colours::white).getText();
}

} // namespace manual

//==============================================================================
class ManualPanel::ChapterList : public juce::Component
{
public:
    ChapterList() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    std::function<void (int)> onSelect;
    void setSelected (int i) { selected = i; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto& all = manual::chapters();
        for (int i = 0; i < (int) all.size(); ++i)
        {
            auto r = row (i).toFloat().reduced (0.0f, 1.0f);
            const bool on = i == selected;
            if (on)
            {
                glassWell (g, r, 6.0f, true);
                g.setColour (col::red);
                g.fillRoundedRectangle (r.withWidth (3.0f).reduced (0.0f, 6.0f), 1.5f);
            }
            g.setColour (on ? col::goldLight : (i == hover ? col::goldPale : col::text.withAlpha (0.7f)));
            g.setFont (ui (12.5f, on));
            g.drawFittedText (all[(size_t) i].title, r.withTrimmedLeft (13.0f).withTrimmedRight (6.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.8f);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override  { setHover (rowAt (e.y)); }
    void mouseExit (const juce::MouseEvent&) override    { setHover (-1); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = rowAt (e.y);
        if (i >= 0 && onSelect) onSelect (i);
    }

private:
    int rowHeight() const
    {
        return juce::jlimit (18, 32, getHeight() / juce::jmax (1, (int) manual::chapters().size()));
    }
    juce::Rectangle<int> row (int i) const   { return { 0, i * rowHeight(), getWidth(), rowHeight() }; }
    int rowAt (int y) const
    {
        const int i = y / rowHeight();
        return y >= 0 && i < (int) manual::chapters().size() ? i : -1;
    }
    void setHover (int i) { if (i != hover) { hover = i; repaint(); } }

    int selected = 0, hover = -1;
};

//==============================================================================
/** One chapter, laid out to a width: text, lists, notes and tables. */
class ManualPanel::Body : public juce::Component
{
public:
    Body()
    {
        nextBtn.onClick = [this] { if (onNext) onNext(); };
        addChildComponent (nextBtn);
    }

    std::function<void (const juce::String&)> onCopy;
    std::function<void()> onNext;

    void setChapter (const manual::Chapter* c, const juce::String& nextTitle)
    {
        chapter = c;
        nextBtn.setButtonText (nextTitle.isNotEmpty() ? "NEXT:  " + nextTitle.toUpperCase() : juce::String());
        nextBtn.setVisible (nextTitle.isNotEmpty());

        copyButtons.clear();
        if (chapter != nullptr)
            for (auto& b : chapter->blocks)
                if (b.copyText.isNotEmpty())
                {
                    auto* btn = copyButtons.add (new juce::TextButton ("COPY"));
                    setStyle (*btn, "chip");
                    btn->setTooltip ("Copy this command");
                    btn->onClick = [this, text = b.copyText] { if (onCopy) onCopy (text); };
                    addAndMakeVisible (btn);
                }
    }

    /** Lays the chapter out for this width and returns the height it needs. */
    int layout (int widthIn)
    {
        using Type = manual::Block::Type;
        items.clear();
        const float x = 4.0f, w = (float) juce::jmax (120, widthIn - 8);
        float y = 56.0f;   // under the chapter title
        int copyIndex = 0;

        if (chapter == nullptr)
            return 0;

        const float bodySize = 13.5f;
        auto fit = [] (juce::TextLayout& tl, const juce::String& text, float size, juce::Colour c, float width)
        {
            tl.createLayout (manual::styled (text, size, c), juce::jmax (40.0f, width));
            return tl.getHeight();
        };

        const auto& blocks = chapter->blocks;
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            const auto& b = blocks[i];
            const auto next = i + 1 < blocks.size() ? blocks[i + 1].type : Type::paragraph;
            Item it;
            it.block = &b;

            switch (b.type)
            {
                case Type::heading:
                    if (i > 0) y += 12.0f;
                    it.area = { x, y, w, 24.0f };
                    y += 32.0f;
                    break;

                case Type::paragraph:
                    it.area = { x, y, w, fit (it.a, b.text, bodySize, col::text, w) };
                    y += it.area.getHeight() + 11.0f;
                    break;

                case Type::bullet:
                case Type::step:
                {
                    const float indent = b.type == Type::step ? 28.0f : 20.0f;
                    it.area = { x, y, w, fit (it.a, b.text, bodySize, col::text, w - indent) };
                    it.indent = indent;
                    y += it.area.getHeight() + (next == b.type ? 7.0f : 13.0f);
                    break;
                }

                case Type::note:
                {
                    const bool hasCopy = b.copyText.isNotEmpty();
                    const float textW = w - 30.0f - (hasCopy ? 70.0f : 0.0f);
                    const float h = juce::jmax (fit (it.a, b.text, 13.0f, col::goldPale, textW), hasCopy ? 26.0f : 0.0f) + 22.0f;
                    y += 3.0f;
                    it.area = { x, y, w, h };
                    if (hasCopy)
                        if (auto* btn = copyButtons[copyIndex++])
                            btn->setBounds (juce::Rectangle<float> (it.area.getRight() - 72.0f, y + 11.0f, 60.0f, 26.0f).toNearestInt());
                    y += h + 14.0f;
                    break;
                }

                case Type::tableHead:
                case Type::tableRow:
                {
                    it.col1 = juce::jlimit (120.0f, 250.0f, w * 0.3f);
                    const bool head = b.type == Type::tableHead;
                    const float size = head ? 10.5f : 13.0f;
                    const float h1 = head ? 14.0f : fit (it.a, "**" + manual::plain (b.text) + "**", size, col::goldPale, it.col1 - 14.0f);
                    const float h2 = head ? 14.0f : fit (it.b, b.text2, size, col::text, w - it.col1 - 8.0f);
                    it.area = { x, y, w, juce::jmax (h1, h2) + (head ? 10.0f : 15.0f) };
                    y += it.area.getHeight();
                    if (next != Type::tableRow) y += 16.0f;
                    break;
                }
            }
            items.push_back (std::move (it));
        }

        if (nextBtn.isVisible())
        {
            y += 8.0f;
            nextBtn.setBounds ((int) x, (int) y, juce::jmin ((int) w, 340), 34);
            y += 34.0f;
        }
        return (int) std::ceil (y + 18.0f);
    }

    void paint (juce::Graphics& g) override
    {
        using Type = manual::Block::Type;
        if (chapter == nullptr)
            return;

        auto titleRow = juce::Rectangle<float> (4.0f, 2.0f, (float) getWidth() - 8.0f, 34.0f);
        goldText (g, chapter->title, titleRow, display (24.0f, true));
        g.setColour (col::red);
        g.fillRect (juce::Rectangle<float> (titleRow.getX(), titleRow.getBottom() + 3.0f, 42.0f, 2.0f));

        for (auto& it : items)
        {
            const auto& b = *it.block;
            auto r = it.area;
            switch (b.type)
            {
                case Type::heading:
                    g.setColour (col::goldLight);
                    g.setFont (display (17.0f, true));
                    g.drawText (b.text, r, juce::Justification::centredLeft, true);
                    break;

                case Type::paragraph:
                    it.a.draw (g, r);
                    break;

                case Type::bullet:
                    g.setColour (col::gold);
                    g.fillEllipse (r.getX() + 6.0f, r.getY() + 6.5f, 5.0f, 5.0f);
                    it.a.draw (g, r.withTrimmedLeft (it.indent));
                    break;

                case Type::step:
                    g.setColour (col::goldLight);
                    g.setFont (ui (13.5f, true));
                    g.drawText (juce::String (b.number) + ".", r.withWidth (it.indent - 6.0f).withHeight (18.0f).translated (0.0f, -2.0f),
                                juce::Justification::centredRight, false);
                    it.a.draw (g, r.withTrimmedLeft (it.indent));
                    break;

                case Type::note:
                    glassWell (g, r, 8.0f);
                    g.setColour (col::gold);
                    g.fillRoundedRectangle (r.withWidth (3.0f).reduced (0.0f, 7.0f), 1.5f);
                    it.a.draw (g, r.reduced (0.0f, 11.0f).withTrimmedLeft (16.0f));
                    break;

                case Type::tableHead:
                    g.setColour (col::textDim);
                    g.setFont (ui (10.5f, true).withExtraKerningFactor (0.12f));
                    g.drawText (b.text.toUpperCase(), r.withWidth (it.col1 - 14.0f), juce::Justification::centredLeft, true);
                    g.drawText (b.text2.toUpperCase(), r.withTrimmedLeft (it.col1), juce::Justification::centredLeft, true);
                    g.setColour (col::gold.withAlpha (0.45f));
                    g.fillRect (r.withTop (r.getBottom() - 1.0f));
                    break;

                case Type::tableRow:
                    it.a.draw (g, r.withTrimmedTop (7.0f).withWidth (it.col1 - 14.0f));
                    it.b.draw (g, r.withTrimmedTop (7.0f).withTrimmedLeft (it.col1));
                    g.setColour (col::line);
                    g.fillRect (r.withTop (r.getBottom() - 1.0f));
                    break;
            }
        }
    }

private:
    struct Item
    {
        const manual::Block* block = nullptr;
        juce::TextLayout a, b;
        juce::Rectangle<float> area;
        float indent = 0.0f, col1 = 0.0f;
    };

    const manual::Chapter* chapter = nullptr;
    std::vector<Item> items;
    juce::OwnedArray<juce::TextButton> copyButtons;
    juce::TextButton nextBtn;
};

//==============================================================================
ManualPanel::ManualPanel (EditorContext& c) : ctx (c)
{
    closeBtn.onClick = [this] { close(); };
    addAndMakeVisible (closeBtn);

    setStyle (onlineBtn, "ghost");
    onlineBtn.setTooltip ("Open the manual, with pictures, in your web browser");
    onlineBtn.onClick = [] { juce::URL ("https://github.com/DK7602/Sample-Snagger/blob/main/docs/MANUAL.md").launchInDefaultBrowser(); };
    addAndMakeVisible (onlineBtn);

    list = std::make_unique<ChapterList>();
    list->onSelect = [this] (int i) { showChapter (i); };
    addAndMakeVisible (*list);

    body = std::make_unique<Body>();
    body->onNext = [this] { showChapter (chapter + 1); };
    body->onCopy = [this] (const juce::String& text)
    {
        juce::SystemClipboard::copyTextToClipboard (text);
        ctx.toast ("Copied to the clipboard");
    };
    viewport.setViewedComponent (body.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    setWantsKeyboardFocus (true);
    setVisible (false);
    showChapter ((int) ctx.getProcessor().uiState.getProperty ("manualChapter", 0));
}

ManualPanel::~ManualPanel()
{
    viewport.setViewedComponent (nullptr, false);
}

int ManualPanel::getNumChapters() const { return (int) manual::chapters().size(); }

void ManualPanel::open()
{
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
}

void ManualPanel::close()
{
    if (! isVisible())
        return;
    setVisible (false);
    if (auto* parent = getParentComponent())
        parent->grabKeyboardFocus();
}

void ManualPanel::showChapter (int index)
{
    const auto& all = manual::chapters();
    if (all.empty())
        return;

    chapter = juce::jlimit (0, (int) all.size() - 1, index);
    ctx.getProcessor().uiState.setProperty ("manualChapter", chapter, nullptr);
    list->setSelected (chapter);
    body->setChapter (&all[(size_t) chapter], chapter + 1 < (int) all.size() ? all[(size_t) chapter + 1].title : juce::String());
    layoutBody();
    viewport.setViewPosition (0, 0);
    repaint();
}

void ManualPanel::layoutBody()
{
    const int w = juce::jmax (100, viewport.getWidth() - viewport.getScrollBarThickness() - 6);
    body->setSize (w, body->layout (w));
    body->repaint();
}

void ManualPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! card.contains (e.getPosition()))
        close();
}

bool ManualPanel::keyPressed (const juce::KeyPress& k)
{
    const int page = juce::jmax (40, viewport.getHeight() - 60);
    auto scroll = [this] (int dy) { viewport.setViewPosition (0, juce::jmax (0, viewport.getViewPositionY() + dy)); return true; };

    if (k == juce::KeyPress::escapeKey || k == juce::KeyPress::F1Key)    { close(); return true; }
    if (k == juce::KeyPress::downKey)                                    return scroll (48);
    if (k == juce::KeyPress::upKey)                                      return scroll (-48);
    if (k == juce::KeyPress::pageDownKey || k == juce::KeyPress::spaceKey) return scroll (page);
    if (k == juce::KeyPress::pageUpKey)                                  return scroll (-page);
    if (k == juce::KeyPress::homeKey)                                    return scroll (-1000000);
    if (k == juce::KeyPress::endKey)                                     return scroll (1000000);
    if (k == juce::KeyPress::rightKey)                                   { showChapter (chapter + 1); return true; }
    if (k == juce::KeyPress::leftKey)                                    { showChapter (chapter - 1); return true; }
    return false;
}

void ManualPanel::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (juce::jmin (1020, getWidth() - 40), juce::jmin (740, getHeight() - 40));
    auto r = card.reduced (24, 18);
    closeBtn.setBounds (r.getRight() - 30, r.getY(), 30, 30);
    r.removeFromTop (50);

    auto foot = r.removeFromBottom (26);
    onlineBtn.setBounds (foot.removeFromRight (124));
    r.removeFromBottom (10);

    listArea = r.removeFromLeft (juce::jmin (236, r.getWidth() / 3));
    r.removeFromLeft (22);
    bodyArea = r;

    list->setBounds (listArea);
    viewport.setBounds (bodyArea);
    layoutBody();
}

void ManualPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.72f));

    auto c = card.toFloat();
    juce::Path shape; shape.addRoundedRectangle (c, 12.0f);
    neonGlow (g, shape, col::gold, 24.0f, 0.25f);
    glassWindow (g, c, 12.0f, 0.8f, false);
    goldBorder (g, c, 12.0f, 1.2f, 0.9f);

    auto r = card.reduced (24, 18).toFloat();
    auto titleRow = r.removeFromTop (34.0f);
    goldText (g, "Manual", titleRow, display (26.0f, true));
    g.setColour (col::red);
    g.fillRect (juce::Rectangle<float> (titleRow.getX(), titleRow.getBottom() + 2.0f, 42.0f, 2.0f));

    // a fine line between the chapters and the page
    g.setColour (col::line);
    g.fillRect (juce::Rectangle<float> ((float) listArea.getRight() + 10.0f, (float) listArea.getY(), 1.0f, (float) listArea.getHeight()));

    g.setColour (col::textFaint);
    g.setFont (ui (10.5f));
   #ifdef SNAGGER_VERSION
    const juce::String version ("Sample Snagger " SNAGGER_VERSION "  -  ");
   #else
    const juce::String version;
   #endif
    g.drawFittedText (version + "Arrow keys turn the pages and scroll. F1 or Esc closes the manual.",
                      card.reduced (24, 18).removeFromBottom (26).withTrimmedRight (134), juce::Justification::centredLeft, 1);
}

} // namespace snag

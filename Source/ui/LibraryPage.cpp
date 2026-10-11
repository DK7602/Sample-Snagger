#include "LibraryPage.h"
#include "../PluginProcessor.h"
#include "../Actions.h"
#include "../core/AudioFileIO.h"

namespace snag
{
using namespace theme;

//==============================================================================
class LibraryRow : public juce::Component
{
public:
    explicit LibraryRow (LibraryPage& o) : owner (o) {}

    void update (int r, bool sel) { row = r; selected = sel; repaint(); }

    void paint (juce::Graphics& g) override
    {
        if (row < 0 || row >= (int) owner.shown.size()) return;
        auto& e = owner.shown[(size_t) row];
        auto r = getLocalBounds().toFloat().reduced (2.0f, 2.0f);

        if (selected)
        {
            glassWell (g, r, 6.0f, true);
            g.setColour (col::red);
            g.fillRoundedRectangle (r.withWidth (3.0f).reduced (0.0f, 8.0f).translated (1.0f, 0.0f), 1.5f);
        }
        else if (isMouseOver())
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (r, 6.0f);
        }
        else if (row % 2 == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.025f));
            g.fillRoundedRectangle (r, 6.0f);
        }

        auto content = r.reduced (12.0f, 0.0f);
        auto icon = icons::wave();
        auto iconArea = content.removeFromLeft (18.0f).withSizeKeepingCentre (16.0f, 16.0f);
        icon.applyTransform (icon.getTransformToScaleToFit (iconArea, true));
        g.setColour (selected ? col::red : col::gold);
        g.fillPath (icon);
        content.removeFromLeft (12.0f);

        auto date = content.removeFromRight (150.0f);
        auto fmt  = content.removeFromRight (120.0f);
        auto len  = content.removeFromRight (80.0f);

        g.setColour (selected ? col::goldLight : col::text);
        g.setFont (ui (13.0f, selected));
        g.drawText (e.file.getFileNameWithoutExtension(), content, juce::Justification::centredLeft, true);

        g.setFont (ui (11.0f, true));
        g.setColour (col::goldPale);
        if (! e.isMidi)
            g.drawText (formatTime (e.seconds, true), len, juce::Justification::centredRight);
        g.setColour (col::textDim);
        g.drawText (e.isMidi ? juce::String ("MIDI NOTES")
                             : e.file.getFileExtension().substring (1).toUpperCase() + "  " + juce::String (e.sampleRate / 1000.0, 1) + "k  "
                                   + (e.channels > 1 ? "ST" : "MONO"), fmt, juce::Justification::centredRight);
        g.drawText (e.modified.formatted ("%d %b %Y  %H:%M"), date, juce::Justification::centredRight);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragged = false;
        if (e.mods.isPopupMenu())
        {
            owner.list.selectRow (row);
            owner.showRowMenu (row);
            return;
        }
        owner.list.selectRow (row);
        owner.previewRow (row);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override { owner.importRow (row); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragged || e.getDistanceFromDragStart() < 6 || row < 0 || row >= (int) owner.shown.size())
            return;
        dragged = true;
        DragHandle::startExternalDrag (*this, owner.shown[(size_t) row].file);
    }

private:
    LibraryPage& owner;
    int row = -1;
    bool selected = false, dragged = false;
};

//==============================================================================
LibraryPage::LibraryPage (EditorContext& c) : ctx (c), proc (c.getProcessor())
{
    folderLabel.setFont (ui (12.0f, true));
    folderLabel.setColour (juce::Label::textColourId, col::goldPale);
    folderLabel.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (folderLabel);

    countLabel.setFont (ui (11.0f, true));
    countLabel.setColour (juce::Label::textColourId, col::textDim);
    countLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (countLabel);

    search.setTextToShowWhenEmpty ("Search samples...", col::textFaint);
    search.setFont (ui (13.0f));
    search.setIndents (12, 0);
    search.setJustification (juce::Justification::centredLeft);
    search.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible (search);

    changeBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose your sample library folder", proc.getSettings().getLibraryDir());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f.isDirectory())
            {
                proc.getSettings().setLibraryDir (f);
                refresh();
            }
        });
    };
    revealBtn.onClick = [this] { proc.getSettings().getLibraryDir().revealToUser(); };
    refreshBtn.setTooltip ("Rescan the folder");
    refreshBtn.onClick = [this] { refresh(); };
    addAndMakeVisible (changeBtn);
    addAndMakeVisible (revealBtn);
    addAndMakeVisible (refreshBtn);

    list.setRowHeight (40);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);

    // (the folder is read when the page is first shown - not every time the plug-in window opens)
    folderLabel.setText (proc.getSettings().getLibraryDir().getFullPathName(), juce::dontSendNotification);
}

LibraryPage::~LibraryPage()
{
    ++*scanGeneration;   // tells a scan that is still running to stop; the pool then waits for it
}

std::vector<LibraryPage::Entry> LibraryPage::scanFolder (const juce::File& dir, const std::function<bool()>& keepGoing, bool& hitLimit)
{
    hitLimit = false;
    std::vector<Entry> found;

    // Every file is looked at by name here (the wildcard search is case-sensitive on Linux, so
    // "KICK.WAV" was missing), hidden files are skipped (the "._name.wav" twins macOS leaves on
    // USB drives), and a link that leads back to a parent folder is not followed round in circles.
    static const juce::StringArray audioTypes { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg" };
    for (const auto& entry : juce::RangedDirectoryIterator (dir, true, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles,
                                                            juce::File::FollowSymlinks::noCycles))
    {
        if (keepGoing && ! keepGoing())
            return {};
        const auto f = entry.getFile();
        const auto ext = f.getFileExtension().toLowerCase();
        const bool midi = ext == ".mid" || ext == ".midi";
        if (f.getFileName().startsWith ("._") || ! (midi || audioTypes.contains (ext)))
            continue;
        if ((int) found.size() >= maxEntries)
        {
            hitLimit = true;
            break;
        }
        Entry e;
        e.file = f;
        e.isMidi = midi;
        e.modified = entry.getModificationTime();
        found.push_back (e);
    }

    std::sort (found.begin(), found.end(), [] (const Entry& a, const Entry& b) { return a.modified > b.modified; });

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    for (auto& e : found)
    {
        if (keepGoing && ! keepGoing())
            return {};
        if (e.isMidi)
            continue;
        if (std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor (e.file) })
        {
            e.sampleRate = r->sampleRate;
            e.channels = (int) r->numChannels;
            e.seconds = r->sampleRate > 0 ? (double) r->lengthInSamples / r->sampleRate : 0.0;
        }
    }
    return found;
}

void LibraryPage::refresh()
{
    auto dir = proc.getSettings().getLibraryDir();
    folderLabel.setText (dir.getFullPathName(), juce::dontSendNotification);

    const int generation = ++*scanGeneration;
    scanning = true;
    countLabel.setText (all.empty() ? "LOOKING..." : countLabel.getText(), juce::dontSendNotification);

    auto current = scanGeneration;
    juce::Component::SafePointer<LibraryPage> safe (this);
    scanner.addJob ([dir, generation, current, safe]
    {
        bool hitLimit = false;
        auto found = scanFolder (dir, [&] { return current->load() == generation; }, hitLimit);
        juce::MessageManager::callAsync ([safe, generation, current, hitLimit, found = std::move (found)]() mutable
        {
            if (safe == nullptr || current->load() != generation)
                return;   // the window went away, or a newer scan is on its way
            safe->all = std::move (found);
            safe->tooMany = hitLimit;
            safe->scanning = false;
            safe->applyFilter();
        });
    });
}

void LibraryPage::applyFilter()
{
    shown.clear();
    const auto q = search.getText().trim();
    for (auto& e : all)
        if (q.isEmpty() || e.file.getFileName().containsIgnoreCase (q))
            shown.push_back (e);
    countLabel.setText (juce::String ((int) shown.size()) + (tooMany && q.isEmpty() ? "+ SAMPLES (FIRST " + juce::String (maxEntries) + ")" : " SAMPLES"),
                        juce::dontSendNotification);
    list.updateContent();
    list.repaint();
    repaint();
}

int LibraryPage::getNumRows() { return (int) shown.size(); }

void LibraryPage::paintListBoxItem (int, juce::Graphics&, int, int, bool) {}

juce::Component* LibraryPage::refreshComponentForRow (int row, bool selected, juce::Component* existing)
{
    auto* r = dynamic_cast<LibraryRow*> (existing);
    if (r == nullptr)
    {
        delete existing;
        r = new LibraryRow (*this);
    }
    r->update (row, selected);
    return r;
}

void LibraryPage::selectedRowsChanged (int) {}

void LibraryPage::previewRow (int row)
{
    if (row < 0 || row >= (int) shown.size()) return;
    auto f = shown[(size_t) row].file;
    if (shown[(size_t) row].isMidi)
    {
        proc.stopPreview();
        ctx.toast ("That's a MIDI file (notes, not sound). Drag it onto a MIDI or instrument track in your DAW.");
        return;
    }
    if (f != previewFile || previewAudio == nullptr)
    {
        if (f.getSize() > 200 * 1024 * 1024) { ctx.toast ("That file is too big to audition here - double-click to load it.", true); return; }
        auto r = audioio::loadFile (f, proc.getTools().getPath (ToolManager::Tool::ffmpeg));
        if (r.audio == nullptr) { ctx.toast (r.error, true); return; }
        previewAudio = r.audio;
        previewFile = f;
    }
    proc.preview ({ previewAudio }, 0, -1, false);
}

void LibraryPage::importRow (int row)
{
    if (row < 0 || row >= (int) shown.size()) return;
    if (shown[(size_t) row].isMidi)
    {
        ctx.toast ("That's a MIDI file. Drag it onto a MIDI or instrument track in your DAW.");
        return;
    }
    actions::importFiles (proc, { shown[(size_t) row].file.getFullPathName() });
}

void LibraryPage::showRowMenu (int row)
{
    if (row < 0 || row >= (int) shown.size()) return;
    auto f = shown[(size_t) row].file;
    const bool midi = shown[(size_t) row].isMidi;
    juce::PopupMenu m;
    m.addItem (1, "Load into tray", ! midi);
    m.addItem (2, "Show in folder");
    m.addSeparator();
    m.addItem (3, "Move to trash");
    // (by file, not by row: the list can have been reloaded while the menu was open)
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe = juce::Component::SafePointer<LibraryPage> (this), f] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1) actions::importFiles (safe->proc, { f.getFullPathName() });
        else if (r == 2) f.revealToUser();
        else if (r == 3)
        {
            if (f.moveToTrash()) { safe->ctx.toast ("Moved " + f.getFileName() + " to the trash"); safe->refresh(); }
            else safe->ctx.toast ("Couldn't delete " + f.getFileName(), true);
        }
    });
}

//==============================================================================
void LibraryPage::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    auto top = r.removeFromTop (46);
    toolbarArea = top;
    top = top.reduced (14, 4);
    refreshBtn.setBounds (top.removeFromRight (34).reduced (3));
    top.removeFromRight (6);
    revealBtn.setBounds (top.removeFromRight (140));
    top.removeFromRight (6);
    changeBtn.setBounds (top.removeFromRight (170));
    top.removeFromRight (14);
    search.setBounds (top.removeFromRight (juce::jmin (300, top.getWidth() / 2)).reduced (0, 3));
    top.removeFromRight (14);
    folderLabel.setBounds (top);

    r.removeFromTop (10);
    listGlass = r;
    r = r.reduced (10, 8);
    auto header = r.removeFromTop (22);
    headerArea = header;
    countLabel.setBounds (header.removeFromRight (160));
    r.removeFromTop (4);
    list.setBounds (r);
}

void LibraryPage::paint (juce::Graphics& g)
{
    glassWindow (g, toolbarArea.toFloat().reduced (3.0f, 1.0f), 9.0f, 0.8f);
    glassWindow (g, listGlass.toFloat().reduced (3.0f), 10.0f, 1.0f);
    auto listArea = list.getBounds().toFloat();

    sectionLabel (g, "Sample library", headerArea.toFloat().withTrimmedLeft (4.0f));

    if (shown.empty())
    {
        auto c = listArea.withSizeKeepingCentre (juce::jmin (560.0f, listArea.getWidth() - 40), 120.0f);
        goldText (g, scanning && all.empty() ? "Reading your library..." : all.empty() ? "Your library is empty" : "No matches", c.removeFromTop (32.0f), display (22.0f, true), juce::Justification::centred);
        g.setColour (col::textDim);
        g.setFont (ui (13.0f));
        g.drawFittedText (all.empty() ? "Hit SAVE in STUDIO or STEMS and your samples land here - a normal folder you can use in any DAW."
                                      : "Try a different search.", c.toNearestInt(), juce::Justification::centredTop, 3);
    }
}

} // namespace snag

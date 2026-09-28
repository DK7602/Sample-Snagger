#pragma once

#include "Page.h"

namespace snag
{

//==============================================================================
/** Your saved samples (a normal folder on disk). Click to audition, double-click to load
    into the tray, drag straight into the DAW. */
class LibraryPage : public juce::Component, private juce::ListBoxModel
{
public:
    explicit LibraryPage (EditorContext&);
    ~LibraryPage() override;

    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
    void visibilityChanged() override { if (isVisible()) refresh(); }

    struct Entry
    {
        juce::File file;
        double seconds = 0;
        double sampleRate = 0;
        int channels = 0;
    };

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void applyFilter();
    void previewRow (int row);
    void importRow (int row);
    void showRowMenu (int row);

    friend class LibraryRow;

    EditorContext& ctx;
    SnaggerProcessor& proc;

    juce::Label folderLabel, countLabel;
    juce::TextEditor search;
    IconButton changeBtn { "change", theme::icons::folder(), "CHANGE FOLDER", "gold" };
    IconButton revealBtn { "reveal", theme::icons::folder(), "OPEN FOLDER", "ghost" };
    IconButton refreshBtn { "refresh", theme::icons::reload(), {}, "icon" };
    juce::ListBox list { "library", this };

    std::vector<Entry> all, shown;
    std::unique_ptr<juce::FileChooser> chooser;
    AudioData::Ptr previewAudio;
    juce::File previewFile;
};

} // namespace snag

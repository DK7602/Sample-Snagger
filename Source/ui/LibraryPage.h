#pragma once

#include "Page.h"
#include <atomic>
#include <memory>

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
    juce::Rectangle<int> toolbarArea, listGlass, headerArea;
    void visibilityChanged() override { if (isVisible()) refresh(); }

    struct Entry
    {
        juce::File file;
        juce::Time modified;
        double seconds = 0;
        double sampleRate = 0;
        int channels = 0;
        bool isMidi = false;    // a saved .mid: listed so it can be found and dragged, not auditioned
    };

    /** Lists a library folder (newest first). Runs on a background thread; `keepGoing` stops it early. */
    static std::vector<Entry> scanFolder (const juce::File& dir, const std::function<bool()>& keepGoing, bool& hitLimit);
    static constexpr int maxEntries = 3000;
    bool isScanning() const noexcept { return scanning; }


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

    // the folder is read on its own thread: a big sample drive must not freeze the window
    std::shared_ptr<std::atomic<int>> scanGeneration = std::make_shared<std::atomic<int>> (0);
    bool scanning = false, tooMany = false;
    juce::ThreadPool scanner { juce::ThreadPoolOptions{}.withThreadName ("Snagger library").withNumberOfThreads (1) };
};

} // namespace snag

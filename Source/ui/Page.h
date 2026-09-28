#pragma once

#include "Widgets.h"

class SnaggerProcessor;

namespace snag
{

enum class Tab { browse = 0, studio, stems, library };

/** What pages can ask of the editor. */
struct EditorContext
{
    virtual ~EditorContext() = default;
    virtual SnaggerProcessor& getProcessor() = 0;
    virtual void toast (const juce::String& message, bool isError = false) = 0;
    virtual void showTab (Tab) = 0;
    virtual void openSettings() = 0;
    virtual void chooseAndImportFiles() = 0;
};

} // namespace snag

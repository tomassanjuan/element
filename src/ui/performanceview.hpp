// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <element/ui/content.hpp>

#define EL_VIEW_PERFORMANCE "PerformanceView"

namespace element {

/** Shows audio engine load and the render time of each node in each loaded graph. */
class PerformanceView : public ContentView
{
public:
    PerformanceView();
    ~PerformanceView();

    void resized() override;
    void initializeView (Services&) override;

private:
    class Content;
    std::unique_ptr<Content> content;
};

} // namespace element

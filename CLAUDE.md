# Copilot Instructions for Element

## General Conventions

- **Always check documentation**: Before making assumptions about APIs, libraries, or tools, consult the official documentation first.
- **Do not make assumptions and guesses**: When uncertain about implementation details, research or ask rather than guessing.
- **Avoid workarounds**: Do things "the right way" by using the proper APIs and intended patterns. Take time to research the correct solution rather than applying quick hacks that create technical debt.
- **KISS (Keep It Simple, Stupid)**: Favor simple, straightforward solutions over complex ones.
- **DRY (Don't Repeat Yourself)**: Avoid code duplication. Extract common functionality into reusable functions or components.

## Code Quality

- Write clear, readable code with descriptive names for variables, functions, and classes.
- Maintain consistency with the existing codebase style and patterns.
- Consider maintainability and future developers who will read the code.
- Format code with `util/format.py`

## Headers & Includes

- **Never `using namespace ...` in a header** (repository-wide rule). It leaks the namespace into every translation unit that includes the header. Fully qualify names (e.g. `juce::Component`) in headers instead. A file-local `using namespace juce;` inside a `.cpp` is fine.
- **Do not include `ElementApp.h` in new source files.** It is a legacy helper that declares `using namespace juce;` at public scope, which the codebase is moving away from. Instead include the specific clean umbrella headers under `element/juce/` (e.g. `<element/juce/gui_basics.hpp>`, `<element/juce/audio_basics.hpp>`) or `<element/juce.hpp>` (which does not pull in the juce namespace).

## Documentation

- Use Doxygen-style comments with `/** ... */` for documenting classes, functions, and methods.
- Include a brief description, parameter documentation with `@param`, and return value documentation with `@return`.
- Document what the function does, not how it does it (implementation details belong in inline comments).
- Example:
  ```cpp
  /** Checks if the application can safely shut down.
      
      Determines whether there are any unsaved changes in the current session
      that would be lost on shutdown.
      
      @return true if there are no pending session changes and shutdown can proceed,
              false if the session has unsaved changes
  */
  static bool canShutdown();
  ```

## Data Models (ValueTree)

- Domain objects (e.g. `Session`, `Node`, `Control`, `Controller`) subclass `Model` and wrap a `juce::ValueTree` (`objectData`). They are lightweight value types — copy them freely; identity lives in the underlying tree.
- Are the data representation of a real audio plugin `Processor`.
- Declare new types and property names in `include/element/tags.hpp`: `EL_TAG(MyType)` for a tree type (under `types::`), and a `juce::Identifier` for each property (under `tags::`). Reuse existing tags rather than introducing string duplicates.
- Use the `EL_MODEL_GETTER`/`EL_MODEL_SETTER` macros for property accessors.
- Give each model a version constant (`#define EL_MYTYPE_VERSION 1`) and a private `setMissingProperties()` that calls `stabilizePropertyString`/`stabilizePropertyPOD` to fill defaults. This same method is the place to do in-place migration of legacy properties (see `Control::setMissingProperties` converting legacy `mappingData`).
- Persistence is just the ValueTree serialized to XML; there is no separate DTO layer.

## Services & Context

- App logic lives in `Service` subclasses (`src/services/`) with `activate()`/`deactivate()` lifecycle hooks. Reach another service from within one via `sibling<OtherService>()`.
- Shared singletons are reached through `context()` — e.g. `context().session()`, `context().mapping()`, `context().midi()`.
- Marshal state changes off the audio/MIDI thread to the message thread with `juce::AsyncUpdater`. Plugin parameter changes must be wrapped in `beginChangeGesture()` / `setValueNotifyingHost()` / `endChangeGesture()`.

## Testing

- Tests use **Boost.Test** and live in `test/` (built into the `test_element` console app). `test/CMakeLists.txt` globs all `*.cpp`, but each suite must ALSO be registered with an explicit `add_test(NAME "MySuite" COMMAND test_element --run_test=MySuite)` line — forgetting this is the usual reason a new test "doesn't run."
- Write suites with `BOOST_AUTO_TEST_SUITE(Name)` / `BOOST_AUTO_TEST_CASE(...)` / `BOOST_AUTO_TEST_SUITE_END()`.
- Standard fixtures: construct a `Context`, then `GraphNode graph(context)` and `graph.addNode(new SomeNode(...))` (see `test/MidiProgramMapTests.cpp`). Reusable nodes live in `test/fixture/` (e.g. `TestNode.h`).
- Build and run:
  ```
  cmake --build build
  ctest --test-dir build --output-on-failure        # or -R MySuite for one suite
  ```
- Prefer designing engine/runtime code so its core logic is callable without real hardware (e.g. a plain `process(...)` method), so it can be unit tested directly.

## Audio Graph Architecture

- **IONodes** (audio/MIDI input/output) require a parent `GraphNode` to be set before ports can be properly initialized.
- `IONode::refreshPorts()` queries the parent graph's port count via `graph->getNumPorts()`. If the parent is null or has zero ports, the IONode will have zero ports.
- When adding IONodes, ensure the parent graph has a valid port count first using `graph->setNumPorts()`.
- Default port counts: 2 channels for audio (stereo), 1 channel for MIDI.
- Message flow for adding nodes: `AddPluginMessage` → `AddPluginAction::perform()` → `EngineService::addPlugin()` → `GraphManager::addNode()`.

## Comments in Code

- Don't write comments when it is obvious what the code is doing.

---

# Local fork notes (Tomas's Windows build of Element 1.2.0)

Everything below is specific to this checkout, not upstream. It is the working context carried over from the session that created these changes.

## Workflow

- Checkout: `C:\Users\Tomas\Desktop\element-ts`, branch `tom-wip`, based on tag `1.2.0` (`09116740`). Windows 11, MSVC 2022 Build Tools, CMake at `C:\Program Files\CMake\bin\cmake.exe` (not on PATH in the Claude shell, use the full path).
- Configure once: `cmake -B build -DELEMENT_ENABLE_ASIO=ON`. ASIO must stay on, the user needs it. Boost headers are in `C:\Users\Tomas\SDKs\include\boost` (Boost 1.87, picked up by the CMake fallback).
- Build: `cmake --build build --config Release --parallel`. Output: `build\element_app_artefacts\Release\element.exe`.
- Build without asking. After every successful build: close the running Element gracefully (`Get-Process element | % CloseMainWindow`, never force kill, the user may have an unsaved session or a save prompt), copy the exe over `C:\Program Files\Kushview\Element\element.exe`, start it again. Do not make backup copies of exes. If Element is still running after ~10 s, tell the user.
- Lingering `MSBuild.exe` worker processes hold the folder locked. Stop them before moving or deleting this folder.
- Format with `util/format.py`. No tests exist for the changes below (all UI/engine glue that needs a live app).
- This file is tracked by upstream. The section you are reading is local (committed in its own commit on this branch), leave that commit out of anything sent upstream.

## What was done (all verified to compile; user-tested items marked)

Committed (`6b4589f8`):
- OSC `/element/graph <int|float index>`: `GuiService::activateGraph(int)` (`ui.hpp`, `guiservice.cpp`) holds the steps the per-graph hotkey used to do (close plugin windows, set active, `EngineService::setRootNode`, show windows). Hotkey code and the new `GraphOSCListener` (`oscservice.cpp`) both call it. User-tested, works. Upstream's `/element/command` handler is a no-op, left alone.

Committed after `6b4589f8` as separate commits on this branch (see `git log`), listed here with their state:
- Minimise to tray with the native title bar: `MainWindow::minimisationStateChanged` (`mainwindow.cpp`), because the native minimise button never reaches `minimiseButtonPressed()`. User-tested, works.
- Tray icon: single and double click restore the hidden window through `restoreMainWindow()` (`systemtray.cpp`). Upstream's `setVisible(true)` did nothing for a window removed from the desktop. User-tested, works.
- Global UI zoom: Ctrl+scroll over the main window, Ctrl+=/+/-/0 (`KeyPressManager` in `guiservice.cpp`, `GuiService::adjustUiScale`). Drives the existing "Desktop scale" setting, range 0.5-3.0, saved. Not user-tested yet. (An earlier graph-editor zoom was reverted, the user meant the global one.)
- Plugin window resize: for hosted `AudioProcessorEditor`s that report `isResizable()`, `PluginWindow` is resizable with a maximise button, and `PluginWindowContent::resized()` stretches the editor to the window (`pluginwindow.cpp`). Root cause: `ResizableWindow::setResizable(false, true)` creates no handle. Not confirmed with FabFilter Q4 yet. If it still fails, ask whether the plugin is VST2 (JUCE reports VST2 as non-resizable) or VST3.
- Graph mixer (`graphmixerview.cpp`): meter bridge column on the right, VU-style meters, toggled with a "VU" button (state is a file-static, not saved across restarts). While it shows, the bottom meter bridge collapses (`StandardContent::setMeterBridgeSuppressed`). The bottom bridge keeps its original segment look.
- Channel strips (`channelstrip.*`, `nodechannelstrip.hpp`): dB readout under the gain (calibrated +3.0103 dB to match `SimpleMeter`), title bar tinted with the node's block colour (`Node::getColor`), clip light above the meter, fader double-click resets to 0 dB, Shift while dragging = 0.4x speed (`FaderSlider`). User-tested the readout and tint.
- Meters (`simplemeter.*`, `cliphold.hpp`): peak hold line turns red when the level goes over 0 dB and clears after 5 s without a clip or on click (`ClipHold`). New Preferences > General "Meter colors": Original, or Standard (green below -6 dB, yellow to 0, red above), stored as `Settings::meterColorScheme` (`SimpleMeter::setColorScheme`).
- Meter bridge (`meterbridge.*`): labels renamed IN/OUT. In the mixer's VU style each meter has a box on top showing the level in dB that turns red on clipping (click to clear).

## Open jobs

- Menu bar (File/Edit) item highlight is missing. Cause not found: the bar is a standard JUCE `MenuBarComponent`, `LookAndFeel_E1::drawMenuBarItem` (`style_v1.cpp`) delegates to V3/V2, and `PopupMenu::highlightedBackgroundColourId` is set to `Colors::elemental` in the E1 constructor. Need from the user: missing on hover, on click, or both. Probably needs a look at the running app.
- Commits are one per feature or bugfix. The intermediate commits were derived from the final files and were not compiled one by one, only the branch tip is known to build. The tip's source is identical to the build that is installed (15:24 build of 2026-11-08).
- Hand-off state: this folder was moved from `C:\Users\Tomas\element`, so `build\` was recreated and re-configured (`-DELEMENT_ENABLE_ASIO=ON`) but no full build has completed here yet. The first build in this folder is a full rebuild of several minutes. Run it, then do the stop/replace/start cycle described under Workflow.
- Status bar drop counter (`Content::StatusBar` in `content.cpp`: `Drops: N` from `AudioDeviceManager::getXRunCount()`, refresh 1 s) is committed and installed but not user-validated. When the user confirms it works: add it to "What was done" as user-tested and commit that CLAUDE.md change. Possible follow-up the user was asked about: memory / working set / page faults / time load like Cantabile's Load & Info (needs Windows process APIs).
- Not yet verified by the user: global zoom, plugin resize (Q4), mixer bridge toggle and bottom-bridge collapse, Meter colors option, strip clip light, IN/OUT level box visibility (the earlier text under the meters was not visible, root cause unknown, replaced by the top box).
- Not done by choice: clip light and level readout on the bottom bridge (user said keep the bottom bridge as it was), persistence of the mixer bridge toggle, unit tests.
- An empty `C:\Users\Tomas\element` may remain from the folder move, it can be deleted.

## User preferences

- Short, plain replies. Ask only when a decision is really theirs, otherwise pick a sensible default and say so. Say clearly what is verified and what is not.

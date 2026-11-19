#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <iostream>
#include <memory>

/** Headless editor snapshot tool (visual review only, not a CTest test).

    Instantiates the processor + editor, simulates ~1.5 s of sustained gain
    reduction so the meter needle sits mid-scale, and writes the editor
    render to build/editor.png (or the path given as argv[1]).

    Run under xvfb-run -a from the repo root:
      xvfb-run -a ./build/glint_snapshot
*/
int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI guiInitialiser;

    GlintAudioProcessor processor;

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr)
    {
        std::cerr << "createEditor() returned nullptr\n";
        return 1;
    }
    // The editor fixes its own faceplate size in its constructor.

    // Drive the meter ballistics with a sustained 6.5 dB reduction so the
    // snapshot shows the needle mid-scale.
    processor.getGainReductionDb().store (6.5f, std::memory_order_relaxed);
    if (auto* ge = dynamic_cast<glint::GlintEditor*> (editor.get()))
        for (int i = 0; i < 45; ++i)
            ge->tickMeter();

    const auto snapshot = editor->createComponentSnapshot (
        { editor->getWidth(), editor->getHeight() }, true);
    if (snapshot.isNull())
    {
        std::cerr << "createComponentSnapshot failed\n";
        return 1;
    }

    const auto path = juce::File::getCurrentWorkingDirectory()
                          .getChildFile (argc > 1 ? argv[1] : "build/editor.png");
    path.getParentDirectory().createDirectory();

    // FileOutputStream appends to an existing file (JUCE opens with O_RDWR,
    // no O_TRUNC) — delete first so the PNG is never a layered palimpsest of
    // older renders.
    path.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream (path.createOutputStream());
    if (stream == nullptr || ! juce::PNGImageFormat().writeImageToStream (snapshot, *stream))
    {
        std::cerr << "Failed to write " << path.getFullPathName() << "\n";
        return 1;
    }

    std::cout << "Wrote " << path.getFullPathName() << "\n";
    return 0;
}

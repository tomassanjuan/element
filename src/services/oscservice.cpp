// Copyright 2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <element/juce/osc.hpp>

#include <element/context.hpp>
#include <element/devices.hpp>
#include <element/settings.hpp>
#include <element/ui.hpp>
#include <element/ui/commands.hpp>

#include "services/oscservice.hpp"

#define ELEMENT_OSC_ADDRESS_COMMAND "/element/command"
#define ELEMENT_OSC_ADDRESS_ENGINE "/element/engine"
#define ELEMENT_OSC_ADDRESS_GRAPH "/element/graph"

using namespace juce;

namespace element {

struct CommandOSCListener final : juce::OSCReceiver::ListenerWithOSCAddress<>
{
    CommandOSCListener (Context& w)
        : world (w)
    {
    }

    void oscMessageReceived (const juce::OSCMessage& message) override
    {
        const auto msg = message[0];
        if (! msg.isString())
            return;

        const auto command = Commands::fromString (msg.getString());
        if (command != Commands::invalid)
        {
            // noop
        }
    }

private:
    [[maybe_unused]] Context& world;
};

//=============================================================================
struct EngineOSCListener final : OSCReceiver::ListenerWithOSCAddress<>
{
    EngineOSCListener (Context& g)
        : globals (g)
    {
    }

    void oscMessageReceived (const juce::OSCMessage& message) override
    {
        const auto slug = message[0];
        if (! slug.isString())
            return;

        if (message.size() >= 2 && slug.getString().toLowerCase().trim() == "samplerate")
            handleSampleRate (message[1]);
    }

private:
    Context& globals;

    void handleSampleRate (const OSCArgument& arg)
    {
        double sampleRate = 0.0;
        switch (arg.getType())
        {
            // see juce_OSCTypes.cpp for other types
            case 'i':
                sampleRate = (double) arg.getInt32();
                break;
            case 'f':
                sampleRate = (double) roundToInt (arg.getFloat32());
                break;
            default:
                break;
        }

        if (sampleRate <= 0.0)
            return;

        auto& devs = globals.devices();
        auto setup = devs.getAudioDeviceSetup();
        if (sampleRate != setup.sampleRate)
        {
            setup.sampleRate = sampleRate;
            devs.setAudioDeviceSetup (setup, true);
        }
    }
};

//=============================================================================
/** Handles `/element/graph <index>`: switches the active session graph. */
struct GraphOSCListener final : OSCReceiver::ListenerWithOSCAddress<>
{
    GraphOSCListener (Context& g)
        : globals (g)
    {
    }

    void oscMessageReceived (const juce::OSCMessage& message) override
    {
        if (message.isEmpty())
            return;

        const auto& arg = message[0];
        int index = -1;
        if (arg.isInt32())
            index = arg.getInt32();
        else if (arg.isFloat32())
            index = roundToInt (arg.getFloat32());
        else
            return;

        // OSC arrives on a network thread; graph changes must run on the message thread.
        MessageManager::callAsync ([this, index]() {
            if (auto* gui = globals.services().find<GuiService>())
                gui->activateGraph (index);
        });
    }

private:
    Context& globals;
};

//=============================================================================
class OSCService::Impl
{
public:
    Impl (OSCService& o)
        : owner (o) {}
    ~Impl() {}

    bool startServer()
    {
        if (isServing())
            return true;
        serving = receiver.connect (serverPort);
        return serving;
    }

    bool stopServer()
    {
        if (! isServing())
            return true;
        const auto result = receiver.disconnect();
        if (result)
            serving = false;
        return result;
    }

    bool isServing() const { return serving; }

    void setServerPort (int newPort)
    {
        if (newPort == serverPort)
            return;
        const auto wasServing = isServing();
        stopServer();
        serverPort = newPort;
        if (wasServing)
            startServer();
    }

    void initialize()
    {
        if (listenersReady == true)
            return;

        application.reset (new CommandOSCListener (owner.context()));
        receiver.addListener (application.get(), ELEMENT_OSC_ADDRESS_COMMAND);

        engine.reset (new EngineOSCListener (owner.context()));
        receiver.addListener (engine.get(), ELEMENT_OSC_ADDRESS_ENGINE);

        graph.reset (new GraphOSCListener (owner.context()));
        receiver.addListener (graph.get(), ELEMENT_OSC_ADDRESS_GRAPH);

        listenersReady = true;
    }

    void shutdown()
    {
        if (! listenersReady)
            return;
        listenersReady = false;

        receiver.removeListener (application.get());
        receiver.removeListener (engine.get());
        receiver.removeListener (graph.get());

        application.reset();
        engine.reset();
        graph.reset();
    }

    int getHostPort() const { return serverPort; }

private:
    OSCService& owner;
    OSCSender sender;
    OSCReceiver receiver { "elosc" };

    bool listenersReady = false;
    bool serving { false };
    int serverPort { 9000 };

    std::unique_ptr<CommandOSCListener> application;
    std::unique_ptr<EngineOSCListener> engine;
    std::unique_ptr<GraphOSCListener> graph;
};

//=============================================================================

OSCService::OSCService()
{
    impl.reset (new Impl (*this));
}

OSCService::~OSCService()
{
    impl.reset();
}

void OSCService::refreshWithSettings (bool alertOnFail)
{
    auto& settings = context().settings();
    impl->stopServer();
    impl->setServerPort (settings.getOscHostPort());

    if (settings.isOscHostEnabled())
    {
        if (! impl->startServer() && alertOnFail)
        {
            String msg = "Could not start OSC host on port ";
            msg << impl->getHostPort();
            AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon,
                                              "OSC Host",
                                              msg);
        }
    }
}

void OSCService::activate()
{
    impl->initialize();
    refreshWithSettings (false);
}

void OSCService::deactivate()
{
    impl->stopServer();
    impl->shutdown();
}

} // namespace element

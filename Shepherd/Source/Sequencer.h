/*
  ==============================================================================

    Sequencer.h
    Created: 10 Jun 2022 12:07:14pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "helpers.h"
#include "MusicalContext.h"
#include "HardwareDevice.h"
#include "SynthAudioSource.h"
#include "Playhead.h"
#include "Clip.h"
#include "Track.h"
#include "server_ws.hpp"


class Sequencer; // Forward declaration

using WsServer = SimpleWeb::SocketServer<SimpleWeb::WS>;


class WebSocketsServer: public juce::Thread
{
public:
   
    WebSocketsServer(): juce::Thread ("ShepherdWebsocketsServer")
    {
    }
   
    ~WebSocketsServer(){
        if (serverPtr != nullptr){
            serverPtr.release();
        }
    }
    
    void setSequencerPointer(Sequencer* _sequencer){
        sequencerPtr = _sequencer;
    }
    
    inline void run();  // Implemented after ServerInterface is defined

    int assignedPort = -1;
    Sequencer* sequencerPtr;
    std::unique_ptr<WsServer> serverPtr;
};


class Sequencer: private juce::Timer,
                 private juce::OSCReceiver,
                 private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>,
                 protected juce::ValueTree::Listener,
                 public juce::ActionBroadcaster

{
public:
    Sequencer();
    ~Sequencer();
    
    void prepareSequencer (int samplesPerBlockExpected, double sampleRate);
    void getNextMIDISlice (const juce::AudioSourceChannelInfo& bufferToFill);
    
    // Some public functions used for testing
    void debugState();
    
    // Public method for receiving WS messages
    void wsMessageReceived  (const juce::String& serializedMessage);
    
    juce::File getDataLocation();
    
protected:
    juce::ValueTree state;
    void bindState();
    
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree& parentTree, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree& parentTree, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree& parentTree, int, int) override;
    void valueTreeParentChanged (juce::ValueTree&) override;

private:
    GlobalSettingsStruct getGlobalSettings();
    
    bool sequencerInitialized = false;
    
    // Save/load
    void loadSession(juce::ValueTree& stateToLoad);
    void loadNewEmptySession(int numTracks, int numScenes);
    void loadSessionFromFile(juce::String filePath);
    bool validateAndUpdateStateToLoad(juce::ValueTree& state);
    void saveCurrentSessionToFile(juce::String filePath);

    // Settings file
    juce::String getPropertyFromSettingsFile(juce::String propertyName);
    
    // Communication with controller
    WebSocketsServer wsServer;
    juce::OSCSender oscSender;
    int oscReceivePort = OSC_BACKEND_RECEIVE_PORT;
    int oscSendPort = OSC_CONRTOLLER_RECEIVE_PORT;
    juce::String oscSendHost = "127.0.0.1";
    bool oscSenderIsConnected = false;
    void initializeWS();
    void initializeOSC();
    juce::String serliaizeOSCMessage(const juce::OSCMessage& message);
    void sendMessageToController(const juce::OSCMessage& message);
    void sendWSMessage(const juce::OSCMessage& message);
    void sendOscMessage (const juce::OSCMessage& message);
    // wsMessageReceived is defined in the public API
    void oscMessageReceived (const juce::OSCMessage& message) override;
    void processMessageFromController (const juce::String action, juce::StringArray parameters);
    int stateUpdateID = 0;
    double lastTimeIsAliveWasSent = 0;
    
    // Midi devices and other midi stuff
    bool midiDeviceAlreadyInitialized(const juce::String& deviceName);
    
    void initializeMIDIInputs();
    bool shouldTryInitializeMidiInputs = false;
    juce::int64 lastTimeMidiInputInitializationAttempted = 0;
    juce::OwnedArray<MidiInputDeviceData> midiInDevices = {};
    MidiInputDeviceData* initializeMidiInputDevice(juce::String deviceName);
    MidiInputDeviceData* getMidiInputDeviceData(juce::String deviceName);
    void clearMidiDeviceInputBuffers();
    void collectorsRetrieveLatestBlockOfMessages(int sliceNumSamples);
    void resetMidiInCollectors(double sampleRate);
    
    void initializeMIDIOutputs();
    bool shouldTryInitializeMidiOutputs = false;
    juce::int64 lastTimeMidiOutputInitializationAttempted = 0;
    juce::OwnedArray<MidiOutputDeviceData> midiOutDevices = {};
    MidiOutputDeviceData* initializeMidiOutputDevice(juce::String deviceName);
    MidiOutputDeviceData* getMidiOutputDeviceData(juce::String deviceName);
    void clearMidiDeviceOutputBuffers();
    void clearMidiTrackBuffers();
    void sendMidiDeviceOutputBuffers();
    void writeMidiToDevicesMidiBuffer(juce::MidiBuffer& buffer, std::vector<juce::String> midiOutDeviceNames);
    std::unique_ptr<juce::MidiOutput> notesMonitoringMidiOutput;
    
    std::array<int, 8> pushEncodersCCMapping = {-1, -1, -1, -1, -1, -1, -1, -1};
    std::array<int, 64> pushPadsNoteMapping = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, };
    juce::String pushEncodersCCMappingHardwareDeviceShortName = "";
    
    // Aux MIDI buffers
    // We call .ensure_size for these buffers to make sure we don't to allocations in the RT thread
    juce::MidiBuffer midiClockMessages;
    juce::MidiBuffer midiMetronomeMessages;
    juce::MidiBuffer pushMidiClockMessages;
    juce::MidiBuffer monitoringNotesMidiBuffer;
    juce::MidiBuffer internalSynthCombinedBuffer; // Only used for debugging
    
    // Hardware devices
    std::unique_ptr<HardwareDeviceList> hardwareDevices;
    void initializeHardwareDevices();
    HardwareDevice* getHardwareDeviceByName(juce::String name);
    
    // Transport and basic settings
    double sampleRate = 0.0;
    int samplesPerSlice = 0;
    bool shouldToggleIsPlaying = false;
    juce::CachedValue<juce::String> name;
    juce::CachedValue<int> fixedLengthRecordingBars;
    juce::CachedValue<bool> recordAutomationEnabled;
    juce::CachedValue<int> fixedVelocity;
    
    // Musical context
    std::unique_ptr<MusicalContext> musicalContext;
    double nextBpm = 0.0;
    int nextMeter = 0;
    bool sendMidiClock = true;
    bool shouldStartSendingPushMidiClockBurst = true;
    double lastTimePushMidiClockBurstStarted = -1.0;
    std::vector<juce::String> sendMidiClockMidiDeviceNames = {};
    std::vector<juce::String> sendMetronomeMidiDeviceNames = {};
    std::vector<juce::String> sendPushMidiClockDeviceNames = {};

    // Tracks
    std::unique_ptr<TrackList> tracks;
    juce::String activeUiNotesMonitoringTrack = "";
    Track* getTrackWithUUID(juce::String trackUUID);
    
    // Scenes
    void playScene(int sceneN);
    void duplicateScene(int sceneN);

    // Recurring tasks
    void timerCallback() override;  // Callback used to update UI components

    // Other testing/debugging stuff
    juce::Synthesiser sineSynth;
    juce::CachedValue<bool> renderWithInternalSynth;
    int nSynthVoices = 32;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sequencer)
};

void WebSocketsServer::run()
{
    WsServer server;
    server.config.port = WEBSOCKETS_SERVER_PORT;  // Use a known port so python UI can connect to it
    serverPtr.reset(&server);
    
    auto &source_coms_endpoint = server.endpoint["^/shepherd_coms/?$"];
    source_coms_endpoint.on_message = [&server, this](std::shared_ptr<WsServer::Connection> /*connection*/, std::shared_ptr<WsServer::InMessage> in_message) {
        juce::String message = juce::String(in_message->string());
        if (sequencerPtr != nullptr){
            sequencerPtr->wsMessageReceived(message);
        }
    };
    
    server.start([this](unsigned short port) {
        assignedPort = port;
        DBG("- Started Websockets Server listening at 0.0.0.0:" << port);
    });
}

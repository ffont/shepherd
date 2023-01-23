/*
  ==============================================================================

    Sequencer.h
    Created: 10 Jun 2022 12:07:14pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "helpers_shepherd.h"
#include "MusicalContext.h"
#include "HardwareDevice.h"
#include "SynthAudioSource.h"
#include "Playhead.h"
#include "Clip.h"
#include "Track.h"
#if USE_WS_SERVER
#include "server_ws.hpp"
#endif


class Sequencer; // Forward declaration

#if USE_WS_SERVER
using WsServer = SimpleWeb::SocketServer<SimpleWeb::WS>;
#endif

class ShepherdWebSocketsServer: public juce::Thread
{
public:
   
    ShepherdWebSocketsServer(): juce::Thread ("ShepherdWebsocketsServer")
    {
    }
   
    ~ShepherdWebSocketsServer(){
        #if USE_WS_SERVER
        if (serverPtr != nullptr){
            serverPtr.release();
        }
        #endif
    }
    
    void setSequencerPointer(Sequencer* _sequencer){
        sequencerPtr = _sequencer;
    }
    
    inline void run();  // Implemented after ServerInterface is defined

    int assignedPort = -1;
    Sequencer* sequencerPtr;
    #if USE_WS_SERVER
    std::unique_ptr<WsServer> serverPtr;
    #endif 
};


class Sequencer: private juce::Timer,
                 protected juce::ValueTree::Listener,
                 public juce::ActionBroadcaster

{
public:
    Sequencer();
    ~Sequencer();
    
    void prepareSequencer (int samplesPerBlockExpected, double sampleRate);
    void getNextMIDISlice (int sliceNumSamples);
    
    // Some public functions used for testing
    void debugState();
    
    // Public method for receiving WS messages
    void wsMessageReceived  (const juce::String& serializedMessage);
    
    // Other useful public functions
    juce::File getDataLocation();
    bool shouldRenderWithInternalSynth() { return renderWithInternalSynth;}
    juce::OwnedArray<MidiOutputDeviceData>* getMidiOutDevices() {return &midiOutDevices;}
    //std::unique_ptr<HardwareDeviceList>& getHardwareDevices() {return hardwareDevices;}
    
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
    juce::String getStringPropertyFromSettingsFile(juce::String propertyName);
    int getIntPropertyFromSettingsFile(juce::String propertyName);
    std::vector<juce::String> getListStringPropertyFromSettingsFile(juce::String propertyName);
    
    // Communication with controller
    ShepherdWebSocketsServer wsServer;
    void initializeWS();
    
    juce::String serliaizeOSCMessage(const juce::OSCMessage& message);
    void sendMessageToController(const juce::OSCMessage& message);
    void sendWSMessage(const juce::OSCMessage& message);
    // wsMessageReceived is defined in the public API
    void processMessageFromController (const juce::String action, juce::StringArray parameters);
    int stateUpdateID = 0;
    
    // Midi devices and other midi stuff
    bool midiOutputDeviceAlreadyInitialized(const juce::String& deviceName);
    bool midiInputDeviceAlreadyInitialized(const juce::String& deviceName);
    
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
        
    // Aux MIDI buffers
    // We call .ensure_size for these buffers to make sure we don't to allocations in the RT thread
    juce::MidiBuffer midiClockMessages;
    juce::MidiBuffer midiMetronomeMessages;
    juce::MidiBuffer pushMidiClockMessages;
    juce::MidiBuffer monitoringNotesMidiBuffer;
    
    // Hardware devices
    std::unique_ptr<HardwareDeviceList> hardwareDevices;
    void initializeHardwareDevices();
    HardwareDevice* getHardwareDeviceByName(juce::String name, HardwareDeviceType type);
    
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
    bool sendPushLikeMidiClockBursts = false;
    bool shouldStartSendingPushMidiClockBurst = true;
    double lastTimePushMidiClockBurstStarted = -1.0;
    int metronomeMidiChannel = 0;
    juce::String sendMetronomeMidiDeviceName = "";
    std::vector<juce::String> sendMidiClockMidiDeviceNames = {};
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
    juce::CachedValue<bool> renderWithInternalSynth;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sequencer)
};

void ShepherdWebSocketsServer::run()
{
    #if USE_WS_SERVER
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
    #endif
}

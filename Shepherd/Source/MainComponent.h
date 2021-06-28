
#pragma once

#include <JuceHeader.h>
#include "helpers.h"
#include "MusicalContext.h"
#include "Playhead.h"
#include "Clip.h"
#include "Track.h"
#include "HardwareDevice.h"
#include "SynthAudioSource.h"
#include "DevelopmentUIComponent.h"


//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
class MainComponent  : public juce::AudioAppComponent,
                       private juce::Timer,
                       private juce::OSCReceiver,
                       private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>,
                       protected juce::ValueTree::Listener
                       
{
public:
    //==============================================================================
    MainComponent();
    ~MainComponent() override;

    //==============================================================================
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    //==============================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;

protected:
    juce::ValueTree state;
    void bindState();
    
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree& parentTree, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree& parentTree, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree& parentTree, int, int) override;
    void valueTreeParentChanged (juce::ValueTree&) override;

private:
    //==============================================================================

    GlobalSettingsStruct getGlobalSettings();
    
    bool mainComponentInitialized = false;
    
    // Save/load
    void saveCurrentSessionToFile();
    void loadSessionFromFile(juce::String fileName);
    
    // OSC
    void initializeOSC();
    void oscMessageReceived (const juce::OSCMessage& message) override;
    void sendOscMessage (const juce::OSCMessage& message);
    juce::OSCSender oscSender;
    int oscReceivePort = OSC_BACKEND_RECEIVE_PORT;
    int oscSendPort = OSC_CONRTOLLER_RECEIVE_PORT;
    juce::String oscSendHost = "127.0.0.1";
    bool oscSenderIsConnected = false;
    
    // Midi devices and other midi stuff
    void initializeMIDIInputs();
    juce::int64 lastTimeMidiInputInitializationAttempted = 0;
    std::unique_ptr<juce::MidiInput> midiIn;
    bool midiInIsConnected = false;
    juce::MidiMessageCollector midiInCollector;
    std::unique_ptr<juce::MidiInput> midiInPush;
    bool midiInPushIsConnected = false;
    juce::MidiMessageCollector pushMidiInCollector;
    
    void initializeMIDIOutputs();
    bool shouldTryInitializeMidiOutputs = false;
    juce::int64 lastTimeMidiOutputInitializationAttempted = 0;
    MidiOutputDeviceData* initializeMidiOutputDevice(juce::String deviceName);
    juce::OwnedArray<MidiOutputDeviceData> midiOutDevices = {};
    juce::MidiOutput* getMidiOutputDevice(juce::String deviceName);
    juce::MidiBuffer* getMidiOutputDeviceBuffer(juce::String deviceName);
    void clearMidiDeviceOutputBuffers();
    void clearMidiTrackBuffers();
    void sendMidiDeviceOutputBuffers();
    void writeMidiToDevicesMidiBuffer(juce::MidiBuffer& buffer, std::vector<juce::String> midiOutDeviceNames);
    std::unique_ptr<juce::MidiOutput> notesMonitoringMidiOutput;
    
    std::array<int, 8> pushEncodersCCMapping = {-1, -1, -1, -1, -1, -1, -1, -1};
    std::array<int, 64> pushPadsNoteMapping = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, };
    std::vector<juce::MidiMessage> lastMidiNoteOnMessages = {};
    int lastMidiNoteOnMessagesToStore = 20;
    juce::String pushEncodersCCMappingHardwareDeviceShortName = "";
    
    // Aux MIDI buffers
    juce::MidiBuffer midiClockMessages;
    juce::MidiBuffer midiMetronomeMessages;
    juce::MidiBuffer pushMidiClockMessages;
    juce::MidiBuffer incomingMidi;
    juce::MidiBuffer incomingMidiKeys;
    juce::MidiBuffer incomingMidiPush;
    juce::MidiBuffer monitoringNotesMidiBuffer;
    
    // Hardware devices
    juce::OwnedArray<HardwareDevice> hardwareDevices;
    void initializeHardwareDevices();
    HardwareDevice* getHardwareDeviceByName(juce::String name);
    juce::StringArray availableHardwareDeviceNames = {};
    
    // Transport and basic settings
    double sampleRate = 0.0;
    int samplesPerSlice = 0;
    bool shouldToggleIsPlaying = false;
    juce::CachedValue<juce::String> name;
    juce::CachedValue<double> playheadPositionInBeats;
    juce::CachedValue<bool> isPlaying;
    juce::CachedValue<bool> doingCountIn;
    juce::CachedValue<double> countInplayheadPositionInBeats;
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

    // Tracks
    void initializeTracks();
    std::unique_ptr<TrackList> tracks;
    int activeUiNotesMonitoringTrack = -1;
    
    // Scenes
    void playScene(int sceneN);
    void duplicateScene(int sceneN);
    #if RPI_BUILD
    int nScenes = 8;
    #else
    int nScenes = 8;  // Note that 4 of the scences are hidden in the test app JUCE UI
    #endif
    
    #if !RPI_BUILD
    // Desktop app UI
    DevelopmentUIComponent devUiComponent;
    #endif

    // Recurring tasks
    void timerCallback() override;  // Callback used to update UI components

    // Sine synth (for testing purposes only)
    juce::Synthesiser sineSynth;
    bool renderWithInternalSynth = true;
    int nSynthVoices = 32;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

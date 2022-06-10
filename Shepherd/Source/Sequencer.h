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
    void randomizeClipsNotes();
    
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
    bool midiDeviceAlreadyInitialized(const juce::String& deviceName);
    
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

    // Recurring tasks
    void timerCallback() override;  // Callback used to update UI components

    // Other testing/debugging stuff
    juce::Synthesiser sineSynth;
    bool renderWithInternalSynth = true;
    int nSynthVoices = 32;
    
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sequencer)
};

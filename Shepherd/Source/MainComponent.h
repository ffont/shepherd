
#pragma once

#include <JuceHeader.h>
#include "defines.h"
#include "MusicalContext.h"
#include "Playhead.h"
#include "Clip.h"
#include "Track.h"
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
                       private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
                       
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

private:
    //==============================================================================

    GlobalSettingsStruct getGlobalSettings();
    
    // OSC
    void initializeOSC();
    void oscMessageReceived (const juce::OSCMessage& message) override;
    void sendOscMessage (const juce::OSCMessage& message);
    juce::OSCSender oscSender;
    int oscReceivePort = 9003;
    int oscSendPort = 9004;
    juce::String oscSendHost = "127.0.0.1";
    bool oscSenderIsConnected = false;
    
    // Midi devices and other midi stuff
    void initializeMIDI();
    juce::int64 lastTimeMidiInitializationAttempted = 0;
    std::unique_ptr<juce::MidiInput> midiIn;
    bool midiInIsConnected = false;
    juce::MidiMessageCollector midiInCollector;
    std::unique_ptr<juce::MidiOutput> midiOutA;
    bool midiOutAIsConnected = false;
    int midiOutChannel = 1;
    std::unique_ptr<juce::MidiInput> midiInPush;
    bool midiInPushIsConnected = false;
    juce::MidiMessageCollector pushMidiInCollector;
    std::array<int, 8> pushEncodersCCMapping = {-1, -1, -1, -1, -1, -1, -1, -1};
    std::array<int, 64> pushPadsNoteMapping = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, };
    int fixedVelocity = -1;
    std::vector<juce::MidiMessage> lastMidiNoteOnMessages = {};
    int lastMidiNoteOnMessagesToStore = 20;
    
    // Transport and basic audio settings
    double sampleRate = 44100.0;
    int samplesPerBlock = 0;
    double playheadPositionInBeats = 0.0;
    bool isPlaying = false;
    bool shouldToggleIsPlaying = false;
    bool doingCountIn = false;
    double countInplayheadPositionInBeats = 0.0;
    double fixedLengthRecordingAmount = 0.0;
    
    // Musical context
    MusicalContext musicalContext;
    double nextBpm = 0.0;
    int nextMeter = 0;
    
    // Metronome
    bool metronomeOn = true;
    int metronomeMidiChannel = 16;
    int metronomeLowMidiNote = 67;
    int metronomeHighMidiNote = 80;
    float metronomeMidiVelocity = 1.0f;
    int metronomeTickLengthInSamples = 100;
    
    // Tracks
    int nTestTracks = 8;
    juce::OwnedArray<Track> tracks;
    
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

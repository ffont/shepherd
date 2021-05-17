
#pragma once

#include <JuceHeader.h>
#include "Playhead.h"
#include "Clip.h"
#include "Track.h"
#include "SynthAudioSource.h"

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

    // OSC
    void oscMessageReceived (const juce::OSCMessage& message) override;
    void sendOscMessage (const juce::OSCMessage& message);
    juce::OSCSender oscSender;
    int oscReceivePort = 9003;
    int oscSendPort = 9004;
    juce::String oscSendHost = "127.0.0.1";
    bool oscSenderIsConnected = false;
    
    // Midi devices and other midi stuff
    std::unique_ptr<juce::MidiOutput> midiOutA;
    std::unique_ptr<juce::MidiInput> midiIn;
    juce::MidiMessageCollector midiInCollector;
    int midiOutChannel = 1;
    
    // Transport and basic audio settings
    double sampleRate = 44100.0;
    int samplesPerBlock = 0;
    double bpm = 120.0;
    double nextBpm = 0.0;
    double playheadPositionInBeats = 0.0;
    bool isPlaying = false;
    bool shouldToggleIsPlaying = false;
    
    bool doingCountIn = false;
    double countInLengthInBeats = 4.0;
    double countInplayheadPositionInBeats = 0.0;
    
    // Metronome
    #if !RPI_BUILD
    bool metronomeOn = true;
    #else
    bool metronomeOn = false;
    #endif
    int metronomeMidiChannel = 16;
    int metronomeLowMidiNote = 67;
    int metronomeHighMidiNote = 80;
    float metronomeMidiVelocity = 1.0f;
    int metronomeTickLengthInSamples = 100;
    int metronomePendingNoteOffSamplePosition = -1;
    bool metronomePendingNoteOffIsHigh = false;
    
    // Tracks
    int nTestTracks = 8;
    int selectedTrack = 0;
    juce::OwnedArray<Track> tracks;
    
    // Desktop app UI
    void timerCallback() override;  // Callback used to update UI
    
    juce::Slider tempoSlider;
    juce::Label tempoSliderLabel;
    juce::Label playheadLabel;
    juce::TextButton globalStartStopButton;
    juce::TextButton globalRecordButton;
    juce::Label selectedTrackLabel;
    juce::TextButton selectTrackButton;
    juce::TextButton metronomeToggleButton;
    juce::TextButton internalSynthButton;
    
    juce::OwnedArray<juce::Label> midiClipsPlayheadLabels;
    juce::OwnedArray<juce::TextButton> midiClipsClearButtons;
    juce::OwnedArray<juce::TextButton> midiClipsStartStopButtons;
    juce::OwnedArray<juce::TextButton> midiClipsRecordButtons;
    bool clipControlElementsCreated = false;
    
    // Sine synth (for testing purposes only)
    juce::Synthesiser sineSynth;
    bool renderWithInternalSynth = true;
    int nSynthVoices = 32;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

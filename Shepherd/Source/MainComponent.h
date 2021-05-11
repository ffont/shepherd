#pragma once

#include <JuceHeader.h>
#include "Playhead.h"
#include "Clip.h"
#include "synthAudioSource.h"

//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
class MainComponent  : public juce::AudioAppComponent,
                       private juce::Timer
                       
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
    void timerCallback() override;
    
    // Midi devices and other midi stuff
    std::unique_ptr<juce::MidiOutput> midiOutA;
    std::unique_ptr<juce::MidiInput> midiIn;
    juce::MidiMessageCollector midiInCollector;
    int midiOutChannel = 1;
    
    // Playhead and main app state
    double bpm = 120.0;
    double sampleRate = 44100.0;
    int samplesPerBlock = 0;
    
    // Clips and global playhead
    double playheadPositionInBeats = 0.0;
    bool isPlaying = true;
    bool shouldToggleIsPlaying = false;
    std::unique_ptr<Clip> midiClip;
    
    // Sequenced notes
    std::vector<std::pair<int, float>> noteOnTimes = {};
    juce::MidiMessageSequence recordedSequence = {};
    juce::MidiMessageSequence recordingSequence = {};
    bool isPlayingRecordedSequence = false;
    bool willPlayRecordedSequence = true;
    int beatRecordedSequenceLastTriggered = 0;
    bool shouldClearSequence = false;
    
    // Desktop app UI
    juce::Slider tempoSlider;
    juce::Label tempoSliderLabel;
    juce::Label playheadLabel;
    juce::TextButton globalStartStopButton;
    juce::Label midiOutChannelLabel;
    juce::TextButton midiOutChannelSetButton;
    juce::Label clipPlayheadLabel;
    juce::Label clipRecorderPlayheadLabel;
    juce::TextButton clipClearButton;
    juce::TextButton clipRecordButton;
    juce::TextButton clipStartStopButton;
    
    // Sine synth (for testing purposes only)
    juce::Synthesiser sineSynth;
    

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

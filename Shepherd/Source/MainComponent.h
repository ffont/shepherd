#pragma once

#include <JuceHeader.h>

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
    juce::SortedSet<int> currentNotesMidiIn;
    
    // Playhead and main app state
    int samplesSinceLastBeat = 0;
    int beatCount = 0;
    int currentBeat = 0;
    int currentBar = 0;
    double currentBeatFraction = 0;
    double bpm = 120.0;
    int beatsPerBar = 4;
    double sampleRate = 44100.0;
    bool isRecordingMidi = false;
    bool willStartRecordingMidi = false;
    int midiRecorderTime = 2 * beatsPerBar;
    int beatStartedRecordingMidi = 0;
    
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
    juce::TextButton recordButton;
    juce::TextButton clearSequenceButton;
    

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

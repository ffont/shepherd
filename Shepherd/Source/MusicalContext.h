/*
  ==============================================================================

    MusicalContext.h
    Created: 7 Jun 2021 5:30:02pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "helpers.h"


class MusicalContext
{
public:
    MusicalContext(std::function<GlobalSettingsStruct()> globalSettingsGetter, juce::ValueTree& _state);
    void bindState();
    
    double getNextQuantizedBarPosition();
    double getSliceLengthInBeats();
    
    double getPlayheadPositionInBeats();
    void setPlayheadPosition(double newPosition);
    
    bool playheadIsPlaying();
    void setPlayheadIsPlaying(bool onOff);
    
    bool playheadIsDoingCountIn();
    void setPlayheadIsDoingCountIn(bool onOff);
    
    double getCountInPlayheadPositionInBeats();
    void setCountInPlayheadPosition(double newPosition);
    
    void setMeter(int newMeter);
    int getMeter();
    void setBpm(double newBpm);
    double getBpm();
    
    void setMetronome(bool onOff);
    void toggleMetronome();
    bool metronomeIsOn();
    
    void updateBarsCounter(juce::Range<double> currentSliceRange);
    void resetCounters();
    int getBarCount();
    double getBeatsInBarCount();
    
    void renderMetronomeInSlice(juce::MidiBuffer& bufferToFill);
    void renderMidiClockInSlice(juce::MidiBuffer& bufferToFill);
    void renderMidiStartInSlice(juce::MidiBuffer& bufferToFill);
    void renderMidiStopInSlice(juce::MidiBuffer& bufferToFill);
    
private:
    juce::ValueTree state;
    
    juce::CachedValue<double> playheadPositionInBeats;
    juce::CachedValue<bool> isPlaying;
    juce::CachedValue<bool> doingCountIn;
    juce::CachedValue<double> countInPlayheadPositionInBeats;
    
    juce::CachedValue<double> bpm;
    juce::CachedValue<int> meter;
    juce::CachedValue<int> barCount;
    juce::CachedValue<bool> metronomeOn;
    
    double lastBarCountedPlayheadPosition = 0.0;
    int metronomeMidiChannel = 16;
    int metronomeLowMidiNote = 67;
    int metronomeHighMidiNote = 80;
    float metronomeMidiVelocity = 1.0f;
    int metronomeTickLengthInSamples = 100;
    int metronomePendingNoteOffSamplePosition = -1;
    bool metronomePendingNoteOffIsHigh = false;
    
    std::function<GlobalSettingsStruct()> getGlobalSettings;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusicalContext)
};

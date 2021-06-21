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
    juce::ValueTree& state;
    
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
};

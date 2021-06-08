/*
  ==============================================================================

    MusicalContext.h
    Created: 7 Jun 2021 5:30:02pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "defines.h"


class MusicalContext
{
public:
    MusicalContext(std::function<GlobalSettingsStruct()> globalSettingsGetter);
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
    
    void renderMetronomeInSlice(juce::MidiBuffer& bufferToFill, int bufferSize);
    void renderMidiClockInSlice(juce::MidiBuffer& bufferToFill, int bufferSize);
    void renderMidiStartInSlice(juce::MidiBuffer& bufferToFill, int bufferSize);
    void renderMidiStopInSlice(juce::MidiBuffer& bufferToFill, int bufferSize);
    
private:
    double bpm = 120.0;
    int meter = 4;
    
    int barCount = 0;
    double lastBarCountedPlayheadPosition = 0.0;

    bool metronomeOn = true;
    int metronomeMidiChannel = 16;
    int metronomeLowMidiNote = 67;
    int metronomeHighMidiNote = 80;
    float metronomeMidiVelocity = 1.0f;
    int metronomeTickLengthInSamples = 100;
    int metronomePendingNoteOffSamplePosition = -1;
    bool metronomePendingNoteOffIsHigh = false;
    
    std::function<GlobalSettingsStruct()> getGlobalSettings;
};

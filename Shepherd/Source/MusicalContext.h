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
    
    MusicalContext(std::function<GlobalSettingsStruct()> globalSettingsGetter)
    {
        getGlobalSettings = globalSettingsGetter;
    }
    
    double getNextQuantizedBarPosition()
    {
        if (getGlobalSettings().playheadPositionInBeats == 0.0){
             // Edge case in which global playhead is stopped
            return 0.0;
        } else {
            return std::round(lastBarCountedPlayheadPosition + (double)meter);
        }
    }

    void resetCounters()
    {
        barCount = 0;
        lastBarCountedPlayheadPosition = 0.0;
    }
    
    void setBpm(double newBpm)
    {
        jassert(newBpm > 0.0);
        bpm = newBpm;
    }
    
    void setMeter(int newMeter)
    {
        jassert(newMeter > 0);
        meter = newMeter;
    }
    
    int getMeter()
    {
        return meter;
    }
    
    double getBpm()
    {
        return bpm;
    }
    
    int getBarCount()
    {
        return barCount;
    }
    
    double getBeatsInBarCount()
    {
        // Don't use this for any serious check as there might be some innacuracies in this count
        return getGlobalSettings().playheadPositionInBeats - lastBarCountedPlayheadPosition;
    }
    
    void updateBarsCounter(juce::Range<double> currentSliceRange)
    {
        // If beat is changing in the current slice, check if new bar has been reached and increase the count
        // Note that this counter is not updated in a sample-accurate manner, and is also correctly synced at the
        // buffer size level (which should be good enough for most purposes)
        // Dont count if global playgead is below 0, as this could happen when switching from count in to positive global playhead
        double flooredRangeStart = std::floor(currentSliceRange.getStart());
        double flooredRangeEnd = std::floor(currentSliceRange.getEnd());
        if (currentSliceRange.getStart() > 0){
            if (flooredRangeEnd > flooredRangeStart){
                if ((double)flooredRangeEnd - lastBarCountedPlayheadPosition >= (double)meter){
                    barCount += 1;
                    lastBarCountedPlayheadPosition = flooredRangeEnd;
                }
            }
        }
    }
    
    void setMetronome(bool onOff)
    {
        metronomeOn = onOff;
    }
    
    void toggleMetronome()
    {
        metronomeOn != metronomeOn;
    }
    
    bool metronomeIsOn()
    {
        return metronomeOn;
    }
    
    void renderMetronomeInSlice(juce::MidiBuffer& bufferToFill, int bufferSize)
    {
        // Add metronome ticks to the buffer
        if (metronomePendingNoteOffSamplePosition > -1){
            // If there was a noteOff metronome message pending from previous block, add it now to the buffer
            juce::MidiMessage msgOff = juce::MidiMessage::noteOff(metronomeMidiChannel, metronomePendingNoteOffIsHigh ? metronomeHighMidiNote: metronomeLowMidiNote, 0.0f);
            #if !RPI_BUILD
            // Don't send note off messages in RPI_BUILD as it messed up external metronome
            // Should investigate why...
            bufferToFill.addEvent(msgOff, metronomePendingNoteOffSamplePosition);
            #endif
            metronomePendingNoteOffSamplePosition = -1;
        }
        if (metronomeOn && (getGlobalSettings().isPlaying || getGlobalSettings().doingCountIn)) {
            
            double previousBeat = getGlobalSettings().isPlaying ? getGlobalSettings().playheadPositionInBeats : getGlobalSettings().countInplayheadPositionInBeats;
            double beatsPerSample = 1 / (60.0 * getGlobalSettings().sampleRate / getBpm());
            for (int i=0; i<bufferSize; i++){
                double nextBeat = previousBeat + beatsPerSample;
                if (previousBeat == 0.0) {
                    previousBeat = -0.1;  // Edge case for when global playhead has just started, otherwise we miss tick at time 0.0
                }
                if ((std::floor(nextBeat)) != std::floor(previousBeat)) {
                    bool tickIsHigh = (nextBeat - lastBarCountedPlayheadPosition) < (getGlobalSettings().samplesPerBlock * beatsPerSample);
                    
                    juce::MidiMessage msgOn = juce::MidiMessage::noteOn(metronomeMidiChannel, tickIsHigh ? metronomeHighMidiNote: metronomeLowMidiNote, metronomeMidiVelocity);
                    bufferToFill.addEvent(msgOn, i);
                    if (i + metronomeTickLengthInSamples < bufferSize){
                        juce::MidiMessage msgOff = juce::MidiMessage::noteOff(metronomeMidiChannel, tickIsHigh ? metronomeHighMidiNote: metronomeLowMidiNote, 0.0f);
                        #if !RPI_BUILD
                        // Don't send note off messages in RPI_BUILD as it messed up external metronome
                        // Should investigate why...
                        bufferToFill.addEvent(msgOff, i + metronomeTickLengthInSamples);
                        #endif
                    } else {
                        metronomePendingNoteOffSamplePosition = i + metronomeTickLengthInSamples - bufferSize;
                        metronomePendingNoteOffIsHigh = tickIsHigh;
                    }
                }
                previousBeat = nextBeat;
            }
        }
    }
    
    void renderMidiClockInSlice(juce::MidiBuffer& bufferToFill, int bufferSize)
    {
    }
    

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

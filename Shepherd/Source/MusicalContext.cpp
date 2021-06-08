/*
  ==============================================================================

    MusicalContext.cpp
    Created: 8 Jun 2021 6:36:05am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#include "MusicalContext.h"

MusicalContext::MusicalContext(std::function<GlobalSettingsStruct()> globalSettingsGetter)
{
    getGlobalSettings = globalSettingsGetter;
}


//==============================================================================

double MusicalContext::getNextQuantizedBarPosition()
{
    if (getGlobalSettings().playheadPositionInBeats == 0.0){
         // Edge case in which global playhead is stopped
        return 0.0;
    } else {
        return std::round(lastBarCountedPlayheadPosition + (double)meter);
    }
}

//==============================================================================

void MusicalContext::setMeter(int newMeter)
{
    jassert(newMeter > 0);
    meter = newMeter;
}

int MusicalContext::getMeter()
{
    return meter;
}

void MusicalContext::setBpm(double newBpm)
{
    jassert(newBpm > 0.0);
    bpm = newBpm;
}

double MusicalContext::getBpm()
{
    return bpm;
}

void MusicalContext::setMetronome(bool onOff)
{
    metronomeOn = onOff;
}

void MusicalContext::toggleMetronome()
{
    metronomeOn = !metronomeOn;
}

bool MusicalContext::metronomeIsOn()
{
    return metronomeOn;
}

void MusicalContext::updateBarsCounter(juce::Range<double> currentSliceRange)
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

void MusicalContext::resetCounters()
{
    barCount = 0;
    lastBarCountedPlayheadPosition = 0.0;
}

int MusicalContext::getBarCount()
{
    return barCount;
}

double MusicalContext::getBeatsInBarCount()
{
    // Don't use this for any serious check as there might be some innacuracies in this count
    return getGlobalSettings().playheadPositionInBeats - lastBarCountedPlayheadPosition;
}


//==============================================================================

void MusicalContext::renderMetronomeInSlice(juce::MidiBuffer& bufferToFill, int bufferSize)
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
        double beatsPerSample = 1.0 / (60.0 * getGlobalSettings().sampleRate / getBpm());
        for (int i=0; i<bufferSize; i++){
            
            double nextBeat = previousBeat + beatsPerSample;
            double previousBeatNearestQuantized = std::round(previousBeat);
            double nextBeatNearestQuantized = std::round(nextBeat);
            double tickTime = -1.0;
        
            if (previousBeat <= previousBeatNearestQuantized && previousBeatNearestQuantized < nextBeat){
                // If previousBeatNearestQuantized is in the middle of both beats, add midi metronome message
                tickTime = previousBeatNearestQuantized;
            }
            else if (previousBeat <= nextBeatNearestQuantized && nextBeatNearestQuantized < nextBeat) {
                // If nextBeatNearestQuantized is in the middle of both beats, add midi metronome message
                tickTime = nextBeatNearestQuantized;
            }
            
            if (tickTime > -1.0){
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

void MusicalContext::renderMidiClockInSlice(juce::MidiBuffer& bufferToFill, int bufferSize)
{
    // Addd 24 ticks per beat
    if (getGlobalSettings().isPlaying){
        double previousBeat = getGlobalSettings().playheadPositionInBeats;
        double beatsPerSample = 1.0 / (60.0 * getGlobalSettings().sampleRate / getBpm());
        for (int i=0; i<bufferSize; i++){
            double nextBeat = previousBeat + beatsPerSample;
            double previousBeatNearestQuantized = std::round(previousBeat * 24.0) / 24.0;
            double nextBeatNearestQuantized = std::round(nextBeat * 24.0) / 24.0;
            if (previousBeat <= previousBeatNearestQuantized && previousBeatNearestQuantized < nextBeat){
                // If previousBeatNearestQuantized is in the middle of both beats, add midi clock tick
                juce::MidiMessage clockMsg = juce::MidiMessage::midiClock();
                bufferToFill.addEvent(clockMsg, i);
            }
            else if (previousBeat <= nextBeatNearestQuantized && nextBeatNearestQuantized < nextBeat) {
                // If nextBeatNearestQuantized is in the middle of both beats, add midi clock tick
                juce::MidiMessage clockMsg = juce::MidiMessage::midiClock();
                bufferToFill.addEvent(clockMsg, i);
            }
            previousBeat = nextBeat;
        }
    }
}

void MusicalContext::renderMidiStartInSlice(juce::MidiBuffer& bufferToFill, int bufferSize)
{
    juce::MidiMessage clockMsg = juce::MidiMessage::midiStart();
    bufferToFill.addEvent(clockMsg, 0);
}

void MusicalContext::renderMidiStopInSlice(juce::MidiBuffer& bufferToFill, int bufferSize)
{
    juce::MidiMessage clockMsg = juce::MidiMessage::midiStop();
    bufferToFill.addEvent(clockMsg, 0);
}

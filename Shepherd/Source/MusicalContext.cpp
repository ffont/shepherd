/*
  ==============================================================================

    MusicalContext.cpp
    Created: 8 Jun 2021 6:36:05am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#include "MusicalContext.h"

MusicalContext::MusicalContext(std::function<GlobalSettingsStruct()> globalSettingsGetter, juce::ValueTree& _state): state(_state)
{
    getGlobalSettings = globalSettingsGetter;
    bindState();
}

void MusicalContext::bindState()
{
    // Bind cached values to state
    playheadPositionInBeats.referTo(state, IDs::playheadPositionInBeats, nullptr, Defaults::playheadPosition);
    isPlaying.referTo(state, IDs::isPlaying, nullptr, Defaults::isPlaying);
    doingCountIn.referTo(state, IDs::doingCountIn, nullptr, Defaults::doingCountIn);
    countInPlayheadPositionInBeats.referTo(state, IDs::countInPlayheadPositionInBeats, nullptr, Defaults::playheadPosition);
    bpm.referTo(state, IDs::bpm, nullptr, Defaults::bpm);
    meter.referTo(state, IDs::meter, nullptr, Defaults::meter);
    barCount.referTo(state, IDs::barCount, nullptr, Defaults::barCount);
    metronomeOn.referTo(state, IDs::metronomeOn, nullptr, Defaults::metronomeOn);
}

//==============================================================================

double MusicalContext::getNextQuantizedBarPosition()
{
    if (playheadPositionInBeats == 0.0){
         // Edge case in which global playhead is stopped
        return 0.0;
    } else {
        return std::round(lastBarCountedPlayheadPosition + (double)meter);
    }
}

double MusicalContext::getSliceLengthInBeats()
{
    return (double)getGlobalSettings().samplesPerSlice / (60.0 * getGlobalSettings().sampleRate / bpm);
}


//==============================================================================

double MusicalContext::getPlayheadPositionInBeats()
{
    return playheadPositionInBeats;
}

void MusicalContext::setPlayheadPosition(double newPosition)
{
    playheadPositionInBeats = newPosition;
}

bool MusicalContext::playheadIsPlaying()
{
    return isPlaying;
}

void MusicalContext::setPlayheadIsPlaying(bool onOff)
{
    isPlaying = onOff;
}

bool MusicalContext::playheadIsDoingCountIn()
{
    return doingCountIn;
}

void MusicalContext::setPlayheadIsDoingCountIn(bool onOff)
{
    doingCountIn = onOff;
}

double MusicalContext::getCountInPlayheadPositionInBeats()
{
    return countInPlayheadPositionInBeats;
}

void MusicalContext::setCountInPlayheadPosition(double newPosition)
{
    countInPlayheadPositionInBeats = newPosition;
}

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
                barCount = barCount + 1;
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
    return playheadPositionInBeats - lastBarCountedPlayheadPosition;
}


//==============================================================================

void MusicalContext::renderMetronomeInSlice(juce::MidiBuffer& bufferToFill)
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
    if ((metronomeOn && isPlaying) || doingCountIn) {
        
        double previousBeat = isPlaying ? playheadPositionInBeats : countInPlayheadPositionInBeats;
        double beatsPerSample = 1.0 / (60.0 * getGlobalSettings().sampleRate / getBpm());
        for (int i=0; i<getGlobalSettings().samplesPerSlice; i++){
            
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
                bool tickIsHigh = (nextBeat - lastBarCountedPlayheadPosition) < (getGlobalSettings().samplesPerSlice * beatsPerSample);
                juce::MidiMessage msgOn = juce::MidiMessage::noteOn(metronomeMidiChannel, tickIsHigh ? metronomeHighMidiNote: metronomeLowMidiNote, metronomeMidiVelocity);
                bufferToFill.addEvent(msgOn, i);
                if (i + metronomeTickLengthInSamples < getGlobalSettings().samplesPerSlice){
                    juce::MidiMessage msgOff = juce::MidiMessage::noteOff(metronomeMidiChannel, tickIsHigh ? metronomeHighMidiNote: metronomeLowMidiNote, 0.0f);
                    #if !RPI_BUILD
                    // Don't send note off messages in RPI_BUILD as it messed up external metronome
                    // Should investigate why...
                    bufferToFill.addEvent(msgOff, i + metronomeTickLengthInSamples);
                    #endif
                } else {
                    metronomePendingNoteOffSamplePosition = i + metronomeTickLengthInSamples - getGlobalSettings().samplesPerSlice;
                    metronomePendingNoteOffIsHigh = tickIsHigh;
                }
            }
            
            previousBeat = nextBeat;
        }
    }
}

void MusicalContext::renderMidiClockInSlice(juce::MidiBuffer& bufferToFill)
{
    // Addd 24 ticks per beat
    if (isPlaying){
        double previousBeat = playheadPositionInBeats;
        double beatsPerSample = 1.0 / (60.0 * getGlobalSettings().sampleRate / getBpm());
        for (int i=0; i<getGlobalSettings().samplesPerSlice; i++){
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

void MusicalContext::renderMidiStartInSlice(juce::MidiBuffer& bufferToFill)
{
    juce::MidiMessage clockMsg = juce::MidiMessage::midiStart();
    bufferToFill.addEvent(clockMsg, 0);
}

void MusicalContext::renderMidiStopInSlice(juce::MidiBuffer& bufferToFill)
{
    juce::MidiMessage clockMsg = juce::MidiMessage::midiStop();
    bufferToFill.addEvent(clockMsg, 0);
}

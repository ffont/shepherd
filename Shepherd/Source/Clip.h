/*
  ==============================================================================

    Clip.h
    Created: 9 May 2021 9:50:14am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Playhead.h"

class Clip
{
public:
    Clip(std::function<juce::Range<double>()> playheadParentSliceGetter,
         std::function<double()> globalBpmGetter,
         std::function<double()> sampleRateGetter,
         std::function<int()> samplesPerBlockGetter,
         std::function<int()> midiOutChannelGetter
         );
    
    void playNow();
    void playNow(double sliceOffset);
    void playAt(double positionInParent);
    void stopNow();
    void stopAt(double positionInParent);
    void togglePlayStop();
    
    void recordNow();
    void recordAt(double positionInParent);
    void toggleRecord();
    
    void renderSliceIntoMidiBuffer(juce::MidiBuffer& bufferToFill, int bufferSize);
    void renderRemainingNoteOffsIntoMidiBuffer(juce::MidiBuffer& bufferToFill);
    void recordFromBuffer(juce::MidiBuffer& incommingBuffer, int bufferSize);
    
    void clearSequence();
    
    Playhead* getPlayerPlayhead();
    Playhead* getRecorderPlayhead();
    
    double getLengthInBeats();
    
private:
    
    Playhead playerPlayhead;
    Playhead recorderPlayhead;
    
    juce::MidiMessageSequence midiSequence = {};
    juce::MidiMessageSequence recordedMidiSequence = {};
    double clipLengthInBeats = 0.0;
    
    juce::SortedSet<int> notesCurrentlyPlayed;
    std::function<double()> getGlobalBpm;
    std::function<double()> getSampleRate;
    std::function<int()> getSamplesPerBlock;
    std::function<int()> getMidiOutChannel;
    
    bool shouldClearSequence = false;
};

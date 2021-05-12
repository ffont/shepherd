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
    
    void startRecordingNow();
    void stopRecordingNow();
    void startRecordingAt(double positionInClipPlayhead);
    void stopRecordingAt(double positionInClipPlayhead);
    void toggleRecord();
    
    bool isPlaying();
    bool isCuedToPlay();
    bool isCuedToStop();
    bool isRecording();
    bool isCuedToStartRecording();
    bool isCuedToStopRecording();
    
    //void renderSliceIntoMidiBuffer(juce::MidiBuffer& bufferToFill, int bufferSize);
    void renderRemainingNoteOffsIntoMidiBuffer(juce::MidiBuffer& bufferToFill);
    //oid recordFromBuffer(juce::MidiBuffer& incommingBuffer, int bufferSize);
    void processSlice(juce::MidiBuffer& incommingBuffer, juce::MidiBuffer& bufferToFill, int bufferSize);
    
    
    void clearSequence();
    double getPlayheadPosition();
    void resetPlayheadPosition();
    double getLengthInBeats();
    
private:
    
    void addRecordedSequenceToSequence();
    bool hasJustStoppedRecording();
    
    
    Playhead playhead;
    
    juce::MidiMessageSequence midiSequence = {};
    juce::MidiMessageSequence recordedMidiSequence = {};
    double clipLengthInBeats = 0.0;
    bool recording = false;
    double willStartRecordingAt = -1.0;
    double willStopRecordingAt = -1.0;
    double hasJustStoppedRecordingFlag = false;
    
    juce::SortedSet<int> notesCurrentlyPlayed;
    std::function<double()> getGlobalBpm;
    std::function<double()> getSampleRate;
    std::function<int()> getSamplesPerBlock;
    std::function<int()> getMidiOutChannel;
    
    bool shouldClearSequence = false;
};

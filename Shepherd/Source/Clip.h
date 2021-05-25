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

#define CLIP_STATUS_PLAYING "p"
#define CLIP_STATUS_STOPPED "s"
#define CLIP_STATUS_CUED_TO_PLAY "c"
#define CLIP_STATUS_CUED_TO_STOP "C"
#define CLIP_STATUS_RECORDING "r"
#define CLIP_STATUS_CUED_TO_RECORD "w"
#define CLIP_STATUS_CUED_TO_STOP_RECORDING "W"
#define CLIP_STATUS_NO_RECORDING "n"
#define CLIP_STATUS_IS_EMPTY "E"
#define CLIP_STATUS_IS_NOT_EMPTY "e"

class Clip
{
public:
    Clip(std::function<juce::Range<double>()> playheadParentSliceGetter,
         std::function<double()> globalBpmGetter,
         std::function<double()> sampleRateGetter,
         std::function<int()> samplesPerBlockGetter,
         std::function<int()> midiOutChannelGetter
         );
    Clip* clone() const;
    
    void playNow();
    void playNow(double sliceOffset);
    void playAt(double positionInGlobalPlayhead);
    void stopNow();
    void stopAt(double positionInGlobalPlayhead);
    void togglePlayStop();
    void clearPlayCue();
    void clearStopCue();
    
    void startRecordingNow();
    void stopRecordingNow();
    void startRecordingAt(double positionInClipPlayhead);
    void stopRecordingAt(double positionInClipPlayhead);
    void toggleRecord();
    void clearStartRecordingCue();
    void clearStopRecordingCue();
    
    bool isPlaying();
    bool isCuedToPlay();
    bool isCuedToStop();
    bool isRecording();
    bool isCuedToStartRecording();
    bool isCuedToStopRecording();
    bool isEmpty();
    juce::String getStatus();
    
    void renderRemainingNoteOffsIntoMidiBuffer(juce::MidiBuffer& bufferToFill);
    void processSlice(juce::MidiBuffer& incommingBuffer, juce::MidiBuffer& bufferToFill, int bufferSize);
    void clearSequence();
    void doubleSequence();
    void cycleQuantization();
    void replaceSequence(juce::MidiMessageSequence newSequence, double newLength);
    void resetPlayheadPosition();
    double getPlayheadPosition();
    double getLengthInBeats();
    
private:
    
    void addRecordedSequenceToSequence();
    bool hasJustStoppedRecording();
    
    Playhead playhead;
    
    juce::MidiMessageSequence midiSequence = {};
    juce::MidiMessageSequence quantizedMidiSequence = {};
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
    void doubleSequenceHelper();
    bool shouldDoubleSequence = false;
    
    double currentQuantizationStep = 0.0;
    bool shouldUpdateQuantizedSequence = false;
    double findNearestQuantizedBeatPosition(double beatPosition, double quantizationStep);
    void computeQuantizedSequence(double quantizationStep);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Clip)
};

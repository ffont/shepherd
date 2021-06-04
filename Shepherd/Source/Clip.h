/*
  ==============================================================================

    Clip.h
    Created: 9 May 2021 9:50:14am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "defines.h"
#include "Playhead.h"

class Clip
{
public:
    Clip(std::function<juce::Range<double>()> playheadParentSliceGetter,
         std::function<GlobalSettingsStruct()> globalSettingsGetter,
         std::function<TrackSettingsStruct()> trackSettingsGetter
         );
    Clip* clone() const;
    
    void processSlice(juce::MidiBuffer& incommingBuffer, juce::MidiBuffer& bufferToFill, int bufferSize, std::vector<juce::MidiMessage>& lastMidiNoteOnMessages);
    void renderRemainingNoteOffsIntoMidiBuffer(juce::MidiBuffer& bufferToFill);
    
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
    
    void setNewClipLength(double newLength);
    void clearClip();
    void doubleSequence();
    void quantizeSequence(double quantizationStep);
    void replaceSequence(juce::MidiMessageSequence newSequence, double newLength);
    void resetPlayheadPosition();
    void undo();
    
    double getPlayheadPosition();
    double getLengthInBeats();
    bool isPlaying();
    bool isCuedToPlay();
    bool isCuedToStop();
    bool isRecording();
    bool isCuedToStartRecording();
    bool isCuedToStopRecording();
    bool isEmpty();
    juce::String getStatus();
    
private:
    
    Playhead playhead;
    
    double clipLengthInBeats = 0.0;
    double nextClipLength = -1.0;
    juce::MidiMessageSequence midiSequence = {};
    juce::MidiMessageSequence preProcessedMidiSequence = {};
    juce::MidiMessageSequence recordedMidiSequence = {};
    juce::MidiMessageSequence nextMidiSequence = {};
    bool recording = false;
    double willStartRecordingAt = -1.0;
    double willStopRecordingAt = -1.0;
    double hasJustStoppedRecordingFlag = false;
    double preRecordingBeatsThreshold = 0.20;  // When starting to record, if notes are played up to this amount before the recording start position, quantize them to the recording start position
    void addRecordedSequenceToSequence();
    bool hasJustStoppedRecording();
    
    std::vector<std::pair<juce::MidiMessageSequence, double>> midiSequenceAndClipLengthUndoStack;
    int allowedUndoLevels = 5;
    void saveToUndoStack();
    bool shouldUndo = false;
    
    juce::SortedSet<int> notesCurrentlyPlayed;
    std::function<GlobalSettingsStruct()> getGlobalSettings;
    std::function<TrackSettingsStruct()> getTrackSettings;
    
    void stopClipNowAndClearAllCues();
    bool shouldReplaceSequence = false;
    
    void clearClipHelper();
    bool shouldclearClip = false;
    
    void doubleSequenceHelper();
    bool shouldDoubleSequence = false;
    
    bool shouldUpdatePreProcessedSequence = false;
    void computePreProcessedSequence();
    
    void removeUnmatchedNotesFromSequence(juce::MidiMessageSequence& sequence);
    void removeEventsAfterTimestampFromSequence(juce::MidiMessageSequence& sequence, double maxTimestamp);
    
    double currentQuantizationStep = 0.0;
    double findNearestQuantizedBeatPosition(double beatPosition, double quantizationStep);
    void quantizeSequence(juce::MidiMessageSequence& sequence, double quantizationStep);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Clip)
};

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
#include "MusicalContext.h"
#include "HardwareDevice.h"


struct TrackSettingsStruct {
    int midiOutChannel;
    HardwareDevice* device;
};

class SequenceEvent
{
public:
    SequenceEvent(const juce::ValueTree& _state) : state(_state)
    {
        bindState();
    }
    
    void bindState()
    {
        name.referTo(state, IDs::name, nullptr, Defaults::name);
        type.referTo(state, IDs::type, nullptr, Defaults::eventType);
        eventMidiBytes.referTo(state, IDs::eventMidiBytes, nullptr, Defaults::eventMidiBytes);
        timestamp.referTo(state, IDs::timestamp, nullptr, Defaults::timestamp);
    }
    juce::ValueTree state;

private:
    juce::CachedValue<juce::String> name;
    juce::CachedValue<juce::String> type;
    juce::CachedValue<juce::String> eventMidiBytes;
    juce::CachedValue<double> timestamp;

};

struct SequenceEventList: public drow::ValueTreeObjectList<SequenceEvent>
{
    SequenceEventList (const juce::ValueTree& v)
    : drow::ValueTreeObjectList<SequenceEvent> (v)
    {
        rebuildObjects();
    }

    ~SequenceEventList()
    {
        freeObjects();
    }

    bool isSuitableType (const juce::ValueTree& v) const override
    {
        return v.hasType (IDs::SEQUENCE_EVENT);
    }

    SequenceEvent* createNewObject (const juce::ValueTree& v) override
    {
        hasUnappliedChanges = true;
        return new SequenceEvent (v);
    }

    void deleteObject (SequenceEvent* c) override
    {
        delete c;
    }

    void newObjectAdded (SequenceEvent*) override    { hasUnappliedChanges = true; }
    void objectRemoved (SequenceEvent*) override     { hasUnappliedChanges = true; }
    void objectOrderChanged() override               { hasUnappliedChanges = true; }
    
    bool hasUnappliedChanges = true;
};


class Clip
{
public:
    Clip(const juce::ValueTree& state,
         std::function<juce::Range<double>()> playheadParentSliceGetter,
         std::function<GlobalSettingsStruct()> globalSettingsGetter,
         std::function<TrackSettingsStruct()> trackSettingsGetter,
         std::function<MusicalContext*()> musicalContextGetter
         );
    Clip* clone() const;
    void bindState();
    juce::ValueTree state;
    void recreateMidiSequenceFromState();
    
    void processSlice(juce::MidiBuffer& incommingBuffer, juce::MidiBuffer* bufferToFill, std::vector<juce::MidiMessage>& lastMidiNoteOnMessages);
    void renderRemainingNoteOffsIntoMidiBuffer(juce::MidiBuffer* bufferToFill);
    
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
    bool hasActiveStartCues();
    bool hasActiveStopCues();
    bool hasActiveCues();
    bool isEmpty();
    juce::String getStatus();
    
private:
    
    juce::CachedValue<juce::String> name;
    
    std::unique_ptr<Playhead> playhead;
    
    juce::CachedValue<double> clipLengthInBeats;
    double nextClipLength = -1.0;
    std::unique_ptr<SequenceEventList> sequenceEvents;
    juce::MidiMessageSequence midiSequence = {};
    juce::MidiMessageSequence preProcessedMidiSequence = {};
    juce::MidiMessageSequence recordedMidiSequence = {};
    juce::MidiMessageSequence nextMidiSequence = {};
    juce::CachedValue<bool> recording;
    juce::CachedValue<double> willStartRecordingAt;
    juce::CachedValue<double> willStopRecordingAt;
    double hasJustStoppedRecordingFlag = false;
    double preRecordingBeatsThreshold = 0.20;  // When starting to record, if notes are played up to this amount before the recording start position, quantize them to the recording start position
    void addRecordedSequenceToSequence();
    bool hasJustStoppedRecording();
    
    std::vector<std::pair<juce::MidiMessageSequence, double>> midiSequenceAndClipLengthUndoStack;
    int allowedUndoLevels = 5;
    void saveToUndoStack();
    bool shouldUndo = false;
    
    juce::SortedSet<int> notesCurrentlyPlayed;
    bool sustainPedalBeingPressed = false;
    std::function<GlobalSettingsStruct()> getGlobalSettings;
    std::function<TrackSettingsStruct()> getTrackSettings;
    std::function<MusicalContext*()> getMusicalContext;
    
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
    void makeSureSequenceResetsPitchBend(juce::MidiMessageSequence& sequence);
    
    juce::CachedValue<double> currentQuantizationStep;
    double findNearestQuantizedBeatPosition(double beatPosition, double quantizationStep);
    void quantizeSequence(juce::MidiMessageSequence& sequence, double quantizationStep);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Clip)
};

struct ClipList: public drow::ValueTreeObjectList<Clip>
{
    ClipList (const juce::ValueTree& v,
              std::function<juce::Range<double>()> playheadParentSliceGetter,
              std::function<GlobalSettingsStruct()> globalSettingsGetter,
              std::function<TrackSettingsStruct()> trackSettingsGetter,
              std::function<MusicalContext*()> musicalContextGetter)
    : drow::ValueTreeObjectList<Clip> (v)
    {
        getPlayheadParentSlice = playheadParentSliceGetter;
        getGlobalSettings = globalSettingsGetter;
        getTrackSettings = trackSettingsGetter;
        getMusicalContext = musicalContextGetter;
        rebuildObjects();
    }

    ~ClipList()
    {
        freeObjects();
    }

    bool isSuitableType (const juce::ValueTree& v) const override
    {
        return v.hasType (IDs::CLIP);
    }

    Clip* createNewObject (const juce::ValueTree& v) override
    {
        return new Clip (v,
                         getPlayheadParentSlice,
                         getGlobalSettings,
                         getTrackSettings,
                         getMusicalContext);
    }

    void deleteObject (Clip* c) override
    {
        delete c;
    }

    void newObjectAdded (Clip*) override    {}
    void objectRemoved (Clip*) override     {}
    void objectOrderChanged() override      {}
    
    std::function<juce::Range<double>()> getPlayheadParentSlice;
    std::function<GlobalSettingsStruct()> getGlobalSettings;
    std::function<TrackSettingsStruct()> getTrackSettings;
    std::function<MusicalContext*()> getMusicalContext;
};


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
#include "Fifo.h"
#include "ReleasePool.h"


struct TrackSettingsStruct {
    bool enabled;
    int midiOutChannel;
    HardwareDevice* device;
};


struct ClipSequence : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<ClipSequence>;
    juce::MidiMessageSequence midiSequence = {};
    juce::MidiMessageSequence sequenceAsMidi() {
        // Using helper function here as in the future we might want to store sequences with another format other than MIDI
        return midiSequence;
    }
};


class Clip: protected juce::ValueTree::Listener,
            private juce::Timer
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
    
    bool isEnabled() { return enabled.get(); };
    juce::String getUUID() { return uuid.get(); };
    juce::String getName() { return name.get(); };
    
    void recreateMidiSequenceFromState();
    
    void prepareSlice();
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
    
protected:
    
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree& parentTree, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree& parentTree, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree& parentTree, int, int) override;
    void valueTreeParentChanged (juce::ValueTree&) override;
    
private:
    
    juce::CachedValue<bool> enabled;
    juce::CachedValue<juce::String> uuid;
    juce::CachedValue<juce::String> name;
    
    std::unique_ptr<Playhead> playhead;
    
    juce::CachedValue<double> clipLengthInBeats;
    double nextClipLength = -1.0;
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
    
    void timerCallback() override;
    
    // RT sharing stuff
    void recreateSequenceAndAddToFifo() {
        juce::MidiMessageSequence midiSequence;
        for (int i=0; i<state.getNumChildren(); i++){
            auto child = state.getChild(i);
            if (child.hasType (IDs::SEQUENCE_EVENT)){
                midiSequence.addEvent(Helpers::eventValueTreeToMidiMessage(child));
            }
        }
        ClipSequence::Ptr clipSequenceObject = new ClipSequence();
        clipSequenceObject->midiSequence = midiSequence;

        clipSequenceObjectsReleasePool.add(clipSequenceObject);  // Add object to release pool so it is never deleted in the audio thread
        clipSequenceObjectsFifo.push(clipSequenceObject);  // Add object to the fifo si it can be pulled from the audio thread (when MIDI messages are added to buffers)
        
        if (clipSequenceObjectsFifo.getAvailableSpace() == 0){
            DBG("WARNING, fifo for clip " << getName() << " is full");
        } else if (clipSequenceObjectsFifo.getAvailableSpace() < 10){
            DBG("WARNING, fifo for clip " << getName() << " getting close to full");
            DBG("- Available space: " << clipSequenceObjectsFifo.getAvailableSpace() << ", available for reading: " << clipSequenceObjectsFifo.getNumAvailableForReading());
        }
    }
    Fifo<ClipSequence::Ptr, 20> clipSequenceObjectsFifo;
    ReleasePool<ClipSequence> clipSequenceObjectsReleasePool; // ReleasePool<ClipSequence::Ptr> ?
    ClipSequence::Ptr clipSequenceForRTThread = new ClipSequence();
    bool sequenceNeedsUpdate = false;
    
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


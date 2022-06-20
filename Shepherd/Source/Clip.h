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
    double lengthInBeats = 0.0;
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
    void loadStateFromOtherClipState(const juce::ValueTree& _state);
    void bindState();
    void updateStateMemberVersions();
    juce::ValueTree state;
    
    bool isEnabled() { return enabled.get(); };
    juce::String getUUID() { return uuid.get(); };
    juce::String getName() { return name.get(); };
    
    void prepareSlice();
    void processSlice(juce::MidiBuffer& incommingBuffer, juce::MidiBuffer* bufferToFill, std::vector<juce::MidiMessage>& lastMidiNoteOnMessages);
    void renderRemainingNoteOffsIntoMidiBuffer(juce::MidiBuffer* bufferToFill);
    bool shouldSendRemainingNotesOff = false;
    
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
    
    void setNewClipLength(double newLength, bool force);
    void clearClip();
    void doubleSequence();
    void quantizeSequence(double quantizationStep);
    void replaceSequence(juce::ValueTree newSequence, double newLength);
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
    // The following members (starting with stateX) have non-CachedValue equivalents below which are the ones really used.
    // The stateX versions are used to copy the values to the state so that these get send to the UI
    juce::CachedValue<double> stateClipLengthInBeats;
    juce::CachedValue<bool> stateRecording;
    juce::CachedValue<double> stateWillStartRecordingAt;
    juce::CachedValue<double> stateWillStopRecordingAt;
    juce::CachedValue<double> stateCurrentQuantizationStep;
    
    double clipLengthInBeats = Defaults::clipLengthInBeats;
    bool recording = Defaults::recording;
    double willStartRecordingAt = Defaults::willStopRecordingAt;
    double willStopRecordingAt = Defaults::willStopRecordingAt;
    double currentQuantizationStep = Defaults::currentQuantizationStep;
    
    std::unique_ptr<Playhead> playhead;
    juce::MidiMessageSequence recordedMidiSequence = {};
    
    double hasJustStoppedRecordingFlag = false;
    double preRecordingBeatsThreshold = 0.20;  // When starting to record, if notes are played up to this amount before the recording start position, quantize them to the recording start position
    void addRecordedSequenceToSequence();
    bool hasJustStoppedRecording();
    
    std::vector<juce::ValueTree> midiSequenceAndClipLengthUndoStack;
    int allowedUndoLevels = 5;
    void saveToUndoStack();
    bool shouldUndo = false;
    
    juce::SortedSet<int> notesCurrentlyPlayed;
    bool sustainPedalBeingPressed = false;
    std::function<GlobalSettingsStruct()> getGlobalSettings;
    std::function<TrackSettingsStruct()> getTrackSettings;
    std::function<MusicalContext*()> getMusicalContext;
    
    void stopClipNowAndClearAllCues();

    
    // Pre-processing of MIDI sequence
    void preProcessSequence(juce::MidiMessageSequence& sequence);
    void quantizeSequence(juce::MidiMessageSequence& sequence, double quantizationStep);
    double findNearestQuantizedBeatPosition(double beatPosition, double quantizationStep);
    void removeUnmatchedNotesFromSequence(juce::MidiMessageSequence& sequence);
    void removeEventsAfterTimestampFromSequence(juce::MidiMessageSequence& sequence, double maxTimestamp);
    void makeSureSequenceResetsPitchBend(juce::MidiMessageSequence& sequence);
    
    // Trigger re-creation of sequence if needed
    void timerCallback() override;
    
    // Real-time thread state sharing stuff
    void recreateSequenceAndAddToFifo() {
        
        // Create sequence of MIDI messages by reading from SEQUENCE_EVENT elements in the state
        juce::MidiMessageSequence midiSequence;
        for (int i=0; i<state.getNumChildren(); i++){
            auto child = state.getChild(i);
            if (child.hasType (IDs::SEQUENCE_EVENT)){
                midiSequence.addEvent(Helpers::eventValueTreeToMidiMessage(child));
            }
        }
        // Pre-process de MIDI sequence (update quantization, etc)
        preProcessSequence(midiSequence);
        
        // Creat ClipSequence::Ptr object to share with the RT thread
        ClipSequence::Ptr clipSequenceObject = new ClipSequence();
        clipSequenceObject->lengthInBeats = clipLengthInBeats;
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


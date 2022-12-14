/*
  ==============================================================================

    Clip.cpp
    Created: 9 May 2021 9:50:14am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#include "Clip.h"


Clip::Clip(const juce::ValueTree& _state,
           std::function<juce::Range<double>()> playheadParentSliceGetter,
           std::function<GlobalSettingsStruct()> globalSettingsGetter,
           std::function<TrackSettingsStruct()> trackSettingsGetter,
           std::function<MusicalContext*()> musicalContextGetter)
: state(_state)
{
    getGlobalSettings = globalSettingsGetter;
    getTrackSettings = trackSettingsGetter;
    getMusicalContext = musicalContextGetter;
    
    bindState();
    
    playhead = std::make_unique<Playhead>(state, playheadParentSliceGetter);
    
    startTimer(50); // Check if sequence should be updated and do it!
}

void Clip::loadStateFromOtherClipState(const juce::ValueTree& otherClipState)
{
    if (otherClipState.hasType(IDs::CLIP)){
        currentQuantizationStep = otherClipState.getProperty(IDs::currentQuantizationStep);
        replaceSequence(otherClipState, otherClipState.getProperty(IDs::clipLengthInBeats));
        updateStateMemberVersions();
    }
}

void Clip::bindState()
{
    uuid.referTo(state, IDs::uuid, nullptr, Defaults::emptyString);
    name.referTo(state, IDs::name, nullptr, Defaults::emptyString);
    clipLengthInBeats.referTo(state, IDs::clipLengthInBeats, nullptr, Defaults::clipLengthInBeats);
    wrapEventsAcrossClipLoop.referTo(state, IDs::wrapEventsAcrossClipLoop, nullptr, Defaults::wrapEventsAcrossClipLoop);
    
    // For variables that have a "state" version and a non-cached version, also assign the non-cached one so it is loaded from state
    stateCurrentQuantizationStep.referTo(state, IDs::currentQuantizationStep, nullptr, state.getProperty(IDs::currentQuantizationStep));
    currentQuantizationStep = stateCurrentQuantizationStep;
    stateWillStartRecordingAt.referTo(state, IDs::willStartRecordingAt, nullptr, Defaults::willStartRecordingAt);
    willStartRecordingAt = stateWillStartRecordingAt;
    stateWillStopRecordingAt.referTo(state, IDs::willStopRecordingAt, nullptr, Defaults::willStopRecordingAt);
    willStopRecordingAt = stateWillStopRecordingAt;
    stateRecording.referTo(state, IDs::recording, nullptr, Defaults::recording);
    recording = stateRecording;
    
    state.addListener(this);
}

void Clip::updateStateMemberVersions()
{
    // Updates all the stateX versions of the members so that their status gets reflected in the state
    if (stateRecording != recording){
        stateRecording = recording;
    }
    if (stateWillStartRecordingAt != willStartRecordingAt){
        stateWillStartRecordingAt = willStartRecordingAt;
    }
    if (stateWillStopRecordingAt != willStopRecordingAt){
        stateWillStopRecordingAt = willStopRecordingAt;
    }
    if (stateCurrentQuantizationStep != currentQuantizationStep){
        stateCurrentQuantizationStep = currentQuantizationStep;
    }
}

void Clip::timerCallback(){
        
    // Add pending recorded notes to the sequence
    addRecordedNotesToSequence();
    
    // Update clip length if requested from the processSlice method (in the RT thread)
    if (shouldUpdateClipLenthInTimerTo > -1.0){
        if (hasZeroLength() && hasSequenceEvents()){
            setClipLength(shouldUpdateClipLenthInTimerTo);
        }
        shouldUpdateClipLenthInTimerTo = -1.0;
    }
    
    // Recreate the MIDI sequence object and add it to the fifo if it has changed
    if (sequenceNeedsUpdate){
        recreateSequenceAndAddToFifo();
        sequenceNeedsUpdate = false;
    }
    
    // Update stateX member values if these have changed
    updateStateMemberVersions();
    playhead->updateStateMemberVersions();
}

void Clip::playNow()
{
    playhead->playNow();
}

void Clip::playNow(double sliceOffset)
{
    playhead->playNow(sliceOffset);
}

void Clip::playAt(double positionInGlobalPlayhead)
{
    playhead->playAt(positionInGlobalPlayhead);
}

void Clip::stopNow()
{
    if (isRecording()){
        stopRecordingNow();
    }
    playhead->stopNow();
    resetPlayheadPosition();
}

void Clip::stopAt(double positionInGlobalPlayhead)
{
    playhead->stopAt(positionInGlobalPlayhead);
}

void Clip::togglePlayStop()
{
    
    double positionInGlobalPlayhead = getMusicalContext()->getNextQuantizedBarPosition();
    
    if (isPlaying()){
        if (!isCuedToStop()){
            // If clip is playing and not cued to stop, add cue
            stopAt(positionInGlobalPlayhead);
        } else {
            // If already cued to stop, clear the cue so it will continue playing
            clearStopCue();
        }
    } else {
        if (playhead->isCuedToPlay()){
            // If clip is not playing but it is already cued to start, cancel the cue
            playhead->clearPlayCue();
            
            // And if it is also cued to start recording, clear that cue as well
            if (isCuedToStartRecording()){
                clearStartRecordingCue();
            }
        } else {
            if (!hasZeroLength()){
                // If not already cued and clip is not empty, cue to play
                playAt(positionInGlobalPlayhead);
            } else {
                // If clip has no notes, don't cue to play
            }
        }
    }
}

void Clip::clearPlayCue()
{
    playhead->clearPlayCue();
}

void Clip::clearStopCue()
{
    playhead->clearStopCue();
}

void Clip::startRecordingNow()
{
    clearStartRecordingCue();
    recording = true;
    hasJustStoppedRecordingFlag = false;
    willStopRecordingAt = -1.0;
}

void Clip::stopRecordingNow()
{
    clearStopRecordingCue();
    recording = false;
    hasJustStoppedRecordingFlag = true;
    willStopRecordingAt = -1.0;
}

void Clip::startRecordingAt(double positionInClipPlayhead)
{
    willStartRecordingAt = positionInClipPlayhead;
}

void Clip::stopRecordingAt(double positionInClipPlayhead)
{
    willStopRecordingAt = positionInClipPlayhead;
}

void Clip::toggleRecord()
{
    double nextBeatPosition;
    if (playhead->getCurrentSlice().getStart() == 0.0){
        nextBeatPosition = 0.0;  // Edge case in which the clip playhead has not yet started and it is exactly 0.0 (happens when arming clip to record with global playhead stopped, or when arming clip to record while clip is stopped (and will start playing at the same time as recording))
    } else {
        if (clipLengthInBeats > 0.0){
            // If clip has length, it could loop, therefore make sure that next beat position will also "loop"
            nextBeatPosition = std::fmod(std::floor(playhead->getCurrentSlice().getStart()) + 1, clipLengthInBeats);
        } else {
            // If clip has no length, no need to account for potential "loop" of nextBeatPosition
            nextBeatPosition = std::floor(playhead->getCurrentSlice().getStart()) + 1;
        }
    }
    if (isRecording()){
        stopRecordingNow();
        //stopRecordingAt(nextBeatPosition);  // Record until next integer beat
    } else {
        // Save current sequence state to undo stack so it can be recovered later
        saveToUndoStack();
        
        // If clip is empty and fixed length is set in main componenet, pre-set the length of the clip
        if (hasZeroLength() && getGlobalSettings().fixedLengthRecordingBars > 0){
            setClipLengthToGlobalFixedLength();
        }
        
        if (isCuedToStartRecording()){
            // If clip is already cued to start recording but it has not started, cancel the cue
            clearStartRecordingCue();
            if (isCuedToPlay()){
                clearPlayCue();
            }
        } else {
            // Otherwise, cue the clip to start recording
            startRecordingAt(nextBeatPosition);  // Start recording at next beat integer
            if (!isPlaying()){
                double positionInGlobalPlayhead = getMusicalContext()->getNextQuantizedBarPosition();
                playAt(positionInGlobalPlayhead);
            }
        }
    }
}

void Clip::clearStartRecordingCue()
{
    willStartRecordingAt = -1.0;
}

void Clip::clearStopRecordingCue()
{
    willStopRecordingAt = -1.0;
}

bool Clip::isPlaying()
{
    return playhead->isPlaying();
}

bool Clip::isCuedToPlay()
{
    return playhead->isCuedToPlay();
}

bool Clip::isCuedToStop()
{
    return playhead->isCuedToStop();
}

bool Clip::isRecording()
{
    return recording;
}

bool Clip::isCuedToStartRecording()
{
    return willStartRecordingAt >= 0.0;
}

bool Clip::isCuedToStopRecording()
{
    return willStopRecordingAt >= 0.0;
}

bool Clip::hasActiveStartCues()
{
    return isCuedToPlay() || isCuedToStartRecording();
}

bool Clip::hasActiveStopCues()
{
    return isCuedToStop() || isCuedToStopRecording();
}

bool Clip::hasActiveCues()
{
    return hasActiveStartCues() || hasActiveStopCues();
}

bool Clip::hasZeroLength()
{
    return clipLengthInBeats == 0.0;
}

int Clip::getNumSequenceEvents()
{
    return numSequenceEvents;
}

bool Clip::hasSequenceEvents()
{
    return numSequenceEvents > 0;
}

bool Clip::hasJustStoppedRecording()
{
    // This funciton will return true the first time it is called after recording has been stopped
    // Starting recording resets the flag (even if this function was never called)
    if (hasJustStoppedRecordingFlag){
        hasJustStoppedRecordingFlag = false;
        return true;
    } else {
        return false;
    }
}

void Clip::clearAllCues()
{
    clearPlayCue();
    clearStopCue();
    clearStartRecordingCue();
    clearStopRecordingCue();
}

void Clip::stopClipNowAndClearAllCues()
{
    clearAllCues();
    stopNow();
}

void Clip::setClipLength(double newLength)
{
    // NOTE: this should NOT be called from RT thread
        
    // Stop existing queues to avoid issues with cues becoming invalid (noe sure if this is needed?)
    /*if (hasActiveStopCues()){
        clearStopCue();
        clearStopRecordingCue();
    }*/
        
    clipLengthInBeats = newLength;
    
    if (newLength == 0.0){
        stopClipNowAndClearAllCues();
    }
}

void Clip::setClipLengthToGlobalFixedLength()
{
    double newLength = (double)getGlobalSettings().fixedLengthRecordingBars * (double)getMusicalContext()->getMeter();
    setClipLength(newLength);
}


void Clip::clearClipSequence()
{
    // Removes all sequence events from VT
    for (int i=state.getNumChildren() - 1; i>=0; i--){
        auto child = state.getChild(i);
        if (child.hasType (IDs::SEQUENCE_EVENT)){
            state.removeChild(i, nullptr);
        }
    }
    
    // Send note off messages for notes being played
    shouldSendRemainingNotesOff = true;
}

void Clip::clearClip()
{
    // NOTE: this should NOT be called from RT thread
    clearClipSequence();
    
    // Also sets new length to 0.0 (and this will strigger stopping the clip and clearing queues)
    setClipLength(0.0);
}

void Clip::doubleSequence()
{
    // NOTE: this should NOT be called from RT thread
    
    // Makes the midi sequence twice as long and duplicates existing events in the second repetition of it
    saveToUndoStack();
    
    // Iterate over all sequence events and re-add them at the end with doubled length
    int numChildrenBeforeDoubling = state.getNumChildren();
    for (int i=0; i<numChildrenBeforeDoubling; i++){
        auto child = state.getChild(i);
        if (child.hasType (IDs::SEQUENCE_EVENT)){
            auto childAtDoubleTime = child.createCopy();
            childAtDoubleTime.setProperty(IDs::timestamp, (double)child.getProperty(IDs::timestamp) + clipLengthInBeats, nullptr);
            state.addChild(childAtDoubleTime, -1, nullptr);
        }
    }
    setClipLength(clipLengthInBeats * 2);
}


void Clip::saveToUndoStack()
{
    // NOTE: this should NOT be called from RT thread
    
    // Add copy of clip state to the undo stack
    // If more than X elements are added to the stack, remove the older ones
    updateStateMemberVersions(); // Make sure state member versions are updated before adding to stack as IDs::clipLengthInBeats will be needed when undoing
    midiSequenceAndClipLengthUndoStack.push_back(state.createCopy());
    if (midiSequenceAndClipLengthUndoStack.size() > allowedUndoLevels){
        midiSequenceAndClipLengthUndoStack.erase(midiSequenceAndClipLengthUndoStack.begin());
    }
}

void Clip::undo()
{
    // NOTE: this should NOT be called from RT thread
    
    // Check if there are any contents available in the undo stack
    // If there are, replace the relevant parts of the current state with the relevant parts of the saved state
    if (midiSequenceAndClipLengthUndoStack.size() > 0){
        auto newState = midiSequenceAndClipLengthUndoStack.back().createCopy();
        replaceSequence(newState, newState.getProperty(IDs::clipLengthInBeats));
        midiSequenceAndClipLengthUndoStack.pop_back();
    }
}

void Clip::quantizeSequence(double quantizationStep)
{
    jassert(quantizationStep >= 0.0);
    currentQuantizationStep = quantizationStep;
}

void Clip::replaceSequence(juce::ValueTree newSequence, double newLength)
{
    // NOTE: this should NOT be called from RT thread
    
    // First remove all sequence events
    for (int i=0; i<state.getNumChildren(); i++){
        auto child = state.getChild(i);
        if (child.hasType (IDs::SEQUENCE_EVENT)){
            state.removeChild(i, nullptr);
            i = i-1;
        }
    }
    
    shouldSendRemainingNotesOff = true;
    
    // Now add the new sequence events to the clip
    for (int i=0; i<newSequence.getNumChildren(); i++){
        auto child = newSequence.getChild(i);
        if (child.hasType (IDs::SEQUENCE_EVENT)){
            auto childToAdd = child.createCopy();
            state.addChild(childToAdd, -1, nullptr);
        }
    }
    
    // Finally replace length
    setClipLength(newLength);
}

double Clip::getPlayheadPosition()
{
    return playhead->getCurrentSlice().getEnd();
}

void Clip::resetPlayheadPosition()
{
    playhead->resetSlice();
}

double Clip::getLengthInBeats()
{
    return clipLengthInBeats;
}

void Clip::renderRemainingNoteOffsIntoMidiBuffer(juce::MidiBuffer* bufferToFill)
{
    // Add midi messages to the buffer to stop all currently playing midi notes
    // Also send sustain pedal off message if sustain was on
    // Add all the messages at the very end of the buffer to make sure they go after any potential note on message sent in this buffer
    int midiOutputChannel = getTrackSettings().midiOutChannel;
    if (midiOutputChannel > -1){
        for (int i=0; i<128; i++){
            bool noteIsActive = notesCurrentlyPlayed[i] == true;
            if (noteIsActive){
                juce::MidiMessage msg = juce::MidiMessage::noteOff(midiOutputChannel, i, 0.0f);
                if (bufferToFill != nullptr) bufferToFill->addEvent(msg, getGlobalSettings().samplesPerSlice - 1);
                notesCurrentlyPlayed.setBit(i, false);
            }
        }
        
        if (sustainPedalBeingPressed){
            juce::MidiMessage msg = juce::MidiMessage::controllerEvent(midiOutputChannel, MIDI_SUSTAIN_PEDAL_CC, 0);  // Sustain pedal down!
            if (bufferToFill != nullptr) bufferToFill->addEvent(msg, getGlobalSettings().samplesPerSlice - 1);
            sustainPedalBeingPressed = false;
        }
    }
}

/** Pulls pending ClipSequence from the fifo and assigns to pointer
 */
void Clip::prepareSlice()
{
    // Pull MIDI sequence from the FIFO
    ClipSequence::Ptr t;
    while( clipSequenceObjectsFifo.pull(t) ) { ; }
    if( t != nullptr )
        clipSequenceForRTThread = t;
}

/** Process the current slice of the global playhead to tigger notes that this clip should be playing (if any) and/or record incoming notes to the clip recording sequence (if any).
    @param incommingBuffer                  MIDI buffer with the incoming MIDI notes for that slice
    @param bufferToFill                         MIDI buffer to be filled with notes triggered by this clip
    @param lastMidiNoteOnMessages   list of recent MIDI note on messages triggered during and before this slice
 
 This method should be called for each processed slice of the global playhead, regardless of whether the actual clip is being played or not. The implementation of this method is
 structured as follows:
 
 1) Check if all currently played notes should be stopped and do it if necessary. Also obtain the sequence that will need to be played back as a juce::MidiMessageSequence& object
 
 2) Make some checks about cue times and store them in variables that will be useful later for making comparissons.
 
 3) Trigger clip start if clip should start playing in this slice.
 
 4) If clip is playing (or was just triggered to start playing), trigger any notes of the clip's MIDI sequence that should be triggerd in this slice. This step takes into consideration clip's
 start and stop cue times to make sure no notes are added to "bufferToFill" which should not be added.
 
 5) If clip is playing, make some checks about start/stop recording cue times and store them in variables that will be useful later for making comparissons.
 
 6) If clip is playing, trigger clip "start recording" if it should start recording in this slice. When doing that, automatically add to "recordedMidiMessages" the MIDI notes that were played
 in the last 1/4 beat (which are in "lastMidiNoteOnMessages") as these were most probably intended to be recorded in the clip.
 
 7) If clip is playing and recording (or was just triggered to start recording), add any incoming note to the clip's MIDI sequence that should be recorded during this slice. This step takes
 into consideration clip's start recording and stop recording cue times to make sure no notes are added to "recordedMidiMessages" which should not be added.
 
 8) If clip is playing and recording, and is cued to stop recording in this slice, trigger stop recording.
 
 9) If clip is playing and should loop in this slice, loop clip's playhead position.
 
 10) Trigger clip stop if clip is cued to stop in this slice.
 
 11) If clip was stopped during this slice, send MIDI note off messages for all notes currently being played whose note off messages were not sent (because of the clip being stopped).
 
 12) If clip stopped recording in this slice, add the newly recorded notes to the clip's MIDI sequence, update clip length (if there was none set) and trigger clip loop if after setting the new length
 the clip's playhead has gone beyond it.
 
 
 See comments in the implementation for more details about each step.
 
*/
void Clip::processSlice(juce::MidiBuffer& incommingBuffer, juce::MidiBuffer* bufferToFill, juce::Array<juce::MidiMessage>& lastMidiNoteOnMessages)
{
    // 1) -------------------------------------------------------------------------------------------------
    
    if (shouldSendRemainingNotesOff){
        renderRemainingNoteOffsIntoMidiBuffer(bufferToFill);
        shouldSendRemainingNotesOff = false;
    }
    
    if (clipSequenceForRTThread == nullptr){
        return;
    }
    juce::MidiMessageSequence& sequenceToRender = clipSequenceForRTThread->sequenceAsMidi();
    
    
    // 2) -------------------------------------------------------------------------------------------------
    
    juce::Range<double> parentSliceInBeats = playhead->getParentSlice();
    bool isCuedToPlayInThisSlice = playhead->isCuedToPlay() && parentSliceInBeats.contains(playhead->getPlayAtCueBeats());
    bool isCuedToStopInThisSlice = playhead->isCuedToStop() && parentSliceInBeats.contains(playhead->getStopAtCueBeats());
    double willStartPlayingAtGlobalBeats = playhead->getPlayAtCueBeats();
    double willStopPlayingAtGlobalBeats = playhead->getStopAtCueBeats();
    
    // 3) -------------------------------------------------------------------------------------------------
    
    if (isCuedToPlayInThisSlice){
        // When calling playNow we set the playhead position so that clip start is not quantized to the start time of the
        // current slice but at the exact block sample corresponding to the start time cue
        playhead->playNow(playhead->getPlayAtCueBeats() - parentSliceInBeats.getStart());
    }
    
    if (playhead->isPlaying()){
        
        // ----------------------------------------------------------------------------------------------------
        // Acquire current playhead's slice, check if clip will loop in this slice (useful later to do some checks)
        
        playhead->captureSlice();
        const auto sliceInBeats = playhead->getCurrentSlice();
        
        bool loopingInThisSlice = false;
        if (clipSequenceForRTThread->lengthInBeats > 0.0 && sliceInBeats.contains(clipSequenceForRTThread->lengthInBeats)){
            loopingInThisSlice = true;
        }
        
        // 4) -------------------------------------------------------------------------------------------------
        // If the clip is playing, check if any notes should be added to the current slice
        // Note that if the clip starts in the middle of this slice, playhead->isPlaying() will already be
        // true because start playing cue has already been updated. In this case, we take care of not triggering
        // notes that should happen before the start time cue. Similarly, if the clip is cued to stop in this slice,
        // playhead->isPlaying() will also be true but we make sure that we don't add notes that would happen after
        // stop time cue. Note that some things like note quantization (if any), clip length adjustment, matched note
        // on/offs, etc., are already rendered in the sequence.
        for (int i=0; i < sequenceToRender.getNumEvents(); i++){
            juce::MidiMessage& msg = sequenceToRender.getEventPointer(i)->message;
            SequenceEventAnnotations* eventAnnotations = clipSequenceForRTThread->annotations[i];  // Note this could be nullptr

            double eventPositionInBeats = msg.getTimeStamp();
            if (loopingInThisSlice && eventPositionInBeats < sliceInBeats.getStart()){
                // If we're looping and the event position is before the start of the slice, make checks using looped version
                // of the event position to account for the case in which event would fall inside the looped slice
                // See example:
                // Clip notes:      [x---------------][x------ ...
                // Playhead slices: |s0  |s1  |s2  |s3  |s4  |...
                // The clip example above has only one note at the very start of it. In slice 0 (s0), the note will be correctly
                // triggered because it's starting time will be coantined in slice 0. However, the looping of the clip falls
                // in slice 3 (s3), and in that case the slice will start have a range that goes beyond the clip length time
                // (e.g. if clip has length 16.0, this could be 14.0-18.0). Therefore to correctly trigger the note at the start
                // of the clip repetition, we need to check if it is inside the slice by adding the clip length to it (checking
                // for the "looped" version).
                // Note that to make the above example easier we use slice sizes which are much bigger than what they'll really
                // be in the real app
                eventPositionInBeats += clipSequenceForRTThread->lengthInBeats;
            }

            if (sliceInBeats.contains(eventPositionInBeats))
            {
                double eventPositionInSliceInBeats = eventPositionInBeats - sliceInBeats.getStart();                
                double eventPositionInGlobalPlayheadInBeats = eventPositionInSliceInBeats + playhead->getParentSlice().getStart();
                if (isCuedToStopInThisSlice && eventPositionInGlobalPlayheadInBeats >= willStopPlayingAtGlobalBeats){
                    // Case in which the current event of the sequence falls inside the current slice but the clip is
                    // cued to stop at some point in the middle of the slice and the current event happens after that
                } else if (isCuedToPlayInThisSlice && eventPositionInGlobalPlayheadInBeats < willStartPlayingAtGlobalBeats) {
                    // Case in which the current event of the sequence falls inside the current slice but the clip is only
                    // cued to start at some point in the middle of the slice and the current event happens before that
                } else {
                    // Normal case in which notes should be triggered
                    
                    // Check if note should be triggered depending on the chance parameter
                    // Compute chance values for events of type "note on" when the chance property is lower than 1.0,
                    // otherwise there is no need to compute the chance as notes will allways be played
                    // Because note on and note off pairs will refer to the same SequenceEventAnnotations*
                    // object, when the chance is compute for the note on is the same chance value for the
                    // corresponding note off
                    if (eventAnnotations != nullptr && msg.isNoteOn() && eventAnnotations->chance < 1.0){
                        eventAnnotations->lastComputedChance = juce::Random::getSystemRandom().nextFloat();
                    }
                    // If the last computed chance is above the event chance, then skip this message
                    // as it should not be rendered in the buffer
                    // NOTE that events for which "chance" does not make sense, will have set eventAnnotations->chance to
                    // 1.0 and eventAnnotations->lastComputedChance to 0.0 by default
                    if (eventAnnotations != nullptr && eventAnnotations->lastComputedChance > eventAnnotations->chance) {
                        continue;
                    }

                    // Calculate note position for the MIDI buffer (in samples)
                    int eventPositionInSliceInSamples = eventPositionInSliceInBeats * (int)std::round(60.0 * getGlobalSettings().sampleRate / getMusicalContext()->getBpm());
                    jassert(juce::isPositiveAndBelow(eventPositionInSliceInSamples, getGlobalSettings().samplesPerSlice));
                    
                    // Re-write MIDI channel to use track's configured device, and add note to the buffer
                    int midiOutputChannel = getTrackSettings().midiOutChannel;
                    if (midiOutputChannel > -1){
                        msg.setChannel(midiOutputChannel);
                        if (bufferToFill != nullptr) bufferToFill->addEvent(msg, eventPositionInSliceInSamples);
                    }
                    
                    // If the message is of type controller, also update the internal stored state of the controller
                    if (msg.isController()){
                        auto device = getTrackSettings().device;
                        if (device != nullptr){
                            device->setMidiCCParameterValue(msg.getControllerNumber(), msg.getControllerValue());
                        }
                    }
                    
                    // Keep track of notes currently played so later we can send note offs if needed (also store sustain pedal state)
                    if      (msg.isNoteOn())  notesCurrentlyPlayed.setBit(msg.getNoteNumber(), true);
                    else if (msg.isNoteOff()) notesCurrentlyPlayed.setBit(msg.getNoteNumber(), false);
                    if      (msg.isController() && msg.getControllerName(MIDI_SUSTAIN_PEDAL_CC) && msg.getControllerValue() > 0)  sustainPedalBeingPressed = true;
                    else if (msg.isController() && msg.getControllerName(MIDI_SUSTAIN_PEDAL_CC) && msg.getControllerValue() == 0) sustainPedalBeingPressed = false;
                }
            }
        }
        
        // 5) -------------------------------------------------------------------------------------------------
        
        double willStartRecordingAtClipPlayheadBeats = willStartRecordingAt;
        double willStopRecordingAtClipPlayheadBeats = willStopRecordingAt;
        if (loopingInThisSlice){
            // If clip is looping in this slice, the sliceInBeats range can have the end value happen after the clip's
            // length and therefore "sliceInBeats.contains()" checks can fail if we are not careful and "wrap" the
            // time we're checking. See the example given in step 4, this is the same case but with recording cue times.
            if (willStartRecordingAtClipPlayheadBeats < sliceInBeats.getStart()){
                willStartRecordingAtClipPlayheadBeats += clipSequenceForRTThread->lengthInBeats;
            }
            if (willStopRecordingAtClipPlayheadBeats < sliceInBeats.getStart()){
                willStopRecordingAtClipPlayheadBeats += clipSequenceForRTThread->lengthInBeats;
            }
        }
        bool isCuedToStartRecordingInThisSlice = isCuedToStartRecording() && sliceInBeats.contains(willStartRecordingAtClipPlayheadBeats);
        bool isCuedToStopRecordingInThisSlice = isCuedToStopRecording() && sliceInBeats.contains(willStopRecordingAtClipPlayheadBeats);
                
        // 6) -------------------------------------------------------------------------------------------------
        // We should start recording if the current slice contains the willStartRecordingAt position
        // If we're starting to record, we also check if there are recent notes that were played right before the
        // start recording time and we quantize them to the start recording time as these notes were most probably
        // meant to be recorded and we don't want to skip them.
        
        if (isCuedToStartRecordingInThisSlice){
            startRecordingNow();
            
            for (auto msg: lastMidiNoteOnMessages){
                double startRecordingTimeBeatPositionInGlobalPlayhead = playhead->getParentSlice().getStart() + willStartRecordingAtClipPlayheadBeats - sliceInBeats.getStart();
                double beatsBeforeStartRecordingTimeOfCurrentMessage = startRecordingTimeBeatPositionInGlobalPlayhead - msg.getTimeStamp();
                if ((beatsBeforeStartRecordingTimeOfCurrentMessage > 0) && (beatsBeforeStartRecordingTimeOfCurrentMessage < preRecordingBeatsThreshold)){
                    // If the event time happened in the last 1/4 before the recording start position, quantize it to the start
                    // position (beat 0.0) and add it to the recorded midi sequence
                    msg.setTimeStamp(0.0);
                    recordedMidiMessages.push(msg);
                } else {
                    // If event time is equal or after the start recording time, we ignore it as it will be recorded while iterating
                    // incommingBuffer in the next step (7)
                }
            }
           
        }
    
        // 7) -------------------------------------------------------------------------------------------------
        // If clip is recording (or has started recorded during that slice), iterate over the incomming notes and add them to the record buffer.
        // If the clip only started recording in that slice, make sure we don't add notes that happen before the recording start time cue.
        // Also, if the clip should stop recording in that slice, make sure we don't add notes that happen after the recording stop time cue.
        
        if (recording){
            for (const auto metadata : incommingBuffer)
            {
                auto msg = metadata.getMessage();
                double eventPositionInBeats = sliceInBeats.getStart() + sliceInBeats.getLength() * metadata.samplePosition / getGlobalSettings().samplesPerSlice;
                
                if (!getGlobalSettings().recordAutomationEnabled && msg.isController()){
                    // If message is of type controller but record automation is not enabled, don't record the message
                } else {
                    
                    if (isCuedToStartRecordingInThisSlice && eventPositionInBeats < willStartRecordingAtClipPlayheadBeats) {
                        // Case in which we started to record in this slice and the current event happens before the start recording time. In
                        // that case we don't want to record the event.
                    } else if (isCuedToStopRecordingInThisSlice &&  eventPositionInBeats > willStopRecordingAtClipPlayheadBeats) {
                        // Case in which we will stop recording in this slice and the currnet event happens after the stop recording time. In
                        // that case we don't want to record the event.
                    } else {
                        // Case in which note should be recorded :)
                        msg.setTimeStamp(eventPositionInBeats);
                        recordedMidiMessages.push(msg);
                        
                        if (recordedMidiMessages.getAvailableSpace() < 10){
                            DBG("WARNING, recording fifo for clip " << getName() << " getting close to full or full");
                            DBG("- Available space: " << clipSequenceObjectsFifo.getAvailableSpace() << ", available for reading: " << clipSequenceObjectsFifo.getNumAvailableForReading());
                        }
                    }
                }
            }
        }
        
        // 8) -------------------------------------------------------------------------------------------------
        
        if (isCuedToStopRecordingInThisSlice){
            stopRecordingNow();
        }
        
        // 9) -------------------------------------------------------------------------------------------------
        // Check if clip length is set and, if current slice contains it, reset slice so the clip loops.
        // Note that in the next slice clip will start with an offset of some samples (positive) because the
        // loop point falls before the end of the current slice and we need to compensate for that.
        // Also consider edge case in which clipLength was changed during playback and set to something lower
        // than the current playhead position.
        
        if ((clipSequenceForRTThread->lengthInBeats > 0.0) && (sliceInBeats.contains(clipSequenceForRTThread->lengthInBeats) || clipSequenceForRTThread->lengthInBeats < sliceInBeats.getStart())){
            playhead->resetSlice(clipSequenceForRTThread->lengthInBeats - sliceInBeats.getEnd());
        }
        
        // ----------------------------------------------------------------------------------------------------
        // Release playeahd slice as we finished processing it
        // Note that releaseSlice sets the end of the playhead slice to be the same as the start. This won't have
        // any negative effect if we've manually set the playhead (e.g. because clip is looping), but it will me
        // "meaningless" as manually setting the playhead aready makes start and end time of the slice to be the same.
        
        playhead->releaseSlice();
        
    }
    
    // 10) -------------------------------------------------------------------------------------------------
    
    if (isCuedToStopInThisSlice){
        stopNow();
    }
    
    // 11) -------------------------------------------------------------------------------------------------
    // If clip has just stopped (because of a cue or because playead.stopNow() has been called externally
    // for some other reason, make sure we send note offs for pending notes
    
    if (playhead->hasJustStopped()){
        renderRemainingNoteOffsIntoMidiBuffer(bufferToFill);
    }
    
    // 12) -------------------------------------------------------------------------------------------------
    // If the clip just stopped recording, update clip length (if needed). This step needs to happen after
    // checking if the clip was cued to stop because stopping the clip might set the hasJustStoppedRecording flag
    
    if (hasJustStoppedRecording()){
        // Set new clip length in main thread if notes exist and clip had no length until now
        double newLength = 0.0;
        if (clipSequenceForRTThread->lengthInBeats == 0.0){
            newLength = std::ceil(playhead->getCurrentSlice().getEnd());
            shouldUpdateClipLenthInTimerTo = newLength;
        }
        if (clipSequenceForRTThread->lengthInBeats == 0.0 && newLength > 0.0 && newLength > playhead->getCurrentSlice().getEnd()){
            // If a new length has just been set, check if the clip should loop in this slice
            playhead->resetSlice(newLength - playhead->getCurrentSlice().getEnd());
        }
    }
}


void Clip::addRecordedNotesToSequence()
{
    // Add messages from the recordedMidiMessages fifo to the state
    // Note that corresponding note on/off messages can arrive in different calls to
    // addRecordedNotesToSequence. We only add a SEQUENCE_EVENT child when we receive
    // a note off message for a corresponding note on which was stored in
    // "recordedNoteOnMessagesPendingToAdd"
    
    juce::MidiMessage msg;
    while (recordedMidiMessages.pull(msg)) {
        if (msg.isNoteOn()){
            // Save the message to the "recordedNoteOnMessagesPendingToAdd" of pending note on messages
            // that will persist consecutive calls to addRecordedNotesToSequence
            recordedNoteOnMessagesPendingToAdd.push_back(msg);
        } else if (msg.isNoteOff()){
            // Find the corresponding pending note on message from "recordedNoteOnMessagesPendingToAdd"
            // and create a new SEQUENCE_EVENT of type "note"
            for (int i=0; i<recordedNoteOnMessagesPendingToAdd.size(); i++){
                if (recordedNoteOnMessagesPendingToAdd[i].getNoteNumber() == msg.getNoteNumber()){
                    // Found corresponding note on message, create SEQUENCE_EVENT event and remove the note on message from "recordedNoteOnMessagesPendingToAdd"
                    int midiNote = msg.getNoteNumber();
                    float midiVelocity = recordedNoteOnMessagesPendingToAdd[i].getFloatVelocity();
                    double timestamp = recordedNoteOnMessagesPendingToAdd[i].getTimeStamp();
                    // TODO: is it a problem if we obtain negative durations?
                    double duration = msg.getTimeStamp() - recordedNoteOnMessagesPendingToAdd[i].getTimeStamp();
                    if (duration < 0.0){
                        // If duration is negative, add clip length as playhead will have wrapped
                        duration += clipLengthInBeats;
                    }
                    state.addChild(Helpers::createSequenceEventOfTypeNote(timestamp, midiNote, midiVelocity, duration), -1, nullptr);
                    recordedNoteOnMessagesPendingToAdd.erase(recordedNoteOnMessagesPendingToAdd.begin() + i);
                    break;
                }
            }
        } else if (msg.isAftertouch() || msg.isController() || msg.isChannelPressure() || msg.isPitchWheel() ){
            // Save the message as SEQUENCE_EVENT of type "midi"
            state.addChild(Helpers::createSequenceEventFromMidiMessage(msg), -1, nullptr);
        }
    }
    
    if (!isRecording() && recordedNoteOnMessagesPendingToAdd.size() > 0){
        // If clip is no longer recording and there are still elements in recordedNoteOnMessagesPendingToAdd, clear them
        recordedNoteOnMessagesPendingToAdd.clear();
    }
}

double Clip::findNearestQuantizedBeatPosition(double beatPosition, double quantizationStep)
{
    if (quantizationStep > 0.0){
        return std::round(beatPosition / quantizationStep) * quantizationStep;
    } else {
        return beatPosition;
    }
}

void Clip::preProcessSequence(juce::MidiMessageSequence& sequence)
{
    // Update sequences noteOn<>noteOff pointers
    updateMatchedNoteOnOffPairs(sequence);
    
    // Remove overlapping note on/offs
    // NOTE: this seems to be not necessary as updateMatchedNoteOnOffPairs is already
    // preventing that issue from happening because it adds extra noteOff messages if
    // two consecutive noteOn messages are found
    // removeOverlappingNotesOfSameNumber(sequence);
    
    // Remove unmatched notes (just in case...)
    // NOTE: No need to remove unmatched notes because the sequence is always generated
    // in a controleld way and therefore we won't have this issue
    //removeUnmatchedNotesFromSequence(sequence);
    
    // Fix "orphan" pitch-bend messages
    makeSureSequenceResetsPitchBend(sequence);
}

void Clip::updateMatchedNoteOnOffPairs(juce::MidiMessageSequence& sequence)
{
    // Use JUCE's updateMatchedPairs which will sequentially iterate the sequence and
    // set pointers from noteOn messages to the following noteOff messages that are
    // found. If two consecutive noteOn messages are found without a noteOff (of the same
    // note number) in the middle, a new noteOff message will be inserted before the second
    // note on.
    sequence.updateMatchedPairs();
    
    // Note that the previous process can still leave some noteOn messages without the
    // pointer to the corresponding noteOffObject if the note off appears before the
    // noteOn in the sequence. This can happen if the notes wrap across clip length, and
    // this is a case not considered by JUCE (JUCE assumes note offs always come after
    // the note on). We could set the references to the matching note off manually but
    // then this will make some of JUCE's code to break (for example when copying a
    // sequence). We opt to leave these note ons without mathing pair like that, as this
    // is only used in the "getIndexOfMatchingKeyUp" midi sequence method and we don't
    // really rely on this.
        
    /*
    for (int i=0; i < sequence.getNumEvents(); i++){
        auto* meh1 = sequence.getEventPointer(i); // midi event holder
        auto& m1 = meh1->message;
        if ((m1.isNoteOn()) && (meh1->noteOffObject == nullptr)){
            // If message is note on, find a note off message with the same note number,
            // channel and which is not being pointer by another note on meh
            auto note = m1.getNoteNumber();
            auto chan = m1.getChannel();
            
            for (int j=0; j < sequence.getNumEvents(); j++){
                auto* meh2 = sequence.getEventPointer(j);
                auto& m2 = meh2->message;
                if (m2.isNoteOff() && m2.getNoteNumber() == note && m2.getChannel() == chan) {
                    // Found a candidate of note off message to use. If it is note already
                    // been pointer by another note on message, use that one
                    bool m2IsAlreadyBeingPointedByAnotherNoteOnMessage = false;
                    for (int k=0; k < sequence.getNumEvents(); k++){
                        auto* meh3 = sequence.getEventPointer(k);
                        auto& m3 = meh3->message;
                        if (m3.isNoteOn() && meh3->noteOffObject == meh2){
                            m2IsAlreadyBeingPointedByAnotherNoteOnMessage = true;
                            break;
                        }
                    }
                    
                    if (!m2IsAlreadyBeingPointedByAnotherNoteOnMessage) {
                        meh1->noteOffObject = meh2;
                        break;
                    }
                }
            }
        }
    }*/
}

void Clip::removeUnmatchedNotesFromSequence(juce::MidiMessageSequence& sequence)
{
    // Check that there are no unmatched note on/offs in the sequence and remove them if that is the case
    // Note that this assumes that sequence.updateMatchedPairs() has been called
    juce::SortedSet<int> eventsToRemove = {};
    for (int i=0; i < sequence.getNumEvents(); i++){
        juce::MidiMessage& msg = sequence.getEventPointer(i)->message;
        if (msg.isNoteOn()){
            int noteOffIndex = getIndexOfMatchingKeyUpInSequence(sequence, i);
            if (noteOffIndex == -1){
                eventsToRemove.add(i);
            }
        }
    }
    
    // Remove the events selected
    // Note that we use SoretedSet for eventsToRemove so we can iterate backwards and make sure that index positions are always decreasing and not repeated
    for (int i=(int)eventsToRemove.size() - 1; i >= 0; i--){
        sequence.deleteEvent(eventsToRemove[i], false);
    }
}

void Clip::removeOverlappingNotesOfSameNumber(juce::MidiMessageSequence& sequence)
{
    // Check that there are no two consecutive note on messages for the same note
    // number. If this is the case remove the second one (and the corresponding
    // note off message)
    juce::SortedSet<int> eventsToRemove = {};
    juce::BigInteger activeNotes = {};
    for (int i=0; i < sequence.getNumEvents(); i++){
        juce::MidiMessage& msg = sequence.getEventPointer(i)->message;
        if (msg.isNoteOn()){
            if (activeNotes[msg.getNoteNumber()] == true){
                // If note is already active it means that no note off was issued before this note on for the same number. Remove this event as we don't allow overlapping events (it could mess things up when storing "notesCurrentlyPlayed"). Also remove matched note off (if any)
                eventsToRemove.add(i);
                int noteOffIndex = getIndexOfMatchingKeyUpInSequence(sequence, i);
                if (noteOffIndex == -1){
                    eventsToRemove.add(noteOffIndex);
                }
                
            } else {
                activeNotes.setBit(msg.getNoteNumber(), true);
            }
        } else if (msg.isNoteOff()) {
            activeNotes.setBit(msg.getNoteNumber(), false);
        }
    }
    
    // Remove the events selected
    // Note that we use SoretedSet for eventsToRemove so we can iterate backwards and make sure that index positions are always decreasing and not repeated
    for (int i=(int)eventsToRemove.size() - 1; i >= 0; i--){
        sequence.deleteEvent(eventsToRemove[i], false);
    }
}

void Clip::makeSureSequenceResetsPitchBend(juce::MidiMessageSequence& sequence)
{
    // Add pitch-bend reset message at the beggining if sequence contains pitch bend messages which do not end at 0
    int lastPitchWheelMessage = 8192;
    for (int i=sequence.getNumEvents() - 1; i>=0; i--){
        juce::MidiMessage& msg = sequence.getEventPointer(i)->message;
        if (msg.isPitchWheel()){
            lastPitchWheelMessage = msg.getPitchWheelValue();
            break;
        }
    }
    if (lastPitchWheelMessage != 8192){
        // NOTE: don't care about the midi channel as it is re-written when message is thrown to the output
        juce::MidiMessage pitchWheelResetMessage = juce::MidiMessage::pitchWheel(1, 8192);
        pitchWheelResetMessage.setTimeStamp(0.0);
        sequence.addEvent(pitchWheelResetMessage);
    }
}

int Clip::getIndexOfMatchingKeyUpInSequence(juce::MidiMessageSequence& sequence, int index)
{
    // This is similar to sequence.getIndexOfMatchingKeyUp(i) but it finds
    // note off messages which can be before the note on and not only after it
    
    if (auto* meh = sequence.getEventPointer(index))
    {
        if (auto* noteOff = meh->noteOffObject)
        {
            for (int i = 0; i < sequence.getNumEvents(); i++)
                if (sequence.getEventPointer(i) == noteOff)
                    return i;

            jassertfalse; // we've somehow got a pointer to a note-off object that isn't in the sequence
        }
    }

    return -1;
}

juce::ValueTree Clip::getSequenceEventWithUUID(const juce::String& uuid)
{
    for (int i=state.getNumChildren() - 1; i>=0; i--){
        auto child = state.getChild(i);
        if (child.hasType (IDs::SEQUENCE_EVENT) && child.getProperty(IDs::uuid) == uuid){
            return child;
        }
    }
    return juce::ValueTree();
}

void Clip::removeSequenceEventWithUUID(const juce::String& uuid)
{
    auto sequenceEvent = getSequenceEventWithUUID(uuid);
    if (sequenceEvent.isValid()){
        int midiNote = -1;
        if ((int)sequenceEvent.getProperty(IDs::type) == SequenceEventType::note){
            midiNote = (int)sequenceEvent.getProperty(IDs::midiNote);
        }
        state.removeChild(sequenceEvent, nullptr);
        if (midiNote > -1){
            if (notesCurrentlyPlayed[midiNote] == true){
                juce::MidiMessage msg = juce::MidiMessage::noteOff(getTrackSettings().device->getMidiOutputChannel(), midiNote, 0.0f);
                getTrackSettings().device->sendMidi(msg);
                notesCurrentlyPlayed.setBit(midiNote, false);
            }
        }
    }
}

//==============================================================================

void Clip::valueTreePropertyChanged (juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property)
{
    // Eg: change in quantization or individual note property
    if ((property == IDs::currentQuantizationStep) ||
        (property == IDs::clipLengthInBeats) ||
        (property == IDs::timestamp) ||
        (property == IDs::uTime) ||
        (property == IDs::chance) ||
        (property == IDs::midiNote) ||
        (property == IDs::duration) ||
        (property == IDs::eventMidiBytes) ||
        (property == IDs::midiVelocity)){
        sequenceNeedsUpdate = true;
    }
}

void Clip::valueTreeChildAdded (juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenAdded)
{
    // Eg: new note added
    sequenceNeedsUpdate = true;
    
    // Update "numSequenceEvents"
    int count = 0;
    for (auto child: state) { if (child.hasType(IDs::SEQUENCE_EVENT)) {count += 1;}}
    numSequenceEvents = count;
}

void Clip::valueTreeChildRemoved (juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenRemoved, int indexFromWhichChildWasRemoved)
{
    // Eg: note removed
    sequenceNeedsUpdate = true;
    
    // Update "numSequenceEvents"
    int count = 0;
    for (auto child: state) { if (child.hasType(IDs::SEQUENCE_EVENT)) {count += 1;}}
    numSequenceEvents = count;
}

void Clip::valueTreeChildOrderChanged (juce::ValueTree& parentTree, int oldIndex, int newIndex)
{
}

void Clip::valueTreeParentChanged (juce::ValueTree& treeWhoseParentHasChanged)
{
}

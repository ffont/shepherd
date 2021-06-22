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
    sequenceEvents = std::make_unique<SequenceEventList>(state);
    
    /*
    #if !RPI_BUILD
    // Certain chance to initialize midiSequence with some notes
    // This makes testing quicker
    if (juce::Random::getSystemRandom().nextInt (juce::Range<int> (0, 10)) > 5){
        clipLengthInBeats = (double)juce::Random::getSystemRandom().nextInt (juce::Range<int> (5, 13));
        std::vector<std::pair<int, float>> noteOnTimes = {};
        for (int i=0; i<clipLengthInBeats; i++){
            noteOnTimes.push_back({i, juce::Random::getSystemRandom().nextFloat() * 0.5});
        };
        for (auto note: noteOnTimes) {
            // NOTE: don't care about the channel here because it is re-written when filling midi buffer
            int midiNote = juce::Random::getSystemRandom().nextInt (juce::Range<int> (64, 85));
            juce::MidiMessage msgNoteOn = juce::MidiMessage::noteOn(1, midiNote, 1.0f);
            msgNoteOn.setTimeStamp(note.first + note.second);
            state.addChild(Helpers::midiMessageToSequenceEventValueTree(msgNoteOn), -1, nullptr);
            
            juce::MidiMessage msgNoteOff = juce::MidiMessage::noteOff(1, midiNote, 0.0f);
            msgNoteOff.setTimeStamp(note.first + note.second + 0.25);
            state.addChild(Helpers::midiMessageToSequenceEventValueTree(msgNoteOff), -1, nullptr);
        }
    }
    #endif
     */
    
    recreateMidiSequenceFromState();
}

/** Return a pointer to a "cloned" version of the current clip which has the same MIDI sequence
 Note that the playhead status is not copied therefore the new clip will have same MIDI sequence and length, but it will be stopped, etc.
    @param clipN         position where to insert the new clip
    @param clip           clip to insert at clipN
*/
Clip* Clip::clone() const
{
    auto newClip = new Clip(
        this->state,
        this->playhead->getParentSlice,
        this->getGlobalSettings,
        this->getTrackSettings,
        this->getMusicalContext
    );
    newClip->replaceSequence(this->midiSequence, this->clipLengthInBeats);
    newClip->quantizeSequence(this->currentQuantizationStep);
    return newClip;
}

void Clip::bindState()
{
    name.referTo(state, IDs::name, nullptr, Defaults::name);
    clipLengthInBeats.referTo(state, IDs::clipLengthInBeats, nullptr, Defaults::clipLengthInBeats);
    currentQuantizationStep.referTo(state, IDs::currentQuantizationStep, nullptr, Defaults::currentQuantizationStep);
    willStartRecordingAt.referTo(state, IDs::willStartRecordingAt, nullptr, Defaults::willStartRecordingAt);
    willStopRecordingAt.referTo(state, IDs::willStopRecordingAt, nullptr, Defaults::willStopRecordingAt);
    recording.referTo(state, IDs::recording, nullptr, Defaults::recording);
}

void Clip::recreateMidiSequenceFromState()
{
    if (sequenceEvents->hasUnappliedChanges){
        midiSequence.clear();
        for (auto event: sequenceEvents->objects){
            midiSequence.addEvent(Helpers::eventValueTreeToMidiMessage(event->state));
        }
        sequenceEvents->hasUnappliedChanges = false;
        shouldUpdatePreProcessedSequence = true;
    }
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
        } else {
            if (!isEmpty() || isCuedToStartRecording()){
                // If not already cued and clip has recorded notes or it is cued to record, cue to play as well
                playAt(positionInGlobalPlayhead);
            } else {
                // If clip has no notes and it is not cued to record, don't cue to play either
                // Do nothing...
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
    saveToUndoStack(); // Save current sequence and clip length to undo stack so these can be recovered later
    recording = true;
    hasJustStoppedRecordingFlag = false;
    if (isEmpty() && getGlobalSettings().fixedLengthRecordingBars > 0){
        // If clip is empty and fixed length is set in main componenet, pre-set the length of the clip
        clipLengthInBeats = (double)getGlobalSettings().fixedLengthRecordingBars * (double)getMusicalContext()->getMeter();
    }
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
                // If clip is not playing, toggle play
                togglePlayStop();
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

bool Clip::isEmpty()
{
    // A clip is empty when it's length is 0.0 beats
    // If a clip does not have midi events but still has some length, then it is not empty
    return clipLengthInBeats == 0.0;
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

juce::String Clip::getStatus()
{
    juce::String playStatus = "";
    juce::String recordStatus = "";
    juce::String emptyStatus = "";
    
    if (isCuedToStartRecording()) {
        recordStatus = CLIP_STATUS_CUED_TO_RECORD;
    } else if (isCuedToStopRecording()) {
        recordStatus = CLIP_STATUS_CUED_TO_STOP_RECORDING;
    } else if (isRecording()) {
        recordStatus = CLIP_STATUS_RECORDING;
    } else {
        recordStatus = CLIP_STATUS_NO_RECORDING;
    }
    
    if (isCuedToPlay()) {
        playStatus = CLIP_STATUS_CUED_TO_PLAY;
    } else if (isCuedToStop()) {
        playStatus = CLIP_STATUS_CUED_TO_STOP;
    } else if (isPlaying()) {
        playStatus = CLIP_STATUS_PLAYING;
    } else {
        playStatus = CLIP_STATUS_STOPPED;
    }
    
    if (isEmpty()){
        emptyStatus = CLIP_STATUS_IS_EMPTY;
    } else {
        emptyStatus = CLIP_STATUS_IS_NOT_EMPTY;
    }
    
    return playStatus + recordStatus + emptyStatus + "|" + juce::String(clipLengthInBeats, 3) + "|" + juce::String(currentQuantizationStep);
}

void Clip::stopClipNowAndClearAllCues()
{
    clearPlayCue();
    clearStopCue();
    clearStartRecordingCue();
    clearStartRecordingCue();
    stopNow();
}

void Clip::setNewClipLength(double newLength)
{
    // Only allow to set new clip length if current length is above 0.0 and clip has no active cues to stop playing/recording,
    // otherwise this could led to edge cases which are hard to handle such as cues getting invalid or having clips with
    // empty sequences (TODO: elaborate this more)
    jassert(newLength >= 0.0);
    if (clipLengthInBeats > 0.0 && !hasActiveStopCues()){
        if (isPlaying()){
            nextClipLength = newLength;
            shouldUpdatePreProcessedSequence = true;
        } else {
            clipLengthInBeats = newLength;
            computePreProcessedSequence();
        }
    }
}

void Clip::clearClip()
{
    // Removes all midi events from the midi sequence and sets clip length to 0
    if (isPlaying()){
        // If is playing, clear sequence at the start of next process block
        shouldclearClip = true;
    } else {
        // If not playing, clear sequence immediately
        clearClipHelper();
    }
}

void Clip::clearClipHelper()
{
    // TODO: maybe refactor this so we don't need the helper?
    // This will be related to a mechanism for not editing state from real-time thread
    for (int i=0; i<state.getNumChildren(); i++){
        auto child = state.getChild(i);
        if (child.hasType (IDs::SEQUENCE_EVENT)){
            state.removeChild(i, nullptr);
            i = i-1;
        }
    }
    recreateMidiSequenceFromState();    
    clipLengthInBeats = 0.0;
    stopClipNowAndClearAllCues();
}

void Clip::doubleSequence()
{
    // Makes the midi sequence twice as long and duplicates existing events in the second repetition of it
    if (isPlaying()){
        // If is playing, double sequence at the start of next process block
        shouldDoubleSequence = true;
    } else {
        // If not playing, double sequence immediately
        doubleSequenceHelper();
    }
}

void Clip::doubleSequenceHelper()
{
    saveToUndoStack(); // Save current sequence and clip length to undo stack so these can be recovered later
    juce::MidiMessageSequence doubledSequence;
    for (int i=0; i < midiSequence.getNumEvents(); i++){
        juce::MidiMessage msg = midiSequence.getEventPointer(i)->message;
        // Add original event
        doubledSequence.addEvent(msg);
        // Add event delayed by length of clip
        msg.setTimeStamp(msg.getTimeStamp() + clipLengthInBeats);
        doubledSequence.addEvent(msg);
    }
    midiSequence = doubledSequence;
    clipLengthInBeats = clipLengthInBeats * 2;
    shouldUpdatePreProcessedSequence = true;
}


void Clip::saveToUndoStack()
{
    // Add pair of <current midi sequence, current clip length> to the undo stack so they can be used later
    // If more than X elements are added to the stack, remove the older ones
    std::pair<juce::MidiMessageSequence, double> pairForStack = {midiSequence, clipLengthInBeats};
    midiSequenceAndClipLengthUndoStack.push_back(pairForStack);
    if (midiSequenceAndClipLengthUndoStack.size() > allowedUndoLevels){
        std::vector<std::pair<juce::MidiMessageSequence, double>> newMidiSequenceAndClipLengthUndoStack;
        for (int i=allowedUndoLevels-(int)midiSequenceAndClipLengthUndoStack.size(); i<allowedUndoLevels; i++){
            newMidiSequenceAndClipLengthUndoStack.push_back(midiSequenceAndClipLengthUndoStack[i]);
        }
        midiSequenceAndClipLengthUndoStack = newMidiSequenceAndClipLengthUndoStack;
    }
}

void Clip::undo()
{
    // Check if there are any contents available in the undo stack
    // If there are, set the current midi sequence and clip length to replace
    if (midiSequenceAndClipLengthUndoStack.size() > 0){
        std::pair<juce::MidiMessageSequence, double> pairFromStack = midiSequenceAndClipLengthUndoStack.back();
        replaceSequence(pairFromStack.first, pairFromStack.second);
        midiSequenceAndClipLengthUndoStack.pop_back();
    }
}

void Clip::quantizeSequence(double quantizationStep)
{
    jassert(quantizationStep >= 0.0);
    currentQuantizationStep = quantizationStep;
    if (isPlaying()){
        // Re-compute pre-processed sequence (including new quantization) at next process slice call
        shouldUpdatePreProcessedSequence = true;
    } else {
        // Re-compute pre-processed sequence (including new quantization) now
        computePreProcessedSequence();
    }
}

void Clip::replaceSequence(juce::MidiMessageSequence newSequence, double newLength)
{
    // Replace sequence and length by new ones
    if (isPlaying()){
        // If is playing, do the replace at start of the next processSlice block
        nextMidiSequence = newSequence;
        nextClipLength = newLength;
        shouldReplaceSequence = true;
    } else {
        // If not playing, do it immediately
        midiSequence = newSequence;
        clipLengthInBeats = newLength;
    }
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
        for (int i=0; i<notesCurrentlyPlayed.size(); i++){
            juce::MidiMessage msg = juce::MidiMessage::noteOff(midiOutputChannel, notesCurrentlyPlayed[i], 0.0f);
            if (bufferToFill != nullptr) bufferToFill->addEvent(msg, getGlobalSettings().samplesPerSlice - 1);
        }
        notesCurrentlyPlayed.clear();
        
        if (sustainPedalBeingPressed){
            juce::MidiMessage msg = juce::MidiMessage::controllerEvent(midiOutputChannel, MIDI_SUSTAIN_PEDAL_CC, 0);  // Sustain pedal down!
            if (bufferToFill != nullptr) bufferToFill->addEvent(msg, getGlobalSettings().samplesPerSlice - 1);
            sustainPedalBeingPressed = false;
        }
    }
}

/** Process the current slice of the global playhead to tigger notes that this clip should be playing (if any) and/or record incoming notes to the clip recording sequence (if any).
    @param incommingBuffer                  MIDI buffer with the incoming MIDI notes for that slice
    @param bufferToFill                         MIDI buffer to be filled with notes triggered by this clip
    @param lastMidiNoteOnMessages   list of recent MIDI note on messages triggered during and before this slice
 
 This method should be called for each processed slice of the global playhead, regardless of whether the actual clip is being played or not. The implementation of this method is
 struecutred as follows:
 
 1) Apply modifications to the MIDI sequence of the clip that should be done before processing any note and that were asynchronously triggered by the message thread: clear clip,
 update length, quantize, double clip, replace sequence, etc.
 
 2) Make some checks about cue times and store them in variables that will be useful later for making comparissons.
 
 3) Trigger clip start if clip should start playing in this slice.
 
 4) If clip is playing (or was just triggered to start playing), trigger any notes of the clip's MIDI sequence that should be triggerd in this slice. This step takes into consideration clip's
 start and stop cue times to make sure no notes are added to "bufferToFill" which should not be added.
 
 5) If clip is playing, make some checks about start/stop recording cue times and store them in variables that will be useful later for making comparissons.
 
 6) If clip is playing, trigger clip "start recording" if it should start recording in this slice. When doing that, automatically add to "recordedMidiSequence" the MIDI notes that were played
 in the last 1/4 beat (which are in "lastMidiNoteOnMessages") as these were most probably intended to be recorded in the clip.
 
 7) If clip is playing and recording (or was just triggered to start recording), add any incoming note to the clip's MIDI sequence that should be recorded during this slice. This step takes
 into consideration clip's start recording and stop recording cue times to make sure no notes are added to "recordedMidiSequence" which should not be added.
 
 8) If clip is playing and recording, and is cued to stop recording in this slice, trigger stop recording.
 
 9) If clip is playing and should loop in this slice, loop clip's playhead position. If clip is recording, also add currently recorded notes to the clip's MIDI sequence so these get already
 played in the loop repetition.
 
 10) Trigger clip stop if clip is cued to stop in this slice.
 
 11) If clip was stopped during this slice, send MIDI note off messages for all notes currently being played whose note off messages were not sent (because of the clip being stopped).
 
 12) If clip stopped recording in this slice, add the newly recorded notes to the clip's MIDI sequence, update clip length (if there was none set) and trigger clip loop if after setting the new length
 the clip's playhead has gone beyond it.
 
 
 See comments in the implementation for more details about each step.
 
*/
void Clip::processSlice(juce::MidiBuffer& incommingBuffer, juce::MidiBuffer* bufferToFill, std::vector<juce::MidiMessage>& lastMidiNoteOnMessages)
{
    // 1) -------------------------------------------------------------------------------------------------
    
    recreateMidiSequenceFromState();
    
    if ((nextClipLength > -1.0) && (nextClipLength != clipLengthInBeats)){
        if (nextClipLength < clipLengthInBeats){
            // To avoid possible hanging notes, send note off to all playing notes
            renderRemainingNoteOffsIntoMidiBuffer(bufferToFill);
        }
        clipLengthInBeats = nextClipLength;
        nextClipLength = -1.0;
        if (clipLengthInBeats == 0.0){
            // If new length is set to be 0, this equivalent to celaring the clip
            shouldclearClip = true;
        }
    }
    
    if (shouldclearClip){
        renderRemainingNoteOffsIntoMidiBuffer(bufferToFill);
        clearClipHelper();
        shouldclearClip = false;
    }
    
    if (shouldReplaceSequence){
        // To avoid possible hanging notes, send note off to all playing notes
        renderRemainingNoteOffsIntoMidiBuffer(bufferToFill);
        midiSequence = nextMidiSequence;
        shouldReplaceSequence = false;
        if (isEmpty()){
            // If after replacing the clip is empty, then stop the clip and all cues
            stopClipNowAndClearAllCues();
        }
        // Trigger should update pre-processed sequence after replacing it
        shouldUpdatePreProcessedSequence = true;
    }
    
    if (shouldDoubleSequence){
        doubleSequenceHelper();
        shouldDoubleSequence = false;
    }
    
    if (shouldUpdatePreProcessedSequence){
        computePreProcessedSequence();
        shouldUpdatePreProcessedSequence = false;
    }
    
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
        if (clipLengthInBeats > 0.0 && sliceInBeats.contains(clipLengthInBeats)){
            loopingInThisSlice = true;
        }
        
        // 4) -------------------------------------------------------------------------------------------------
        // If the clip is playing, check if any notes should be added to the current slice
        // Note that if the clip starts in the middle of this slice, playhead->isPlaying() will already be
        // true because start playing cue has already been updated. In this case, we take care of not triggering
        // notes that should happen before the start time cue. Similarly, if the clip is cued to stop in this slice,
        // playhead->isPlaying() will also be true but we make sure that we don't add notes that would happen after
        // stop time cue. Note that we never read from the original sequence but from the preProcessedSequence that
        // has some things pre-computed like note quantization (if any), clip length adjustment, matched note on/offs, etc.
        
        for (int i=0; i < preProcessedMidiSequence.getNumEvents(); i++){
            juce::MidiMessage msg = preProcessedMidiSequence.getEventPointer(i)->message;
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
                eventPositionInBeats += clipLengthInBeats;
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
                            device->setMidiCCParameterValue(msg.getControllerNumber(), msg.getControllerValue(), true);
                        }
                    }
                    
                    // Keep track of notes currently played so later we can send note offs if needed (also store sustain pedal state)
                    if      (msg.isNoteOn())  notesCurrentlyPlayed.add (msg.getNoteNumber());
                    else if (msg.isNoteOff()) notesCurrentlyPlayed.removeValue (msg.getNoteNumber());
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
                willStartRecordingAtClipPlayheadBeats += clipLengthInBeats;
            }
            if (willStopRecordingAtClipPlayheadBeats < sliceInBeats.getStart()){
                willStopRecordingAtClipPlayheadBeats += clipLengthInBeats;
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
                    recordedMidiSequence.addEvent(msg);
                } else {
                    // If event time is equal or after the start recording time, we ignore it as it will be recorded while iterating
                    // incommingBuffer in the next step
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
                        recordedMidiSequence.addEvent(msg);
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
        
        if ((clipLengthInBeats > 0.0) && (sliceInBeats.contains(clipLengthInBeats) || clipLengthInBeats < sliceInBeats.getStart())){
            addRecordedSequenceToSequence();
            playhead->resetSlice(clipLengthInBeats - sliceInBeats.getEnd());
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
    // If the clip just stopped recording, add recorded notes to the sequence and update clip length (if needed). This step needs
    // to happen after checking if the clip was cued to stop because stopping the clip might set the hasJustStoppedRecording flag
    
    if (hasJustStoppedRecording()){
        if (recordedMidiSequence.getNumEvents() > 0){
            // If it has just stopped recording and there are notes to add to the sequence, add them to the sequence and
            // set new length if clip had no length. Quantize new length to the next integer beat.
            double previousLength = clipLengthInBeats;
            if (clipLengthInBeats == 0.0){
                clipLengthInBeats = std::ceil(playhead->getCurrentSlice().getEnd());
            }
            addRecordedSequenceToSequence();
            if (previousLength == 0.0 && clipLengthInBeats > 0.0 && clipLengthInBeats > playhead->getCurrentSlice().getEnd()){
                // If a new length has just been set, check if the clip should loop in this slice
                playhead->resetSlice(clipLengthInBeats - playhead->getCurrentSlice().getEnd());
            }
        } else {
            // If stopping to record, the clip is new and no new notes have been added, trigger clear clip to make it stop
            // and clear any remaining cues.
            if (isEmpty()){
                clearClipHelper();
            }
        }
    }
}


void Clip::addRecordedSequenceToSequence()
{
    if (recordedMidiSequence.getNumEvents() > 0){
        
        // If there are events in the recordedMidiSequence, add them to the sequence buffer and clear the recording buffer
        midiSequence.addSequence(recordedMidiSequence, 0);
        recordedMidiSequence.clear();
        
        // Trigger pre-process the sequence at the start of next process slice call
        shouldUpdatePreProcessedSequence = true;
    }
}

void Clip::computePreProcessedSequence()
{
    // Reset pre-processed sequence to the contents of the midi sequence
    preProcessedMidiSequence.clear();
    preProcessedMidiSequence.addSequence(midiSequence, 0);
    
    // Updates preProcessedMidiSequence to quantize it
    if (currentQuantizationStep > 0.0){
        quantizeSequence(preProcessedMidiSequence, currentQuantizationStep);
    }
    
    // Adjust length of the sequence (remove events after the length)
    removeEventsAfterTimestampFromSequence(preProcessedMidiSequence, clipLengthInBeats);
    
    // Remove unmatched notes
    removeUnmatchedNotesFromSequence(preProcessedMidiSequence);
    
    // Fix "orphan" pitch-bend messages
    makeSureSequenceResetsPitchBend(preProcessedMidiSequence);
    
}

void Clip::quantizeSequence(juce::MidiMessageSequence& sequence, double quantizationStep)
{
    if (quantizationStep > 0.0){
        for (int i=0; i < sequence.getNumEvents(); i++){
            juce::MidiMessage msgOn = sequence.getEventPointer(i)->message;
            if (msgOn.isNoteOn()){
                // If message is note on, quantize the start position of the message and the duration
                double quantizedNoteOnPositionInBeats = findNearestQuantizedBeatPosition(msgOn.getTimeStamp(), quantizationStep);
                int msgOffIndex = sequence.getIndexOfMatchingKeyUp(i);
                if (msgOffIndex > -1){
                    juce::MidiMessage msgOff = sequence.getEventPointer(msgOffIndex)->message;
                    double noteDuration = msgOff.getTimeStamp() - msgOn.getTimeStamp();
                    double quantizedNoteDuration = findNearestQuantizedBeatPosition(noteDuration, quantizationStep);
                    double quantizedNoteOffPositionInBeats = quantizedNoteOnPositionInBeats + quantizedNoteDuration;
                    if (quantizedNoteOffPositionInBeats >= clipLengthInBeats){
                        // If after quantization one event falls after the clip length, set it slightly before its end to avoid
                        // hanging notes
                        quantizedNoteOffPositionInBeats = clipLengthInBeats - 0.01;
                    }
                    sequence.getEventPointer(msgOffIndex)->message.setTimeStamp(quantizedNoteOffPositionInBeats);
                }
                sequence.getEventPointer(i)->message.setTimeStamp(quantizedNoteOnPositionInBeats);
            }
        }
    }
}

double Clip::findNearestQuantizedBeatPosition(double beatPosition, double quantizationStep)
{
    return std::round(beatPosition / quantizationStep) * quantizationStep;
}

void Clip::removeUnmatchedNotesFromSequence(juce::MidiMessageSequence& sequence)
{
    // Check that there are no unmatched note on/offs in the sequence and remove them if that is the case
    std::vector<int> eventsToRemove = {};
    sequence.updateMatchedPairs();
    for (int i=0; i < sequence.getNumEvents(); i++){
        juce::MidiMessage msg = sequence.getEventPointer(i)->message;
        if (msg.isNoteOn()){
            int noteOffIndex = sequence.getIndexOfMatchingKeyUp(i);
            if (noteOffIndex == -1){
                eventsToRemove.push_back(i);
            }
        }
    }
    for (int i=0; i < eventsToRemove.size(); i++){
        sequence.deleteEvent(eventsToRemove[i], false);
    }
}

void Clip::removeEventsAfterTimestampFromSequence(juce::MidiMessageSequence& sequence, double maxTimestamp)
{
    // Delete all events in the sequence that have timestamp greater or equal than maxTimestamp
    std::vector<int> eventsToRemove = {};
    for (int i=0; i < sequence.getNumEvents(); i++){
        juce::MidiMessage msg = sequence.getEventPointer(i)->message;
        if (msg.getTimeStamp() >= maxTimestamp){
            eventsToRemove.push_back(i);
        }
    }
    for (int i=0; i < eventsToRemove.size(); i++){
        sequence.deleteEvent(eventsToRemove[i], false);
    }
}

void Clip::makeSureSequenceResetsPitchBend(juce::MidiMessageSequence& sequence)
{
    // Add pitch-bend reset message at the beggining if sequence contains pitch bend messages which do not end at 0
    int lastPitchWheelMessage = 8192;
    for (int i=sequence.getNumEvents() - 1; i>=0; i--){
        juce::MidiMessage msg = sequence.getEventPointer(i)->message;
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

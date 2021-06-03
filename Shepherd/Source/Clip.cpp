/*
  ==============================================================================

    Clip.cpp
    Created: 9 May 2021 9:50:14am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#include "Clip.h"


Clip::Clip(std::function<juce::Range<double>()> playheadParentSliceGetter,
           std::function<double()> globalBpmGetter,
           std::function<double()> sampleRateGetter,
           std::function<int()> samplesPerBlockGetter,
           std::function<int()> midiOutChannelGetter
           )
: playhead(playheadParentSliceGetter)
{
    getGlobalBpm = globalBpmGetter;
    getSampleRate = sampleRateGetter;
    getSamplesPerBlock = samplesPerBlockGetter;
    getMidiOutChannel = midiOutChannelGetter;
    
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
            midiSequence.addEvent(msgNoteOn);
            juce::MidiMessage msgNoteOff = juce::MidiMessage::noteOff(1, midiNote, 0.0f);
            msgNoteOff.setTimeStamp(note.first + note.second + 0.25);
            midiSequence.addEvent(msgNoteOff);
        }
        shouldUpdatePreProcessedSequence = true;
    }
    #endif
}

/** Return a pointer to a "cloned" version of the current clip which has the same MIDI sequence
 Note that the playhead status is not copied therefore the new clip will have same MIDI sequence and length, but it will be stopped, etc.
    @param clipN         position where to insert the new clip
    @param clip           clip to insert at clipN
*/
Clip* Clip::clone() const
{
    auto newClip = new Clip(
        this->playhead.getParentSlice,
        this->getGlobalBpm,
        this->getSampleRate,
        this->getSamplesPerBlock,
        this->getMidiOutChannel
    );
    newClip->replaceSequence(this->midiSequence, this->clipLengthInBeats);
    return newClip;
}


void Clip::playNow()
{
    playhead.playNow();
}

void Clip::playNow(double sliceOffset)
{
    playhead.playNow(sliceOffset);
}

void Clip::playAt(double positionInGlobalPlayhead)
{
    playhead.playAt(positionInGlobalPlayhead);
}

void Clip::stopNow()
{
    if (isRecording()){
        stopRecordingNow();
    }
    playhead.stopNow();
    resetPlayheadPosition();
}

void Clip::stopAt(double positionInGlobalPlayhead)
{
    playhead.stopAt(positionInGlobalPlayhead);
}

void Clip::togglePlayStop()
{
    double globalPlayheadPosition = playhead.getParentSlice().getStart();
    double beatsRemainingForNextBar;
    if (globalPlayheadPosition == 0.0){
        // Edge case in which global playhead is stopped
        beatsRemainingForNextBar = 0.0;
    } else {
        beatsRemainingForNextBar = 4.0 - std::fmod(globalPlayheadPosition, 4.0);
    }
    double positionInGlobalPlayhead = std::round(globalPlayheadPosition + beatsRemainingForNextBar);
    
    if (isPlaying()){
        if (!isCuedToStop()){
            // If clip is playing and not cued to stop, add cue
            stopAt(positionInGlobalPlayhead);
        } else {
            // If already cued to stop, clear the cue so it will continue playing
            clearStopCue();
        }
    } else {
        if (playhead.isCuedToPlay()){
            // If clip is not playing but it is already cued to start, cancel the cue
            playhead.clearPlayCue();
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
    playhead.clearPlayCue();
}

void Clip::clearStopCue()
{
    playhead.clearStopCue();
}

void Clip::startRecordingNow()
{
    clearStartRecordingCue();
    saveToUndoStack(); // Save current sequence and clip length to undo stack so these can be recovered later
    recording = true;
    hasJustStoppedRecordingFlag = false;
}

void Clip::stopRecordingNow()
{
    clearStopRecordingCue();
    recording = false;
    hasJustStoppedRecordingFlag = true;
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
    if (playhead.getCurrentSlice().getStart() == 0.0){
        nextBeatPosition = 0.0;  // Edge case in which the clip playhead has not yet started and it is exactly 0.0 (happens when arming clip to record with global playhead stopped, or when arming clip to record while clip is stopped (and will start playing at the same time as recording))
    } else {
        if (clipLengthInBeats > 0.0){
            // If clip has length, it could loop, therefore make sure that next beat position will also "loop"
            nextBeatPosition = std::fmod(std::floor(playhead.getCurrentSlice().getStart()) + 1, clipLengthInBeats);
        } else {
            // If clip has no length, no need to account for potential "loop" of nextBeatPosition
            nextBeatPosition = std::floor(playhead.getCurrentSlice().getStart()) + 1;
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
    return playhead.isPlaying();
}

bool Clip::isCuedToPlay()
{
    return playhead.isCuedToPlay();
}

bool Clip::isCuedToStop()
{
    return playhead.isCuedToStop();
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
    jassert(newLength >= 0.0);
    nextClipLength = newLength;
    shouldUpdatePreProcessedSequence = true;
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
    midiSequence.clear();
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
    return playhead.getCurrentSlice().getEnd();
}

void Clip::resetPlayheadPosition()
{
    playhead.resetSlice();
}

double Clip::getLengthInBeats()
{
    return clipLengthInBeats;
}

void Clip::renderRemainingNoteOffsIntoMidiBuffer(juce::MidiBuffer& bufferToFill)
{
    for (int i=0; i<notesCurrentlyPlayed.size(); i++){
        juce::MidiMessage msg = juce::MidiMessage::noteOff(getMidiOutChannel(), notesCurrentlyPlayed[i], 0.0f);
        bufferToFill.addEvent(msg, 0);
    }
    notesCurrentlyPlayed.clear();
}

void Clip::processSlice(juce::MidiBuffer& incommingBuffer, juce::MidiBuffer& bufferToFill, int bufferSize, std::vector<juce::MidiMessage>& lastMidiNoteOnMessages)
{
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
    
    // Check if Clip's player is cued to play and call playNow if needed (accounting for potential offset in samples)
    if (playhead.isCuedToPlay()){
        const auto parentSliceInBeats = playhead.getParentSlice();
        if (parentSliceInBeats.contains(playhead.getPlayAtCueBeats())){
            playhead.playNow(playhead.getPlayAtCueBeats() - parentSliceInBeats.getStart());
        }
    }
    
    // If the clip is playing, check if any notes should be added to the current slice
    if (playhead.isPlaying()){
        playhead.captureSlice();
        const auto sliceInBeats = playhead.getCurrentSlice();
        
        // Read clip's sequence and play notes if needed
        // Note that we never read from the original sequence but from the preProcessedSequence that includes edits like
        // quantization (if any), length adjustment, matched note on/offs, etc.
        for (int i=0; i < preProcessedMidiSequence.getNumEvents(); i++){
            juce::MidiMessage msg = preProcessedMidiSequence.getEventPointer(i)->message;
            double eventPositionInBeats = msg.getTimeStamp();

            bool sliceContainsEvent = sliceInBeats.contains(eventPositionInBeats);
            bool sliceContainsLoopedEvent = sliceInBeats.contains(eventPositionInBeats + clipLengthInBeats);
            
            if (sliceContainsEvent || sliceContainsLoopedEvent)
            {
                double eventPositionInSliceInBeats;
                if (sliceContainsEvent) {
                    eventPositionInSliceInBeats = eventPositionInBeats - sliceInBeats.getStart();
                } else {
                    eventPositionInSliceInBeats = eventPositionInBeats + clipLengthInBeats - sliceInBeats.getStart();
                }
                double eventPositionInGlobalPlayheadInBeats = eventPositionInSliceInBeats + playhead.getParentSlice().getStart();
                if (isCuedToStop() && eventPositionInGlobalPlayheadInBeats >= playhead.getStopAtCueBeats()){
                    // Edge case in which the current event of the sequence falls inside the current slice, but the clip is cued to stop
                    // at some point in the middle of the slice therefore some notes of the sequence should not be triggered if they are after the stop point
                } else if (isCuedToPlay() && eventPositionInGlobalPlayheadInBeats < playhead.getPlayAtCueBeats()) {
                    // Edge case in which the current event of the sequence falls inside the current slice, but the clip is cued to start
                    // at some point in the middle of the slice therefore some notes of the sequence should not be triggered if they are before the start point
                } else {
                    // Normal case in which notes should be triggered
                    
                    // Calculate note position for the midi buffer (in samples)
                    int eventPositionInSliceInSamples = eventPositionInSliceInBeats * (int)std::round(60.0 * getSampleRate() / getGlobalBpm());
                    jassert(juce::isPositiveAndBelow(eventPositionInSliceInSamples, bufferSize));
                    
                    // Re-write MIDI channel (in might have changed...) and add note to the buffer
                    msg.setChannel(getMidiOutChannel());
                    bufferToFill.addEvent(msg, eventPositionInSliceInSamples);
                    
                    // Keep track of notes currently played so later we can send note offs if needed
                    if      (msg.isNoteOn())  notesCurrentlyPlayed.add (msg.getNoteNumber());
                    else if (msg.isNoteOff()) notesCurrentlyPlayed.removeValue (msg.getNoteNumber());
                }
            }
        }
        
        // Record midi events (and check if we should start/stop recording)
        // We should start recording if the current slice contains the willStartRecordingAt postiion
        // We also consider the edge case in which we start recording at end of clip after it loops
        double hasJustStartedRecordingAt = 0.0;
        if (isCuedToStartRecording()){
            if (sliceInBeats.contains(willStartRecordingAt) || sliceInBeats.contains(willStartRecordingAt + clipLengthInBeats)){
                // Store 'hasJustStartedRecordingAt' as willStartRecordingAt is reset when calling startRecordingNow() and it will be useful later
                if (sliceInBeats.contains(willStartRecordingAt)){
                    hasJustStartedRecordingAt = willStartRecordingAt;
                } else {
                    hasJustStartedRecordingAt = willStartRecordingAt + clipLengthInBeats;
                }
                startRecordingNow();
                
                // If we just started recording, we can add the latest note on messages that happened right before the current time
                // to account for human innacuracy in the timing
                for (auto msg: lastMidiNoteOnMessages){
                    double startRecordingTimeBeatPositionInGlobalPlayhead = playhead.getParentSlice().getStart() + hasJustStartedRecordingAt - sliceInBeats.getStart();
                    double beatsBeforeStartRecordingTime = startRecordingTimeBeatPositionInGlobalPlayhead - msg.getTimeStamp();
                    if ((beatsBeforeStartRecordingTime > 0) && (beatsBeforeStartRecordingTime < preRecordingBeatsThreshold)){
                       // If the event time happened in the last 1/4 before the recording start position, quantize it to the start position (beat 0.0)
                       // and add it to the recorded midi sequence
                        msg.setTimeStamp(0.0);
                        recordedMidiSequence.addEvent(msg);
                    } else {
                       // If event time is equal or after the start recording time we ignore it as it will be recorded while iterating
                       // incommingBuffer below
                    }
                }
            }
        }
    
        if (recording){
            for (const auto metadata : incommingBuffer)
            {
                auto msg = metadata.getMessage();
                double eventPositionInBeats = sliceInBeats.getStart() + sliceInBeats.getLength() * metadata.samplePosition / bufferSize;
                
                if (sliceInBeats.contains(eventPositionInBeats) && eventPositionInBeats >= hasJustStartedRecordingAt) {
                    // This condition should always be true except maybe when recorder was just started or just stopped
                    // TODO: the current implementation does record events in the slice that happen after willStopRecordingAt. This should be refactored...
                    msg.setTimeStamp(eventPositionInBeats);
                    recordedMidiSequence.addEvent(msg);
                } else {
                    //jassert() ?
                }
            }
        }
        
        if (isCuedToStopRecording()){
            if (sliceInBeats.contains(willStopRecordingAt) || sliceInBeats.contains(willStopRecordingAt + clipLengthInBeats)){ // Edge case: stop recording at end of clip
                stopRecordingNow();
            }
        }
        
        // Loop the clip if needed
        // Check if Clip length and, if current slice contains it, reset slice so the clip loops.
        // Note that in the next slice clip will start with an offset of some samples (positive) because the
        // loop point falls before the end of the current slice and we need to compensate for that.
        // Also consider edge case in which clipLength was changed during playback and set to something lower than the current playhead position
        if ((clipLengthInBeats > 0.0) && (sliceInBeats.contains(clipLengthInBeats) || clipLengthInBeats < sliceInBeats.getStart())){
            addRecordedSequenceToSequence();
            playhead.resetSlice(clipLengthInBeats - sliceInBeats.getEnd());
        }
        
        // Release slice as we finished processing it
        // Note that releaseSlice sets the end of the playhead slice to be the same as the start. This won't have
        // any negative effect if we've restarted playhead because clip is looping, but it will me meaningless.
        playhead.releaseSlice();
        
    }
    if (playhead.hasJustStopped()){
        renderRemainingNoteOffsIntoMidiBuffer(bufferToFill);
    }
    
    if (hasJustStoppedRecording()){
        if (recordedMidiSequence.getNumEvents() > 0){
            // If it has just stopped recording and there are notes to add to the sequence, do it now and set new length
            double previousLength = clipLengthInBeats;
            if (clipLengthInBeats == 0.0){
                // If clip had no length, set it to the current time quantized to the next beat integer
                clipLengthInBeats = std::ceil(playhead.getCurrentSlice().getEnd());
            }
            addRecordedSequenceToSequence();
            if (previousLength == 0.0 && clipLengthInBeats > 0.0 && clipLengthInBeats > playhead.getCurrentSlice().getEnd()){
                // If the clip had no length and after stopping recording now it has, check if we should reset the playeahd for lopping
                playhead.resetSlice(clipLengthInBeats - playhead.getCurrentSlice().getEnd());
            }
        } else {
            // If stopping to record, the clip is new and no new notes have been added, trigger clear clip to stop playing, etc.
            if (isEmpty()){
                clearClipHelper();
            }
        }
    }
    
    // Check if Clip's player is cued to stop and call stopNow if needed
    if (playhead.isCuedToStop()){
        const auto parentSliceInBeats = playhead.getParentSlice();
        if (parentSliceInBeats.contains(playhead.getStopAtCueBeats())){
            stopNow();
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

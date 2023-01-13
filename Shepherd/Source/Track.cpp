/*
  ==============================================================================

    Track.cpp
    Created: 12 May 2021 4:14:25pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#include "Track.h"

Track::Track(const juce::ValueTree& _state,
             std::function<juce::Range<double>()> playheadParentSliceGetter,
             std::function<GlobalSettingsStruct()> globalSettingsGetter,
             std::function<MusicalContext*()> musicalContextGetter,
             std::function<HardwareDevice*(juce::String deviceName, HardwareDeviceType type)> hardwareDeviceGetter,
             std::function<MidiOutputDeviceData*(juce::String deviceName)> midiOutputDeviceDataGetter
             ): state(_state)
{
    lastMidiNoteOnMessages.ensureStorageAllocated(MIDI_BUFFER_MIN_BYTES);
    lastSliceMidiBuffer.ensureSize(MIDI_BUFFER_MIN_BYTES);
    incomingMidiBuffer.ensureSize(MIDI_BUFFER_MIN_BYTES);
    
    getPlayheadParentSlice = playheadParentSliceGetter;
    getGlobalSettings = globalSettingsGetter;
    getMusicalContext = musicalContextGetter;
    getHardwareDeviceByName = hardwareDeviceGetter;
    getMidiOutputDeviceData = midiOutputDeviceDataGetter;
    bindState();
    
    if (hardwareDeviceName != ""){
        setOutputHardwareDeviceByName(hardwareDeviceName);
    }
    prepareClips();
}

void Track::bindState()
{
    uuid.referTo(state, ShepherdIDs::uuid, nullptr, ShepherdDefaults::emptyString);
    name.referTo(state, ShepherdIDs::name, nullptr, ShepherdDefaults::emptyString);
    
    inputMonitoring.referTo(state, ShepherdIDs::inputMonitoring, nullptr, ShepherdDefaults::inputMonitoring);
    hardwareDeviceName.referTo(state, ShepherdIDs::outputHardwareDeviceName, nullptr, ShepherdDefaults::emptyString);
}

void Track::setOutputHardwareDeviceByName(juce::String deviceName)
{
    auto device = getHardwareDeviceByName(deviceName, HardwareDeviceType::output);
    if (device != nullptr) {
        // If a device is found with that name, set it, otherwise do nothing
        setOutputHardwareDevice(device);
    }
}

void Track::setOutputHardwareDevice(HardwareDevice* device)
{
    if ((device != nullptr) && (device->isTypeOutput())){
        outputHwDevice = device;
        hardwareDeviceName = outputHwDevice->getShortName();
    }
}

HardwareDevice* Track::getOutputHardwareDevice()
{
    return outputHwDevice;
}

juce::MidiBuffer* Track::getMidiOutputDeviceBufferIfDevice()
{
    if (outputHwDevice == nullptr){
        // If device is null pointer, it means no hardware device is yet assinged and no therefore no corresponding MIDI buffer
        return nullptr;
    }
    auto midiOutputDeviceData = getMidiOutputDeviceData(outputHwDevice->getMidiOutputDeviceName());
    if (midiOutputDeviceData == nullptr) { return nullptr; }
    juce::MidiBuffer* bufferToFill = &midiOutputDeviceData->buffer;
    if (bufferToFill == nullptr){
        // If the buffer to fill is null pointer, it means the corresponding MIDI device could not be initialized and there's no corresponding MIDI buffer
        return nullptr;
    } else {
        return bufferToFill;
    }
}

juce::String Track::getMidiOutputDeviceName()
{
    if (outputHwDevice != nullptr){
        return outputHwDevice->getMidiOutputDeviceName();
    } else {
        return "";
    }
}

int Track::getMidiOutputChannel()
{
    if (outputHwDevice != nullptr){
        return outputHwDevice->getMidiOutputChannel();
    } else {
        return -1;
    }
}

void Track::prepareClips()
{
    clips = std::make_unique<ClipList>(state,
                                       getPlayheadParentSlice,
                                       getGlobalSettings,
                                       [this]{
                                           TrackSettingsStruct settings;
                                           settings.midiOutChannel = getMidiOutputChannel();
                                           settings.outputHwDevice = getOutputHardwareDevice();
                                           return settings;
                                       },
                                       getMusicalContext);
}

int Track::getNumberOfClips()
{
    return clips->objects.size();
}

void Track::processInputMessagesFromInputHardwareDevice(HardwareDevice* inputDevice,
                                                        double sliceLengthInBeats,
                                                        int sliceNumSamples,
                                                        double countInPlayheadPositionInBeats,
                                                        double playheadPositionInBeats,
                                                        int meter,
                                                        bool playheadIsDoingCountIn)
{
    if (inputDevice->isTypeOutput()) {return;}  // Provided device is not of type input
    if (getOutputHardwareDevice() == nullptr){return;} // Track's output device has not been initialized
    if (!hasClipsCuedToRecordOrRecording() && !inputMonitoringEnabled()) {return;}  // If track has no clips cued to record/recording and is not input monitoring, don't handle input data
    
    // Process the incoming messages from the input device according to the track's output device (e.g. change midi channel,
    // change CC values if CC input is relative, etc...)
    inputDevice->processAndRenderIncomingMessagesIntoBuffer(incomingMidiBuffer, getOutputHardwareDevice());
    
    // store the lastMidiNoteOnMessages as this is needed when processing slice in Clip to account for notes that should be recorded with timestamp "0"
    for (auto metadata: incomingMidiBuffer){
        juce::MidiMessage msg = metadata.getMessage();
        
        // Store message in the "list of last played notes" and set its timestamp to the global playhead position
        if (msg.isNoteOn()){
            juce::MidiMessage msgToStoreInQueue = juce::MidiMessage(msg);
            if (playheadIsDoingCountIn){
                msgToStoreInQueue.setTimeStamp(countInPlayheadPositionInBeats - meter + metadata.samplePosition/sliceNumSamples * sliceLengthInBeats);
            } else {
                msgToStoreInQueue.setTimeStamp(playheadPositionInBeats + metadata.samplePosition/sliceNumSamples * sliceLengthInBeats);
            }
            lastMidiNoteOnMessages.insert(0, msgToStoreInQueue);
        }
    }
    
    // Remove old messages from lastMidiNoteOnMessages if we are already storing lastMidiNoteOnMessagesToStore
    if (lastMidiNoteOnMessages.size() > lastMidiNoteOnMessagesToStore){
        lastMidiNoteOnMessages.removeLast(lastMidiNoteOnMessages.size() - lastMidiNoteOnMessagesToStore);
    }
    
    // Copy notes to output buffer if inpur monitoring is enabled
    if (inputMonitoringEnabled()){
        // If input monitoring is enabled, copy the processed contents of incomingMidiBuffer to the lastSliceMidiBuffer so these get passed
        //lastSliceMidiBuffer.addEvents(incomingMidiBuffer, 0, sliceNumSamples, 0);
        for (const auto metadata : incomingMidiBuffer){
            lastSliceMidiBuffer.addEvent(metadata.getMessage(), metadata.samplePosition);
        }
    }
}

void Track::clipsProcessSlice()
{
    for (auto clip: clips->objects){
        clip->processSlice(incomingMidiBuffer, &lastSliceMidiBuffer, lastMidiNoteOnMessages);
    }
}

void Track::clipsPrepareSlice()
{
    for (auto clip: clips->objects){
        clip->prepareSlice();
    }
}

void Track::clipsRenderRemainingNoteOffsIntoMidiBuffer()
{
    for (auto clip: clips->objects){
        clip->renderRemainingNoteOffsIntoMidiBuffer(&lastSliceMidiBuffer);
    }
}

void Track::clipsResetPlayheadPosition()
{
    for (auto clip: clips->objects){
        clip->resetPlayheadPosition();
    }
}

Clip* Track::getClipAt(int clipN)
{
    jassert(clipN < clips->objects.size());
    return clips->objects[clipN];
}

Clip* Track::getClipWithUUID(juce::String clipUUID)
{
    return clips->getObjectWithUUID(clipUUID);
}


/** Stop all track clips that are currently playing
    @param now             stop clips immediately, otherwise wait until next quatized step
    @param deCue         de-cue all clips cued to play or record but that did not yet start playing or recording
    @param reCue         re-cue all non-empty clips that where stopped so that they start playing again at next 0.0 global beat position
*/
void Track::stopAllPlayingClips(bool now, bool deCue, bool reCue)
{
    // See stopAllPlayingClipsExceptFor for docs
    stopAllPlayingClipsExceptFor(-1, now, deCue, reCue);
}

/** Stop all track clips that are currently playing
    @param clipN         do not stop this clip
    @param now             stop clips immediately, otherwise wait until next quatized step
    @param deCue         de-cue all clips cued to play or record but that did not yet start playing or recording
    @param reCue         re-cue all non-empty clips that where stopped so that they start playing again at next 0.0 global beat position
*/
void Track::stopAllPlayingClipsExceptFor(int clipN, bool now, bool deCue, bool reCue)
{
    jassert(clipN < clips->objects.size());
    for (int i=0; i<clips->objects.size(); i++){
        if (i != clipN){
            auto clip = clips->objects[i];
            bool wasPlaying = false;
            if (clip->isPlaying()){
                wasPlaying = true;
                if (!now){
                    if (!clip->isCuedToStop()){
                        // Only toggle if not already cued to stop
                        clip->togglePlayStop();
                    }
                } else {
                    clip->stopNow();
                }
            }
            if (deCue){
                if (clip->isCuedToPlay()){
                    clip->clearPlayCue();
                }
                if (clip->isCuedToStartRecording()){
                    clip->clearStartRecordingCue();
                }
            }
            if (reCue && wasPlaying && !clip->hasZeroLength()){
                clip->playAt(0.0);
            }
        }
    }
}

/** Stop all track clips that are currently playing
    @param clipUUID  do not stop this clip
    @param now             stop clips immediately, otherwise wait until next quatized step
    @param deCue         de-cue all clips cued to play or record but that did not yet start playing or recording
    @param reCue         re-cue all non-empty clips that where stopped so that they start playing again at next 0.0 global beat position
*/
void Track::stopAllPlayingClipsExceptFor(juce::String clipUUID, bool now, bool deCue, bool reCue)
{

    for (int i=0; i<clips->objects.size(); i++){
        auto clip = clips->objects[i];
        if (clip->getUUID() == clipUUID){
            stopAllPlayingClipsExceptFor(i, now, deCue, reCue);
            return;
        }
    }
}

std::vector<int> Track::getCurrentlyPlayingClipsIndex()
{
    std::vector<int> currentlyPlayingClips = {};
    for (int i=0; i<clips->objects.size(); i++){
        auto clip = clips->objects[i];
        if (clip->isPlaying()){
            currentlyPlayingClips.push_back(i);
        }
    }
    return currentlyPlayingClips;
}

/** Duplicates the contents of the clip at scene N to the clip of scene N+1.  All clips below that position will be shifted downwards to accomodate the new clip. Contents of the last clip will be lost if overflowing max num clips.
    @param clipN         position of the clip to duplicate
*/
void Track::duplicateClipAt(int clipN)
{
    if ((clipN >= 0) && (clipN < clips->objects.size() - 2)){
        // Don't try to duplicate last clip as there's no more space for it
        juce::ValueTree previousClipState = clips->objects[clipN]->state.createCopy();
        for (int i=clipN + 1; i<clips->objects.size(); i++){
            juce::ValueTree previousClipStateAux = clips->objects[i]->state.createCopy();
            bool replaceSequenceEventUUIDs = i == (clipN + 1);  // For the duplicated clip, change UUIDs of sequence events to avoid their repetitions
            clips->objects[i]->loadStateFromOtherClipState(previousClipState, replaceSequenceEventUUIDs);
            previousClipState = previousClipStateAux;
        }
    }
}

bool Track::hasClipsCuedToRecord()
{
    for (auto clip: clips->objects){
        if (clip->isCuedToStartRecording()){
            return true;
        }
    }
    return false;
}

bool Track::hasClipsCuedToRecordOrRecording()
{
    for (auto clip: clips->objects){
        if (clip->isCuedToStartRecording() || clip->isRecording()){
            return true;
        }
    }
    return false;
}

bool Track::inputMonitoringEnabled()
{
    return inputMonitoring == true;
}

void Track::setInputMonitoring(bool enabled)
{
    inputMonitoring = enabled;
}

void Track::clearMidiBuffers()
{
    lastSliceMidiBuffer.clear();
    incomingMidiBuffer.clear();
}

juce::MidiBuffer* Track::getLastSliceMidiBuffer()
{
    return &lastSliceMidiBuffer;
}

void Track::writeLastSliceMidiBufferToHardwareDeviceMidiBuffer()
{
    juce::MidiBuffer* hardwareDeviceMidiBuffer = getMidiOutputDeviceBufferIfDevice();
    if (hardwareDeviceMidiBuffer != nullptr){
        hardwareDeviceMidiBuffer->addEvents(lastSliceMidiBuffer, 0, getGlobalSettings().samplesPerSlice, 0);
    }
}

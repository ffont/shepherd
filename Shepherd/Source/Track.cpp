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
             std::function<HardwareDevice*(juce::String deviceName)> hardwareDeviceGetter,
             std::function<juce::MidiBuffer*(juce::String deviceName)> midiOutputDeviceBufferGetter
             ): state(_state)
{
    getPlayheadParentSlice = playheadParentSliceGetter;
    getGlobalSettings = globalSettingsGetter;
    getMusicalContext = musicalContextGetter;
    getHardwareDeviceByName = hardwareDeviceGetter;
    getMidiOutputDeviceBuffer = midiOutputDeviceBufferGetter;
    bindState();
    
    if (hardwareDeviceName != ""){
        setHardwareDeviceByName(hardwareDeviceName);
    }
    prepareClips();
}

void Track::bindState()
{
    enabled.referTo(state, IDs::enabled, nullptr, true);
    uuid.referTo(state, IDs::uuid, nullptr, Defaults::emptyString);
    name.referTo(state, IDs::name, nullptr, Defaults::emptyString);
    order.referTo(state, IDs::order, nullptr, Defaults::order);
    
    inputMonitoring.referTo(state, IDs::inputMonitoring, nullptr, Defaults::inputMonitoring);
    hardwareDeviceName.referTo(state, IDs::hardwareDeviceName, nullptr, Defaults::emptyString);
}

void Track::setHardwareDeviceByName(juce::String deviceName)
{
    setHardwareDevice(getHardwareDeviceByName(deviceName));
}

void Track::setHardwareDevice(HardwareDevice* _device)
{
    device = _device;
}

HardwareDevice* Track::getHardwareDevice()
{
    return device;
}

juce::MidiBuffer* Track::getMidiOutputDeviceBufferIfDevice()
{
    if (device == nullptr){
        // If device is null pointer, it means no hardware device is yet assinged and no therefore no corresponding MIDI buffer
        return nullptr;
    }
    
    juce::MidiBuffer* bufferToFill = getMidiOutputDeviceBuffer(device->getMidiOutputDeviceName());
    if (bufferToFill == nullptr){
        // If the buffer to fill is null pointer, it means the corresponding MIDI device could not be initialized and there's no corresponding MIDI buffer
        return nullptr;
    } else {
        return bufferToFill;
    }
}

juce::String Track::getMidiOutputDeviceName()
{
    if (device != nullptr){
        return device->getMidiOutputDeviceName();
    } else {
        return "";
    }
}

int Track::getMidiOutputChannel()
{
    if (device != nullptr){
        return device->getMidiOutputChannel();
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
                                           settings.enabled = isEnabled();
                                           settings.midiOutChannel = getMidiOutputChannel();
                                           settings.device = getHardwareDevice();
                                           return settings;
                                       },
                                       getMusicalContext);
}

int Track::getNumberOfClips()
{
    return clips->objects.size();
}

void Track::processInputMonitoring(juce::MidiBuffer& incommingBuffer)
{
    if (inputMonitoringEnabled()){
        for (const auto metadata : incommingBuffer)
        {
            int midiOutputChannel = getMidiOutputChannel();
            if (midiOutputChannel > -1){
                // If channel is -1, it means that device has not been initialized
                auto msg = metadata.getMessage();
                msg.setChannel(getMidiOutputChannel());
                lastSliceMidiBuffer.addEvent(msg, metadata.samplePosition);
                
                // If message is of type controller, also update the internal stored state of the controller
                if (msg.isController()){
                    if (device != nullptr){
                        device->setMidiCCParameterValue(msg.getControllerNumber(), msg.getControllerValue(), true);
                    }
                }
            }
        }
    }
}

void Track::clipsProcessSlice(juce::MidiBuffer& incommingBuffer, juce::Array<juce::MidiMessage>& lastMidiNoteOnMessages)
{
    for (auto clip: clips->objects){
        clip->processSlice(incommingBuffer, &lastSliceMidiBuffer, lastMidiNoteOnMessages);
    }
}

void Track::clipsPrepareSliceSlice()
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
            clips->objects[i]->loadStateFromOtherClipState(previousClipState);
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

bool Track::inputMonitoringEnabled()
{
    return inputMonitoring == true;
}

void Track::setInputMonitoring(bool enabled)
{
    inputMonitoring = enabled;
}

void Track::clearLastSliceMidiBuffer()
{
    lastSliceMidiBuffer.clear();
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

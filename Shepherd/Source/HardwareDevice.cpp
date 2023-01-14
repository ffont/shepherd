/*
  ==============================================================================

    HardwareDevice.cpp
    Created: 8 Jun 2021 6:35:41am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#include "HardwareDevice.h"


HardwareDevice::HardwareDevice(const juce::ValueTree& _state,
                               std::function<MidiOutputDeviceData*(juce::String deviceName)> midiOutputDeviceDataGetter,
                               std::function<MidiInputDeviceData*(juce::String deviceName)> midiInputDeviceDataGetter
                               ): state(_state)
{
    getMidiOutputDeviceData = midiOutputDeviceDataGetter;
    getMidiInputDeviceData = midiInputDeviceDataGetter;
    
    bindState();
    
    if (isTypeOutput()){
        for (int i=0; i<midiCCParameterValues.size(); i++){
            midiCCParameterValues[i] = 64;  // Initialize all midi ccs to 64 (middle value)
        }
        stateMidiCCParameterValues = ShepherdHelpers::serialize128IntArray(midiCCParameterValues); // Update the state version of the midiCCParameterValues list so change is reflected in state
    }
    
    if (isTypeInput()){
        if (stateControlChangeMapping == ""){
            for (int i=0; i<controlChangeMapping.size(); i++){
                controlChangeMapping[i] = i;  // Initialize all midi cc mappings to the input number (no transformation)
            }
            stateControlChangeMapping = ShepherdHelpers::serialize128IntArray(controlChangeMapping); // Update the state version of the controlChangeMapping list so change is reflected in state
        } else {
            controlChangeMapping = ShepherdHelpers::deserialize128IntArray(stateControlChangeMapping);
        }

        if (stateNotesMapping == ""){
            for (int i=0; i<notesMapping.size(); i++){
                notesMapping[i] = i;  // Initialize all midi note number mappings to the input number (no transformation)
            }
            stateNotesMapping = ShepherdHelpers::serialize128IntArray(notesMapping); // Update the state version of the notesMapping list so change is reflected in state
        } else {
            notesMapping = ShepherdHelpers::deserialize128IntArray(stateNotesMapping);
        }
    }
}

void HardwareDevice::bindState()
{
    uuid.referTo(state, ShepherdIDs::uuid, nullptr, ShepherdDefaults::emptyString);
    type.referTo(state, ShepherdIDs::type, nullptr, HardwareDeviceType::output);
    name.referTo(state, ShepherdIDs::name, nullptr, ShepherdDefaults::emptyString);
    shortName.referTo(state, ShepherdIDs::shortName, nullptr, ShepherdDefaults::emptyString);
    
    midiOutputDeviceName.referTo(state, ShepherdIDs::midiOutputDeviceName, nullptr, ShepherdDefaults::emptyString);
    midiOutputChannel.referTo(state, ShepherdIDs::midiChannel, nullptr, -1);
    
    midiInputDeviceName.referTo(state, ShepherdIDs::midiInputDeviceName, nullptr, ShepherdDefaults::emptyString);
    allowedMidiInputChannel.referTo(state, ShepherdIDs::allowedMidiInputChannel, nullptr, ShepherdDefaults::allowedMidiInputChannel);
    allowNoteMessages.referTo(state, ShepherdIDs::allowNoteMessages, nullptr, ShepherdDefaults::allowNoteMessages);
    allowControllerMessages.referTo(state, ShepherdIDs::allowControllerMessages, nullptr, ShepherdDefaults::allowControllerMessages);
    allowPitchBendMessages.referTo(state, ShepherdIDs::allowPitchBendMessages, nullptr, ShepherdDefaults::allowPitchBendMessages);
    allowAftertouchMessages.referTo(state, ShepherdIDs::allowAftertouchMessages, nullptr, ShepherdDefaults::allowAftertouchMessages);
    allowChannelPressureMessages.referTo(state, ShepherdIDs::allowChannelPressureMessages, nullptr, ShepherdDefaults::allowChannelPressureMessages);
    controlChangeMessagesAreRelative.referTo(state, ShepherdIDs::controlChangeMessagesAreRelative, nullptr, ShepherdDefaults::controlChangeMessagesAreRelative);

    stateMidiCCParameterValues.referTo(state, ShepherdIDs::midiCCParameterValuesList, nullptr, ShepherdDefaults::emptyString);
    stateControlChangeMapping.referTo(state, ShepherdIDs::controlChangeMapping, nullptr, ShepherdDefaults::emptyString);
    stateNotesMapping.referTo(state, ShepherdIDs::notesMapping, nullptr, ShepherdDefaults::emptyString);
    // NOTE: unlike other stateXXX properties in other objects like Clip, midiCCParameterValues and others here should never be loaded from state, so we don't do it here
}

// -------------------------------------- OUTPUT DEVICES

void HardwareDevice::sendMidi(juce::MidiMessage msg)
{
    auto midiDevice = getMidiOutputDeviceData(getMidiOutputDeviceName())->device.get();
    if (midiDevice != nullptr){
        addMidiMessageToRenderInBufferFifo(msg);
        if (msg.isController()){
            setMidiCCParameterValue(msg.getControllerNumber(), msg.getControllerValue());
        }
    }
}

void HardwareDevice::sendAllNotesOff()
{
    // The MIDI specification has a method to set all notes off, but this does not seem to always work well
    // Insetad, we send one note off for each possible note. Additionally we send sustain pedal off
    for (int i=0; i<128; i++){
        sendMidi(juce::MidiMessage::noteOff(getMidiOutputChannel(), i));
    }
    sendMidi(juce::MidiMessage::controllerEvent(getMidiOutputChannel(), MIDI_SUSTAIN_PEDAL_CC, 0));
}

void HardwareDevice::loadPreset(int bankNumber, int presetNumber)
{
    // Send a bank change followed by a preset change event
    // Add some sleep in the middle as some synths are known to have issues if the two messages
    // are sent at the same time
    sendMidi(juce::MidiMessage::controllerEvent(getMidiOutputChannel(), MIDI_BANK_CHANGE_CC, bankNumber));
    juce::Time::waitForMillisecondCounter(juce::Time::getMillisecondCounter() + 50);
    sendMidi(juce::MidiMessage::programChange(getMidiOutputChannel(), presetNumber));
}

int HardwareDevice::getMidiCCParameterValue(int index)
{
    // NOTE: this function is to read the parameter value form the internal state, but it is not expected to read
    // the value from the hardware device
    jassert(index >= 0 && index < 128);
    return midiCCParameterValues[index];
}

void HardwareDevice::setMidiCCParameterValue(int index, int value)
{
    // NOTE: this function is to store the parameter value in the internal state, but it is not expected to communicate
    // this value to the hardware device
    jassert(index >= 0 && index < 128);
    midiCCParameterValues[index] = value;
    stateMidiCCParameterValues = ShepherdHelpers::serialize128IntArray(midiCCParameterValues); // Update the state version of the midiCCParameterValues list so change is reflected in state
}

void HardwareDevice::addMidiMessageToRenderInBufferFifo(juce::MidiMessage msg)
{
    midiMessagesToRenderInBuffer.push(msg);
    
    if (midiMessagesToRenderInBuffer.getAvailableSpace() < 10){
        DBG("WARNING, midi messages fifo for hardware device " << getName() << " getting close to full or full");
        DBG("- Available space: " << midiMessagesToRenderInBuffer.getAvailableSpace() << ", available for reading: " << midiMessagesToRenderInBuffer.getNumAvailableForReading());
    }
}

void HardwareDevice::renderPendingMidiMessagesToRenderInBuffer()
{
    // If there are pending MIDI messages to be rendered in the hardware device buffer buffer, send them
    juce::MidiMessage msg;
    auto midiOutputDeviceData = getMidiOutputDeviceData(getMidiOutputDeviceName());
    if (midiOutputDeviceData == nullptr) { return; }
    juce::MidiBuffer* buffer = &midiOutputDeviceData->buffer;
    while (midiMessagesToRenderInBuffer.pull(msg)) {
        int deviceMidiOutputChannel = getMidiOutputChannel();
        if ((buffer != nullptr) && (deviceMidiOutputChannel > -1)){
            msg.setChannel(deviceMidiOutputChannel);
            buffer->addEvent(msg, 0);
        }
    }
}

// -------------------------------------- INPUT DEVICES

bool HardwareDevice::filterAndProcessIncomingMidiMessage(juce::MidiMessage& msg, HardwareDevice* outputDevice)
{
    // Return false if message should not be added to incoming buffer, also modify message according to the target output device
    // (e.g. change midi ouput channel)
    
    if (allowedMidiInputChannel.get() != 0){
        if (msg.getChannel() != allowedMidiInputChannel.get()){
            return false;
        }
    }
    
    int newMidiChannel = outputDevice->getMidiOutputChannel();
    
    if (msg.isNoteOnOrOff() || msg.isAftertouch()){
        int newNoteNumber = notesMapping[msg.getNoteNumber()];
        if (newNoteNumber == -1){
            return false;  // Message should be discarted
        } else {
            msg.setNoteNumber(newNoteNumber);  // Update note number according to mapping
        }
        if ((msg.isNoteOnOrOff() && allowNoteMessages.get()) || (msg.isAftertouch() && allowAftertouchMessages.get())){
            msg.setChannel(newMidiChannel);
            return true;
        }
    }
    else if (msg.isController() && allowControllerMessages.get()){
        int newControllerNumber = controlChangeMapping[msg.getControllerNumber()];
        int newControllerValue = msg.getControllerValue();
        if (newControllerNumber == -1){
            return false;  // Message should be discarted
        } else {
            // If cc messages are from a "relative" controller, compute the absolute cc value that shoud be sent, otherwise keep original value
            if (controlChangeMessagesAreRelative.get()){
                int rawControllerValue = msg.getControllerValue();
                int increment = 0;
                if (rawControllerValue > 0 && rawControllerValue < 64){
                    increment = rawControllerValue;
                } else {
                    increment = rawControllerValue - 128;
                }
                int currentValue = outputDevice->getMidiCCParameterValue(newControllerNumber);
                int absoluteControllerValue = currentValue + increment;
                if (absoluteControllerValue > 127){
                    absoluteControllerValue = 127;
                } else if (absoluteControllerValue < 0){
                    absoluteControllerValue = 0;
                }
                newControllerValue = absoluteControllerValue;
            }
            auto newMsg = juce::MidiMessage::controllerEvent (msg.getChannel(), newControllerNumber, newControllerValue);
            newMsg.setTimeStamp (msg.getTimeStamp());
            msg = newMsg;
            // NOTE: isn't there a way to simple set controller number like when setting note number?
        }
        msg.setChannel(newMidiChannel);
        outputDevice->setMidiCCParameterValue(newControllerNumber, newControllerValue);  // If message is of type controller, also update the internal stored state of the controller
        return true;
    }
    else if (msg.isPitchWheel() && allowPitchBendMessages.get()){
        msg.setChannel(newMidiChannel);
        return true;
    }
    else if (msg.isChannelPressure() && allowChannelPressureMessages.get()){
        msg.setChannel(newMidiChannel);
        return true;
    }
    // NOTE: if none of the explictely specified MIDI message types is allowed, return false (i.e. always exclude sysex, program change, clock, etc.)
    return false;
}

void HardwareDevice::processAndRenderIncomingMessagesIntoBuffer(juce::MidiBuffer& bufferToFill, HardwareDevice* outputDevice)
{
    // use getMidiInputDeviceData to get latest block of messages for the device, process them and add to the buffer
    auto midiInputDeviceData = getMidiInputDeviceData(getMidiInputDeviceName());
    if (midiInputDeviceData == nullptr) { return; }
    juce::MidiBuffer* lastBlockOfMessages = &midiInputDeviceData->buffer;
    if (lastBlockOfMessages != nullptr){
        for (auto metadata: *lastBlockOfMessages){
            juce::MidiMessage msg = metadata.getMessage();
            if (filterAndProcessIncomingMidiMessage(msg, outputDevice)){
                // NOTE: msg should have been processed here as it is passed to filterAndProcessIncomingMidiMessage by reference
                bufferToFill.addEvent(msg, metadata.samplePosition);
            }
        }
    }
}

void HardwareDevice::setNotesMapping(juce::String& serializedNotesMapping)
{
    notesMapping = ShepherdHelpers::deserialize128IntArray(serializedNotesMapping);
    stateNotesMapping = ShepherdHelpers::serialize128IntArray(notesMapping);
}

void HardwareDevice::setControlChangeMapping(juce::String& serializedControlChangeMapping)
{
    controlChangeMapping = ShepherdHelpers::deserialize128IntArray(serializedControlChangeMapping);
    stateControlChangeMapping = ShepherdHelpers::serialize128IntArray(controlChangeMapping);
}

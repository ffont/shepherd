/*
  ==============================================================================

    HardwareDevice.cpp
    Created: 8 Jun 2021 6:35:41am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#include "HardwareDevice.h"


HardwareDevice::HardwareDevice(const juce::ValueTree& _state,
                               std::function<MidiOutputDeviceData*(juce::String deviceName)> midiOutputDeviceDataGetter
                               ): state(_state)
{
    getMidiOutputDeviceData = midiOutputDeviceDataGetter;
    
    bindState();
    
    for (int i=0; i<midiCCParameterValues.size(); i++){
        midiCCParameterValues[i] = 64;  // Initialize all midi ccs to 64 (mid value)
    }
    stateMidiCCParameterValues = Helpers::serialize128IntArray(midiCCParameterValues); // Update the state version of the midiCCParameterValues list so change is reflected in state
}

void HardwareDevice::bindState()
{
    uuid.referTo(state, IDs::uuid, nullptr, Defaults::emptyString);
    type.referTo(state, IDs::type, nullptr, HardwareDeviceType::output);
    name.referTo(state, IDs::name, nullptr, Defaults::emptyString);
    shortName.referTo(state, IDs::shortName, nullptr, Defaults::emptyString);
    midiOutputDeviceName.referTo(state, IDs::midiDeviceName, nullptr, Defaults::emptyString);
    midiOutputChannel.referTo(state, IDs::midiChannel, nullptr, -1);

    stateMidiCCParameterValues.referTo(state, IDs::midiCCParameterValuesList, nullptr, Defaults::emptyString);
    // NOTE: unlike other stateXXX properties in other objects like Clip, midiCCParameterValues should never be loaded from state, so we don't do it here
}

void HardwareDevice::updateStateMemberVersions()
{
    stateMidiCCParameterValues = Helpers::serialize128IntArray(midiCCParameterValues);
}

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
    stateMidiCCParameterValues = Helpers::serialize128IntArray(midiCCParameterValues); // Update the state version of the midiCCParameterValues list so change is reflected in state
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
    juce::MidiBuffer* buffer = &getMidiOutputDeviceData(getMidiOutputDeviceName())->buffer;
    while (midiMessagesToRenderInBuffer.pull(msg)) {
        int deviceMidiOutputChannel = getMidiOutputChannel();
        if ((buffer != nullptr) && (deviceMidiOutputChannel > -1)){
            msg.setChannel(deviceMidiOutputChannel);
            buffer->addEvent(msg, 0);
        }
    }
}

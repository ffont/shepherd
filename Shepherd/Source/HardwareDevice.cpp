/*
  ==============================================================================

    HardwareDevice.cpp
    Created: 8 Jun 2021 6:35:41am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#include "HardwareDevice.h"


HardwareDevice::HardwareDevice(juce::String _name,
               juce::String _shortName,
               std::function<juce::MidiOutput*(juce::String deviceName)> outputMidiDeviceGetter,
               std::function<void(const juce::OSCMessage& message)> oscMessageSender)
{
    name = _name;
    shortName = _shortName;
    getMidiOutputDevice = outputMidiDeviceGetter;
    sendOscMessage = oscMessageSender;
    
    for (int i=0; i<midiCCParameterValues.size(); i++){
        midiCCParameterValues[i] = 64;  // Initialize all midi ccs to 64 (mid value)
    }
}

juce::String HardwareDevice::getName()
{
    return name;
}

juce::String HardwareDevice::getShortName()
{
    return shortName;
}

void HardwareDevice::configureMidiOutput(juce::String deviceName, int channel)
{
    midiOutputDeviceName = deviceName;
    midiOutputChannel = channel;
}

int HardwareDevice::getMidiOutputChannel()
{
    return midiOutputChannel;
}

juce::String HardwareDevice::getMidiOutputDeviceName()
{
    return midiOutputDeviceName;
}

void HardwareDevice::sendMidi(juce::MidiMessage msg)
{
    auto midiDevice = getMidiOutputDevice(midiOutputDeviceName);
    if (midiDevice != nullptr){
        midiDevice->sendMessageNow(msg);
        if (msg.isController()){
            setMidiCCParameterValue(msg.getControllerNumber(), msg.getControllerValue(), true);
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

void HardwareDevice::setMidiCCParameterValue(int index, int value, bool notifyController)
{
    // NOTE: this function is to store the parameter value in the internal state, but it is not expected to communicate
    // this value to the hardware device
    jassert(index >= 0 && index < 128);
    midiCCParameterValues[index] = value;
    
    if (notifyController){
        juce::OSCMessage returnMessage = juce::OSCMessage(OSC_ADDRESS_MIDI_CC_PARAMETER_VALUES_FOR_DEVICE);
        returnMessage.addString(getShortName());
        returnMessage.addInt32(index);
        returnMessage.addInt32(value);
        sendOscMessage(returnMessage);
    }
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
    while (midiMessagesToRenderInBuffer.pull(msg)) {
        if (device != nullptr){
            int trackMidiOutputChannel = getMidiOutputChannel();
            if (trackMidiOutputChannel > -1){
                msg.setChannel(trackMidiOutputChannel);
                
                device->getMidiOutputDevice
            }
        }
    }
}

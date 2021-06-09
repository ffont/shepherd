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
               std::function<juce::MidiOutput*(juce::String deviceName)> outputMidiDeviceGetter)
{
    name = _name;
    shortName = _shortName;
    getMidiOutputDevice = outputMidiDeviceGetter;
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
    }
}

void HardwareDevice::sendMidi(juce::MidiBuffer& buffer)
{
    auto midiDevice = getMidiOutputDevice(midiOutputDeviceName);
    if (midiDevice != nullptr){
        midiDevice->sendBlockOfMessagesNow(buffer);
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

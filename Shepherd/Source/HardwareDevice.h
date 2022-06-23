/*
  ==============================================================================

    HardwareDevice.h
    Created: 8 Jun 2021 6:35:41am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "helpers.h"
#include "Fifo.h"

class HardwareDevice
{
public:
    HardwareDevice(juce::String name,
                   juce::String shortName,
                   std::function<juce::MidiOutput*(juce::String deviceName)> outputMidiDeviceGetter,
                   std::function<void(const juce::OSCMessage& message)> oscMessageSender,
                   std::function<juce::MidiBuffer*(juce::String deviceName)> midiOutputDeviceBufferGetter);
    
    juce::String getName();
    juce::String getShortName();
    
    void configureMidiOutput(juce::String deviceName, int channel);
    
    int getMidiOutputChannel();
    juce::String getMidiOutputDeviceName();
    
    void sendMidi(juce::MidiMessage msg);
    
    void sendAllNotesOff();
    void loadPreset(int bankNumber, int presetNumber);
    
    int getMidiCCParameterValue(int index);
    void setMidiCCParameterValue(int index, int value, bool notifyController);
    
    void addMidiMessageToRenderInBufferFifo(juce::MidiMessage msg);
    void renderPendingMidiMessagesToRenderInBuffer();
    
private:
    juce::String name = "Generic device";
    juce::String shortName = "Generic";
    
    juce::String midiOutputDeviceName = "";
    std::function<juce::MidiOutput*(juce::String deviceName)> getMidiOutputDevice;
    std::function<juce::MidiBuffer*(juce::String deviceName)> getMidiOutputDeviceBuffer;
    int midiOutputChannel = -1;
    
    std::array<int, 128> midiCCParameterValues = {0};
    
    std::function<void(const juce::OSCMessage& message)> sendOscMessage;
    
    Fifo<juce::MidiMessage, 100> midiMessagesToRenderInBuffer;
};

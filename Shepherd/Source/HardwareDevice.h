/*
  ==============================================================================

    HardwareDevice.h
    Created: 8 Jun 2021 6:35:41am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "defines.h"

class HardwareDevice
{
public:
    HardwareDevice(juce::String name,
                   juce::String shortName,
                   std::function<juce::MidiOutput*(juce::String deviceName)> outputMidiDeviceGetter);
    
    juce::String getName();
    juce::String getShortName();
    
    void configureMidiOutput(juce::String deviceName, int channel);
    
    int getMidiOutputChannel();
    juce::String getMidiOutputDeviceName();
    
    void sendMidi(juce::MidiMessage msg);
    void sendMidi(juce::MidiBuffer& buffer);
    
    void sendAllNotesOff();
    void loadPreset(int bankNumber, int presetNumber);
    
private:
    juce::String name = "Generic device";
    juce::String shortName = "Generic";
    
    juce::String midiOutputDeviceName = "";
    std::function<juce::MidiOutput*(juce::String deviceName)> getMidiOutputDevice;
    int midiOutputChannel = -1;
};

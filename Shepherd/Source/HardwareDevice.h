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
    HardwareDevice(const juce::ValueTree& state,
                   std::function<MidiOutputDeviceData*(juce::String deviceName)> midiOutputDeviceDataGetter);
    void bindState();
    void updateStateMemberVersions();
    juce::ValueTree state;
    
    bool isTypeInput() { return type.get() == HardwareDeviceType::input; };
    bool isTypeOutput() { return type.get() == HardwareDeviceType::output; };
    
    juce::String getUUID() { return uuid.get(); };
    juce::String getName() { return name.get(); };
    juce::String getShortName() { return shortName.get(); };
    
    // Relevant for output devices
    int getMidiOutputChannel() { return midiOutputChannel.get(); }
    juce::String getMidiOutputDeviceName(){ return midiOutputDeviceName.get();}
    void sendMidi(juce::MidiMessage msg);
    void sendAllNotesOff();
    void loadPreset(int bankNumber, int presetNumber);
    int getMidiCCParameterValue(int index);
    void setMidiCCParameterValue(int index, int value);
    void addMidiMessageToRenderInBufferFifo(juce::MidiMessage msg);
    void renderPendingMidiMessagesToRenderInBuffer();
    
    // Relevant for input devices
    // TODO: implement
    /*
    bool allowIncomingMidiMessage(juce::MidiMessage msg);  // Return true/false after applying filters to message
    juce::MidiMessage processIncomingMidiMessage(juce::MidiMessage msg);  // Re-map message to new note/midi cc according to mapping
    void renderMessagesIntoBuffer(juce::MidiBuffer* buffer);  // use getMidiInputDeviceData to get latest block of messages for the device, process them and add to the buffer
     */
    
private:
    juce::CachedValue<juce::String> uuid;
    juce::CachedValue<int> type;  // Should correspond to HardwareDeviceType
    juce::CachedValue<juce::String> name;
    juce::CachedValue<juce::String> shortName;
    
    // For output devices
    juce::CachedValue<juce::String> midiOutputDeviceName;
    juce::CachedValue<int> midiOutputChannel;
    std::array<int, 128> midiCCParameterValues = {0};
    juce::CachedValue<juce::String> stateMidiCCParameterValues;
    
    std::function<MidiOutputDeviceData*(juce::String deviceName)> getMidiOutputDeviceData;
    Fifo<juce::MidiMessage, 100> midiMessagesToRenderInBuffer;
    
    // For input devices
    // TODO: implement binding to state, adding nedded IDs, add helper method to create input
    /*
    juce::CachedValue<int> allowedMidiInputChannel;
    juce::CachedValue<bool> allowNoteMessages;
    juce::CachedValue<bool> allowControllerMessages;
    juce::CachedValue<bool> allowPitchBendMessages;
    juce::CachedValue<bool> allowAftertouchMessages;
    juce::CachedValue<bool> allowChannelPressureMessages;
    juce::CachedValue<bool> allowProgramChangeMessages;
    std::array<int, 128> ccMapping = {};
    juce::CachedValue<juce::String> stateCcMapping;
    std::array<int, 128> notesMapping = {};
    juce::CachedValue<juce::String> stateNotesMapping;
    
    std::function<MidiInputDeviceData*(juce::String deviceName)> getMidiInputDeviceData;*/
};

struct HardwareDeviceList: public drow::ValueTreeObjectList<HardwareDevice>
{
    HardwareDeviceList (const juce::ValueTree& v,
                        std::function<MidiOutputDeviceData*(juce::String deviceName)> midiOutputDeviceDataGetter)
    : drow::ValueTreeObjectList<HardwareDevice> (v)
    {
        getMidiOutputDeviceData = midiOutputDeviceDataGetter;
        rebuildObjects();
    }

    ~HardwareDeviceList()
    {
        freeObjects();
    }

    bool isSuitableType (const juce::ValueTree& v) const override
    {
        return v.hasType (IDs::HARDWARE_DEVICE);
    }

    HardwareDevice* createNewObject (const juce::ValueTree& v) override
    {
        return new HardwareDevice (v,
                                   getMidiOutputDeviceData);
    }

    void deleteObject (HardwareDevice* c) override
    {
        delete c;
    }

    void newObjectAdded (HardwareDevice*) override    {}
    void objectRemoved (HardwareDevice*) override     {}
    void objectOrderChanged() override       {}
    
    HardwareDevice* getObjectWithUUID(const juce::String& uuid) {
        for (auto* object: objects){
            if (object->getUUID() == uuid){
                return object;
            }
        }
        return nullptr;
    }
    
    std::function<MidiOutputDeviceData*(juce::String deviceName)> getMidiOutputDeviceData;
    
    juce::StringArray getAvailableOutputHardwareDeviceNames() {
        juce::StringArray availableHardwareDeviceNames = {};
        for (auto* object: objects){
            if (object->isTypeOutput()){
                availableHardwareDeviceNames.add(object->getName());
            }
        }
        return availableHardwareDeviceNames;
    }
    
    juce::StringArray getAvailableInputHardwareDeviceNames() {
        juce::StringArray availableHardwareDeviceNames = {};
        for (auto* object: objects){
            if (object->isTypeInput()){
                availableHardwareDeviceNames.add(object->getName());
            }
        }
        return availableHardwareDeviceNames;
    }
};

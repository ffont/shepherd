/*
  ==============================================================================

    HardwareDevice.h
    Created: 8 Jun 2021 6:35:41am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "helpers_shepherd.h"
#include "Fifo.h"
#include "MusicalContext.h"

class HardwareDevice
{
public:
    HardwareDevice(const juce::ValueTree& state,
                   std::function<MidiOutputDeviceData*(juce::String deviceName)> midiOutputDeviceDataGetter,
                   std::function<MidiInputDeviceData*(juce::String deviceName)> midiInputDeviceDataGetter);
    void bindState();
    juce::ValueTree state;
    
    bool isTypeInput() { return type.get() == HardwareDeviceType::input; };
    bool isTypeOutput() { return type.get() == HardwareDeviceType::output; };
    bool isMidiInitialized() {
        if (isTypeInput()){
            return getMidiInputDeviceData(getMidiInputDeviceName()) != nullptr;
        } else {
            return getMidiOutputDeviceData(getMidiOutputDeviceName()) != nullptr;
        }
    }
    
    juce::String getUUID() { return uuid.get(); };
    juce::String getName() { return name.get(); };
    juce::String getShortName() { return shortName.get(); };
    HardwareDeviceType getType() {
        if (type.get() == HardwareDeviceType::input){
            return HardwareDeviceType::input;
        } else {
            return HardwareDeviceType::output;
        }
    };
    
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
    juce::String getMidiInputDeviceName(){ return midiInputDeviceName.get();}
    bool filterAndProcessIncomingMidiMessage(juce::MidiMessage& msg, HardwareDevice* outputDevice);
    void processAndRenderIncomingMessagesIntoBuffer(juce::MidiBuffer& bufferToFill, HardwareDevice* outputDevice);
    void setNotesMapping(juce::String& serializedNotesMapping);
    void setControlChangeMapping(juce::String& serializedControlChangeMapping);
    
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
    juce::CachedValue<juce::String> midiInputDeviceName;
    juce::CachedValue<int> allowedMidiInputChannel;
    juce::CachedValue<bool> allowNoteMessages;
    juce::CachedValue<bool> allowControllerMessages;
    juce::CachedValue<bool> allowPitchBendMessages;
    juce::CachedValue<bool> allowAftertouchMessages;
    juce::CachedValue<bool> allowChannelPressureMessages;
    juce::CachedValue<bool> controlChangeMessagesAreRelative;
    std::array<int, 128> controlChangeMapping = {};
    juce::CachedValue<juce::String> stateControlChangeMapping;
    std::array<int, 128> notesMapping = {};
    juce::CachedValue<juce::String> stateNotesMapping;
    
    std::function<MidiInputDeviceData*(juce::String deviceName)> getMidiInputDeviceData;
};

struct HardwareDeviceList: public drow::ValueTreeObjectList<HardwareDevice>
{
    HardwareDeviceList (const juce::ValueTree& v,
                        std::function<MidiOutputDeviceData*(juce::String deviceName)> midiOutputDeviceDataGetter,
                        std::function<MidiInputDeviceData*(juce::String deviceName)> midiInputDeviceDataGetter)
    : drow::ValueTreeObjectList<HardwareDevice> (v)
    {
        getMidiOutputDeviceData = midiOutputDeviceDataGetter;
        getMidiInputDeviceData = midiInputDeviceDataGetter;
        rebuildObjects();
    }

    ~HardwareDeviceList()
    {
        freeObjects();
    }

    bool isSuitableType (const juce::ValueTree& v) const override
    {
        return v.hasType (ShepherdIDs::HARDWARE_DEVICE);
    }

    HardwareDevice* createNewObject (const juce::ValueTree& v) override
    {
        return new HardwareDevice (v,
                                   getMidiOutputDeviceData,
                                   getMidiInputDeviceData);
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
    std::function<MidiInputDeviceData*(juce::String deviceName)> getMidiInputDeviceData;
    
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

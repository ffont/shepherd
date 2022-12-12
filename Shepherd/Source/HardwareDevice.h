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
                   std::function<void(const juce::OSCMessage& message)> messageSender,
                   std::function<juce::MidiOutput*(juce::String deviceName)> midiOutputDeviceGetter,
                   std::function<juce::MidiBuffer*(juce::String deviceName)> midiOutputDeviceBufferGetter);
    void bindState();
    juce::ValueTree state;
    
    juce::String getUUID() { return uuid.get(); };
    juce::String getName() { return name.get(); };
    juce::String getShortName() { return shortName.get(); };
    int getMidiOutputChannel() { return midiOutputChannel.get(); }
    juce::String getMidiOutputDeviceName(){ return midiOutputDeviceName.get();}
    
    void sendMidi(juce::MidiMessage msg);
    
    void sendAllNotesOff();
    void loadPreset(int bankNumber, int presetNumber);
    
    int getMidiCCParameterValue(int index);
    void setMidiCCParameterValue(int index, int value, bool notifyController);
    
    void addMidiMessageToRenderInBufferFifo(juce::MidiMessage msg);
    void renderPendingMidiMessagesToRenderInBuffer();
    
private:
    juce::CachedValue<juce::String> uuid;
    juce::CachedValue<int> type;  // Should correspond to HardwareDeviceType
    juce::CachedValue<juce::String> name;
    juce::CachedValue<juce::String> shortName;
    juce::CachedValue<juce::String> midiOutputDeviceName;
    juce::CachedValue<int> midiOutputChannel;
    
    std::function<juce::MidiOutput*(juce::String deviceName)> getMidiOutputDevice;
    std::function<juce::MidiBuffer*(juce::String deviceName)> getMidiOutputDeviceBuffer;
    
    std::array<int, 128> midiCCParameterValues = {0};
    
    std::function<void(const juce::OSCMessage& message)> sendMessageToController;
    
    Fifo<juce::MidiMessage, 100> midiMessagesToRenderInBuffer;
};

struct HardwareDeviceList: public drow::ValueTreeObjectList<HardwareDevice>
{
    HardwareDeviceList (const juce::ValueTree& v,
                        std::function<void(const juce::OSCMessage& message)> messageSender,
                        std::function<juce::MidiOutput*(juce::String deviceName)> midiOutputDeviceGetter,
                        std::function<juce::MidiBuffer*(juce::String deviceName)> midiOutputDeviceBufferGetter)
    : drow::ValueTreeObjectList<HardwareDevice> (v)
    {
        sendMessageToController = messageSender;
        getMidiOutputDevice = midiOutputDeviceGetter;
        getMidiOutputDeviceBuffer = midiOutputDeviceBufferGetter;
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
                                   sendMessageToController,
                                   getMidiOutputDevice,
                                   getMidiOutputDeviceBuffer);
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
    
    std::function<juce::MidiOutput*(juce::String deviceName)> getMidiOutputDevice;
    std::function<juce::MidiBuffer*(juce::String deviceName)> getMidiOutputDeviceBuffer;
    std::function<void(const juce::OSCMessage& message)> sendMessageToController;
    
    juce::StringArray getAvailableHardwareDeviceNames() {
        juce::StringArray availableHardwareDeviceNames = {};
        for (auto* object: objects){
            availableHardwareDeviceNames.add(object->getName());
        }
        return availableHardwareDeviceNames;
    }
};

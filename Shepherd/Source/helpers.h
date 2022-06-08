/*
  ==============================================================================

    helpers.h
    Created: 21 Jun 2021 6:48:10pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "defines.h"
#include "drow_ValueTreeObjectList.h"

namespace Helpers
{

    inline juce::ValueTree createUuidProperty (juce::ValueTree& v)
    {
        if (! v.hasProperty (IDs::uuid))
            v.setProperty (IDs::uuid, juce::Uuid().toString(), nullptr);
        return v;
    }

    inline juce::ValueTree createDefaultSession(juce::StringArray availableHardwareDeviceNames, int numEnabledTracks)
    {
        juce::ValueTree session (IDs::SESSION);
        Helpers::createUuidProperty (session);
        session.setProperty (IDs::name, juce::Time::getCurrentTime().formatted("%Y%m%d") + " unnamed", nullptr);
        
        for (int tn = 0; tn < MAX_NUM_TRACKS; ++tn)
        {
            // Create track
            juce::ValueTree t (IDs::TRACK);
            Helpers::createUuidProperty (t);
            t.setProperty (IDs::enabled, tn < numEnabledTracks, nullptr);
            t.setProperty (IDs::order, tn, nullptr);
            if (tn < numEnabledTracks){
                // Track is enabled (not deleted), add name and hardware device to it
                const juce::String trackName ("Track " + juce::String (tn + 1));
                t.setProperty (IDs::name, trackName, nullptr);
                if (tn < availableHardwareDeviceNames.size()){
                    t.setProperty (IDs::hardwareDeviceName, availableHardwareDeviceNames[tn], nullptr);
                } else {
                    t.setProperty (IDs::hardwareDeviceName, Defaults::emptyString, nullptr);
                }
            }
            
            // Now add clips to track (for now clips are still empty and disabled)
            for (int cn = 0; cn < MAX_NUM_SCENES; ++cn)
            {
                juce::ValueTree c (IDs::CLIP);
                Helpers::createUuidProperty (c);
                c.setProperty (IDs::name, t.getProperty(IDs::order).toString() + "-" + juce::String (cn), nullptr);
                c.setProperty (IDs::enabled, false, nullptr);
                t.addChild (c, -1, nullptr);
            }
            
            session.addChild (t, -1, nullptr);
        }

        return session;
    }

    inline juce::ValueTree midiMessageToSequenceEventValueTree(juce::MidiMessage msg)
    {
        juce::ValueTree sequenceEvent {IDs::SEQUENCE_EVENT};
        Helpers::createUuidProperty (sequenceEvent);
        sequenceEvent.setProperty(IDs::type, "midi", nullptr);
        sequenceEvent.setProperty(IDs::timestamp, msg.getTimeStamp(), nullptr);
        juce::StringArray bytes = {};
        // Only support 3-byte MIDI messages so far
        jassert(msg.getRawDataSize() == 3);
        for (int i=0; i<msg.getRawDataSize(); i++){
            bytes.add(juce::String(msg.getRawData()[i]));
        }
        sequenceEvent.setProperty(IDs::eventMidiBytes, bytes.joinIntoString(","), nullptr);
        return sequenceEvent;
    }

    inline juce::MidiMessage eventValueTreeToMidiMessage(juce::ValueTree& sequenceEvent)
    {
        juce::String bytesString = sequenceEvent.getProperty(IDs::eventMidiBytes, Defaults::eventMidiBytes);
        juce::StringArray bytes;
        bytes.addTokens(bytesString, ",", "");
        // Only support 3-byte MIDI messages so far
        jassert(bytes.size() == 3);
        juce::MidiMessage msg = juce::MidiMessage(bytes[0].getIntValue(), bytes[1].getIntValue(), bytes[2].getIntValue());
        msg.setTimeStamp(sequenceEvent.getProperty(IDs::timestamp, Defaults::timestamp));
        return msg;
    }

}

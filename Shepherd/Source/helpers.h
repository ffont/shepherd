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
                c.setProperty (IDs::clipLengthInBeats, Defaults::clipLengthInBeats, nullptr);
                c.setProperty (IDs::currentQuantizationStep, Defaults::currentQuantizationStep, nullptr);
                c.setProperty (IDs::wrapEventsAcrossClipLoop, Defaults::wrapEventsAcrossClipLoop, nullptr);
                
                c.setProperty (IDs::recording, Defaults::recording, nullptr);
                c.setProperty (IDs::willStartRecordingAt, Defaults::willStartRecordingAt, nullptr);
                c.setProperty (IDs::willStopRecordingAt, Defaults::willStopRecordingAt, nullptr);
                
                c.setProperty (IDs::playing, Defaults::playing, nullptr);
                c.setProperty (IDs::willPlayAt, Defaults::willPlayAt, nullptr);
                c.setProperty (IDs::willStopAt, Defaults::willStopAt, nullptr);
                
                c.setProperty (IDs::playheadPositionInBeats, Defaults::playheadPosition, nullptr);

                t.addChild (c, -1, nullptr);
            }
            
            session.addChild (t, -1, nullptr);
        }

        return session;
    }

    inline juce::ValueTree createSequenceEventFromMidiMessage(juce::MidiMessage msg)
    {
        juce::ValueTree sequenceEvent {IDs::SEQUENCE_EVENT};
        Helpers::createUuidProperty (sequenceEvent);
        sequenceEvent.setProperty(IDs::type, SequenceEventType::midi, nullptr);
        sequenceEvent.setProperty(IDs::timestamp, msg.getTimeStamp(), nullptr);
        sequenceEvent.setProperty(IDs::renderedStartTimestamp, -1.0, nullptr);
        sequenceEvent.setProperty(IDs::renderedEndTimestamp, -1.0, nullptr);
        juce::StringArray bytes = {};
        for (int i=0; i<msg.getRawDataSize(); i++){
            bytes.add(juce::String(msg.getRawData()[i]));
        }
        sequenceEvent.setProperty(IDs::eventMidiBytes, bytes.joinIntoString(","), nullptr);
        return sequenceEvent;
    }

    inline juce::ValueTree createSequenceEventOfTypeNote(double timestamp, int note, float velocity, double duration)
    {
        juce::ValueTree sequenceEvent {IDs::SEQUENCE_EVENT};
        Helpers::createUuidProperty (sequenceEvent);
        sequenceEvent.setProperty(IDs::type, SequenceEventType::note, nullptr);
        sequenceEvent.setProperty(IDs::timestamp, timestamp, nullptr);
        sequenceEvent.setProperty(IDs::renderedStartTimestamp, -1.0, nullptr);
        sequenceEvent.setProperty(IDs::renderedEndTimestamp, -1.0, nullptr);
        sequenceEvent.setProperty(IDs::midiNote, note, nullptr);
        sequenceEvent.setProperty(IDs::midiVelocity, velocity, nullptr);
        sequenceEvent.setProperty(IDs::duration, duration, nullptr);
        return sequenceEvent;
    }

    inline std::vector<juce::MidiMessage> eventValueTreeToMidiMessages(juce::ValueTree& sequenceEvent)
    {
        std::vector<juce::MidiMessage> messages = {};
        
        // NOTE: don't care about MIDI channel here as they will be replaced when sending the notes to the appropriate output device
        int midiChannel = 1;
        
        if ((int)sequenceEvent.getProperty(IDs::type) == SequenceEventType::midi) {
            juce::String bytesString = sequenceEvent.getProperty(IDs::eventMidiBytes, Defaults::eventMidiBytes);
            juce::StringArray bytes;
            bytes.addTokens(bytesString, ",", "");
            juce::MidiMessage msg;
            if (bytes.size() == 2){
                msg = juce::MidiMessage(bytes[0].getIntValue(), bytes[1].getIntValue());
            } else if (bytes.size() == 3){
                msg = juce::MidiMessage(bytes[0].getIntValue(), bytes[1].getIntValue(), bytes[2].getIntValue());
            }
            msg.setChannel(midiChannel);
            msg.setTimeStamp(sequenceEvent.getProperty(IDs::renderedStartTimestamp));
            messages.push_back(msg);
            
        } else if ((int)sequenceEvent.getProperty(IDs::type) == SequenceEventType::note) {
           
            int midiNote = (int)sequenceEvent.getProperty(IDs::midiNote);
            float midiVelocity = (float)sequenceEvent.getProperty(IDs::midiVelocity);
            double noteOnTimestamp = (double)sequenceEvent.getProperty(IDs::renderedStartTimestamp);
            double noteOffTimestamp = (double)sequenceEvent.getProperty(IDs::renderedEndTimestamp);
            
            juce::MidiMessage msgNoteOn = juce::MidiMessage::noteOn(midiChannel, midiNote, midiVelocity);
            msgNoteOn.setTimeStamp(noteOnTimestamp);
            messages.push_back(msgNoteOn);
            
            juce::MidiMessage msgNoteOff = juce::MidiMessage::noteOff(midiChannel, midiNote, 0.0f);
            msgNoteOff.setTimeStamp(noteOffTimestamp);
            messages.push_back(msgNoteOff);
        }
        return messages;
    }

}

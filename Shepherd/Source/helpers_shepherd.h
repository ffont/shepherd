/*
  ==============================================================================

    helpers.h
    Created: 21 Jun 2021 6:48:10pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "defines_shepherd.h"
#include "drow_ValueTreeObjectList.h"

namespace Helpers
{

    inline bool sameMidiMessageWithSameTimestamp(juce::MidiMessage& m1, juce::MidiMessage& m2)
    {
        // Check that two midi messages are exactly the same and have the same timestamp
        int sizeM1 = m1.getRawDataSize();
        int sizeM2 = m2.getRawDataSize();
        if (sizeM1 != sizeM2){
            return false;
        }
        return (memcmp(m1.getRawData(), m2.getRawData(), sizeM1) == 0 && m1.getTimeStamp() == m2.getTimeStamp());
        //return m1.getDescription() + (juce::String)m1.getTimeStamp() == m2.getDescription() + (juce::String)m2.getTimeStamp();
    }

    inline juce::ValueTree createUuidProperty (juce::ValueTree& v)
    {
        if (! v.hasProperty (ShepherdIDs::uuid))
            v.setProperty (ShepherdIDs::uuid, juce::Uuid().toString(), nullptr);
        return v;
    }

    inline juce::ValueTree updateUuidProperty (juce::ValueTree& v)
    {
        v.setProperty (ShepherdIDs::uuid, juce::Uuid().toString(), nullptr);
        return v;
    }

    inline juce::ValueTree createDefaultStateRoot()
    {
        juce::ValueTree state (ShepherdIDs::STATE);
        Helpers::createUuidProperty (state);
        state.setProperty (ShepherdIDs::renderWithInternalSynth, ShepherdDefaults::renderWithInternalSynth, nullptr);
        state.setProperty (ShepherdIDs::dataLocation, ShepherdDefaults::emptyString, nullptr);
        state.setProperty (ShepherdIDs::notesMonitoringDeviceName, SHEPHERD_NOTES_MONITORING_MIDI_DEVICE_NAME, nullptr);
        state.setProperty (ShepherdIDs::version, ProjectInfo::versionString , nullptr);  // Version of running shepherd backend
        return state;
    }

    inline juce::ValueTree createDefaultSession(juce::StringArray availableHardwareDeviceNames, int numTracks, int numScenes)
    {
        juce::ValueTree session (ShepherdIDs::SESSION);
        Helpers::createUuidProperty (session);
        session.setProperty (ShepherdIDs::version, ProjectInfo::versionString , nullptr);
        session.setProperty (ShepherdIDs::name, juce::Time::getCurrentTime().formatted("%Y%m%d") + " unnamed", nullptr);
        session.setProperty (ShepherdIDs::playheadPositionInBeats, ShepherdDefaults::playheadPosition, nullptr);
        session.setProperty (ShepherdIDs::playing, ShepherdDefaults::playing, nullptr);
        session.setProperty (ShepherdIDs::doingCountIn, ShepherdDefaults::doingCountIn, nullptr);
        session.setProperty (ShepherdIDs::countInPlayheadPositionInBeats, ShepherdDefaults::playheadPosition, nullptr);
        session.setProperty (ShepherdIDs::barCount, ShepherdDefaults::barCount, nullptr);
        session.setProperty (ShepherdIDs::bpm, ShepherdDefaults::bpm, nullptr);
        session.setProperty (ShepherdIDs::meter, ShepherdDefaults::meter, nullptr);
        session.setProperty (ShepherdIDs::metronomeOn, ShepherdDefaults::metronomeOn, nullptr);
        session.setProperty (ShepherdIDs::fixedVelocity, ShepherdDefaults::fixedVelocity, nullptr);
        session.setProperty (ShepherdIDs::fixedLengthRecordingBars, ShepherdDefaults::fixedLengthRecordingBars, nullptr);
        session.setProperty (ShepherdIDs::recordAutomationEnabled, ShepherdDefaults::recordAutomationEnabled, nullptr);
        
        for (int tn = 0; tn < numTracks; ++tn)
        {
            // Create track
            juce::ValueTree t (ShepherdIDs::TRACK);
            Helpers::createUuidProperty (t);
            t.setProperty (ShepherdIDs::inputMonitoring, ShepherdDefaults::inputMonitoring, nullptr);
            const juce::String trackName ("Track " + juce::String (tn + 1));
            t.setProperty (ShepherdIDs::name, trackName, nullptr);
            t.setProperty (ShepherdIDs::outputHardwareDeviceName, ShepherdDefaults::emptyString, nullptr);

            // Add hardware device to track
            if (tn < availableHardwareDeviceNames.size()){
                t.setProperty (ShepherdIDs::outputHardwareDeviceName, availableHardwareDeviceNames[tn], nullptr);
            } else {
                t.setProperty (ShepherdIDs::outputHardwareDeviceName, availableHardwareDeviceNames[availableHardwareDeviceNames.size() - 1], nullptr);
            }
            
            // Now add clips to track (for now clips are still empty and disabled)
            for (int cn = 0; cn < numScenes; ++cn)
            {
                juce::ValueTree c (ShepherdIDs::CLIP);
                Helpers::createUuidProperty (c);
                c.setProperty (ShepherdIDs::name, "Clip " + juce::String (tn + 1) + "-" + juce::String (cn + 1), nullptr);
                c.setProperty (ShepherdIDs::clipLengthInBeats, ShepherdDefaults::clipLengthInBeats, nullptr);
                c.setProperty (ShepherdIDs::bpmMultiplier, ShepherdDefaults::bpmMultiplier, nullptr);
                c.setProperty (ShepherdIDs::currentQuantizationStep, ShepherdDefaults::currentQuantizationStep, nullptr);
                c.setProperty (ShepherdIDs::wrapEventsAcrossClipLoop, ShepherdDefaults::wrapEventsAcrossClipLoop, nullptr);
                
                c.setProperty (ShepherdIDs::recording, ShepherdDefaults::recording, nullptr);
                c.setProperty (ShepherdIDs::willStartRecordingAt, ShepherdDefaults::willStartRecordingAt, nullptr);
                c.setProperty (ShepherdIDs::willStopRecordingAt, ShepherdDefaults::willStopRecordingAt, nullptr);
                c.setProperty (ShepherdIDs::playing, ShepherdDefaults::playing, nullptr);
                c.setProperty (ShepherdIDs::willPlayAt, ShepherdDefaults::willPlayAt, nullptr);
                c.setProperty (ShepherdIDs::willStopAt, ShepherdDefaults::willStopAt, nullptr);
                c.setProperty (ShepherdIDs::playheadPositionInBeats, ShepherdDefaults::playheadPosition, nullptr);

                t.addChild (c, -1, nullptr);
            }
            
            session.addChild (t, -1, nullptr);
        }

        return session;
    }

    inline juce::ValueTree createOutputHardwareDevice(juce::String name, juce::String shortName, juce::String midiDeviceName, int midiChannel)
    {
        juce::ValueTree device {ShepherdIDs::HARDWARE_DEVICE};
        Helpers::createUuidProperty (device);
        device.setProperty(ShepherdIDs::type, HardwareDeviceType::output, nullptr);
        device.setProperty(ShepherdIDs::name, name, nullptr);
        device.setProperty(ShepherdIDs::shortName, shortName, nullptr);
        device.setProperty(ShepherdIDs::midiOutputDeviceName, midiDeviceName, nullptr);
        device.setProperty(ShepherdIDs::midiChannel, midiChannel, nullptr);
        device.setProperty(ShepherdIDs::midiCCParameterValuesList, ShepherdDefaults::emptyString, nullptr);
        return device;
    }

    inline juce::ValueTree createInputHardwareDevice(juce::String name,
                                                     juce::String shortName,
                                                     juce::String midiDeviceName,
                                                     bool controlChangeMessagesAreRelative,
                                                     int allowedMidiInputChannel,
                                                     bool allowNoteMessages,
                                                     bool allowControllerMessages,
                                                     bool allowPitchBendMessages,
                                                     bool allowAftertouchMessages,
                                                     bool allowChannelPressureMessages,
                                                     juce::String notesMapping,
                                                     juce::String controlChangeMapping)
    {
        juce::ValueTree device {ShepherdIDs::HARDWARE_DEVICE};
        Helpers::createUuidProperty (device);
        device.setProperty(ShepherdIDs::type, HardwareDeviceType::input, nullptr);
        device.setProperty(ShepherdIDs::name, name, nullptr);
        device.setProperty(ShepherdIDs::shortName, shortName, nullptr);
        device.setProperty(ShepherdIDs::midiInputDeviceName, midiDeviceName, nullptr);
        device.setProperty(ShepherdIDs::allowedMidiInputChannel, allowedMidiInputChannel, nullptr);
        device.setProperty(ShepherdIDs::allowNoteMessages, allowNoteMessages, nullptr);
        device.setProperty(ShepherdIDs::allowControllerMessages, allowControllerMessages, nullptr);
        device.setProperty(ShepherdIDs::allowPitchBendMessages, allowPitchBendMessages, nullptr);
        device.setProperty(ShepherdIDs::allowAftertouchMessages, allowAftertouchMessages, nullptr);
        device.setProperty(ShepherdIDs::allowChannelPressureMessages, allowChannelPressureMessages, nullptr);
        device.setProperty(ShepherdIDs::notesMapping, notesMapping, nullptr);
        device.setProperty(ShepherdIDs::controlChangeMapping, controlChangeMapping, nullptr);
        device.setProperty(ShepherdIDs::controlChangeMessagesAreRelative, controlChangeMessagesAreRelative, nullptr);
        return device;
    }

    inline juce::ValueTree createSequenceEventFromMidiMessage(juce::MidiMessage msg)
    {
        juce::ValueTree sequenceEvent {ShepherdIDs::SEQUENCE_EVENT};
        Helpers::createUuidProperty (sequenceEvent);
        sequenceEvent.setProperty(ShepherdIDs::type, SequenceEventType::midi, nullptr);
        sequenceEvent.setProperty(ShepherdIDs::timestamp, msg.getTimeStamp(), nullptr);
        sequenceEvent.setProperty(ShepherdIDs::uTime, ShepherdDefaults::uTime, nullptr);
        sequenceEvent.setProperty(ShepherdIDs::renderedStartTimestamp, -1.0, nullptr);
        sequenceEvent.setProperty(ShepherdIDs::renderedEndTimestamp, -1.0, nullptr);
        juce::StringArray bytes = {};
        for (int i=0; i<msg.getRawDataSize(); i++){
            bytes.add(juce::String(msg.getRawData()[i]));
        }
        sequenceEvent.setProperty(ShepherdIDs::eventMidiBytes, bytes.joinIntoString(","), nullptr);
        return sequenceEvent;
    }

    inline juce::ValueTree createSequenceEventFromMidiBytesString(double timestamp, const juce::String& eventMidiBytes, double utime)
    {
        // eventMidiBytes = comma separated byte values, eg: 127,75,12
        juce::ValueTree sequenceEvent {ShepherdIDs::SEQUENCE_EVENT};
        Helpers::createUuidProperty (sequenceEvent);
        sequenceEvent.setProperty(ShepherdIDs::type, SequenceEventType::midi, nullptr);
        sequenceEvent.setProperty(ShepherdIDs::timestamp, timestamp, nullptr);
        sequenceEvent.setProperty(ShepherdIDs::uTime, utime, nullptr);
        sequenceEvent.setProperty(ShepherdIDs::renderedStartTimestamp, -1.0, nullptr);
        sequenceEvent.setProperty(ShepherdIDs::renderedEndTimestamp, -1.0, nullptr);
        sequenceEvent.setProperty(ShepherdIDs::eventMidiBytes, eventMidiBytes, nullptr);
        return sequenceEvent;
    }

    inline juce::ValueTree createSequenceEventFromMidiBytesString(double timestamp, const juce::String& eventMidiBytes)
    {
        return createSequenceEventFromMidiBytesString(timestamp, eventMidiBytes, ShepherdDefaults::uTime);
    }

    inline juce::ValueTree createSequenceEventOfTypeNote(double timestamp, int note, float velocity, double duration, double utime, float chance)
    {
        juce::ValueTree sequenceEvent {ShepherdIDs::SEQUENCE_EVENT};
        Helpers::createUuidProperty (sequenceEvent);
        sequenceEvent.setProperty(ShepherdIDs::type, SequenceEventType::note, nullptr);
        sequenceEvent.setProperty(ShepherdIDs::timestamp, timestamp, nullptr);
        sequenceEvent.setProperty(ShepherdIDs::uTime, utime, nullptr);
        sequenceEvent.setProperty(ShepherdIDs::renderedStartTimestamp, -1.0, nullptr);
        sequenceEvent.setProperty(ShepherdIDs::renderedEndTimestamp, -1.0, nullptr);
        sequenceEvent.setProperty(ShepherdIDs::midiNote, note, nullptr);
        sequenceEvent.setProperty(ShepherdIDs::midiVelocity, velocity, nullptr);
        sequenceEvent.setProperty(ShepherdIDs::duration, duration, nullptr);
        sequenceEvent.setProperty(ShepherdIDs::chance, chance, nullptr);
        return sequenceEvent;
    }

    inline juce::ValueTree createSequenceEventOfTypeNote(double timestamp, int note, float velocity, double duration)
    {
        return createSequenceEventOfTypeNote(timestamp, note, velocity, duration, ShepherdDefaults::uTime, ShepherdDefaults::chance);
    }

    inline std::vector<juce::MidiMessage> eventValueTreeToMidiMessages(juce::ValueTree& sequenceEvent)
    {
        std::vector<juce::MidiMessage> messages = {};
        
        // NOTE: don't care about MIDI channel here as they will be replaced when sending the notes to the appropriate output device
        int midiChannel = 1;
        
        if ((int)sequenceEvent.getProperty(ShepherdIDs::type) == SequenceEventType::midi) {
            juce::String bytesString = sequenceEvent.getProperty(ShepherdIDs::eventMidiBytes, ShepherdDefaults::eventMidiBytes);
            juce::StringArray bytes;
            bytes.addTokens(bytesString, ",", "");
            juce::MidiMessage msg;
            if (bytes.size() == 2){
                msg = juce::MidiMessage(bytes[0].getIntValue(), bytes[1].getIntValue());
            } else if (bytes.size() == 3){
                msg = juce::MidiMessage(bytes[0].getIntValue(), bytes[1].getIntValue(), bytes[2].getIntValue());
            }
            msg.setChannel(midiChannel);
            msg.setTimeStamp(sequenceEvent.getProperty(ShepherdIDs::renderedStartTimestamp));
            messages.push_back(msg);
            
        } else if ((int)sequenceEvent.getProperty(ShepherdIDs::type) == SequenceEventType::note) {
           
            int midiNote = (int)sequenceEvent.getProperty(ShepherdIDs::midiNote);
            float midiVelocity = (float)sequenceEvent.getProperty(ShepherdIDs::midiVelocity);
            double noteOnTimestamp = (double)sequenceEvent.getProperty(ShepherdIDs::renderedStartTimestamp);
            double noteOffTimestamp = (double)sequenceEvent.getProperty(ShepherdIDs::renderedEndTimestamp);
            
            juce::MidiMessage msgNoteOn = juce::MidiMessage::noteOn(midiChannel, midiNote, midiVelocity);
            msgNoteOn.setTimeStamp(noteOnTimestamp);
            messages.push_back(msgNoteOn);
            
            juce::MidiMessage msgNoteOff = juce::MidiMessage::noteOff(midiChannel, midiNote, 0.0f);
            msgNoteOff.setTimeStamp(noteOffTimestamp);
            messages.push_back(msgNoteOff);
        }
        return messages;
    }

    inline juce::String serialize128IntArray(std::array<int, 128> array)
    {
        juce::StringArray splittedValues;
        for (int i=0; i<array.size(); i++){
            splittedValues.add(juce::String(array[i]));
        }
        return splittedValues.joinIntoString(",");
    }

    inline std::array<int, 128> deserialize128IntArray(juce::String serializedArray)
    {
        std::array<int, 128> array = {0};
        if (serializedArray == ""){
            // Return array with default midi CC values
            for (int i=0; i<array.size(); i++){
                array[i] = 64;
            }
        } else {
            juce::StringArray splittedValues;
            splittedValues.addTokens (serializedArray, ",", "");
            int i=0;
            for (auto value: splittedValues) {
                array[i] = value.getIntValue();
                i+=1;
            }
        }
        jassert(array.size() == 128);
        return array;
    }
}

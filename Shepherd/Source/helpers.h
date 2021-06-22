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

    inline juce::ValueTree createDefaultSession(juce::StringArray availableHardwareDeviceNames)
    {
        juce::ValueTree session (IDs::SESSION);
        Helpers::createUuidProperty (session);
        session.setProperty (IDs::name, juce::Time::getCurrentTime().formatted("%Y%m%d") + " unnamed", nullptr);
        session.setProperty (IDs::playheadPositionInBeats, Defaults::playheadPosition, nullptr);
        session.setProperty (IDs::isPlaying, Defaults::isPlaying, nullptr);
        session.setProperty (IDs::doingCountIn, Defaults::doingCountIn, nullptr);
        session.setProperty (IDs::countInplayheadPositionInBeats, Defaults::playheadPosition, nullptr);
        session.setProperty (IDs::fixedLengthRecordingBars, Defaults::fixedLengthRecordingBars, nullptr);
        session.setProperty (IDs::recordAutomationEnabled, Defaults::recordAutomationEnabled, nullptr);
        session.setProperty (IDs::fixedVelocity, Defaults::fixedVelocity, nullptr);
        session.setProperty (IDs::bpm, Defaults::bpm, nullptr);
        session.setProperty (IDs::meter, Defaults::meter, nullptr);
        session.setProperty (IDs::barCount, Defaults::barCount, nullptr);
        session.setProperty (IDs::metronomeOn, Defaults::metronomeOn, nullptr);
        
        for (int tn = 0; tn < 8; ++tn)
        {
            juce::ValueTree t (IDs::TRACK);
            const juce::String trackName ("Track " + juce::String (tn + 1));
            Helpers::createUuidProperty (t);
            t.setProperty (IDs::name, trackName, nullptr);
            t.setProperty (IDs::inputMonitoring, Defaults::inputMonitoring, nullptr);
            t.setProperty (IDs::nClips, Defaults::nClips, nullptr);
            if (tn < availableHardwareDeviceNames.size()){
                t.setProperty (IDs::hardwareDeviceName, availableHardwareDeviceNames[tn], nullptr);
            } else {
                t.setProperty (IDs::hardwareDeviceName, Defaults::name, nullptr);
            }
            for (int cn = 0; cn < 8; ++cn)
            {
                juce::ValueTree c (IDs::CLIP);
                Helpers::createUuidProperty (c);
                c.setProperty (IDs::name, trackName + ", Clip " + juce::String (cn + 1), nullptr);
                c.setProperty (IDs::clipLengthInBeats, Defaults::clipLengthInBeats, nullptr);
                t.addChild (c, -1, nullptr);
            }
            session.addChild (t, -1, nullptr);
        }

        return session;
    }

}

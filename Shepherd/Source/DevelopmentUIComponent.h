/*
  ==============================================================================

    DevelopmentUIComponent.h
    Created: 1 Jun 2021 12:57:29pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "defines.h"
#include "Sequencer.h"

//==============================================================================
/*
*/
class DevelopmentUIComponent : public juce::Component
{
public:
    DevelopmentUIComponent(Sequencer* _sequencer)
    {
        sequencer = _sequencer;
        
        reloadBrowserButton.onClick = [this] {
            reloadBrowser();
        };
        reloadBrowserButton.setButtonText("Reload UI");
        addAndMakeVisible (reloadBrowserButton);
        
        toggleStateVisualizer.onClick = [this] {
            resized();
        };
        toggleStateVisualizer.setButtonText("Toggle view state");
        addAndMakeVisible (toggleStateVisualizer);
        
        debugStateButton.onClick = [this] {
            if (sequencer != nullptr){
                sequencer->debugState();
            }
        };
        debugStateButton.setButtonText("Debug state");
        addAndMakeVisible (debugStateButton);
        
        randomizeClipsContentButton.onClick = [this] {
            if (sequencer != nullptr){
                sequencer->randomizeClipsNotes();
            }
        };
        randomizeClipsContentButton.setButtonText("Randomize clips notes");
        addAndMakeVisible (randomizeClipsContentButton);
        
        addAndMakeVisible(browser);
        browser.goToURL(DEV_UI_SIMULATOR_URL);
        
        setSize(10, 10);  // Will be reset later
        
        finishedInitialization = true;
    }

    ~DevelopmentUIComponent() override
    {
    }
    
    void reloadBrowser()
    {
        browser.goToURL(DEV_UI_SIMULATOR_URL);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
    }

    void resized() override
    {
        reloadBrowserButton.setBounds(5, 5, 70, 20);
        toggleStateVisualizer.setBounds(80, 5, 120, 20);
        debugStateButton.setBounds(205, 5, 80, 20);
        randomizeClipsContentButton.setBounds(290, 5, 120, 20);
        browser.setBounds(0, 30, browserWidth, browserHeight);
        setSize(browserWidth, 30 + browserHeight);
        if (finishedInitialization){
            getParentComponent()->setSize(browserWidth, 30 + browserHeight);
        }
    }

private:
    
    int browserWidth = 920;
    int browserHeight = 760;
    
    juce::String stateTransport = "";
    juce::String stateTracks = "";
    juce::String xmlState = "";
    
    juce::WebBrowserComponent browser;
    juce::TextButton debugStateButton;
    juce::TextButton reloadBrowserButton;
    juce::TextButton randomizeClipsContentButton;
    juce::TextButton toggleStateVisualizer;
    
    Sequencer* sequencer;

    bool finishedInitialization = false;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DevelopmentUIComponent)
};

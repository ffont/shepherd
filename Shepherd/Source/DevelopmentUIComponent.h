/*
  ==============================================================================

    DevelopmentUIComponent.h
    Created: 1 Jun 2021 12:57:29pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>


//==============================================================================
/*
*/
class DevelopmentUIComponent  : public juce::Component
{
public:
    DevelopmentUIComponent()
    {
        reloadBrowser.onClick = [this] {
            browser.goToURL("http://localhost:6128/");
        };
        reloadBrowser.setButtonText("Reload UI");
        addAndMakeVisible (reloadBrowser);
        
        toggleStateVisualizer.onClick = [this] {
            showState = !showState;
            resized();
        };
        toggleStateVisualizer.setButtonText("Toggle view state");
        addAndMakeVisible (toggleStateVisualizer);
        
        addAndMakeVisible(browser);
        browser.goToURL("http://localhost:6128/");
        
        addAndMakeVisible(stateVisualizer);
        stateVisualizer.setMultiLine(true, true);
        stateVisualizer.setReadOnly(true);
        
        setSize(10, 10);  // Will be reset later
        
        finishedInitialization = true;
    }

    ~DevelopmentUIComponent() override
    {
    }
    
    void updateStateInVisualizer()
    {
        stateVisualizer.setText(
            stateTransport + "\n\n" + stateTracks
        );
    }
    
    void setStateTransport(const juce::String &state)
    {
        stateTransport = state;
        updateStateInVisualizer();
    }
    
    void setStateTracks(const juce::String &state)
    {
        stateTracks = state;
        updateStateInVisualizer();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
    }

    void resized() override
    {
        reloadBrowser.setBounds(5, 5, 70, 20);
        toggleStateVisualizer.setBounds(80, 5, 120, 20);
        browser.setBounds(0, 30, browserWidth, browserHeight);
        if (showState){
            stateVisualizer.setBounds(0, 30 + browserHeight, browserWidth, stateVisualizerHeight);
            setSize(browserWidth, 30 + browserHeight + stateVisualizerHeight);
            if (finishedInitialization){
                getParentComponent()->setSize(browserWidth, 30 + browserHeight + stateVisualizerHeight);
            }
        } else {
            stateVisualizer.setBounds(0, 0, 0, 0);
            setSize(browserWidth, 30 + browserHeight);
            if (finishedInitialization){
                getParentComponent()->setSize(browserWidth, 30 + browserHeight);
            }
        }
    }

private:
    
    int browserWidth = 900;
    int browserHeight = 760;
    int stateVisualizerHeight = 170;
    
    juce::String stateTransport = "";
    juce::String stateTracks = "";
    
    juce::WebBrowserComponent browser;
    juce::TextButton reloadBrowser;
    juce::TextButton toggleStateVisualizer;
    juce::TextEditor stateVisualizer;
    
    bool showState = false;
    bool finishedInitialization = false;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DevelopmentUIComponent)
};
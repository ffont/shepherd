/*
  ==============================================================================

    ReleasePool.h
    Created: 8 Jun 2022 7:09:24pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

// Code adapted from https://github.com/matkatmusic/AudioFilePlayer/blob/91fdf80192135096b044c9d4070f8b3055ee1672/Source/PluginProcessor.h

#pragma once

#include <JuceHeader.h>
#include "Fifo.h"


template<typename ReferenceCountedType>
struct ReleasePool : juce::Timer
{
    ReleasePool()
    {
        deletionPool.reserve(5000);
        startTimer(1 * 1000);
    }
    
    ~ReleasePool() override
    {
        stopTimer();
    }
    
    using Ptr = typename ReferenceCountedType::Ptr;
    
    void add(Ptr ptr)
    {
        if( ptr == nullptr )
            return;
        
        if( juce::MessageManager::getInstance()->isThisTheMessageThread() )
        {
            addIfNotAlreadyThere(ptr);
        }
        else
        {
            if( fifo.push(ptr) )
            {
                successfullyAdded.set(true);
            }
            else
            {
                jassertfalse;
            }
        }
    }
    
    void timerCallback() override
    {
        if( successfullyAdded.compareAndSetBool(false, true))
        {
            Ptr ptr;
            while( fifo.pull(ptr) )
            {
                addIfNotAlreadyThere(ptr);
                ptr = nullptr;
            }
        }
        
        deletionPool.erase(std::remove_if(deletionPool.begin(),
                                          deletionPool.end(),
                                          [](const auto& ptr)
                                          {
                                              return ptr->getReferenceCount() <= 1;
                                          }),
                           deletionPool.end());
    }
private:
    Fifo<Ptr, 512> fifo;
    std::vector<Ptr> deletionPool;
    juce::Atomic<bool> successfullyAdded { false };
    
    void addIfNotAlreadyThere(Ptr ptr)
    {
        auto found = std::find_if(deletionPool.begin(),
                                  deletionPool.end(),
                                  [ptr](const auto& elem)
                                  {
                                      return elem.get() == ptr.get();
                                  });
        
        if( found == deletionPool.end() )
            deletionPool.push_back(ptr);
    }
};

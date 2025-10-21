#pragma once

// Make the iterator traits specialisation for juce::StrideIterator visible
// to every translation unit, including the generated JUCE module amalgamations.
// This must be processed before any standard algorithms are instantiated.
#include "JUCEIteratorFix.h"

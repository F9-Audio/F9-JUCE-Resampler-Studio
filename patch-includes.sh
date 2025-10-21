#!/bin/bash
# Patch JuceLibraryCode includes to add iterator fix header

cd "$(dirname "$0")/JuceLibraryCode" || exit 1

for file in include_juce_audio_devices.mm include_juce_core.mm; do
    if [ -f "$file" ]; then
        # Check if already patched
        if ! grep -q "JUCEIteratorFix.h" "$file"; then
            # Add the fix header right after the comment block
            sed -i.bak '7 a\
#include "../Source/JUCEIteratorFix.h"
' "$file"
            echo "Patched $file"
        else
            echo "$file already patched"
        fi
    fi
done

echo "Done! Now run: cd Builds/MacOSX && xcodebuild"

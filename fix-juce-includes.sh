#!/bin/bash
# Fix JUCE iterator compatibility by injecting the fix header into all module includes

JUCE_LIB_DIR="JuceLibraryCode"
FIX_HEADER='#include "../../Source/PrefixHeader.h"'

echo "Patching JUCE module includes with iterator fix..."

for file in "$JUCE_LIB_DIR"/include_*.mm; do
    if [ -f "$file" ]; then
        # Check if fix is already applied
        if ! grep -q "PrefixHeader.h" "$file"; then
            # Insert the fix header after the warning comment block
            sed -i.bak '/^$/,/^$/ {
                /^$/a\
'"$FIX_HEADER"'
                /^$/!b
                d
            }' "$file"
            echo "  Patched: $(basename "$file")"
        else
            echo "  Already patched: $(basename "$file")"
        fi
    fi
done

echo "Done!"

#!/bin/bash
# Patch JUCE StrideIterator to add C++20 iterator traits

JUCE_FILE="/Applications/JUCE/modules/juce_audio_devices/native/juce_CoreAudio_mac.cpp"
BACKUP_FILE="${JUCE_FILE}.backup-$(date +%Y%m%d)"

echo "Patching JUCE StrideIterator for C++20 compatibility..."

# Check if file exists
if [ ! -f "$JUCE_FILE" ]; then
    echo "Error: JUCE file not found at $JUCE_FILE"
    exit 1
fi

# Check if already patched
if grep -q "C++20 iterator traits" "$JUCE_FILE"; then
    echo "Already patched! Skipping."
    exit 0
fi

# Create backup
echo "Creating backup at $BACKUP_FILE"
sudo cp "$JUCE_FILE" "$BACKUP_FILE"

# Apply patch - insert after line 1063 (the opening brace)
sudo sed -i '' '1063 a\
\
        // C++20 iterator traits - required for compatibility with modern standard library\
        using iterator_category = std::random_access_iterator_tag;\
        using value_type        = typename std::iterator_traits<Iterator>::value_type;\
        using difference_type   = std::ptrdiff_t;\
        using pointer           = Iterator;\
        using reference         = typename std::iterator_traits<Iterator>::reference;
' "$JUCE_FILE"

echo ""
echo "✓ Patch applied successfully!"
echo "  Backup saved at: $BACKUP_FILE"
echo ""
echo "The StrideIterator now has proper C++20 iterator traits."
echo ""
echo "Next steps:"
echo "  cd Builds/MacOSX"
echo "  rm -rf build"
echo "  xcodebuild -project NewProject.xcodeproj -configuration Debug -target \"NewProject - App\""

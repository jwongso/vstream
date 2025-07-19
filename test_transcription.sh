#!/bin/bash
# test_transcription.sh

MODEL="models/vosk-model-en-us-0.42-gigaspeech"
REFERENCE="benchmark.wav.txt"
OUTPUT="transcribed_$(date +%Y%m%d_%H%M%S).txt"

echo "Starting transcription test..."
echo "Output will be saved to: $OUTPUT"

# Run vstream and capture all output
./build_rel/vstream --model $MODEL --mic --buffer-ms 100 --silence-ms 100 --finalize-ms 5000 --no-partial > raw_output.txt 2>/dev/null

# Extract just the final results
grep "\[FINAL\]" raw_output.txt | sed 's/\[FINAL\] //' > $OUTPUT

echo -e "\n\nTranscription complete. Comparing with reference..."

# Show differences
echo -e "\n=== DIFFERENCES ==="
diff --color=always $REFERENCE $OUTPUT

# Calculate simple word count accuracy
REF_WORDS=$(wc -w < $REFERENCE)
OUT_WORDS=$(wc -w < $OUTPUT)
echo -e "\n=== WORD COUNT ==="
echo "Reference: $REF_WORDS words"
echo "Transcribed: $OUT_WORDS words"

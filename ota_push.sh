#!/bin/bash

VER=$(cat ./firmware/version)
NEWVER=$(($VER+1))

echo "Updating firmware from $VER to $NEWVER"

$(cp ./.pio/build/featheresp32/firmware.bin ./firmware/$NEWVER.bin)
echo "firmware copied"
$(echo "$NEWVER" > ./firmware/version)
echo "committing"
$(git add ./firmware)
$(git commit -m "Update ProjectThing firmware from $VER to $NEWVER")
echo "pushing"
$(git push)
echo "complete"

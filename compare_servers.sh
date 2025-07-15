#!/bin/bash

# Ports
DEMO_PORT=3333
YOUR_PORT=4444

# Kill old processes on those ports
fuser -k ${DEMO_PORT}/tcp > /dev/null 2>&1
fuser -k ${YOUR_PORT}/tcp > /dev/null 2>&1

echo ">>> Starting Demo Server on port ${DEMO_PORT}..."
util/mazewar -p $DEMO_PORT &
DEMO_PID=$!

sleep 1

echo ">>> Starting Your Server on port ${YOUR_PORT}..."
bin/mazewar -p $YOUR_PORT &
YOUR_PID=$!

sleep 1

echo
echo ">>> Both servers running."
echo ">>> Open two new terminals or tabs and run:"
echo
echo "   util/gclient -p $DEMO_PORT -u demoUser -a D   # Connect to demo"
echo "   util/gclient -p $YOUR_PORT -u myUser -a M     # Connect to your version"
echo
echo ">>> When you're done, press ENTER to stop both servers."
read

echo ">>> Shutting down both servers..."
kill $DEMO_PID $YOUR_PID
wait

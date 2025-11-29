#!/bin/bash
# This script kills all processes related to 'cpptools'
# Run it to get rid of file quota exceeded errors when building
ps aux | grep -v grep | grep -i 'cpptools' | awk '{print $2}' | xargs -r kill -9

# Description

Checks if luma difference between lines are below or above given threshold.

# Requirement

AviSynth+ >+ 3.6.

# Usage

```
LinesLumaDiff (clip, string "output", int "left", int "top", int "right", int "bottom", float "tl", float "tt", float "tr", float "tb")
```

## Parameters:

- clip\
    A clip to process. It must in YUV planar format.    
    
- output\
    The path of the txt file with frames.
    
- left, top, right, bottom\
    How many lines on each side will be checked.\
    0: Skip.\
    Must not be negative.\
    Default: left = top = right = bottom = 5.

- tl, tt, tr, tb\
    Threshold.\
    If the luma difference between the current and the neighbour line is above the threshold, frame property `_LinesDiff` with the difference is set. Optionally the frame number could be written in file by specifying `output`.\
    Must be between 0.0 and 1.0.\
    Default: tl = tt = tr = tb = 0.14.

# Building

## Windows

Use solution files.

## Linux

### Requirements

- Git
- C++11 compiler
- CMake >= 3.16

```
git clone https://github.com/Asd-g/AviSynth-LinesLumaDiff && \
cd AviSynth-LinesLumaDiff && \
mkdir build && \
cd build && \
cmake .. && \
make -j$(nproc) && \
sudo make install
```

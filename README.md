# Description

Checks if luma difference between lines are below or above given threshold and writes frame number in text file.

# Usage

```
LinesLumaDiff (clip, int "left", int "top", int "right", int "bottom", float "tl", float "tt", float "tr", float "tb", string frames_file)
```

## Parameters:

- clip\
    A clip to process. It must in YUV planar format.
    
- left, top, right, bottom\
    How many lines on each side will be checked.\
    Set one (or more) of the sides to 0 to not process it.\
    Must not be negative.\
    Default: left = top = right = bottom = 5.

- tl, tt, tr, tb\
    Threshold for each side.\
    If the luma difference between the current and the neighbour line is above the threshold, the frame number will be written to file.\
    Must not be negative.\
    Default: tl = tt = tr = tb = 2.5.
    
- frames_file\
    Set the path of the file with frame numbers.

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

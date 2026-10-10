# Getting Started: Clone and Build GrooveMorph

This guide explains how to download the GrooveMorph project using Git and build it on Windows.

## Prerequisites

Before starting, install these tools:

1. **Git for Windows**
   - Download: https://git-scm.com/download/win
   - This allows you to clone the repository

2. **Visual Studio 2022 Community** (free)
   - Download: https://visualstudio.microsoft.com/downloads/
   - During installation, select "Desktop development with C++"
   - This is the C++ compiler and IDE

3. **CMake** (version 3.22 or later)
   - Download: https://cmake.org/download/
   - During installation, check "Add CMake to the system PATH"
   - This builds the project

4. **Internet connection**
   - The first build downloads JUCE (~1GB), which takes time

## Step 1: Clone the Repository

Open **Command Prompt** or **PowerShell** and run:

```bash
git clone https://github.com/djshellshoxxx/GrooveMorph.git
cd GrooveMorph
```

This downloads the entire project to a folder called `GrooveMorph`.

## Step 2: Create a Build Folder

From inside the `GrooveMorph` folder, create a build directory:

```bash
mkdir build
cd build
```

## Step 3: Generate the Visual Studio Project

Run CMake to create the Visual Studio solution:

```bash
cmake .. -G "Visual Studio 17 2022" -A x64
```

**What this does:**
- `cmake ..` tells CMake to read the CMakeLists.txt from the parent directory
- `-G "Visual Studio 17 2022"` specifies the build system (adjust for your VS version)
- `-A x64` builds for 64-bit Windows

**Note:** This step may take 5-10 minutes on first run because it downloads JUCE.

## Step 4: Build the Standalone Application

Once CMake finishes, build the app:

```bash
cmake --build . --config Release --target GrooveMorphStandalone
```

**What this does:**
- `--config Release` builds an optimized release version
- `--target GrooveMorphStandalone` builds only the standalone app (not the plugin)

This will take several minutes to compile.

## Step 5: Run the Application

Once the build completes successfully, run:

```bash
.\GrooveMorphStandalone_artefacts\Release\GrooveMorph.exe
```

You should see a window with "GrooveMorph M0 Standalone Spike" and a Master Intensity slider.

---

## Complete Command Sequence

If you want to run all commands at once, here's the full sequence:

```bash
# Clone the repo
git clone https://github.com/djshellshoxxx/GrooveMorph.git
cd GrooveMorph

# Create build folder and generate project
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64

# Build the standalone app
cmake --build . --config Release --target GrooveMorphStandalone

# Run the app
.\GrooveMorphStandalone_artefacts\Release\GrooveMorph.exe
```

---

## Troubleshooting

### "git is not recognized"
- Git is not installed or not in PATH
- Reinstall Git from https://git-scm.com/download/win
- Make sure to check "Add Git to PATH" during installation
- Restart Command Prompt after installation

### "cmake is not recognized"
- CMake is not installed or not in PATH
- Reinstall from https://cmake.org/download/
- Make sure to check "Add CMake to system PATH" during installation
- Restart Command Prompt after installation

### CMake fails to download JUCE
- The download may take 10+ minutes on first run
- Ensure you have a stable internet connection
- If it times out, delete the `build` folder and try again:
  ```bash
  cd ..
  rmdir /s /q build
  mkdir build
  cd build
  cmake .. -G "Visual Studio 17 2022" -A x64
  ```

### Compiler errors during build
- Ensure Visual Studio 2022 with C++ support is installed
- Try building in a new `build` folder from scratch
- Check that you're using the correct Visual Studio version in the cmake command

### "GrooveMorphStandalone_artefacts" folder doesn't exist
- The build may have failed silently
- Check the console output for errors
- Try running the build command again

---

## Next Steps

Once the standalone app runs:

1. **Test the UI**
   - Drag the Master Intensity slider
   - It should feel responsive (no lag)

2. **Check for errors**
   - Look for any error messages or warnings
   - The app should start cleanly without console spam

3. **Next milestone**
   - Once this validates, the project moves to core DSP development
   - See RESEARCH_NOTES.md for the next phase plan

---

## Building the VST3 Plugin (Optional)

If you also want to build the VST3 plugin for testing in a DAW:

```bash
cmake --build . --config Release --target GrooveMorphPlugin
```

The plugin will be at:
```
.\GrooveMorphPlugin_artefacts\Release\GrooveMorph.vst3
```

To use it in your DAW, copy it to:
```
C:\Program Files\Common Files\VST3\
```

Then rescan your DAW's plugin folders.

---

## Questions or Issues?

If you encounter problems:
1. Take a screenshot of the error
2. Note the exact command that failed
3. Check the console output for details
4. Share this information so it can be debugged

This is an M0 spike, so all feedback helps validate the build process before moving to the next phase.

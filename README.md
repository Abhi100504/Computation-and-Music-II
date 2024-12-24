# MUS 205: Computation and Music II - Fall 2024

## Overview
This repository contains code and resources for MUS 205: Computation and Music II. The course focuses on real-time audio system design using **Pure Data (Pd)** and **C/C++**, including topics like digital signal processing (DSP), graphical audio languages, and custom audio object development.

---

## Contents
### Assignments
1. **Reading Pd Source Code**  
   - Explores the core functions in the Pd source code (e.g., `class_new()`, `dsp_add()`).
   - Includes code snippets with comments explaining functionality.
 
2. **Setting Phase in an Oscillator**  
   - Adds a second, signal-only inlet for phase control in the `oscil~` object.
   - Includes a Pd-style help file demonstrating phase modulation.

3. **Building a Digital Filter for a Buffer**  
   - Implements a resonant filter in the `bed` object with user-accessible parameters for center frequency and resonance.
   - Includes a Pd-style help file for usage and examples.

4. **Dynamic Stochastic Synthesis Research**  
   - Explores random distributions and variations for dynamic stochastic synthesis.
   - Serves as a foundation for implementing these features in `dynstoch~`.

5. **Dynamic Stochastic Synthesis Implementation**  
   - Enhances the `dynstoch~` object by implementing additional random distributions and variations based on Luque's proposals.
   - Includes a Pd-style help file demonstrating functionality and examples.

---

## Key Features
- **DSP in Pure Data (Pd):** Hands-on implementations of DSP concepts within the Pd environment.
- **Custom Pd Objects:** Creation and debugging of custom objects in C for Pd.
- **Graphical Audio Synthesis:** Focus on real-time synthesis and processing.

---

## Resources
- **Textbook Reference:**  
  - *Designing Audio Objects for Max/MSP and Pd* by Eric Lyon.  
  - Includes models and detailed explanations for DSP implementations.

- **Supplementary Reading:**  
  - *The Theory and Techniques of Electronic Music* by Miller Puckette ([Free Online](http://msp.ucsd.edu/techniques.htm)).

- **Useful Links for Pd Development:**  
  - [Compile Pd Externals on Windows](https://nc.nubegris.com.ar/index.php/s/3frE6T3iCD9M6Sw)  
  - [How to Write an External for Pd](https://github.com/pure-data/externals-howto)  
  - [Pd Lib Builder](https://github.com/pure-data/pd-lib-builder)

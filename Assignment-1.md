# Assignment 1: Reading Pd Source Code

## Instructions
Based on **Lyon text, Chapter 3, Exercise 3**.

1. Locate the following functions in the **Pd source code**:
   - `class_new()`
   - `class_addmethod()`
   - `post()`
   - `pd_new()`
   - `inlet_new()`
   - `outlet_new()`
   - `dsp_add()`

2. Copy the functions into a text file with the extension `.c` as if they were compilable code.

3. Using **C comments**, explain what the code is doing. You may provide:
   - Line-by-line comments.
   - Longer comments with explanations for more complex code sections.

## Hints
- The functions are located in different files within the Pd source code:
  - `class_new()` is in **`m_class.c`**.
  - `dsp_add()` is in **`d_ugen.c`**.


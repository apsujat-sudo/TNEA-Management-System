# TNEA Management System

A menu-driven C application that simulates selected features of the Tamil Nadu Engineering Admissions (TNEA) process.

## Features

- Student registration and login
- Student profile management
- HSC academic marks and engineering cutoff calculation
- College and course management
- College preference submission
- Demo college allotment based on cutoff and seat availability
- Local file-based data persistence

## Data Structures Used

- **Singly Linked Lists:** Store students, colleges, applications, and preferences.
- **Hash Table with Separate Chaining:** Look up students by email.
- **Binary Search Tree (BST):** Index colleges by college ID.
- **Max Heap / Priority Queue:** Process eligible applicants in descending cutoff order.

## Requirements

- GCC compiler
- Windows, Linux, or another compatible C environment

## How to Compile and Run

Compile using GCC:

```bash
gcc TNEA_DS.c -o tnea_ds
```

On Windows, run:

```bash
tnea_ds.exe
```

On Linux:

```bash
./tnea_ds
```

## Data Persistence

The application stores records in local text files for students, colleges, and applications.

Keep these files in the application's working directory when running the program.

## Disclaimer

This is an educational simulation, not the official TNEA admission or counselling system. The allotment rules are simplified and do not reproduce the official counselling algorithm.

## Technologies

- C programming language
- Data Structures and Algorithms
- File handling
- GCC

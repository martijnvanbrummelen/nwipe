# Contributing to nwipe


Thank you for your interest in contributing to **nwipe**.

Nwipe is a widely used disk wiping tool that prioritizes **reliability, correctness and portability**.  
Because the software is used in system administration and data sanitization environments, stability and careful review are essential.

This document describes the preferred workflow for contributing changes.

---

# Development Setup

To build nwipe for development:

```bash
./configure --prefix=/usr CFLAGS='-O0 -g -Wall -Wextra'
make format
make
```

Explanation of flags:

- **-O0 -g**  
    Disables optimisation and includes debug symbols.  
    This is important when debugging with tools such as `gdb`
    
- **-Wall -Wextra**  
    Enables most useful compiler warnings.  
    Please ensure new code compiles **without warnings**.
    

---

# Code Formatting

The project uses **clang-format**.

The style is defined in:

```
.clang-format
```

Before submitting a pull request run:

```bash
make format
```

This ensures consistent formatting across the codebase.

If `clang-format` is missing, install it via your distribution package manager.

---

# Release-like builds

To build a normal optimized binary:

```bash
./configure --prefix=/usr
make
```

---

# Pull Request Guidelines

To keep review manageable, please follow these guidelines:

### Prefer small, focused pull requests

Large changes to core parts of the codebase can be difficult to review and test.

If you plan a **large feature or architectural change**, please open an **issue first** to discuss the idea.

### Ensure the code is tested

Before submitting a PR:

- build the project
    
- test the functionality on real or virtual devices where possible
    
- ensure no compiler warnings are introduced
    

Testing is often more important as writing the code.

### Explain the change clearly

A good pull request description should explain:

- what the change does
    
- why it is needed
    
- how it was tested
    

For algorithmic or architectural changes, additional documentation is appreciated.

### Keep PR frequency reasonable

Maintainers review contributions in their spare time.  
Submitting many large PRs in a short time can create unnecessary review pressure.

---

# Use of AI-assisted development tools

AI tools (such as ChatGPT, Copilot, or similar systems) may be used to assist development.

However, contributors remain fully responsible for their submissions.

Requirements:

- Contributors **must fully understand all submitted code**.
    
- All code must be **tested before submission**.
    
- Contributors remain responsible for **correctness, security and quality**.
    

AI assistance is treated the same as any other external source of code.

---

# Disclosure of AI-assisted contributions

If AI tools were used in generating part of a contribution, please disclose this in the pull request description.

This is **purely informational** and helps reviewers set expectations for review and testing.

Example:

```
AI assistance: yes
Used for: initial draft of documentation / hrefactoring suggestion
Manually reviewed and tested by contributor
```

Disclosure is encouraged regardless of contribution size.

This policy is **not intended to judge contributors**, but to improve transparency and review efficiency.

---

# Code Quality Expectations

Contributions should aim to maintain the project's long-standing reputation for stability.

Please ensure:

- no compiler warnings
    
- readable code
    
- minimal unnecessary complexity
    
- portability across supported platforms
    

When in doubt, prioritize **clarity and maintainability over cleverness**.

---

# Respecting project scope

Nwipe is a specialized tool used in data sanitization workflows.

Features should align with the project's goals:

- reliable disk wiping
    
- predictable behavior
    
- stable releases
    

Changes that significantly alter behavior may require extended discussion before merging.

---

# Thank you

All contributions code, testing, documentation, or discussion are appreciated.

Maintainers and contributors work together to keep **nwipe stable, reliable, and useful for system administrators worldwide.**



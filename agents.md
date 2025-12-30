# Agent Guidelines: Code Modification Policy

## Objective

This document outlines the strict guidelines for all agents, contributors, and automated processes regarding file modifications within this repository.

## Policy

**All agents must ensure that code modifications are strictly limited to files within the `src/` directory.**

## Prohibited Modifications

Agents are explicitly forbidden from making changes to the following:

*   Configuration files in the root directory (e.g., `package.json`, `tsconfig.json`, `webpack.config.js`).
*   Documentation files (e.g., `README.md`, `CONTRIBUTING.md`, `LICENSE`).
*   Test directories (e.g., `tests/`, `spec/`).
*   Hidden files (e.g., `.gitignore`, `.eslintrc`, `.github/` workflows).
*   Any file or directory outside of `src/`.

## Enforcement

If an agent identifies a need to modify a file outside of the `src/` directory to complete a task, they must:

1.  Halt the current task.
2.  Raise an issue or notify a human administrator regarding the necessary exception or a potential change in project scope.

Adherence to this policy is mandatory.

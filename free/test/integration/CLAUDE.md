# test/integration/ - Integration tests

End-to-end tests exercising the full toolchain pipeline: free-cc -> free-as -> free-ld -> run.

- **cases/** - Individual test programs (C source files)

Each test compiles a C program through the entire toolchain and verifies correct execution (exit code and/or stdout output).

Conventions: Test cases are small, self-contained C programs. Each tests a specific language feature or runtime behavior. A test runner script orchestrates the pipeline.

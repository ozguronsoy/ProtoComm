## Contributing

Contributions are welcome and greatly appreciated! This is an open-source project, and I value all community feedback, bug reports, and new features.

### How You Can Help

There are many ways to contribute:

* **Reporting Bugs:** If you find a bug, please let me know.
* **Suggesting Features:** Have an idea for the `CommStreamES` API or a new `IFrameHandler`? Feel free to suggest it.
* **Writing Documentation:** Improvements to the `README` or in-code Doxygen comments are always welcome.
* **Adding New Protocols:** The `ICommProtocol` interface is designed for extension! Feel free to add implementations using your prefered backend.

### Reporting Issues

Please use the [GitHub Issues](https://github.com/ozguronsoy/ProtoComm/issues) tracker to report all bugs and feature requests.

* **For Bugs:** Please include your **OS** (e.g., Windows 11, Ubuntu 22.04), **Compiler** (GCC, Clang, MSVC), the steps to reproduce the bug, and any error messages (e.g., `SIGILL`, linker errors, or `gtest` failures).
* **For Features:** Please explain the new functionality, its use case, and a potential API design.

### Submitting Pull Requests

This project follows the standard GitHub "Fork & Pull Request" workflow.

1.  **Fork** the repository to your own GitHub account.
2.  Create a **new branch** for your changes (e.g., `feature/crc-handler` or `fix/some-bug`).
3.  **Make your changes.** Please adhere to the existing code style.
4.  **Ensure all CI tests pass.** Your pull request will be automatically built and tested by the GitHub Actions workflow on GCC, Clang, and MSVC. A PR with failing tests will not be merged.
6.  **Submit the Pull Request** against the `main` branch.

### Code Style

Please try to match the existing code style to keep the project clean and maintainable:
- Use pascal case for functions and classes.
- Use camel case for variables.
    - Use `m_` for private member variables.
    - Use `s_` for `static` variables.
    - Use `k_` for `static constexpr` variables.
- Prefer modern C++ patterns.
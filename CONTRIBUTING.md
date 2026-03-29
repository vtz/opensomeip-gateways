<!--
  Copyright (c) 2025 Vinicius Tadeu Zein

  SPDX-License-Identifier: Apache-2.0
-->

# Contributing to OpenSOME/IP Gateways

Thank you for your interest in contributing! This guide covers the development workflow, coding standards, and gateway-specific guidelines.

## Development Workflow

1. **Fork** the repository
2. **Create a branch** from `main` using conventional naming:
   - `feat/gateway-<name>-<feature>` for new features
   - `fix/gateway-<name>-<issue>` for bug fixes
   - `docs/<topic>` for documentation
3. **Implement** your changes following the coding guidelines
4. **Test** your changes (`ctest --output-on-failure`)
5. **Submit a pull request** against `main`

## Coding Standards

- **C++17** standard — same as [opensomeip](https://github.com/vtz/opensomeip)
- **clang-format** enforced via pre-commit hooks (Google-based style, 4-space indent)
- **clang-tidy** checks enabled (see `.clang-tidy`)
- Namespace: `opensomeip::gateway::<name>` for each gateway
- File naming: `snake_case.h` / `snake_case.cpp`

### Pre-commit Hooks

```bash
pip install pre-commit
pre-commit install
pre-commit install --hook-type commit-msg
```

### Commit Messages

We use [Conventional Commits](https://www.conventionalcommits.org/):

```
feat(mqtt): add TLS/mTLS support for broker connections
fix(iceoryx2): handle subscriber disconnect during message translation
docs(grpc): add deployment guide with TLS configuration example
test(dbus): add signal emission integration tests
```

## Adding a New Gateway

See [docs/ADDING_A_GATEWAY.md](docs/ADDING_A_GATEWAY.md) for the step-by-step guide.

Key requirements for every gateway:

1. **Independent build** — each gateway must build independently via its CMake option
2. **No cross-gateway dependencies** — gateways must not depend on each other
3. **Common base** — extend `IGateway` from the common library
4. **Configuration** — support YAML-based configuration
5. **Tests** — unit tests for translation logic, integration tests where feasible
6. **Examples** — at least one working example demonstrating bidirectional communication
7. **Documentation** — gateway-specific README with architecture, configuration reference, and usage

## Testing Requirements

- All new code must have unit tests
- Integration tests should use the actual external protocol library where practical
- Tests must pass in CI before merge
- Run tests locally:

```bash
mkdir build && cd build
cmake .. -DBUILD_GATEWAY_<NAME>=ON
make -j$(nproc)
ctest --output-on-failure
```

## Code Review Process

- All PRs require at least one approving review
- CI must pass (build, tests, format, lint)
- Gateway-specific reviewers will be tagged automatically

## License

By contributing, you agree that your contributions will be licensed under the Apache License 2.0.

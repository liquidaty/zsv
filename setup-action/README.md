# zsv/setup-action

[![CI](https://github.com/liquidaty/zsv/actions/workflows/setup-action.yml/badge.svg?branch=main)](https://github.com/liquidaty/zsv/actions/workflows/setup-action.yml)

[GitHub Action](https://docs.github.com/en/actions) to set up zsv+zsvlib.

Supports Linux, macOS, and Windows runners.

## Usage

### Inputs

|   Input   | Required | Default  | Description                |
| :-------: | :------: | :------: | :------------------------- |
| `version` | `false`  | `latest` | Version/tag of the release |

### Outputs

|     Output     | Description                                 |
| :------------: | :------------------------------------------ |
| `install-path` | Absolute path of the installation directory |

Under the installation directory, the subdirectories will include:

- `bin`: `zsv`/`zsv.exe` executable
- `include`: header files
- `lib`: `libzsv` library file

### Example

Set up the latest version:

```yml
- name: Set up zsv+zsvlib
  uses: liquidaty/zsv/setup-action@main
```

Set up a specific version:

```yml
- name: Set up zsv+zsvlib
  uses: liquidaty/zsv/setup-action@main
  with:
    version: '1.0.0'
```
